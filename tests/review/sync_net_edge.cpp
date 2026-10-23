// SPDX-License-Identifier: GPL-3.0-or-later
//
// Review tools (part SYNC): the lockstep sessions (ADR 0011) under conditions the unit tests do not
// cover: links that stall or reorder, computers that leave while a game is on the way or while the
// host changes, floods of commands and messages, slow computers, a lobby of twelve players, and the
// heaviest commands a player may send. Each test states the behaviour the multiplayer should have;
// one that fails is a finding of the review (docs in the review report, SYNC-n). Measurements are
// printed. Built with -DO1602_REVIEW_TESTS=ON (tests/review), not part of the unit tests.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/net/loopback.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/net/session.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

constexpr std::uint64_t kContent = 0x1602;
constexpr std::uint32_t kStepMs = 16;

const assets::BuildingTable& buildings() {
    static const assets::BuildingTable table;
    return table;
}

const assets::FigureTable& figures() {
    static const assets::FigureTable table;
    return table;
}

/// One computer's game.
struct Game {
    std::unique_ptr<sim::World> world;
    std::unique_ptr<sim::Simulation> simulation;
    int loads = 0;
    bool diverge = false;

    /// `players` slots; slot i is human if humans[i], else a computer player; slots in `reserved`
    /// are neutral (the free trader's and the others' of the original, 4 to 6).
    static Game make(std::size_t players, std::size_t computers_from, const std::vector<std::size_t>& reserved = {}) {
        Game g;
        g.world = std::make_unique<sim::World>(64, 64);
        std::vector<sim::Player> list(players);
        for (std::size_t p = 0; p < list.size(); ++p) {
            if (std::find(reserved.begin(), reserved.end(), p) != reserved.end()) {
                sim::make_neutral(list[p], sim::faction::kTrader);
                continue;
            }
            list[p].role = p < computers_from ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
            list[p].banner = static_cast<sim::BannerId>(p);
            list[p].name = "Spieler " + std::to_string(p);
            list[p].money = 1000;
        }
        g.world->set_players(list);
        for (std::size_t p = 0; p < list.size(); ++p) {
            if (list[p].role == sim::PlayerRole::Human) {
                g.world->add_ship(sim::make_ship("Schiff", 2 + 4 * static_cast<std::uint32_t>(p % 15),
                                                 10 + 4 * static_cast<std::uint32_t>(p / 15), 21,
                                                 static_cast<sim::PlayerId>(p)));
            }
        }
        g.simulation = std::make_unique<sim::Simulation>(*g.world, buildings(), figures());
        return g;
    }

    sim::Simulation* load(std::span<const std::uint8_t> save) {
        auto loaded = sim::read_game(save, buildings(), nullptr, &figures());
        if (!loaded) {
            return nullptr;
        }
        ++loads;
        if (diverge) {
            loaded.value().player(0)->money += 1;
        }
        simulation.reset();
        world = std::make_unique<sim::World>(std::move(loaded).value());
        simulation = std::make_unique<sim::Simulation>(*world, buildings(), figures());
        return simulation.get();
    }
};

/// A transport between a session and the network that can hold back what arrives (a link that
/// stalls: ENet resends lost packets, so loss shows as delay), let the next two messages swap
/// places, or that sends raw bytes of its own.
class Faulty final : public net::Transport {
public:
    explicit Faulty(std::unique_ptr<net::Transport> inner) : m_inner(std::move(inner)) {}

    bool hold = false;                    ///< nothing arrives while set
    bool swap_next = false;               ///< the next two messages that arrive swap places
    std::optional<net::Standby> standby;  ///< the last Standby that came through (what the successor knows)

