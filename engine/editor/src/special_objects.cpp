// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/special_objects.hpp"

#include <algorithm>
#include <utility>

#include "o1602/assets/game_data.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/editor/terrain.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/soldier.hpp"

namespace o1602::editor {

using annokit::core::ErrorCode;
using annokit::core::make_error;

namespace {

/// A ripe crop: a ripe record of a field or plantation (not grass, a tree or fish) that grows
/// from a record before it.
bool ripe_crop(const assets::BuildingTable& buildings, const assets::BuildingType& b) noexcept {
    return b.prod_kind == assets::prod::kRipe && sim::territory_building(buildings, b) &&
           sim::growing_record(buildings, b) != nullptr;
}

/// A ruin of the land or the beach (not a destroyed Kontor or market: those belong to a city).
bool ruin(const assets::BuildingType& b) noexcept {
    return b.kind == assets::kind::kRuin || b.kind == assets::kind::kBeachRuin;
}

bool unused_building(const assets::BuildingType& b) noexcept {
    return std::find(assets::id::kUnusedBuildings.begin(), assets::id::kUnusedBuildings.end(), b.id) !=
           assets::id::kUnusedBuildings.end();
}

/// The stages a growing record shows one after the other as its picture; 1 if its picture is an
/// animation of its own.
std::uint8_t growing_stages(const assets::BuildingType& b) noexcept {
    return b.anim_time == 0 && b.anim_count > 1 ? static_cast<std::uint8_t>(std::min<std::uint32_t>(b.anim_count, 16))
                                                : std::uint8_t{1};
}

/// Each record once, by its record number (ids repeat in the data).
void add_once(std::vector<SpecialPiece>& pieces, std::vector<std::uint32_t>& seen, const assets::BuildingType& b,
              SpecialPiece piece) {
    if (std::find(seen.begin(), seen.end(), b.record) != seen.end() && piece.stage == 0) {
        return;
    }
    if (piece.stage == 0) {
        seen.push_back(b.record);
    }
    pieces.push_back(piece);
}

}  // namespace

std::vector<SpecialPiece> special_pieces(const assets::BuildingTable& buildings) {
    std::vector<SpecialPiece> pieces;
    std::vector<std::uint32_t> seen;
    const auto& all = buildings.types();
    // The crops: growing at each stage, ripe, withered.
    for (const assets::BuildingType& ripe : all) {
        if (buildings.by_id(ripe.id) != &ripe || !ripe_crop(buildings, ripe)) {
            continue;
        }
        const assets::BuildingType* growing = sim::growing_record(buildings, ripe);
        if (buildings.by_id(growing->id) == growing) {
            const std::uint8_t stages = growing_stages(*growing);
            for (std::uint8_t s = 0; s < stages; ++s) {
                add_once(pieces, seen, *growing, SpecialPiece{growing->id, s, stages, SpecialKind::Growing});
            }
        }
        add_once(pieces, seen, ripe, SpecialPiece{ripe.id, 0, 1, SpecialKind::Ripe});
        if (const assets::BuildingType* withered = sim::withered_record(buildings, ripe);
            withered != nullptr && buildings.by_id(withered->id) == withered) {
            add_once(pieces, seen, *withered, SpecialPiece{withered->id, 0, 1, SpecialKind::Withered});
        }
    }
    for (const assets::BuildingType& b : all) {
        if (buildings.by_id(b.id) == &b && ruin(b) && b.size_x > 0 && b.size_y > 0) {
            add_once(pieces, seen, b, SpecialPiece{b.id, 0, 1, SpecialKind::Ruin});
        }
    }
    for (const std::uint32_t id : assets::id::kUnusedBuildings) {
        if (const assets::BuildingType* b = buildings.by_id(id); b != nullptr && b->size_x > 0 && b->size_y > 0) {
            add_once(pieces, seen, *b, SpecialPiece{b->id, 0, 1, SpecialKind::Building});
        }
    }
    return pieces;
}

bool is_special(const assets::BuildingTable& buildings, const assets::BuildingType& building) noexcept {
    if (ruin(building) || unused_building(building) || ripe_crop(buildings, building)) {
        return true;
    }
    if (building.prod_kind != assets::prod::kGrowing) {
        return false;
    }
    const assets::BuildingType* ripe =
        building.withered ? sim::ripe_of_withered(buildings, building) : sim::ripe_record(buildings, building);
    return ripe != nullptr && ripe_crop(buildings, *ripe);
}

SpecialRefusal special_fits(const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                            sim::TilePos origin, const SpecialPiece& piece, std::uint8_t rotation) {
    const assets::BuildingType* b = buildings.by_id(piece.id);
    if (b == nullptr || island >= world.islands().size()) {
        return SpecialRefusal::NoIsland;
    }
    const sim::WorldIsland& isl = world.islands()[island];
    const sim::IslandMap& map = isl.map;
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    sim::rotated_size(*b, rotation, w, h);
    if (origin.x < 0 || origin.y < 0 || static_cast<std::uint32_t>(origin.x) + w > map.width() ||
        static_cast<std::uint32_t>(origin.y) + h > map.height()) {
        return SpecialRefusal::NoIsland;
    }
    const bool beach = b->kind == assets::kind::kBeachRuin;
    for (std::uint32_t j = 0; j < h; ++j) {
        for (std::uint32_t i = 0; i < w; ++i) {
            const std::uint32_t x = static_cast<std::uint32_t>(origin.x) + i;
            const std::uint32_t y = static_cast<std::uint32_t>(origin.y) + j;
            const sim::MapCell& cell = map.top(x, y);
            const assets::BuildingType* now = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (now == nullptr) {
                return SpecialRefusal::NoIsland;
            }
            if (cell.city != sim::MapCell::kNoCity) {
                return SpecialRefusal::Taken;
            }
            const auto group = terrain_group(buildings, *now);
            if (!group) {
                return SpecialRefusal::Taken;  // a building, road, field or ruin
            }
            const bool fits = beach ? *group == TerrainGroup::Beach
                                    : *group == TerrainGroup::Ground || *group == TerrainGroup::Forest;
            if (!fits) {
                return SpecialRefusal::Ground;
            }
        }
    }
    if (piece.kind == SpecialKind::Building) {
        // A soldier on its footprint would be shut in.
        const std::int64_t left = (std::int64_t{isl.x} + origin.x) * sim::kPosUnit;
        const std::int64_t top = (std::int64_t{isl.y} + origin.y) * sim::kPosUnit;
        const std::int64_t right = left + std::int64_t{w} * sim::kPosUnit;
        const std::int64_t bottom = top + std::int64_t{h} * sim::kPosUnit;
        for (const sim::Soldier& s : world.soldiers()) {
            if (s.pos_x >= left && s.pos_x < right && s.pos_y >= top && s.pos_y < bottom) {
                return SpecialRefusal::Unit;
            }
        }
    }
    return SpecialRefusal::None;
}

annokit::core::Result<std::unique_ptr<Operation>> place_special(const sim::World& world,
                                                                const assets::BuildingTable& buildings,
                                                                std::uint16_t island, sim::TilePos origin,
                                                                const SpecialPiece& piece, std::uint8_t rotation) {
    const SpecialRefusal refused = special_fits(world, buildings, island, origin, piece, rotation);
    if (refused != SpecialRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    const assets::BuildingType& b = *buildings.by_id(piece.id);
    const std::uint8_t turn = b.rotate > 0 ? static_cast<std::uint8_t>(rotation & 3U) : std::uint8_t{0};
    const std::uint8_t stage = piece.kind == SpecialKind::Growing ? piece.stage : std::uint8_t{0};
    sim::World after = world;
    sim::IslandMap& map = after.island(island).map;
    // What lay there goes to the ground layer: taken away, the object leaves it again.
    if (auto placed = map.place(b, origin.x, origin.y, turn, stage, sim::kNoPlayer, sim::MapCell::kNoCity); !placed) {
        return make_error(ErrorCode::InvalidFormat, placed.error().message);
    }
    if (piece.kind == SpecialKind::Growing) {
        const auto x = static_cast<std::uint16_t>(origin.x);
        const auto y = static_cast<std::uint16_t>(origin.y);
        if (const auto entry = sim::missing_growth_entry(after.growth(), buildings, island, x, y, map.top(x, y))) {
            after.growth().entries.push_back(*entry);
        }
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "special_place"));
}

const char* refusal_name(SpecialRefusal refusal) noexcept {
    switch (refusal) {
        case SpecialRefusal::None:
            return "fits";
        case SpecialRefusal::NoIsland:
            return "not on the island";
        case SpecialRefusal::Ground:
            return "not on open land";
        case SpecialRefusal::Taken:
            return "land of a city or built";
        case SpecialRefusal::Unit:
            return "a land unit stands there";
    }
    return "unknown";
}

}  // namespace o1602::editor
