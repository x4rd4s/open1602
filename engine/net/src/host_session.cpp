// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <limits>
#include <random>
#include <utility>

#include "o1602/net/session.hpp"
#include "o1602/sim/save_game.hpp"

namespace o1602::net {

namespace {

constexpr std::size_t kKeptHashes = 32;  ///< the host's own fingerprints, for computers behind it
constexpr std::size_t kPeerHashes = 16;  ///< fingerprints of a computer ahead of the host
constexpr std::uint32_t kLeadShrinkMs = 500;
constexpr std::uint32_t kJitterMarginMs = 30;

std::uint64_t random_token() {
    std::random_device device;
    std::uint64_t t = 0;
    while (t == 0) {
        t = (std::uint64_t{device()} << 32) ^ device();
    }
    return t;
}

/// Frames' worth of commands a player may have waiting for room in a frame.
constexpr std::uint32_t kWaitingFrames = 4;
/// Pauses, resumes and renames a computer may send per second.
constexpr std::uint32_t kMaxGameOrdersPerSecond = 4;
/// Time a new connection has to send its Hello.
constexpr std::uint32_t kGreetingMs = 10'000;

/// A name or chat line from the network without control characters (line breaks would split the
/// lists and files they end up in).
std::string printable(std::string text) {
    std::replace_if(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }, ' ');
    return text;
}

}  // namespace

struct HostSession::Peer {
    enum class State : std::uint8_t {
        Greeting,  ///< connected, no Hello yet (or welcomed and waiting for its game)
        Loading,   ///< its game is on the way or loading
        Playing,
    };
    LinkId link = 0;
    State state = State::Greeting;
    bool gone = false;  ///< dropped; removed at the end of the update
    bool welcomed = false;
    bool game_due = false;  ///< send it the game at the end of this update
    Seat seat = sim::kNoPlayer;
    std::string name;
    std::uint32_t epoch = 0;   ///< of the last game sent
    std::uint64_t frames = 0;  ///< run, as last reported
    bool lagging = false;
    std::uint64_t token = 0;        ///< to come back to its slot (0: it watches)
    std::uint32_t chat_lines = 0;   ///< in the current second
    std::uint32_t game_orders = 0;  ///< pauses and renames in the current second
    std::uint32_t greeting_ms = 0;  ///< connected without a Hello this long
    std::uint32_t resyncs = 0;      ///< games sent after desyncs without a matching fingerprint since
    std::uint32_t this_frame = 0;   ///< commands in the open frame
    std::deque<FrameHash> hashes;   ///< reported ahead of the host, not compared yet
};

HostSession::HostSession(std::unique_ptr<Transport> transport, sim::Simulation& simulation,
                         const assets::BuildingTable& buildings, Seat seat, std::string name, std::uint64_t content,
                         SessionConfig config)
    : Session(std::move(transport), config),
      m_simulation(&simulation),
      m_buildings(&buildings),
      m_name(std::move(name)),
      m_content(content),
      m_lead(m_config.min_lead) {
    m_seat = seat;
    // The lobby of a running game: its human slots are open, and so are the computer players'
    // (the computer plays one until someone comes and again when he leaves).
    m_lobby.started = true;
    const auto& players = simulation.world().players();
    for (std::size_t s = 0; s < players.size() && s < kMaxLobbySeats; ++s) {
        LobbySeat lobby_seat;
        if (s == seat) {
            lobby_seat = LobbySeat{LobbySeat::Kind::Human, m_name, true};
        } else if (players[s].role != sim::PlayerRole::Human && players[s].role != sim::PlayerRole::Computer) {
            lobby_seat.kind = LobbySeat::Kind::Reserved;
        } else if (!players[s].active()) {
            lobby_seat.kind = LobbySeat::Kind::Closed;
        } else if (players[s].role == sim::PlayerRole::Computer) {
            m_computer_held.push_back(static_cast<Seat>(s));
        }
        m_lobby.seats.push_back(std::move(lobby_seat));
    }
    init_lobby();
}

