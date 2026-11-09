// SPDX-License-Identifier: GPL-3.0-or-later
// Multiplayer sessions on real games (ADR 0011): a host and other computers over the in-memory
// network, with the computer players, ships and towns of the original's data. Every computer gets
// the game as a savegame from the host and must then compute exactly what the host computes; the
// fingerprints every few seconds would show any difference.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "annokit/net/loopback.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/net/session.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kStepMs = 16;

struct Data {
    assets::GameDirectory game;
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    assets::TextTable text;
};

Data data_or_skip() {
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        SKIP("ANNO1602_DIR not set");
    }
    auto buildings = assets::BuildingTable::load(*dir);
    auto figures = assets::FigureTable::load(*dir);
    auto text = assets::TextTable::load(*dir);
    REQUIRE(buildings.ok());
    REQUIRE(figures.ok());
    REQUIRE(text.ok());
    return Data{std::move(dir).value(), std::move(buildings).value(), std::move(figures).value(),
                std::move(text).value()};
}

/// One computer's game, with the names of the data as the program gives them.
struct Game {
    const Data* data = nullptr;
    std::unique_ptr<sim::World> world;
    std::unique_ptr<sim::Simulation> simulation;
    int loads = 0;

    void start(sim::World w) {
        simulation.reset();
        world = std::make_unique<sim::World>(std::move(w));
        simulation = std::make_unique<sim::Simulation>(*world, data->buildings, data->figures);
        simulation->set_city_names(data->text.section("STAEDTE"));
        simulation->set_ship_names(data->text.section("SHIPS"));
    }

    sim::Simulation* load(std::span<const std::uint8_t> save) {
        auto loaded = sim::read_game(save, data->buildings, nullptr, &data->figures);
        if (!loaded) {
            return nullptr;
        }
        ++loads;
        start(std::move(loaded).value());
        return simulation.get();
    }
};

struct Computer {
    Game game;
    std::unique_ptr<net::ClientSession> session;

    Computer(const Data& d, annokit::net::LoopbackNetwork& network, std::string name) {
        game.data = &d;
        net::Hello hello;
        hello.content = 1;
        hello.name = std::move(name);
        session = std::make_unique<net::ClientSession>(
            network.connect(), hello, [this](std::span<const std::uint8_t> save) { return game.load(save); });
    }
};

/// Runs a session of `world` for `minutes` of real time at `speed`, with a second computer coming
/// in halfway; at the end the games of all must be the same at one frame.
void play(const Data& d, sim::World world, sim::PlayerId host_seat, std::uint32_t minutes, std::uint32_t speed) {
    annokit::net::LoopbackNetwork network(40, 20);
    Game host_game;
    host_game.data = &d;
    host_game.start(std::move(world));
    net::HostSession host(network.listen(), *host_game.simulation, d.buildings, host_seat, "Gastgeber", 1);
    host.submit(sim::GameSpeedOrder{host_seat, sim::GameSpeedOrder::Op::SetSpeed, speed});
    Computer a(d, network, "Anna");
    std::unique_ptr<Computer> b;
    const std::uint32_t total = minutes * 60 * 1000;
    for (std::uint32_t t = 0; t < total; t += kStepMs) {
        if (t == total / 2) {
            b = std::make_unique<Computer>(d, network, "Bernd");  // into the running game
        }
        network.advance(kStepMs);
        host.update(kStepMs);
        a.session->update(kStepMs);
        if (b) {
            b->session->update(kStepMs);
        }
    }
    REQUIRE(a.session->playing());
    REQUIRE(b && b->session->playing());
    CHECK(host.fingerprints_compared() > 0);
    CHECK(host.fingerprints_matched() == host.fingerprints_compared());
    CHECK(a.game.loads == 1);
    CHECK(b->game.loads == 1);

    // The fingerprints of all three games at the same frame, a little ahead.
    const std::uint64_t frame = host.frames() + 40;
    std::optional<std::uint64_t> hashes[3];
    net::Session* sessions[3] = {&host, a.session.get(), b->session.get()};
    Game* games[3] = {&host_game, &a.game, &b->game};
    for (int i = 0; i < 2000 && !(hashes[0] && hashes[1] && hashes[2]); ++i) {
        network.advance(kStepMs);
        for (int k = 0; k < 3; ++k) {
            sessions[k]->update(kStepMs, [&, k] {
                if (sessions[k]->frames() == frame && !hashes[k]) {
                    hashes[k] = games[k]->simulation->state_hash();
                }
                return true;
            });
        }
    }
    REQUIRE(hashes[0].has_value());
    CHECK(hashes[1] == hashes[0]);
    CHECK(hashes[2] == hashes[0]);
}

}  // namespace

TEST_CASE("data: a multiplayer endless game with computer players stays the same on every computer") {
    const Data d = data_or_skip();
    if (!d.game.resolve("Szenes/Endlosspiel00.szm")) {
        SKIP("no multiplayer endless game files");
    }
    sim::NewGameOptions options;
    options.difficulty = 2;
    options.multiplayer = true;
    options.world.seed = 11;
    auto world = sim::start_new_game(d.game, d.buildings, d.text, options);
    REQUIRE(world.ok());
    const sim::PlayerId human = sim::first_human_player(*world);
    // Four minutes at speed 4: sixteen minutes of game, the computer players found and trade.
    play(d, std::move(world).value(), human, 4, 4);
}

TEST_CASE("data: Am Ende allein with three humans stays the same on every computer") {
    const Data d = data_or_skip();
    if (!d.game.resolve("Szenes/Am Ende allein.szm")) {
        SKIP("scenario not installed");
    }
    auto chunks = assets::ChunkFile::load(*d.game.resolve("Szenes/Am Ende allein.szm"));
    REQUIRE(chunks.ok());
    auto world = sim::load_world(*chunks, d.buildings, d.game);
    REQUIRE(world.ok());
    play(d, std::move(world).value(), 0, 3, 4);
}
