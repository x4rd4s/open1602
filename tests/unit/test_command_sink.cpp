// SPDX-License-Identifier: GPL-3.0-or-later
// The way of the local player's commands into the game (engine/app/src/command_sink.hpp).

#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "command_sink.hpp"

#include "o1602/net/loopback.hpp"

using namespace o1602;

namespace {

/// One human player in a world of its own, and the simulation that runs it.
struct Game {
    sim::World world{64, 64};
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    sim::Simulation simulation{world, buildings, figures};

    Game() {
        std::vector<sim::Player> players(1);
        players[0].role = sim::PlayerRole::Human;
        players[0].name = "Alt";
        world.set_players(players);
    }

    /// The name player 0 has after the commands queued so far.
    std::string name_now() {
        simulation.apply_commands();
        return world.player(0)->name;
    }
};

/// Stands in for a multiplayer session: keeps what it is given.
class FakeSession final : public net::Session {
public:
    explicit FakeSession(net::LoopbackNetwork& network) : Session(network.listen(), {}) {}

    void submit(sim::Command command) override { commands.push_back(std::move(command)); }
    void update(std::uint32_t, const sim::Simulation::BeforeTick&) override {}
    void say(std::string) override {}

    std::vector<sim::Command> commands;
};

}  // namespace

TEST_CASE("command sink: a single-player command reaches the simulation") {
    Game g;
    app::CommandSink sink;
    sink.set_simulation(&g.simulation);
    sink.submit(sim::PlayerRename{0, "Neu"});
    CHECK(g.name_now() == "Neu");
}

TEST_CASE("command sink: in multiplayer the session takes the command, not the simulation") {
    Game g;
    net::LoopbackNetwork network;
    FakeSession session(network);
    app::CommandSink sink;
    sink.set_simulation(&g.simulation);
    sink.set_session(&session);
    sink.submit(sim::PlayerRename{0, "Neu"});
    REQUIRE(session.commands.size() == 1);
    const auto* rename = std::get_if<sim::PlayerRename>(&session.commands.front());
    REQUIRE(rename != nullptr);
    CHECK(rename->name == "Neu");
    CHECK(g.name_now() == "Alt");

    // The session ends: the commands go to the simulation again.
    sink.set_session(nullptr);
    sink.submit(sim::PlayerRename{0, "Danach"});
    CHECK(session.commands.size() == 1);
    CHECK(g.name_now() == "Danach");
}

TEST_CASE("command sink: without a game a command is dropped") {
    app::CommandSink sink;
    sink.submit(sim::PlayerRename{0, "Neu"});  // must not crash
    CHECK(true);
}