HostSession::HostSession(std::unique_ptr<Transport> transport, Lobby lobby, Seat seat, std::string name,
                         std::uint64_t content, SessionConfig config)
    : Session(std::move(transport), config),
      m_simulation(nullptr),
      m_buildings(nullptr),
      m_name(std::move(name)),
      m_content(content),
      m_lead(m_config.min_lead) {
    m_lobby = std::move(lobby);
    if (m_lobby.seats.size() > kMaxLobbySeats) {
        m_lobby.seats.resize(kMaxLobbySeats);
    }
    m_lobby.started = false;
    for (LobbySeat& s : m_lobby.seats) {
        if (s.kind == LobbySeat::Kind::Human) {
            s = LobbySeat{};  // only computers that come in are human players
        }
    }
    m_seat = seat < m_lobby.seats.size() ? seat : sim::kNoPlayer;
    if (m_seat != sim::kNoPlayer) {
        m_lobby.seats[m_seat] = LobbySeat{LobbySeat::Kind::Human, m_name, true};
    }
    init_lobby();
}

void HostSession::init_lobby() {
    m_lobby.join_running = m_config.join_running;
    m_lobby.spectators = m_config.spectators;
    m_lobby.password = !m_config.password.empty();
    m_lobby.title.resize(std::min(m_lobby.title.size(), kMaxTitleLength));
    m_lobby.game.resize(std::min(m_lobby.game.size(), kMaxTitleLength));
    if (m_lobby.setup.size() > kMaxSetupBytes) {
        m_lobby.setup.clear();
    }
    refresh_roster();
}

HostSession::~HostSession() {
    for (const auto& p : m_peers) {
        m_transport->disconnect(p->link);
    }
    m_transport->flush();
}

void HostSession::submit(sim::Command command) {
    queue(m_seat, std::move(command));
}

void HostSession::queue(Seat from, sim::Command command) {
    if (from == sim::kNoPlayer || command_player(command) != from) {
        return;  // a player gives orders for himself only (review point 6)
    }
    std::uint32_t* count = &m_open_own;
    if (from != m_seat) {
        const auto it = std::find_if(m_peers.begin(), m_peers.end(),
                                     [from](const auto& p) { return !p->gone && p->welcomed && p->seat == from; });
        if (it == m_peers.end()) {
            return;
        }
        count = &(*it)->this_frame;
    }
    // Per player, and for the frame as a whole: one frame must fit into one message. Each player
    // has his share of it, so that a few cannot crowd out the others; what does not fit waits for
    // the next frames, up to a few frames' worth.
    const std::uint32_t share = frame_share();
    if (*count >= share || m_open.size() >= kMaxCommandsPerMessage) {
        const auto waiting = static_cast<std::size_t>(
            std::count_if(m_overflow.begin(), m_overflow.end(), [from](const auto& w) { return w.first == from; }));
        if (waiting < std::size_t{kWaitingFrames} * share) {
            m_overflow.emplace_back(from, std::move(command));
        }
        return;
    }
    ++*count;
    m_open.push_back(std::move(command));
}

std::uint32_t HostSession::frame_share() const {
    std::uint32_t players = m_seat != sim::kNoPlayer ? 1U : 0U;
    for (const auto& p : m_peers) {
        players += !p->gone && p->welcomed && p->seat != sim::kNoPlayer ? 1U : 0U;
    }
    const auto fair = static_cast<std::uint32_t>(kMaxCommandsPerMessage / std::max(players, 1U));
    return std::max(1U, std::min(m_config.max_commands_per_frame, fair));
}