    void poll(std::vector<net::TransportEvent>& events) override {
        std::vector<net::TransportEvent> got;
        m_inner->poll(got);
        for (auto& e : got) {
            m_queue.push_back(std::move(e));
        }
        if (hold) {
            return;
        }
        if (swap_next) {
            std::vector<std::size_t> received;
            for (std::size_t i = 0; i < m_queue.size() && received.size() < 2; ++i) {
                if (m_queue[i].kind == net::TransportEvent::Kind::Received) {
                    received.push_back(i);
                }
            }
            if (received.size() == 2) {
                std::swap(m_queue[received[0]], m_queue[received[1]]);
                swap_next = false;
            }
        }
        for (const auto& e : m_queue) {
            if (e.kind == net::TransportEvent::Kind::Received) {
                if (auto m = net::decode(e.data)) {
                    if (auto* st = std::get_if<net::Standby>(&*m)) {
                        standby = *st;
                    }
                }
            }
        }
        while (!m_queue.empty()) {
            events.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
        }
    }
    void send(net::LinkId link, std::span<const std::uint8_t> message) override { m_inner->send(link, message); }
    void flush() override { m_inner->flush(); }
    void disconnect(net::LinkId link) override { m_inner->disconnect(link); }
    [[nodiscard]] std::uint32_t round_trip_ms(net::LinkId link) const override { return m_inner->round_trip_ms(link); }
    [[nodiscard]] const net::Transport& inner() const { return *m_inner; }

private:
    std::unique_ptr<net::Transport> m_inner;
    std::deque<net::TransportEvent> m_queue;
};

struct Computer {
    Game game;
    std::unique_ptr<net::ClientSession> session;
    Faulty* link = nullptr;  ///< owned by the session
    std::vector<net::SessionEvent> events;

    Computer(net::LoopbackNetwork& network, std::string name, std::uint64_t token = 0,
             sim::PlayerId seat = sim::kNoPlayer, net::SessionConfig config = {}) {
        net::Hello hello;
        hello.content = kContent;
        hello.name = std::move(name);
        hello.token = token;
        hello.seat = seat;
        auto faulty = std::make_unique<Faulty>(network.connect());
        link = faulty.get();
        session = std::make_unique<net::ClientSession>(
            std::move(faulty), hello, [this](std::span<const std::uint8_t> save) { return game.load(save); }, config);
    }

    void update(std::uint32_t ms, const sim::Simulation::BeforeTick& before = {}) {
        session->update(ms, before);
        for (auto& e : session->take_events()) {
            events.push_back(std::move(e));
        }
    }
    [[nodiscard]] std::size_t count(net::SessionEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
};

struct Host {
    Game game;
    std::unique_ptr<net::HostSession> session;
    std::vector<net::SessionEvent> events;
    double most_ms = 0.0;  ///< the longest update
    double total_ms = 0.0;
    std::uint64_t updates = 0;

