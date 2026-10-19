// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tagged sections of the game state, shared by Open1602's own savegame (save_game.hpp) and the
// chunk it adds to savegames in the original's format (savegame_extension.hpp). Internal to sim.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

class Simulation;

namespace save {

/// How the tiles are saved: all of them with the world's size (own savegame), or only those that
/// loading the original's chunks in front (`before`) would not give back (chunk in a .GAM).
enum class TileMode : std::uint8_t { Full, Differences };

[[nodiscard]] core::Bytes encode_sections(const World& world, const assets::BuildingTable& buildings,
                                          const Simulation* simulation, TileMode tiles,
                                          std::span<const std::uint8_t> before);

/// A world with the size and the islands of the sections (MAPW); the rest comes with apply_sections.
[[nodiscard]] core::Result<World> world_from_sections(std::span<const std::uint8_t> sections,
                                                      const assets::BuildingTable& buildings,
                                                      std::vector<std::string>* warnings);

/// Reads every section and, only if all of them are sound and fit the world, applies them.
/// Sections of a later version that are not required are skipped and reported in `warnings`.
core::Status apply_sections(World& world, const assets::BuildingTable& buildings,
                            std::span<const std::uint8_t> sections, std::vector<std::string>* warnings);

}  // namespace save
}  // namespace o1602::sim