void HostSession::update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    m_incoming.clear();
    m_transport->poll(m_incoming);
    for (TransportEvent& e : m_incoming) {
        switch (e.kind) {
            case TransportEvent::Kind::Connected: {
                auto peer = std::make_unique<Peer>();
                peer->link = e.link;
                m_peers.push_back(std::move(peer));
                break;
            }
            case TransportEvent::Kind::Disconnected:
                if (Peer* p = peer_of(e.link)) {
                    drop(*p, std::nullopt);
                }
                break;
            case TransportEvent::Kind::Received:
                receive(e.link, e.data);
                break;
        }
    }
    if (m_chat_ms += real_ms; m_chat_ms >= 1000) {
        m_chat_ms = 0;
        for (const auto& p : m_peers) {
            p->chat_lines = 0;
            p->game_orders = 0;
        }
    }
    // A connection that never says who it is would hold one of the few connections for good.
    for (const auto& p : m_peers) {
        if (!p->gone && !p->welcomed && (p->greeting_ms += real_ms) > kGreetingMs) {
            drop(*p, std::nullopt);
        }
    }
    if (m_lobby_changed) {
        m_lobby_changed = false;
        broadcast(m_lobby);
        lobby_event();
    }
    if (started()) {
        update_lead(real_ms);
        run(real_ms, before_tick);
        broadcast_frames();
        m_game.reset();
        for (std::size_t i = 0; i < m_peers.size(); ++i) {
            if (m_peers[i]->game_due) {
                send_game(*m_peers[i]);
            }
        }
        m_game.reset();
        forget_frames();
    }
    std::erase_if(m_peers, [](const auto& p) { return p->gone; });
    if (m_roster_changed) {
        refresh_roster();
    }
    m_transport->flush();
}

HostSession::Peer* HostSession::peer_of(LinkId link) {
    const auto it =
        std::find_if(m_peers.begin(), m_peers.end(), [link](const auto& p) { return !p->gone && p->link == link; });
    return it == m_peers.end() ? nullptr : it->get();
}

void HostSession::receive(LinkId link, std::span<const std::uint8_t> bytes) {
    Peer* peer = peer_of(link);
    if (peer == nullptr) {
        return;
    }
    if (bytes.size() > kMaxMessageToHost) {
        drop(*peer, RefuseReason::Invalid);  // a computer sends commands and reports, never games
        return;
    }
    auto message = decode(bytes);
    if (!message) {
        drop(*peer, RefuseReason::Invalid);
        return;
    }
    if (auto* hello_message = std::get_if<Hello>(&*message); hello_message != nullptr && !peer->welcomed) {
        hello(*peer, *hello_message);
    } else if (!peer->welcomed) {
        drop(*peer, RefuseReason::Invalid);  // anything before Hello, or a message only the host sends
    } else if (auto* batch = std::get_if<CommandBatch>(&*message)) {
        commands(*peer, *batch);
    } else if (const auto* report = std::get_if<Progress>(&*message)) {
        progress(*peer, *report);
    } else if (const auto* seat = std::get_if<TakeSeat>(&*message)) {
        take_seat(*peer, seat->seat);
    } else if (const auto* r = std::get_if<SetReady>(&*message)) {
        ready(*peer, r->ready);
    } else if (const auto* line = std::get_if<Chat>(&*message)) {
        chat(*peer, *line);
    } else {
        drop(*peer, RefuseReason::Invalid);
    }
}

void HostSession::hello(Peer& peer, const Hello& m) {
    if (m.protocol != kProtocolVersion) {
        drop(peer, RefuseReason::Version);
        return;
    }
    if (m.content != m_content) {
        drop(peer, RefuseReason::Content);
        return;
    }
    if (m.password != m_config.password) {
        drop(peer, RefuseReason::Password);
        return;
    }
    Seat seat = sim::kNoPlayer;
    if (m.token != 0) {
        const auto it =
            std::find_if(m_tokens.begin(), m_tokens.end(), [&](const auto& t) { return t.second == m.token; });
        // In the lobby a slot is only kept while the computer is in.
        // Never the host's own slot (it has no token, but one handed out before the host took it).
        if (it != m_tokens.end() && it->first != m_seat && (started() || free_seat(it->first) == it->first)) {
            seat = it->first;
            // Its old connection has not timed out yet: this is the same player, back.
            const auto old = std::find_if(m_peers.begin(), m_peers.end(), [&](const auto& p) {
                return p.get() != &peer && !p->gone && p->welcomed && p->seat == seat;
            });
            if (old != m_peers.end()) {
                drop(**old, std::nullopt);
            }
        }
    }
    if (seat == sim::kNoPlayer) {
        if (started() && !m_config.join_running) {
            drop(peer, RefuseReason::Closed);
            return;
        }
        seat = free_seat(m.seat);
        if (seat == sim::kNoPlayer && !m_config.spectators) {
            drop(peer, RefuseReason::Full);
            return;
        }
        // Someone new in the slot: whoever had it before cannot come back with the old token.
        forget_token(seat);
    }
    peer.welcomed = true;
    peer.game_due = started();
    peer.seat = seat;
    peer.name = printable(m.name);
    if (peer.name.empty()) {
        peer.name = "Player";  // replaced by the player's own name (PlayerRename) once the game runs
    }
    if (seat != sim::kNoPlayer) {
        peer.token = token_of(seat);
        m_lobby.seats[seat] = LobbySeat{LobbySeat::Kind::Human, peer.name, false};
        if (started()) {
            // The player takes the slot over from the computer, under the player's name, as
            // commands of that player like any other.
            queue(seat, sim::PlayerControl{seat, false});
            queue(seat, sim::PlayerRename{seat, peer.name});
        }
    }
    send(peer.link, Welcome{kProtocolVersion, seat, peer.token});
    m_lobby_changed = true;
    SessionEvent e;
    e.kind = SessionEvent::Kind::Joined;
    e.seat = seat;
    e.name = peer.name;
    e.frame = m_frames;
    event(std::move(e));
    m_roster_changed = true;
}