    Host(net::LoopbackNetwork& network, Game g, net::SessionConfig config = {}) : game(std::move(g)) {
        session = std::make_unique<net::HostSession>(network.listen(), *game.simulation, buildings(), 0, "Gastgeber",
                                                     kContent, config);
    }
    /// A host that began with a lobby and started `g`.
    Host(Game g, std::unique_ptr<net::HostSession> started) : game(std::move(g)), session(std::move(started)) {}
    void update(std::uint32_t ms) {
        const auto start = Clock::now();
        session->update(ms);
        const double took = ms_since(start);
        most_ms = std::max(most_ms, took);
        total_ms += took;
        ++updates;
        for (auto& e : session->take_events()) {
            events.push_back(std::move(e));
        }
    }
    [[nodiscard]] std::size_t count(net::SessionEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
};

void run_for(net::LoopbackNetwork& network, Host& host, const std::vector<Computer*>& computers,
             std::uint32_t total_ms) {
    for (std::uint32_t t = 0; t < total_ms; t += kStepMs) {
        network.advance(kStepMs);
        host.update(kStepMs);
        for (Computer* c : computers) {
            c->update(kStepMs);
        }
    }
}

/// The hash of each game at the host's current frame, taken before the next frame (the others run
/// until they are there).
bool same_games(net::LoopbackNetwork& network, Host& host, const std::vector<Computer*>& computers) {
    const std::uint64_t frame = host.session->frames() + 10;
    std::optional<std::uint64_t> mine;
    std::vector<std::optional<std::uint64_t>> theirs(computers.size());
    for (int i = 0; i < 2000; ++i) {
        network.advance(kStepMs);
        host.session->update(kStepMs, [&] {
            if (host.session->frames() == frame && !mine) {
                mine = host.game.simulation->state_hash();
            }
            return true;
        });
        for (std::size_t k = 0; k < computers.size(); ++k) {
            Computer& c = *computers[k];
            c.session->update(kStepMs, [&] {
                if (c.session->frames() == frame && !theirs[k] && c.game.simulation) {
                    theirs[k] = c.game.simulation->state_hash();
                }
                return true;
            });
        }
        if (mine && std::all_of(theirs.begin(), theirs.end(), [](const auto& h) { return h.has_value(); })) {
            break;
        }
    }
    if (!mine) {
        return false;
    }
    return std::all_of(theirs.begin(), theirs.end(), [&](const auto& h) { return h == mine; });
}

}  // namespace

// --- Seats ---

TEST_CASE("review: a stranger cannot take the slot of a player whose connection just broke") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(4, 3));  // 0 host, 1 and 2 human, 3 computer
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 1500);
    REQUIRE(b.session->playing());
    const sim::PlayerId seat = b.session->local_seat();
    const std::uint64_t token = b.session->token();
    network.cut(b.link->inner());
    run_for(network, host, {&a}, 300);

    // Within a second someone else asks for exactly that slot ...
    Computer stranger(network, "Mallory", 0, seat);
    run_for(network, host, {&a, &stranger}, 1500);
    std::printf("  the stranger got slot %u (Bernd had %u)\n", static_cast<unsigned>(stranger.session->local_seat()),
                static_cast<unsigned>(seat));
    // ... and Bernd comes back with his token a few seconds later.
    Computer back(network, "Bernd", token);
    run_for(network, host, {&a, &stranger, &back}, 2000);
    std::printf("  Bernd is back in slot %u\n", static_cast<unsigned>(back.session->local_seat()));
    CHECK(stranger.session->local_seat() != seat);
    CHECK(back.session->local_seat() == seat);
}

TEST_CASE("review: during host migration a newcomer cannot take the slot of a player still on his way") {
    net::LoopbackNetwork network(15, 0);
    net::LoopbackNetwork next(15, 0);
    auto host = std::make_unique<Host>(network, Game::make(4, 3));
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, *host, {&a, &b}, 2000);
    REQUIRE(b.session->playing());
    const sim::PlayerId seat = b.session->local_seat();
    host.reset();
    for (int i = 0; i < 50; ++i) {
        network.advance(kStepMs);
        a.update(kStepMs);
        b.update(kStepMs);
    }
    REQUIRE(a.session->hosts_next());
    net::HostSession moved(next.listen(), *a.session, buildings());
    // A newcomer is quicker than Bernd, who notices the host's leaving a few seconds later.
    Computer newcomer(next, "Mallory", 0, seat);
    for (std::uint32_t t = 0; t < 1500; t += kStepMs) {
        next.advance(kStepMs);
        moved.update(kStepMs);
        newcomer.update(kStepMs);
    }
    auto follow = std::make_unique<net::ClientSession>(*b.session, [&next] { return next.connect(); });
    b.session = std::move(follow);
    for (std::uint32_t t = 0; t < 3000; t += kStepMs) {
        next.advance(kStepMs);
        moved.update(kStepMs);
        newcomer.update(kStepMs);
        b.update(kStepMs);
    }
    std::printf("  newcomer in slot %u, Bernd (had %u) in slot %u\n",
                static_cast<unsigned>(newcomer.session->local_seat()), static_cast<unsigned>(seat),
                static_cast<unsigned>(b.session->local_seat()));
    CHECK(b.session->local_seat() == seat);
}

TEST_CASE("review: the successor whose own link breaks does not host a second game beside the host's") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 2000);
    REQUIRE(host.session->successor() == a.session->local_seat());
    // Only Anna's link breaks (her router restarts); the host and Bernd play on.
    network.cut(a.link->inner());
    run_for(network, host, {&a, &b}, 500);
    std::printf("  Anna: ended %d, reason %s, hosts next %d; the host still runs with %zu computers\n",
                a.session->ended(), net::describe(a.session->end_reason()), a.session->hosts_next(),
                host.session->roster().size() - 1);
    // The program (app/multiplayer.cpp, MapViewer::migrate) hosts at once when hosts_next(): two games.
    CHECK(!a.session->hosts_next());
}

