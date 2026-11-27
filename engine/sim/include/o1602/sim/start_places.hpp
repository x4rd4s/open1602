// SPDX-License-Identifier: GPL-3.0-or-later
//
// Start places drawn at random (Open1602, the editor's "Start places at random",
// sim::ScenarioInfo::shuffle_starts): the players who start with ships only take the start places
// of the map in a random order, so that a scenario with fixed islands starts differently each game.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// The names of point markers that are further start places begin with this
/// ("ship_start", "ship_start_2", ...).
inline constexpr std::string_view kShipStartMarker = "ship_start";

/// Whether `marker` is a further start place: a point whose name begins with kShipStartMarker.
[[nodiscard]] bool ship_start_marker(const ScenarioMarker& marker) noexcept;

/// A start place: the world tile a player's first ship starts on.
struct StartPlace {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    friend bool operator==(const StartPlace&, const StartPlace&) = default;
};

/// The humans and computers that start with ships but without a city, in the order of their
/// slots: those whose start places are drawn.
[[nodiscard]] std::vector<PlayerId> players_starting_at_sea(const World& world);

/// The start places of `world`: those of its markers (ship_start_marker) alone when there are at
/// least as many as players_starting_at_sea(), so that the author sets every place; else the first
/// ship of each of those players, then the places of the markers. Each place once.
[[nodiscard]] std::vector<StartPlace> start_places(const World& world);

/// Gives each of players_starting_at_sea() a start place of start_places() drawn from `seed`, each
/// place to one player at most: its ships move there together, as they lay to each other. A ship
/// whose tile there is no water ships sail on, or lies next to another ship, goes to the nearest
/// tile that is, within 16 tiles; else it stays where it was. Nothing changes with fewer than two
/// places.
void shuffle_start_places(World& world, const assets::BuildingTable& buildings, std::uint64_t seed);

}  // namespace o1602::sim