void HostSession::commands(Peer& peer, CommandBatch& m) {
    if (peer.seat == sim::kNoPlayer || peer.state == Peer::State::Greeting) {
        return;  // spectators give no orders, and nobody without a game
    }
    for (sim::Command& c : m.commands) {
        // Only the host hands slots to the computer and sets the speed (ADR 0011); everyone may
        // pause and resume and name himself, a few times a second.
        if (std::holds_alternative<sim::PlayerControl>(c)) {
            continue;
        }
        if (const auto* speed = std::get_if<sim::GameSpeedOrder>(&c)) {
            if (speed->op == sim::GameSpeedOrder::Op::SetSpeed) {
                continue;
            }
        }
        if (std::holds_alternative<sim::GameSpeedOrder>(c) || std::holds_alternative<sim::PlayerRename>(c)) {
            if (peer.game_orders >= kMaxGameOrdersPerSecond) {
                continue;
            }
            ++peer.game_orders;
        }
        queue(peer.seat, std::move(c));
    }
}

void HostSession::progress(Peer& peer, const Progress& m) {
    if (m.epoch != peer.epoch || peer.state == Peer::State::Greeting) {
        return;  // about a game it had before the last one sent
    }
    if (m.frames < peer.frames || m.frames > m_closed) {
        drop(peer, RefuseReason::Invalid);
        return;
    }
    peer.frames = m.frames;
    if (peer.state == Peer::State::Loading) {
        peer.state = Peer::State::Playing;
        // Far behind after loading: it catches up without the others waiting for it.
        peer.lagging = m_frames > peer.frames + ahead_frames();
        m_roster_changed = true;
    } else if (peer.lagging && m_frames <= peer.frames + ahead_frames()) {
        peer.lagging = false;
        m_roster_changed = true;
    }
    if (m.hash) {
        compare(peer, *m.hash);
    }
}

void HostSession::compare(Peer& peer, const FrameHash& theirs) {
    if (theirs.frame > peer.frames) {
        drop(peer, RefuseReason::Invalid);
        return;
    }
    if (theirs.frame > m_frames) {
        // Ahead of the host: compared once the host has run that frame.
        if (peer.hashes.size() == kPeerHashes) {
            peer.hashes.pop_front();
        }
        peer.hashes.push_back(theirs);
        return;
    }
    const auto mine =
        std::find_if(m_hashes.begin(), m_hashes.end(), [&](const FrameHash& h) { return h.frame == theirs.frame; });
    if (mine == m_hashes.end()) {
        return;  // too old, or a frame whose game could not be written
    }
    ++m_compared;
    if (mine->hash == theirs.hash) {
        ++m_matched;
        peer.resyncs = 0;
    } else {
        desync(peer, theirs.frame);
    }
}