TEST_CASE("review: the successor cannot use the tokens it holds for the migration to take another's slot") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");  // the successor: the playing computer with the lowest slot
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 2000);
    REQUIRE(host.session->successor() == a.session->local_seat());
    REQUIRE(a.link->standby.has_value());
    const sim::PlayerId bernd = b.session->local_seat();
    // Since MS-1 was fixed the successor holds verifiers, not tokens: the most it can try is Bernd's
    // verifier in place of his token.
    std::uint64_t token = 0;
    for (const net::SeatToken& t : a.link->standby->tokens) {
        token = t.seat == bernd ? t.verifier : token;
    }
    std::printf("  the successor holds %zu verifiers, Bernd's among them: %d\n", a.link->standby->tokens.size(),
                token != 0);
    // Anna's computer connects a second time with what it holds for Bernd's slot.
    Computer thief(network, "Bernd", token);
    run_for(network, host, {&a, &b, &thief}, 1500);
    std::printf("  second connection in slot %u; Bernd ended %d (%s)\n",
                static_cast<unsigned>(thief.session->local_seat()), b.session->ended(),
                net::describe(b.session->end_reason()));
    CHECK(thief.session->local_seat() != bernd);
    CHECK(!b.session->ended());
}

// --- Leaving at the wrong moment ---

TEST_CASE("review: a computer cut while its game is on the way after a desync comes back and all agree") {
    net::LoopbackNetwork network(30, 10);
    net::SessionConfig config;
    config.hash_interval = 20;
    Host host(network, Game::make(4, 3), config);
    Computer a(network, "Anna", 0, sim::kNoPlayer, config);
    Computer b(network, "Bernd", 0, sim::kNoPlayer, config);
    run_for(network, host, {&a, &b}, 1500);
    REQUIRE(b.session->playing());
    const std::uint64_t token = b.session->token();
    b.game.diverge = false;
    b.game.world->player(0)->money += 5;  // diverges now
    // Until the host sends the game anew, then cut while it travels.
    for (int i = 0; i < 400 && host.count(net::SessionEvent::Kind::Desync) == 0; ++i) {
        run_for(network, host, {&a, &b}, kStepMs);
    }
    REQUIRE(host.count(net::SessionEvent::Kind::Desync) == 1);
    run_for(network, host, {&a}, kStepMs);
    network.cut(b.link->inner());
    run_for(network, host, {&a}, 1000);
    Computer back(network, "Bernd", token, sim::kNoPlayer, config);
    run_for(network, host, {&a, &back}, 2000);
    CHECK(back.session->playing());
    CHECK(back.session->local_seat() == 2);
    CHECK(same_games(network, host, {&a, &back}));
    CHECK(host.session->kept_frames() <= 60u);
}

TEST_CASE("review: the new host leaves again during the migration: the others do not hang") {
    net::LoopbackNetwork network(15, 0);
    net::LoopbackNetwork next(15, 0);
    net::SessionConfig config;
    config.migration_ms = 4000;
    auto host = std::make_unique<Host>(network, Game::make(4, 3), config);
    Computer a(network, "Anna", 0, sim::kNoPlayer, config);
    Computer b(network, "Bernd", 0, sim::kNoPlayer, config);
    run_for(network, *host, {&a, &b}, 2000);
    host.reset();
    for (int i = 0; i < 50; ++i) {
        network.advance(kStepMs);
        a.update(kStepMs);
        b.update(kStepMs);
    }
    REQUIRE(a.session->hosts_next());
    const auto before = b.game.simulation->state_hash();
    auto moved = std::make_unique<net::HostSession>(next.listen(), *a.session, buildings());
    b.session = std::make_unique<net::ClientSession>(*b.session, [&next] { return next.connect(); });
    for (std::uint32_t t = 0; t < 40; t += kStepMs) {  // Bernd connects, the game is on its way ...
        next.advance(kStepMs);
        moved->update(kStepMs);
        b.update(kStepMs);
    }
    moved.reset();  // ... and Anna's program closes.
    std::uint32_t waited = 0;
    for (; waited < 10'000 && !b.session->ended(); waited += kStepMs) {
        next.advance(kStepMs);
        b.update(kStepMs);
    }
    std::printf("  Bernd ended after %u ms: %s, loads %d, can migrate %d\n", waited,
                net::describe(b.session->end_reason()), b.game.loads, b.session->can_migrate());
    CHECK(b.session->ended());
    CHECK(waited <= config.migration_ms + 1000);
    // His game stays as it was (to be saved), or is the one the new host sent.
    CHECK(b.game.simulation != nullptr);
    if (b.game.loads == 1) {
        CHECK(b.game.simulation->state_hash() == before);
    }
}

