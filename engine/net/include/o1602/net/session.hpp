// SPDX-License-Identifier: GPL-3.0-or-later
//
// A multiplayer game (ADR 0011): lockstep through the host. Every computer runs the whole game;
// only commands travel. The host collects the commands of all players, puts each into a frame
// and sends the frames, complete, to everyone; a computer runs a frame only once it has it whole.
// So all run the same commands at the same frame, and the deterministic simulation (ADR 0004)
// does the rest.
//
// Every few seconds each computer sends the host a fingerprint of its game (Simulation::
// state_hash()). One that differs gets the host's game anew, as a savegame (M-003): the same
// way a computer comes in at the start, comes into a running game or comes back after it lost
// the connection.
//
// A session drives the simulation in place of Simulation::advance(): it calls tick() for each
// frame. Nothing else may submit commands to the simulation or advance it while a session runs.
//
// Before the game there may be a lobby: the host has no game yet, only the slots of the game to
// come (Lobby). Computers come in, take slots, say they are ready and chat; when the host starts,
// it makes the game from the lobby and every computer gets it as it would get a running one.
//
// When the host leaves a running game, the game moves on (host migration): the host names one of
// the computers as its successor in the Roster and hands it the slots' tokens (Standby). That
// computer hosts the game from where its own is (a HostSession made from its ClientSession); the
// others follow it (a ClientSession made from theirs), come back into their slots with their tokens
// and get its game, as after a lost connection.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/net/transport.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::net {

struct SessionConfig {
    /// Frames between two fingerprints at normal speed (100 = 5 s), times the speed rounded up to a
    /// power of two at higher speeds. Each costs a savegame in memory.
    std::uint32_t hash_interval = 100;
    /// Frames the host closes ahead of its own game, at least and at most: more than the slowest
    /// connection needs one way, so that the frames are there when a computer wants to run them.
    /// Within these bounds the host follows the measured round trips.
    std::uint16_t min_lead = 2;
    std::uint16_t max_lead = 50;
    /// Game time the host may run ahead of the slowest computer, beyond the lead, before it waits
    /// for it (real time at normal speed; at speed 4 four times the frames).
    std::uint32_t max_ahead_ms = 1000;
    /// After waiting this long for a computer the others go on without it; it catches up or
    /// gets the game anew.
    std::uint32_t stall_ms = 10'000;
    /// A computer the others no longer wait for gets the game anew once it is this many frames
    /// behind the host (30 s at normal speed), instead of running them all.
    std::uint32_t resync_behind = 600;
    /// Commands one player may give per frame; more are dropped (a flood would stall everyone).
    std::uint32_t max_commands_per_frame = 256;
    /// How often a computer tells the host how far it is (a fingerprint goes at once).
    std::uint32_t progress_interval_ms = 100;
    /// Computers without a free player slot may watch.
    bool spectators = true;
    /// Computers may come into a running game (else only back into their own slot).
    bool join_running = true;
    /// Desyncs of one computer within this many fingerprints after it got the game anew: it
    /// cannot follow the host (another build?) and is sent away.
    std::uint32_t max_resyncs = 2;
    /// (Host) Computers must give it to come in; empty: anyone may.
    std::string password;
    /// Chat lines one computer may send per second; more are dropped.
    std::uint32_t max_chat_per_second = 5;
    /// (Host migration) How long a computer whose host left tries to get into the successor's game;
    /// the successor may notice the host's leaving a few seconds later than the others.
    std::uint32_t migration_ms = 30'000;
};

enum class EndReason : std::uint8_t {
    None,
    ConnectFailed,  ///< the host could not be reached
    HostLeft,       ///< the connection to the host broke or the host ended the game
    Refused,        ///< the host did not take this computer (SessionEvent::refused says why)
    ProtocolError,  ///< a message made no sense
    LoadFailed,     ///< the game from the host could not be loaded
    Desync,         ///< this computer kept diverging from the host and was sent away
};

/// A short English text for logs: why the session ended or a computer was refused (what the
/// player reads comes from the program's texts in the game's language).
[[nodiscard]] const char* describe(EndReason reason) noexcept;
[[nodiscard]] const char* describe(RefuseReason reason) noexcept;

struct SessionEvent {
    enum class Kind : std::uint8_t {
        Welcomed,      ///< (computer) the host took this computer in as `seat`
        Loaded,        ///< (computer) a game from the host was loaded; `frame` is its first frame
        Joined,        ///< `seat` (`name`) came in, or back
        Left,          ///< `seat` (`name`) went
        Lagging,       ///< the others no longer wait for `seat`
        Desync,        ///< `seat` diverged from the host after `frame`; it gets the game anew
        Ended,         ///< the session is over (`reason`)
        LobbyChanged,  ///< the lobby (slots, settings) changed, or it started
        Chat,          ///< `seat` (`name`) said `text`
    };
    Kind kind = Kind::Joined;
    Seat seat = sim::kNoPlayer;
    std::string name;
    std::uint64_t frame = 0;
    EndReason reason = EndReason::None;
    RefuseReason refused = RefuseReason::Version;
    std::string text;
};

