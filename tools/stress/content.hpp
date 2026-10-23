// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-stress: what the stress scenarios build their worlds from. Without the original game the
// buildings and figures come from a small synthetic table (the same kind of COD text the unit tests
// parse); with it (--data, ANNO1602_DIR) from the installation. The scenarios only use the base
// game's Ids and figure numbers (assets/game_data.hpp), which both tables have, so every scenario
// that does not need the island generator runs on either.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::stress {

/// Ids of the synthetic terrain (only the synthetic table has them under these Ids).
inline constexpr std::uint32_t kGround = 20101;
inline constexpr std::uint32_t kSea = 21201;
inline constexpr std::uint32_t kBeach = 20501;
inline constexpr std::uint32_t kTree = 20202;
/// Base game buildings both tables have (assets::id), and those of the synthetic table that
/// stand for a workshop and a second house class.
inline constexpr std::uint32_t kRoad = 20401;
inline constexpr std::uint32_t kHouse = 20605;
inline constexpr std::uint32_t kMarket = 20809;
inline constexpr std::uint32_t kKlinik = 20817;
inline constexpr std::uint32_t kBrunnen = 20811;
inline constexpr std::uint32_t kWorkshop = 20507;  ///< Webstube: wool -> cloth
inline constexpr std::uint32_t kKontor = 22101;
inline constexpr std::uint32_t kTower = 21405;
inline constexpr std::uint32_t kVolcano = 21108;
/// Figures (assets::figure): SOLDAT1, MUSKETIER1, HANDEL1, KRIEG1.
inline constexpr std::uint16_t kSword = 1;
inline constexpr std::uint16_t kMusket = 9;
inline constexpr std::uint16_t kTradeShip = 21;
inline constexpr std::uint16_t kWarship = 25;

struct Tables {
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    bool synthetic = true;
};

/// The synthetic tables (no game data needed).
[[nodiscard]] core::Result<Tables> synthetic_tables();

/// Players of a new synthetic world: `players` with a start in the slots of player_slot()
/// (0-3, then 7 on), the free trader, pirates and natives in 4, 5, 6; all of them computers
/// unless `humans`.
[[nodiscard]] std::vector<sim::Player> make_players(std::size_t players, bool humans);
/// The ids of the players with a start (player_slot order) in `world`.
[[nodiscard]] std::vector<sim::PlayerId> start_players(const sim::World& world);

/// An archipelago of square islands of `side` tiles in a grid with `gap` tiles of sea between
/// them, as many as fit (at most `max_islands`, 0 = no limit). Each island is ground with a band
/// of forest, a beach facing east and three columns of sea behind it (the coast a Kontor needs),
/// optionally a volcano mountain in its north-west corner.
struct Archipelago {
    std::uint32_t width = 500;
    std::uint32_t height = 350;
    std::uint32_t side = 40;
    std::uint32_t gap = 12;
    std::size_t max_islands = 0;
    bool volcanoes = false;
};
core::Status build_archipelago(sim::World& world, const Tables& tables, const Archipelago& a);

/// A ship of `owner` at world tile (x, y) with as many holds as its type has.
sim::Ship& add_ship(sim::World& world, const Tables& tables, std::uint32_t x, std::uint32_t y, std::uint16_t figure,
                    sim::PlayerId owner);

/// Founds a city on every island (round robin over `owners`) with a Kontor on its east coast, by
/// a build order as a player gives it. Returns how many were founded.
std::size_t found_cities(sim::Simulation& simulation, sim::World& world, const Tables& tables,
                         const std::vector<sim::PlayerId>& owners, std::size_t per_island, std::string* why_not);

/// Fills the territory of every city of `owners` densely with roads, houses, markets, workshops,
/// doctors and fire brigades (the throwaway approach of docs/performance.md), by build orders.
/// Returns the buildings placed.
std::size_t fill_cities(sim::Simulation& simulation, sim::World& world, const Tables& tables,
                        const std::vector<sim::PlayerId>& owners, std::size_t max_per_city);

/// Every good a city can hold topped up to `amount` (1/32 t), money of the owners topped up.
void top_up(sim::World& world, std::uint32_t amount);

/// Everybody of `players` at war with everybody else of them.
void declare_war(sim::World& world, const std::vector<sim::PlayerId>& players);

/// Land tiles of an island where a unit can stand: ground or forest, no building.
[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> free_land(const sim::World& world,
                                                                             const Tables& tables,
                                                                             std::uint16_t island);

}  // namespace o1602::stress
