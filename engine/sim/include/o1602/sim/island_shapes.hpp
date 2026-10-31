// SPDX-License-Identifier: GPL-3.0-or-later
//
// Islands of their own shape (world generator stage 2, docs/re/worldgen.md "Island shapes"). The
// original only ever loads its 114 island templates; Open1602 can also draw an island's coast
// itself. The land is grown from 2 x 2 blocks (noise and a falloff towards the edge), smoothed until
// every tile of the coast is one the templates use, and then dressed with the templates' tiles:
// beach, beach corners and surf as their rules ask, ground, forest of the climate and ore
// mountains. Only integers are used, so every computer makes the same island from the same seed.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "o1602/assets/game_data.hpp"
#include "o1602/assets/island.hpp"

namespace o1602::sim::shapes {

/// What an island of its own shape must have.
struct ShapeRequest {
    std::uint16_t width = 0;  ///< in tiles, as the template the plan placed (at least 16, at most 255)
    std::uint16_t height = 0;
    std::uint8_t climate = 0;          ///< 0 north, 1 south: the forest
    std::uint8_t size = 0;             ///< size class 0 lit .. 4 lar, for the header
    std::uint8_t number = 0;           ///< island number, kept in every tile
    std::uint8_t big_mountains = 0;    ///< ore mountains for big veins (4 x 4)
    std::uint8_t small_mountains = 0;  ///< ore mountains for small veins (3 x 3)
    std::uint64_t seed = 0;
};

/// The island: an INSEL5 header without a template (all tiles stored) and one layer of tiles.
/// The same request gives the same island. Mountains that find no room are left out.
[[nodiscard]] assets::Island make_island(const ShapeRequest& request);

// The tiles the coast is made of, by the land around a tile. Neighbour bits: N 1, NE 2, E 4,
// SE 8, S 16, SW 32, W 64, NW 128 (bit set: land); outside the island is sea.

enum class Coast : std::uint8_t {
    Inland,       ///< land with land all around
    Beach,        ///< land with the sea on one side
    OuterCorner,  ///< land with the sea on two sides next to each other
    InnerCorner,  ///< land with the sea only on one diagonal
    OpenSea,      ///< sea without land around
    Surf,         ///< sea with land on one side
    SurfCorner,   ///< sea with land only on one diagonal
    SurfBay,      ///< sea with land on two sides next to each other
};

struct CoastTile {
    Coast coast = Coast::Inland;
    std::uint8_t rotation = 0;
};

/// The coast tile of a land or sea tile with these neighbours; nothing when the templates have
/// no tile for it (a strip of land or water one tile wide, land touching only at a corner).
[[nodiscard]] std::optional<CoastTile> coast_tile(bool land, std::uint8_t neighbours);

/// Building ids (haeuser.cod) of the tiles the generator lays.
namespace ids = assets::id::island_shape;

}  // namespace o1602::sim::shapes