/// What the host and the other computers have in common: pacing the frames, fingerprints, events.
class Session {
public:
    virtual ~Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// A command of the local player; it is carried out when its frame comes, on every computer.
    /// One that names another player, or any from a spectator, is dropped.
    virtual void submit(sim::Command command) = 0;
    /// Network and game for `real_ms` of real time: receives, runs the frames due at the game's
    /// speed (as Simulation::advance(), calling `before_tick` before each) as far as they are
    /// complete, and sends.
    virtual void update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick = {}) = 0;
    /// A line of chat to everyone (cut to kMaxChatLength; empty lines are not sent).
    virtual void say(std::string text) = 0;

    /// The player this computer plays, kNoPlayer while it only watches or is not in yet.
    [[nodiscard]] Seat local_seat() const noexcept { return m_seat; }
    /// Frames run so far.
    [[nodiscard]] std::uint64_t frames() const noexcept { return m_frames; }
    /// As Simulation::tick_fraction(), for drawing between frames.
    [[nodiscard]] double tick_fraction() const noexcept;
    /// The last update could not run all the frames its time asked for: the frames were not
    /// complete yet, or the host waits for a slower computer.
    [[nodiscard]] bool waiting() const noexcept { return m_waiting; }
    /// Who is in the game, the host first.
    [[nodiscard]] const std::vector<Member>& roster() const noexcept { return m_roster; }
    /// The game as set up before the start (Lobby::started tells whether it runs).
    [[nodiscard]] const Lobby& lobby() const noexcept { return m_lobby; }
    /// Who carries the game on if the host leaves (kNoPlayer: nobody), and where the others then
    /// connect (Roster::successor).
    [[nodiscard]] Seat successor() const noexcept { return m_successor; }
    [[nodiscard]] const std::string& successor_address() const noexcept { return m_successor_address; }
    [[nodiscard]] bool ended() const noexcept { return m_ended; }
    [[nodiscard]] EndReason end_reason() const noexcept { return m_end_reason; }
    /// What happened since the last call.
    [[nodiscard]] std::vector<SessionEvent> take_events();

protected:
    Session(std::unique_ptr<Transport> transport, SessionConfig config);

    /// Frames the time asks for now (at most Simulation::kMaxTicksPerAdvance); ran() books those run.
    std::uint32_t frames_due(std::uint32_t real_ms, const sim::Simulation& simulation);
    void ran(std::uint32_t frames);
    /// One frame: its commands, in order, then a tick. Returns the fingerprint if one is due.
    std::optional<FrameHash> run_frame(sim::Simulation& simulation, std::span<const sim::Command> commands);
    void event(SessionEvent e) { m_events.push_back(std::move(e)); }
    void end(EndReason reason);
    void send(LinkId link, const Message& message);
    void chat_event(const Chat& chat);
    void lobby_event();

    std::unique_ptr<Transport> m_transport;
    SessionConfig m_config;
    std::vector<TransportEvent> m_incoming;  ///< reused by every poll
    Seat m_seat = sim::kNoPlayer;
    std::uint64_t m_frames = 0;
    std::uint32_t m_pending_ms = 0;
    bool m_waiting = false;
    bool m_ended = false;
    EndReason m_end_reason = EndReason::None;
    std::vector<Member> m_roster;
    Seat m_successor = sim::kNoPlayer;
    std::string m_successor_address;
    Lobby m_lobby;
    std::vector<SessionEvent> m_events;
};

class ClientSession;

/// The host: runs its own game, takes the other computers in and decides the frames. Its player is
/// `seat` (kNoPlayer: a host without a player of its own).
class HostSession final : public Session {
public:
    /// A game that runs at once on the world the host has; the computers get its free human slots.
    HostSession(std::unique_ptr<Transport> transport, sim::Simulation& simulation,
                const assets::BuildingTable& buildings, Seat seat, std::string name, std::uint64_t content,
                SessionConfig config = {});
    /// A lobby first: `lobby` has the slots of the game to come (the host's own `seat` becomes
    /// Human); start() begins the game.
    HostSession(std::unique_ptr<Transport> transport, Lobby lobby, Seat seat, std::string name, std::uint64_t content,
                SessionConfig config = {});
    /// Host migration: carries on the game of `client`, whose host left and named this computer its
    /// successor (client.hosts_next()). The game goes on from where this computer's is, with the
    /// frames the old host closed beyond it, the slots and tokens it handed over, the same password
    /// and options; the old host's slot goes to the computer. The others come back with their tokens.
    HostSession(std::unique_ptr<Transport> transport, ClientSession& client, const assets::BuildingTable& buildings);
    ~HostSession() override;