void HostSession::desync(Peer& peer, std::uint64_t frame) {
    SessionEvent e;
    e.kind = SessionEvent::Kind::Desync;
    e.seat = peer.seat;
    e.name = peer.name;
    e.frame = frame;
    event(std::move(e));
    if (peer.resyncs >= m_config.max_resyncs) {
        drop(peer, RefuseReason::Desync);
        return;
    }
    ++peer.resyncs;
    peer.game_due = true;
    broadcast(Desync{frame, {peer.seat}});
}

void HostSession::close_frame() {
    m_log.push_back(Frame{std::move(m_open)});
    m_open.clear();
    ++m_closed;
    m_open_own = 0;
    for (const auto& p : m_peers) {
        p->this_frame = 0;
    }
    // The commands that waited go first into the new frame, in the order they came.
    std::deque<std::pair<Seat, sim::Command>> waiting;
    waiting.swap(m_overflow);
    for (auto& [from, command] : waiting) {
        queue(from, std::move(command));
    }
}

std::uint64_t HostSession::ahead_frames() const {
    const std::uint32_t speed = m_simulation->paused() ? 1 : std::max<std::uint32_t>(m_simulation->speed(), 1);
    return m_lead + std::uint64_t{m_config.max_ahead_ms} * speed / sim::Simulation::kTickMs;
}

std::uint64_t HostSession::slowest() const {
    std::uint64_t frames = std::numeric_limits<std::uint64_t>::max();
    for (const auto& p : m_peers) {
        if (!p->gone && p->state == Peer::State::Playing && !p->lagging) {
            frames = std::min(frames, p->frames);
        }
    }
    return frames;
}

void HostSession::run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    const auto close_ahead = [this] {
        while (m_closed < m_frames + m_lead) {
            close_frame();
        }
    };
    close_ahead();
    const std::uint32_t due = frames_due(real_ms, *m_simulation);
    const std::uint64_t slow = slowest();
    const std::uint64_t limit = slow == std::numeric_limits<std::uint64_t>::max() ? slow : slow + ahead_frames();
    std::uint32_t done = 0;
    bool held = false;
    while (done < due) {
        if (m_frames >= limit) {
            held = true;
            break;
        }
        if (before_tick && !before_tick()) {
            break;
        }
        const Frame& frame = m_log[static_cast<std::size_t>(m_frames - m_log_start)];
        if (const auto hash = run_frame(*m_simulation, frame.commands)) {
            if (m_hashes.size() == kKeptHashes) {
                m_hashes.pop_front();
            }
            m_hashes.push_back(*hash);
            for (const auto& peer : m_peers) {
                Peer& p = *peer;
                const auto theirs = std::find_if(p.hashes.begin(), p.hashes.end(),
                                                 [&](const FrameHash& h) { return h.frame == hash->frame; });
                if (theirs != p.hashes.end()) {
                    const FrameHash h = *theirs;
                    p.hashes.erase(p.hashes.begin(), theirs + 1);
                    compare(p, h);
                }
            }
        }
        ++done;
        close_ahead();
    }
    ran(done);
    m_waiting = held;
    watch_stalls(real_ms, held);
}

void HostSession::watch_stalls(std::uint32_t real_ms, bool held) {
    if (!held) {
        m_held_ms = 0;
    } else if (m_held_ms += real_ms; m_held_ms >= m_config.stall_ms) {
        m_held_ms = 0;
        for (const auto& p : m_peers) {
            if (!p->gone && p->state == Peer::State::Playing && !p->lagging && p->frames + ahead_frames() <= m_frames) {
                p->lagging = true;
                m_roster_changed = true;
                SessionEvent e;
                e.kind = SessionEvent::Kind::Lagging;
                e.seat = p->seat;
                e.name = p->name;
                e.frame = m_frames;
                event(std::move(e));
            }
        }
    }
    for (const auto& p : m_peers) {
        if (!p->gone && p->lagging && !p->game_due && p->frames + m_config.resync_behind < m_frames) {
            p->game_due = true;  // quicker to load the game as it is now than to run all those frames
        }
    }
}