// --- Floods ---

TEST_CASE("review: twelve players each sending a flood of commands: what a frame costs the host") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(12, 12));
    std::vector<std::unique_ptr<Computer>> computers;
    std::vector<Computer*> all;
    for (int i = 1; i < 12; ++i) {
        computers.push_back(std::make_unique<Computer>(network, "P" + std::to_string(i)));
        all.push_back(computers.back().get());
    }
    run_for(network, host, all, 3000);
    for (Computer* c : all) {
        REQUIRE(c->session->playing());
    }
    host.most_ms = 0.0;
    host.total_ms = 0.0;
    host.updates = 0;
    run_for(network, host, all, 2000);
    const double quiet_mean = host.total_ms / static_cast<double>(host.updates);
    const double quiet_most = host.most_ms;
    host.most_ms = 0.0;
    host.total_ms = 0.0;
    host.updates = 0;
    // Every computer sends a full message of commands every update for five seconds.
    for (std::uint32_t t = 0; t < 5000; t += kStepMs) {
        for (Computer* c : all) {
            for (std::uint32_t i = 0; i < net::kMaxCommandsPerMessage; ++i) {
                c->session->submit(sim::ProductionSwitch{c->session->local_seat(), 0, 1, 1});
            }
        }
        run_for(network, host, all, kStepMs);
    }
    std::printf("  host update without commands: mean %.2f ms, most %.2f ms; flooded: mean %.2f ms, most %.2f ms\n",
                quiet_mean, quiet_most, host.total_ms / static_cast<double>(host.updates), host.most_ms);
    CHECK(host.most_ms < 6.25);
    CHECK(same_games(network, host, all));
}

TEST_CASE("review: one of twelve players sends a flood of commands: what a frame costs the host") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(12, 12));
    std::vector<std::unique_ptr<Computer>> computers;
    std::vector<Computer*> all;
    for (int i = 1; i < 12; ++i) {
        computers.push_back(std::make_unique<Computer>(network, "P" + std::to_string(i)));
        all.push_back(computers.back().get());
    }
    run_for(network, host, all, 3000);
    host.most_ms = 0.0;
    host.total_ms = 0.0;
    host.updates = 0;
    Computer& flooder = *all.front();
    for (std::uint32_t t = 0; t < 5000; t += kStepMs) {
        for (int k = 0; k < 8; ++k) {  // eight full messages per update
            for (std::uint32_t i = 0; i < net::kMaxCommandsPerMessage; ++i) {
                flooder.session->submit(sim::ProductionSwitch{flooder.session->local_seat(), 0, 1, 1});
            }
        }
        run_for(network, host, all, kStepMs);
    }
    std::printf("  one flooder, 8192 commands per update: host update mean %.2f ms, most %.2f ms\n",
                host.total_ms / static_cast<double>(host.updates), host.most_ms);
    CHECK(host.most_ms < 6.25);
}

TEST_CASE("review: a burst of 5000 commands from one player is carried out whole or refused visibly") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 1500);
    REQUIRE(a.session->playing());
    (void)host.game.simulation->take_command_results();
    for (int i = 0; i < 5000; ++i) {
        a.session->submit(sim::ProductionSwitch{a.session->local_seat(), 0, 1, 1});
    }
    std::size_t results = 0;
    for (std::uint32_t t = 0; t < 5000; t += kStepMs) {
        run_for(network, host, {&a}, kStepMs);
        results += host.game.simulation->take_command_results().size();
    }
    std::printf("  of 5000 commands %zu were carried out; no event tells the player of the rest\n", results);
    CHECK(results == 5000u);
}