    void submit(sim::Command command) override;
    void update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick = {}) override;
    void say(std::string text) override;

    // --- The lobby (before start()) ---

    /// The game has started (start(), or the session began with a running game).
    [[nodiscard]] bool started() const noexcept { return m_simulation != nullptr; }
    /// Title, description and the program's setup of the game to come; the slots stay.
    void set_lobby_info(std::string title, std::string game, std::vector<std::uint8_t> setup);
    /// Who plays slot `seat`: Open, Computer or Closed. A computer that had it watches from then
    /// on; the host's own slot and Human are not set this way.
    void set_seat(Seat seat, LobbySeat::Kind kind);
    /// The host's own player: an open slot, or kNoPlayer to only host.
    bool take_seat(Seat seat);
    /// Sends the computer playing `seat` away.
    void kick(Seat seat);
    /// Sends the computer of roster()[`member`] away, a spectator too (0, the host itself: nothing).
    void kick_member(std::size_t member);
    /// Every human slot of another computer is ready (the host starts when it wants).
    [[nodiscard]] bool everyone_ready() const;
    /// Begins the game made from the lobby: every computer in the lobby gets it.
    void start(sim::Simulation& simulation, const assets::BuildingTable& buildings);

    /// Frames complete so far (all frames below this one).
    [[nodiscard]] std::uint64_t closed() const noexcept { return m_closed; }
    /// Frames the host closes ahead of its game now.
    [[nodiscard]] std::uint16_t lead() const noexcept { return m_lead; }
    /// Frames kept: closed but not yet sent, or not yet run by the host.
    [[nodiscard]] std::size_t kept_frames() const noexcept { return m_log.size(); }
    /// Fingerprints of other computers compared with the host's so far (and found equal).
    [[nodiscard]] std::uint64_t fingerprints_compared() const noexcept { return m_compared; }
    [[nodiscard]] std::uint64_t fingerprints_matched() const noexcept { return m_matched; }

private:
    struct Peer;
    struct Frame {
        std::vector<sim::Command> commands;
    };

    void receive(LinkId link, std::span<const std::uint8_t> bytes);
    void hello(Peer& peer, const Hello& m);
    void commands(Peer& peer, CommandBatch& m);
    void progress(Peer& peer, const Progress& m);
    void take_seat(Peer& peer, Seat seat);
    void ready(Peer& peer, bool ready);
    void chat(Peer& peer, const Chat& m);
    void broadcast(const Message& message);
    /// Moves a computer (or the host, `peer` nullptr) to `seat`, or to watching.
    void move(Peer* peer, Seat seat);
    void queue(Seat from, sim::Command command);
    void close_frame();
    void run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick);
    void compare(Peer& peer, const FrameHash& theirs);
    void desync(Peer& peer, std::uint64_t frame);
    void send_game(Peer& peer);
    void broadcast_frames();
    void forget_frames();
    void update_lead(std::uint32_t real_ms);
    void watch_stalls(std::uint32_t real_ms, bool held);
    void drop(Peer& peer, std::optional<RefuseReason> tell);
    void refresh_roster();
    void init_lobby();
    /// The token of a slot (made when it is first needed).
    [[nodiscard]] std::uint64_t token_of(Seat seat);
    /// Commands each player may put into one frame: the configured limit or an equal share of a
    /// message, whichever is less.
    [[nodiscard]] std::uint32_t frame_share() const;
    /// The slot's token is no longer good: the slot changed hands.
    void forget_token(Seat seat);
    /// Tells the successor what it needs to carry the game on, if that changed (Standby).
    void hand_over();
    [[nodiscard]] Peer* peer_of(LinkId link);
    [[nodiscard]] Seat free_seat(Seat wanted) const;
    [[nodiscard]] bool seat_taken(Seat seat) const;
    [[nodiscard]] std::uint64_t slowest() const;
    /// Frames the host may be ahead of a computer at the current speed.
    [[nodiscard]] std::uint64_t ahead_frames() const;

    sim::Simulation* m_simulation;
    const assets::BuildingTable* m_buildings;
    std::string m_name;
    std::uint64_t m_content;
    std::vector<std::unique_ptr<Peer>> m_peers;
    std::deque<Frame> m_log;  ///< frames m_log_start to m_closed - 1
    std::uint64_t m_log_start = 0;
    std::uint64_t m_closed = 0;                            ///< frames below are complete
    std::uint64_t m_broadcast = 0;                         ///< frames below went out to every computer
    std::vector<sim::Command> m_open;                      ///< for frame m_closed
    std::uint32_t m_open_own = 0;                          ///< commands of the host's player in m_open
    std::deque<std::pair<Seat, sim::Command>> m_overflow;  ///< no room in m_open: for the next frames
    std::uint16_t m_lead = 0;
    std::uint32_t m_lead_ms = 0;     ///< time since the lead last shrank
    std::deque<FrameHash> m_hashes;  ///< the host's latest fingerprints
    std::uint32_t m_held_ms = 0;     ///< how long the host has been waiting for a computer
    std::optional<Snapshot> m_game;  ///< the game as sent in this update
    /// Games the host may still write for computers that join or resync, in milliseconds of credit
    /// (kGameCostMs a game, kGameCreditMs at most; it grows with real time).
    std::uint32_t m_game_credit_ms = 4'000;
    std::vector<std::pair<Seat, std::uint64_t>> m_tokens;  ///< seat and token, to come back to it
    std::vector<Seat> m_computer_held;  ///< open at the start: the computer plays it while nobody does
    std::uint64_t m_compared = 0;
    std::uint64_t m_matched = 0;
    std::vector<LinkId> m_roster_links;    ///< the link of each roster() entry (unused for the host's own)
    std::optional<LinkId> m_standby_link;  ///< the successor, as last told
    Standby m_standby;                     ///< what it was told
    bool m_standby_changed = true;
    bool m_roster_changed = true;
    bool m_lobby_changed = true;
    std::uint32_t m_chat_ms = 0;  ///< into the current second of the chat limit
};