void HostSession::broadcast_frames() {
    if (m_broadcast == m_closed) {
        return;
    }
    Turn turn;
    turn.first = m_broadcast;
    turn.closed = m_closed;
    turn.lead = m_lead;
    for (std::uint64_t f = m_broadcast; f < m_closed; ++f) {
        const Frame& frame = m_log[static_cast<std::size_t>(f - m_log_start)];
        if (!frame.commands.empty()) {
            turn.frames.push_back(FrameCommands{f, frame.commands});
        }
    }
    const std::vector<std::uint8_t> bytes = encode(turn);
    for (const auto& p : m_peers) {
        if (!p->gone && p->state != Peer::State::Greeting) {
            m_transport->send(p->link, bytes);
        }
    }
    m_broadcast = m_closed;
}

void HostSession::send_game(Peer& peer) {
    peer.game_due = false;
    if (!m_game) {
        // Written once for all computers that get it in this update.
        auto save = sim::write_game(m_simulation->world(), *m_buildings, m_simulation);
        if (!save || save.value().size() > kMaxSnapshotBytes) {
            drop(peer, RefuseReason::Closed);  // the game cannot be written: nobody can come in
            return;
        }
        m_game = Snapshot{0, m_frames, m_simulation->speed(), m_simulation->paused(), std::move(save).value()};
    }
    m_game->epoch = ++peer.epoch;
    send(peer.link, *m_game);
    // The frames closed beyond the game sent; later ones follow with everyone's.
    Turn turn;
    turn.first = m_frames;
    turn.closed = m_closed;
    turn.lead = m_lead;
    for (std::uint64_t f = m_frames; f < m_closed; ++f) {
        const Frame& frame = m_log[static_cast<std::size_t>(f - m_log_start)];
        if (!frame.commands.empty()) {
            turn.frames.push_back(FrameCommands{f, frame.commands});
        }
    }
    send(peer.link, turn);
    peer.state = Peer::State::Loading;
    peer.frames = m_frames;
    peer.lagging = false;
    peer.hashes.clear();
    m_roster_changed = true;
}

void HostSession::forget_frames() {
    // Kept: what has not gone out yet, and what the host has not run (for a game sent now).
    const std::uint64_t keep = std::min(m_frames, m_broadcast);
    while (m_log_start < keep) {
        m_log.pop_front();
        ++m_log_start;
    }
}

void HostSession::update_lead(std::uint32_t real_ms) {
    std::uint32_t round_trip = 0;
    for (const auto& p : m_peers) {
        if (!p->gone && p->state != Peer::State::Greeting) {
            round_trip = std::max(round_trip, m_transport->round_trip_ms(p->link));
        }
    }
    const std::uint32_t speed = m_simulation->paused() ? 1 : std::max<std::uint32_t>(m_simulation->speed(), 1);
    // Frames that pass while a frame travels one way, and one more.
    const std::uint64_t one_way = (std::uint64_t{round_trip} / 2 + kJitterMarginMs) * speed;
    const std::uint64_t wanted = (one_way + sim::Simulation::kTickMs - 1) / sim::Simulation::kTickMs + 1;
    const auto lead =
        static_cast<std::uint16_t>(std::clamp<std::uint64_t>(wanted, m_config.min_lead, m_config.max_lead));
    if (lead >= m_lead) {
        m_lead = lead;
        m_lead_ms = 0;
    } else if (m_lead_ms += real_ms; m_lead_ms >= kLeadShrinkMs) {
        --m_lead;  // slowly: a short good spell must not empty the computers' buffers
        m_lead_ms = 0;
    }
}

void HostSession::drop(Peer& peer, std::optional<RefuseReason> tell) {
    if (peer.gone) {
        return;
    }
    peer.gone = true;
    peer.game_due = false;
    if (tell) {
        send(peer.link, Refused{*tell});
    }
    m_transport->disconnect(peer.link);
    if (peer.welcomed) {
        if (peer.seat != sim::kNoPlayer) {
            m_lobby.seats[peer.seat] = LobbySeat{};
            m_lobby_changed = true;
            // A slot the computer held before the player came: the computer goes on with it.
            // The host gives it in the player's stead (queue() takes orders only from those here).
            if (started() && m_open.size() < kMaxCommandsPerMessage &&
                std::find(m_computer_held.begin(), m_computer_held.end(), peer.seat) != m_computer_held.end()) {
                m_open.emplace_back(sim::PlayerControl{peer.seat, true});
            }
        }
        SessionEvent e;
        e.kind = SessionEvent::Kind::Left;
        e.seat = peer.seat;
        e.name = peer.name;
        e.frame = m_frames;
        event(std::move(e));
        m_roster_changed = true;
    }
}

