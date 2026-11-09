// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's special objects (ADR 0013): pieces of the game data no build menu offers, placed on
// open land without owner and without city. Crop fields at each of their stages stand as wild
// vegetation, ruins as the remains of a city, and the buildings no list of the game uses as
// scenery. In a game they count like forest and ruins: no city owns them until one claims the
// land they stand on.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "annokit/core/result.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// What a special object is.
enum class SpecialKind : std::uint8_t {
    Growing,   ///< a crop field growing, at one of its stages
    Ripe,      ///< a ripe crop field
    Withered,  ///< a crop field withered by a drought
    Ruin,      ///< a ruin of the land or the beach
    Building,  ///< a building no list of the game offers
};

/// A special object as the editor offers it: a record of haeuser.cod and, for a growing field, the
/// stage it shows.
struct SpecialPiece {
    std::uint32_t id = 0;
    std::uint8_t stage = 0;   ///< a growing field's stage (its picture); 0 for every other piece
    std::uint8_t stages = 1;  ///< how many stages its record shows (a growing field's), 1 for others
    SpecialKind kind = SpecialKind::Building;
    friend bool operator==(const SpecialPiece&, const SpecialPiece&) = default;
};

/// The special objects of the game data, in the order the editor lists them: each crop with its
/// growing stages, ripe and withered, then the ruins, then the unused buildings.
[[nodiscard]] std::vector<SpecialPiece> special_pieces(const assets::BuildingTable& buildings);

/// Whether a record is one the editor places as a special object (a crop field, a ruin or an
/// unused building).
[[nodiscard]] bool is_special(const assets::BuildingTable& buildings, const assets::BuildingType& building) noexcept;

/// Why a special object cannot go where it was asked.
enum class SpecialRefusal : std::uint8_t {
    None,
    NoIsland,  ///< not on the island, or not all of it
    Ground,    ///< not on open land (a beach ruin: not on the beach)
    Taken,     ///< a city's land, a building or another special object there
    Unit,      ///< a land unit stands there
};

/// Whether `piece` turned by `rotation` fits with its top-left corner at `origin` of the island.
[[nodiscard]] SpecialRefusal special_fits(const sim::World& world, const assets::BuildingTable& buildings,
                                          std::uint16_t island, sim::TilePos origin, const SpecialPiece& piece,
                                          std::uint8_t rotation);

/// `piece` turned by `rotation` placed with its top-left corner at `origin`, without owner and
/// city; a growing field starts growing from its stage. An error if it does not fit.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> place_special(const sim::World& world,
                                                                              const assets::BuildingTable& buildings,
                                                                              std::uint16_t island, sim::TilePos origin,
                                                                              const SpecialPiece& piece,
                                                                              std::uint8_t rotation);

/// The name of a refusal for messages and logs.
[[nodiscard]] const char* refusal_name(SpecialRefusal refusal) noexcept;

}  // namespace o1602::editor