/// A computer that joins a host. It has no game until the host sends one: `load` turns the
/// savegame into a running simulation (the caller keeps it) or gives nullptr if it cannot.
class ClientSession final : public Session {
public:
    using Loader = std::function<sim::Simulation*(std::span<const std::uint8_t> save)>;
    /// A connection to the successor (nullptr if none can be made now).
    using Connect = std::function<std::unique_ptr<Transport>()>;

    ClientSession(std::unique_ptr<Transport> transport, Hello hello, Loader load, SessionConfig config = {});
    /// Host migration: follows `old`, whose host left, to the successor: the same name and slot (its
    /// token), the game anew from there. Connects with `connect`, again and again until it is taken
    /// in, for SessionConfig::migration_ms; then it ends with ConnectFailed.
    ClientSession(const ClientSession& old, Connect connect);

    void submit(sim::Command command) override;
    void update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick = {}) override;
    void say(std::string text) override;
    /// In the lobby: asks for an open slot, or kNoPlayer to watch.
    void take_seat(Seat seat);
    /// In the lobby: ready to start, or not.
    void set_ready(bool ready);

    /// Taken in by the host (in the lobby or the game).
    [[nodiscard]] bool welcomed() const noexcept { return m_welcomed; }
    /// The game runs (a game came from the host and was loaded).
    [[nodiscard]] bool playing() const noexcept { return m_simulation != nullptr; }
    /// Token to come back into the same slot after a lost connection (Hello::token).
    [[nodiscard]] std::uint64_t token() const noexcept { return m_token; }
    /// Frames received but not run yet.
    [[nodiscard]] std::uint64_t frames_in_hand() const noexcept { return m_closed - m_frames; }
    /// The host left the running game and it can move on: this computer hosts it next
    /// (hosts_next()), or follows the successor at successor_address().
    [[nodiscard]] bool can_migrate() const noexcept;
    /// The host left and named this computer its successor, with what it needs (Standby).
    [[nodiscard]] bool hosts_next() const noexcept;
    /// The name of the successor as the roster has it.
    [[nodiscard]] std::string successor_name() const;

private:
    friend class HostSession;  // made from a ClientSession when the host leaves

    void receive(std::span<const std::uint8_t> bytes);
    void load(Snapshot& m);
    void turn(Turn& m);
    void run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick);
    void report(std::uint32_t real_ms);

    static constexpr LinkId kHost = 0;
    Hello m_hello;
    Loader m_load;
    sim::Simulation* m_simulation = nullptr;
    bool m_connected = false;
    bool m_welcomed = false;
    std::uint64_t m_token = 0;
    std::uint32_t m_epoch = 0;   ///< of the game last loaded (Snapshot::epoch)
    std::uint64_t m_closed = 0;  ///< frames below have come from the host
    std::uint16_t m_lead = 0;
    std::deque<std::vector<sim::Command>> m_frames_ahead;  ///< frames m_frames to m_closed - 1
    std::vector<sim::Command> m_outgoing;
    std::uint64_t m_reported = 0;      ///< frames last told the host
    std::uint32_t m_since_report = 0;  ///< ms
    std::optional<Standby> m_standby;  ///< from the host, if it named this computer its successor
    Connect m_connect;                 ///< following a successor: connects again after a failure
    std::uint32_t m_migration_ms = 0;  ///< since it began to follow
    std::uint32_t m_retry_ms = 0;      ///< since the last connection failed
};

}  // namespace o1602::net
