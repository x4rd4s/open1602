// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// One game: the world and the simulation that runs on it. A game that is loaded or started is
/// built whole first and then takes the place of the old one, which stays as it was if building
/// fails. Not movable: the simulation keeps a reference to the world next to it.
struct GameState {
    GameState(sim::World w, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
              const sim::Rules& rules)
        : world(std::move(w)), simulation(world, buildings, figures, rules) {}
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    sim::World world;
    sim::Simulation simulation;
};

}  // namespace o1602::app