TEST_CASE("review: malformed, oversized and a flood of small messages do not stall the host") {
    net::LoopbackNetwork network(5, 0);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 1500);
    REQUIRE(a.session->playing());

    // A raw connection that says hello like a computer and then sends 20 000 reports a second.
    auto raw = network.connect();
    net::Hello hello;
    hello.content = kContent;
    hello.name = "Raw";
    raw->send(0, net::encode(hello));
    run_for(network, host, {&a}, 500);
    host.most_ms = 0.0;
    const auto report = net::encode(net::Progress{999, 0, std::nullopt});  // a stale epoch: ignored
    const auto seat = net::encode(net::TakeSeat{1});
    for (std::uint32_t t = 0; t < 2000; t += kStepMs) {
        for (int i = 0; i < 320; ++i) {
            raw->send(0, report);
            raw->send(0, seat);
        }
        run_for(network, host, {&a}, kStepMs);
    }
    std::vector<net::TransportEvent> got;
    raw->poll(got);
    std::printf("  host update under a flood of %d small messages per update: most %.2f ms; raw link still open: %d\n",
                640, host.most_ms, std::none_of(got.begin(), got.end(), [](const auto& e) {
                    return e.kind == net::TransportEvent::Kind::Disconnected;
                }));
    CHECK(host.most_ms < 6.25);

    // Oversized and garbage: the sender goes, the game goes on.
    raw->send(0, std::vector<std::uint8_t>(2U << 20, 1));
    run_for(network, host, {&a}, 200);
    auto garbage = network.connect();
    garbage->send(0, std::vector<std::uint8_t>{0, 1, 2, 3});
    run_for(network, host, {&a}, 200);
    got.clear();
    raw->poll(got);
    garbage->poll(got);
    CHECK(std::count_if(got.begin(), got.end(),
                        [](const auto& e) { return e.kind == net::TransportEvent::Kind::Disconnected; }) == 2);
    CHECK(!a.session->ended());
    CHECK(host.session->roster().size() == 2);
}

TEST_CASE("review: a computer of a newer protocol with a longer Hello is told it is another version") {
    net::LoopbackNetwork network(5, 0);
    Host host(network, Game::make(4, 3));
    auto raw = network.connect();
    net::Hello hello;
    hello.protocol = net::kProtocolVersion + 1;
    hello.content = kContent;
    hello.name = "Future";
    auto bytes = net::encode(hello);
    bytes.push_back(0);  // a field the next version adds at the end
    raw->send(0, bytes);
    run_for(network, host, {}, 200);
    std::vector<net::TransportEvent> got;
    raw->poll(got);
    std::optional<net::RefuseReason> reason;
    for (const auto& e : got) {
        if (e.kind == net::TransportEvent::Kind::Received) {
            if (const auto m = net::decode(e.data)) {
                if (const auto* r = std::get_if<net::Refused>(&*m)) {
                    reason = r->reason;
                }
            }
        }
    }
    std::printf("  refused: %s\n", reason ? net::describe(*reason) : "(nothing said)");
    CHECK(reason == net::RefuseReason::Version);
}

// --- Slow and stalling computers ---