bool HostSession::seat_taken(Seat seat) const {
    return seat == m_seat || std::any_of(m_peers.begin(), m_peers.end(),
                                         [seat](const auto& p) { return !p->gone && p->welcomed && p->seat == seat; });
}

Seat HostSession::free_seat(Seat wanted) const {
    const auto free = [&](std::size_t s) {
        if (s >= m_lobby.seats.size() || seat_taken(static_cast<Seat>(s))) {
            return false;
        }
        if (!started()) {
            return m_lobby.seats[s].kind == LobbySeat::Kind::Open;
        }
        // A slot nobody plays: open in the lobby, still in the game, and steered by a person who
        // left or by the computer that holds it for whoever comes.
        const auto& players = m_simulation->world().players();
        return m_lobby.seats[s].kind == LobbySeat::Kind::Open && s < players.size() && players[s].active() &&
               (players[s].role == sim::PlayerRole::Human || players[s].role == sim::PlayerRole::Computer);
    };
    if (free(wanted)) {
        return wanted;
    }
    for (std::size_t s = 0; s < m_lobby.seats.size(); ++s) {
        if (free(s)) {
            return static_cast<Seat>(s);
        }
    }
    return sim::kNoPlayer;
}

void HostSession::refresh_roster() {
    m_roster_changed = false;
    m_roster.clear();
    m_roster.push_back(Member{m_seat, m_name, Member::State::Playing, true});
    for (const auto& p : m_peers) {
        if (p->gone || !p->welcomed) {
            continue;
        }
        const Member::State state = p->lagging                         ? Member::State::Lagging
                                    : p->state == Peer::State::Playing ? Member::State::Playing
                                                                       : Member::State::Loading;
        m_roster.push_back(Member{p->seat, p->name, state, false});
    }
    broadcast(Roster{m_roster});
}

void HostSession::broadcast(const Message& message) {
    const std::vector<std::uint8_t> bytes = encode(message);
    for (const auto& p : m_peers) {
        if (!p->gone && p->welcomed) {
            m_transport->send(p->link, bytes);
        }
    }
}

std::uint64_t HostSession::token_of(Seat seat) {
    const auto it = std::find_if(m_tokens.begin(), m_tokens.end(), [&](const auto& t) { return t.first == seat; });
    if (it != m_tokens.end()) {
        return it->second;
    }
    // Drawn at random, so that one token tells nothing about the others.
    std::uint64_t token = random_token();
    while (std::any_of(m_tokens.begin(), m_tokens.end(), [&](const auto& t) { return t.second == token; })) {
        token = random_token();
    }
    m_tokens.emplace_back(seat, token);
    return token;
}

void HostSession::forget_token(Seat seat) {
    std::erase_if(m_tokens, [&](const auto& t) { return t.first == seat; });
}

// --- The lobby ---

void HostSession::move(Peer* peer, Seat seat) {
    Seat& current = peer != nullptr ? peer->seat : m_seat;
    if (current != sim::kNoPlayer) {
        m_lobby.seats[current] = LobbySeat{};
        // Whoever takes the slot later gets a new token, so that nobody collects the tokens of
        // the slots he passes through.
        forget_token(current);
    }
    current = seat;
    if (seat != sim::kNoPlayer) {
        const std::string& name = peer != nullptr ? peer->name : m_name;
        m_lobby.seats[seat] = LobbySeat{LobbySeat::Kind::Human, name, peer == nullptr};
    }
    if (peer != nullptr) {
        peer->token = seat != sim::kNoPlayer ? token_of(seat) : 0;
        send(peer->link, Welcome{kProtocolVersion, seat, peer->token});
    }
    m_lobby_changed = true;
    m_roster_changed = true;
}

void HostSession::take_seat(Peer& peer, Seat seat) {
    if (started() || seat == peer.seat) {
        return;
    }
    if (seat == sim::kNoPlayer ? m_config.spectators : free_seat(seat) == seat) {
        move(&peer, seat);
    }
}

