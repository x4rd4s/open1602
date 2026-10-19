// SPDX-License-Identifier: GPL-3.0-or-later
// A game that is saved and loaded continues exactly as the one that ran on (ADR 0004): the base of
// replays from a savegame and of joining a multiplayer game. Any state that the savegame misses,
// and any cache that a loaded game builds differently from a running one, shows up here as a
// difference in the savegames of both games after the same number of ticks.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "fixtures.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

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

core::Bytes save(const sim::World& world, const Data& d, const sim::Simulation& simulation) {
    auto bytes = sim::write_game(world, d.buildings, &simulation, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    return std::move(bytes).value();
}

/// Runs `ticks` ticks on a copy of `start` straight through, and the same with a save and a load
/// every `period` ticks; both savegames must be the same, byte for byte. A period that is no
/// multiple of the game's clocks (1 s, 5 s, 10 s) loads in the middle of everything.
void check_continuity(const char* what, const sim::World& start, const Data& d, std::uint32_t ticks,
                      std::uint32_t period) {
    sim::World straight = start;
    sim::Simulation a(straight, d.buildings, d.figures);
    for (std::uint32_t i = 0; i < ticks; ++i) {
        a.tick();
    }
    const core::Bytes expected = save(straight, d, a);

    sim::World world = start;
    core::Bytes actual;
    for (std::uint32_t done = 0; done < ticks;) {
        {
            sim::Simulation b(world, d.buildings, d.figures);
            const std::uint32_t run = std::min(period, ticks - done);
            for (std::uint32_t i = 0; i < run; ++i) {
                b.tick();
            }
            done += run;
            actual = save(world, d, b);
        }
        auto loaded = sim::read_game(actual, d.buildings);
        REQUIRE(loaded.ok());
        world = std::move(loaded).value();
    }

    if (actual != expected) {
        std::size_t at = 0;
        while (at < actual.size() && at < expected.size() && actual[at] == expected[at]) {
            ++at;
        }
        std::fprintf(stderr, "  %s: savegames differ from byte %zu on (%zu / %zu bytes)\n", what, at, actual.size(),
                     expected.size());
    }
    CHECK(actual == expected);
}

}  // namespace

TEST_CASE("data: a new endless game, saved and loaded again and again, continues as the one that ran on") {
    const Data d = data_or_skip();
    if (!d.game.resolve("Szenes/Endlosspiel00.szs")) {
        SKIP("no endless game files");
    }
    sim::NewGameOptions options;
    options.difficulty = 2;
    options.world.seed = 7;
    auto world = sim::start_new_game(d.game, d.buildings, d.text, options);
    REQUIRE(world.ok());
    // Fifteen minutes of game time: the computer players found their cities and trade.
    check_continuity("endless", *world, d, 15 * 60 * 1000 / sim::Simulation::kTickMs, 731);
}

TEST_CASE("data: games of the original, saved and loaded again and again, continue as the ones that ran on") {
    const Data d = data_or_skip();
    bool any = false;
    for (const char* name : {"SAVEGAME/game00.gam", "SAVEGAME/game10.gam"}) {
        const auto path = test::fixture(d.game, name);
        if (!path) {
            continue;
        }
        auto file = assets::ChunkFile::load(*path);
        REQUIRE(file.ok());
        auto world = sim::load_world(*file, d.buildings, d.game);
        REQUIRE(world.ok());
        any = true;
        // Five minutes: towns, trade, ships and the soldiers of the late game.
        check_continuity(name, *world, d, 5 * 60 * 1000 / sim::Simulation::kTickMs, 731);
    }
    if (!any) {
        SKIP("no savegames");
    }
}