TEST_CASE("review: a computer at two thirds of the needed speed: the others keep a steady game") {
    net::LoopbackNetwork network(20, 5);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");
    Computer slow(network, "Langsam");
    run_for(network, host, {&a, &slow}, 2000);
    REQUIRE(slow.session->playing());
    const std::uint64_t start = host.session->frames();
    std::uint32_t held_ms = 0;
    std::uint32_t longest_hold = 0;
    std::uint32_t hold = 0;
    // Five minutes; the slow computer's processor manages two thirds of the frames the game needs:
    // each update it may run frames worth two thirds of the time that passed.
    std::uint32_t budget = 0;
    const auto cpu = [&budget] {
        if (budget < sim::Simulation::kTickMs) {
            return false;
        }
        budget -= sim::Simulation::kTickMs;
        return true;
    };
    for (std::uint32_t t = 0; t < 5 * 60 * 1000; t += kStepMs) {
        network.advance(kStepMs);
        host.update(kStepMs);
        a.update(kStepMs);
        budget = std::min<std::uint32_t>(budget + kStepMs * 2 / 3, 10 * sim::Simulation::kTickMs);
        slow.update(kStepMs, cpu);
        if (host.session->waiting()) {
            held_ms += kStepMs;
            hold += kStepMs;
            longest_hold = std::max(longest_hold, hold);
        } else {
            hold = 0;
        }
    }
    const double rate = static_cast<double>(host.session->frames() - start) / (5 * 60 * 20.0);
    std::printf(
        "  host ran %.0f%% of the frames; held %u ms in all, at most %u ms at once; slow computer loaded "
        "the game %d times, lagging %zu times\n",
        100.0 * rate, held_ms, longest_hold, slow.game.loads, host.count(net::SessionEvent::Kind::Lagging));
    CHECK(longest_hold <= 3000u);
    CHECK(slow.game.loads <= 3);
}

TEST_CASE("review: a link that stalls for two seconds every ten seconds") {
    net::LoopbackNetwork network(20, 5);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 2000);
    std::uint32_t held = 0;
    for (std::uint32_t t = 0; t < 60'000; t += kStepMs) {
        b.link->hold = (t % 10'000) < 2'000;
        network.advance(kStepMs);
        host.update(kStepMs);
        a.update(kStepMs);
        b.update(kStepMs);
        held += host.session->waiting() ? kStepMs : 0;
    }
    b.link->hold = false;
    std::printf("  the host waited %u ms of 60 s; Bernd loaded %d times, desyncs %zu\n", held, b.game.loads,
                host.count(net::SessionEvent::Kind::Desync));
    CHECK(!b.session->ended());
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(same_games(network, host, {&a, &b}));
}

TEST_CASE("review: two messages out of order end the session cleanly instead of diverging") {
    net::LoopbackNetwork network(10, 0);
    Host host(network, Game::make(4, 3));
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 1500);
    REQUIRE(a.session->playing());
    const int loads = a.game.loads;
    a.link->swap_next = true;
    run_for(network, host, {&a}, 1000);
    std::printf("  after a swap: ended %d (%s), loads %d -> %d\n", a.session->ended(),
                net::describe(a.session->end_reason()), loads, a.game.loads);
    CHECK((a.session->ended() || same_games(network, host, {&a})));
}

// --- Twelve players and the gap in the slots ---

TEST_CASE("review: a lobby of twelve players (slots 0-3 and 7-14) starts and plays the same game everywhere") {
    net::LoopbackNetwork network(10, 3);
    net::Lobby setup;
    setup.seats.resize(15);
    for (std::size_t s = 4; s < 7; ++s) {
        setup.seats[s].kind = net::LobbySeat::Kind::Reserved;
    }
    for (std::size_t s = 0; s < setup.seats.size(); ++s) {
        setup.seats[s].banner = static_cast<sim::BannerId>(s);
    }
    auto lobby = std::make_unique<net::HostSession>(network.listen(), setup, 0, "Merlin", kContent);
    std::vector<std::unique_ptr<Computer>> computers;
    std::vector<Computer*> all;
    for (int i = 0; i < 11; ++i) {
        computers.push_back(std::make_unique<Computer>(network, "P" + std::to_string(i)));
        all.push_back(computers.back().get());
    }
    for (int i = 0; i < 100; ++i) {
        network.advance(kStepMs);
        lobby->update(kStepMs);
        for (Computer* c : all) {
            c->update(kStepMs);
        }
    }
    std::vector<sim::PlayerId> seats;
    for (Computer* c : all) {
        REQUIRE(c->session->welcomed());
        seats.push_back(c->session->local_seat());
        c->session->set_ready(true);
    }
    for (const sim::PlayerId s : seats) {
        CHECK(s < 4 || (s >= 7 && s < 15));
    }
    for (int i = 0; i < 20; ++i) {
        network.advance(kStepMs);
        lobby->update(kStepMs);
        for (Computer* c : all) {
            c->update(kStepMs);
        }
    }
    CHECK(lobby->everyone_ready());
    Game game = Game::make(15, 15, {4, 5, 6});
    lobby->start(*game.simulation, buildings());
    Host host(std::move(game), std::move(lobby));
    run_for(network, host, all, 3000);
    for (Computer* c : all) {
        CHECK(c->session->playing());
    }
    CHECK(same_games(network, host, all));
}