bool HostSession::take_seat(Seat seat) {
    if (started() || seat == m_seat) {
        return !started();
    }
    if (seat != sim::kNoPlayer && free_seat(seat) != seat) {
        return false;
    }
    move(nullptr, seat);
    return true;
}

void HostSession::ready(Peer& peer, bool ready) {
    if (started() || peer.seat == sim::kNoPlayer || m_lobby.seats[peer.seat].ready == ready) {
        return;
    }
    m_lobby.seats[peer.seat].ready = ready;
    m_lobby_changed = true;
}

void HostSession::chat(Peer& peer, const Chat& m) {
    if (m.text.empty() || peer.chat_lines >= m_config.max_chat_per_second) {
        return;
    }
    ++peer.chat_lines;
    const Chat line{peer.seat, peer.name, printable(m.text)};
    broadcast(line);
    chat_event(line);
}

void HostSession::say(std::string text) {
    text.resize(std::min(text.size(), kMaxChatLength));
    if (text.empty()) {
        return;
    }
    const Chat line{m_seat, m_name, std::move(text)};
    broadcast(line);
    chat_event(line);
}

void HostSession::set_lobby_info(std::string title, std::string game, std::vector<std::uint8_t> setup) {
    m_lobby.title = std::move(title);
    m_lobby.game = std::move(game);
    m_lobby.setup = std::move(setup);
    m_lobby.title.resize(std::min(m_lobby.title.size(), kMaxTitleLength));
    m_lobby.game.resize(std::min(m_lobby.game.size(), kMaxTitleLength));
    if (m_lobby.setup.size() > kMaxSetupBytes) {
        m_lobby.setup.clear();
    }
    m_lobby_changed = true;
}

void HostSession::set_seat(Seat seat, LobbySeat::Kind kind) {
    if (started() || seat >= m_lobby.seats.size() || seat == m_seat || kind == LobbySeat::Kind::Human ||
        kind == LobbySeat::Kind::Reserved || m_lobby.seats[seat].kind == LobbySeat::Kind::Reserved) {
        return;
    }
    const auto holder =
        std::find_if(m_peers.begin(), m_peers.end(), [&](const auto& p) { return !p->gone && p->seat == seat; });
    if (holder != m_peers.end()) {
        if (m_config.spectators) {
            move(holder->get(), sim::kNoPlayer);
        } else {
            drop(**holder, RefuseReason::Full);
        }
    }
    m_lobby.seats[seat] = LobbySeat{kind, {}, false};
    m_lobby_changed = true;
}

void HostSession::kick(Seat seat) {
    const auto holder = std::find_if(m_peers.begin(), m_peers.end(),
                                     [&](const auto& p) { return !p->gone && p->welcomed && p->seat == seat; });
    if (seat != sim::kNoPlayer && holder != m_peers.end()) {
        drop(**holder, RefuseReason::Kicked);
        forget_token(seat);  // and it cannot come back with it
    }
}

bool HostSession::everyone_ready() const {
    return std::all_of(m_peers.begin(), m_peers.end(), [&](const auto& p) {
        return p->gone || !p->welcomed || p->seat == sim::kNoPlayer || m_lobby.seats[p->seat].ready;
    });
}

void HostSession::start(sim::Simulation& simulation, const assets::BuildingTable& buildings) {
    if (started()) {
        return;
    }
    m_simulation = &simulation;
    m_buildings = &buildings;
    m_lobby.started = true;
    m_lobby_changed = true;
    const auto& players = simulation.world().players();
    for (std::size_t s = 0; s < m_lobby.seats.size() && s < players.size(); ++s) {
        if (m_lobby.seats[s].kind == LobbySeat::Kind::Open && players[s].role == sim::PlayerRole::Computer) {
            m_computer_held.push_back(static_cast<Seat>(s));
        }
    }
    for (const auto& p : m_peers) {
        if (p->gone || !p->welcomed) {
            continue;
        }
        p->game_due = true;
        if (p->seat != sim::kNoPlayer) {
            queue(p->seat, sim::PlayerRename{p->seat, p->name});
        }
    }
}

}  // namespace o1602::net
