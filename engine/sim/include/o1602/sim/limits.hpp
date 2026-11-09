// SPDX-License-Identifier: GPL-3.0-or-later
//
// Limits that more than one module checks, each defined once (LC-1). Each names what bounds it;
// other modules derive their own from these (net::kMaxLobbySeats, the menus) and state the relation
// with a static_assert. Limits of one file format alone stay with that format (the .GAM export's
// in savegame_writer.cpp, the wire's in net/protocol.hpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace o1602::sim {

/// Players with a start in a new world: as many as there are hand-picked banner colours that stay
/// apart (render::BannerTable::kDefaultColours). The world generator, the start dialogs, the lobby
/// and add_players all take this one. It may be raised: the banners and names beyond the twelve are
/// generated, who knows a deposit has no limit (assets::SlotSet), the ids and the savegame reach
/// kMaxPlayerSlots < kMaxPlayerCount (player.hpp); only the screens with an entry per player need
/// room for them (ADR 0007).
inline constexpr std::size_t kMaxPlayers = 12;

/// Island numbers (assets::IslandHeader::number) are one byte in the islands' files and in every
/// state section that names an island (ISLE, MAPW, EPOC, FLEE), so a world saves at most this many.
inline constexpr std::size_t kMaxIslandNumbers = 256;
/// Islands the world generator places at most: below kMaxIslandNumbers, 0xFF meaning "no island".
inline constexpr std::uint32_t kMaxPlannedIslands = 250;
static_assert(kMaxPlannedIslands < kMaxIslandNumbers);
/// Islands a world holds: island indexes are 16 bits (WorldIsland, commands), 0xFFFF is the sea
/// (World::kSea).
inline constexpr std::size_t kMaxIslands = 0xFFFF;
static_assert(kMaxIslands <= 0xFFFF, "loops over the islands count with std::uint16_t (LC-23)");

/// A set of city numbers of one island, a bit for each: the territory masks of the land a city
/// covers, the cities that still stand. Its width bounds the city numbers (kCitySlotsPerIsland).
using CityMask = std::uint32_t;
/// City numbers an island can carry: a bit for each in a CityMask. Open1602's savegame keeps the
/// number as a whole byte (MAPW, CITY), the original's island record as three bits (eight owner
/// slots, original::kMaxCitiesPerIsland), which the .GAM export checks.
inline constexpr std::uint32_t kCitySlotsPerIsland = 32;
static_assert(kCitySlotsPerIsland <= sizeof(CityMask) * 8);
/// The most cities one island may hold under any setting (Rules::cities_per_island, the editor).
/// It may be raised up to kCitySlotsPerIsland with a wider CityMask.
inline constexpr std::uint32_t kMaxCitiesPerIsland = 31;
static_assert(kMaxCitiesPerIsland < kCitySlotsPerIsland && kCitySlotsPerIsland < 0xFF,
              "MapCell::kNoCity (0xFF) is no city number");

/// The longest side of a world in tiles, checked when a world is loaded before anything is
/// allocated. The world generator stays below (WorldSettings::kMaxSide).
inline constexpr std::uint32_t kMaxWorldSide = 4096;

}  // namespace o1602::sim