// --- The heaviest commands, which every computer carries out ---

namespace {

/// A world with one island of `side` x `side` tiles of forest owned by nobody; player 0 human.
struct Forest {
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    sim::World world;

    explicit Forest(std::int32_t side)
        : world(static_cast<std::uint32_t>(side) + 40, static_cast<std::uint32_t>(side) + 40) {
        const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
        const auto doc = assets::cod::parse(
            "Objekt: HAUS\n"
            "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
            "  @Nummer: 1\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"
            "  @Nummer: 2\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
            "EndObj\n",
            types);
        auto table = assets::BuildingTable::from_cod(doc);
        REQUIRE(table.ok());
        buildings = std::move(table).value();
        sim::IslandMap map(static_cast<std::uint32_t>(side), static_cast<std::uint32_t>(side));
        for (std::int32_t y = 0; y < side; ++y) {
            for (std::int32_t x = 0; x < side; ++x) {
                REQUIRE(map.place(*buildings.by_id(20202), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 20, 20};
        island.header.width = static_cast<std::uint8_t>(side);  // the original's islands: at most 255
        island.header.height = static_cast<std::uint8_t>(side);
        REQUIRE(world.add_island(std::move(island)).ok());
        std::vector<sim::Player> players(2);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 1'000'000;
        players[1].role = sim::PlayerRole::Computer;
        world.set_players(players);
    }
};

}  // namespace

TEST_CASE("review: the heaviest commands a player may put into one frame: what they cost every computer") {
    for (const std::int32_t side : {50, 200}) {
        Forest f(side);
        sim::Simulation s(f.world, f.buildings, f.figures);
        s.tick();
        // The host admits commands into a frame while its work is below kFrameWork (P2: by their
        // cost, not their number); the orders below are as many as it lets into one frame.
        const std::uint64_t now = s.next_tick();
        const sim::Command demolish = sim::DemolishOrder{0, 0, {0, 0}, {side - 1, side - 1}};
        const std::uint64_t demolish_work = net::command_work(f.world, demolish);
        int demolish_count = 0;
        for (std::uint64_t work = 0; demolish_count < 256 && work < net::kFrameWork; work += demolish_work) {
            s.schedule(now, demolish);
            ++demolish_count;
        }
        auto start = Clock::now();
        s.tick();
        const double demolish_ms = ms_since(start);

        // Build orders of the largest size the network carries (kMaxCommandItems items each).
        sim::BuildOrder big;
        big.player = 0;
        big.island = 0;
        for (std::uint32_t i = 0; i < sim::kMaxCommandItems; ++i) {
            big.items.push_back(sim::BuildItem{
                20401, static_cast<std::int32_t>(i % static_cast<std::uint32_t>(side)),
                static_cast<std::int32_t>(i / static_cast<std::uint32_t>(side) % static_cast<std::uint32_t>(side)), 0,
                0});
        }
        const std::size_t bytes = sim::encode_command(big).size();
        const std::size_t per_message = net::kMaxMessageToHost / bytes;
        const std::uint64_t then = s.next_tick();
        const std::uint64_t big_work = net::command_work(f.world, big);
        std::size_t big_count = 0;
        for (std::uint64_t work = 0; big_count < std::min<std::size_t>(per_message, 256) && work < net::kFrameWork;
             work += big_work) {
            s.schedule(then, big);
            ++big_count;
        }
        start = Clock::now();
        s.tick();
        const double build_ms = ms_since(start);
        std::printf(
            "  island %dx%d: %d demolish orders over it %.1f ms; %zu build orders of %u roads (%zu bytes "
            "each) %.1f ms\n",
            side, side, demolish_count, demolish_ms, big_count, sim::kMaxCommandItems, bytes, build_ms);
        CHECK(demolish_ms < 6.25);
        CHECK(build_ms < 6.25);
    }
}
