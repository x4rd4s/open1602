// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/editor/objects.hpp"
#include "o1602/editor/special_objects.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kGrowing = 21301;
constexpr std::uint32_t kRipe = 21302;
constexpr std::uint32_t kWithered = 21371;
constexpr std::uint32_t kTree = 21304;
constexpr std::uint32_t kRuin = 22601;
constexpr std::uint32_t kBeach = 21011;
constexpr std::uint32_t kBeachRuin = 22610;
constexpr std::uint32_t kOutpost = 21801;
constexpr std::uint32_t kBig = 20601;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 21301\n  Kind: 10\n  Gfx: 10\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21302\n  Kind: 10\n  Gfx: 15\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 45\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21371\n  Kind: 10\n  Gfx: 16\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Doerrflg: 1\n  EndObj\n"
        "  @Nummer: 4\n  Id: 21304\n  Kind: 10\n  Gfx: 21\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        "  @Nummer: 5\n  Id: 22601\n  Kind: 12\n  Gfx: 22\n  Size: 1, 1\n"
        "  @Nummer: 6\n  Id: 21011\n  Kind: 23\n  Gfx: 23\n  Size: 1, 1\n"
        "  @Nummer: 7\n  Id: 22610\n  Kind: 29\n  Gfx: 24\n  Size: 1, 1\n"
        "  @Nummer: 8\n  Id: 21801\n  Kind: 14\n  Gfx: 25\n  Size: 1, 1\n  Rotate: 1\n"
        "  @Nummer: 9\n  Id: 20601\n  Kind: 14\n  Gfx: 29\n  Size: 2, 2\n  Rotate: 4\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 20 x 20 ground at world (10, 10): forest on row 5, beach on column 19.
struct Island {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{60, 60};

    Island() {
        sim::IslandMap map(20, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 20; ++x) {
                const std::uint32_t id = x == 19 ? kBeach : y == 5 ? kTree : kGround;
                REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    }

    [[nodiscard]] const assets::BuildingType* at(std::int32_t x, std::int32_t y) const {
        const sim::MapCell& c =
            world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        return buildings.by_sprite(c.sprite);
    }
};

editor::SpecialPiece piece_of(const std::vector<editor::SpecialPiece>& pieces, std::uint32_t id,
                              std::uint8_t stage = 0) {
    for (const editor::SpecialPiece& p : pieces) {
        if (p.id == id && p.stage == stage) {
            return p;
        }
    }
    REQUIRE(false);  // no such special piece
    return {};
}

}  // namespace

TEST_CASE("editor special objects: the crops at each stage, the ruins and the unused buildings") {
    Island island;
    const std::vector<editor::SpecialPiece> pieces = editor::special_pieces(island.buildings);
    REQUIRE(pieces.size() == 11);  // 5 stages, ripe, withered, 2 ruins, 2 buildings; no tree
    using Kind = editor::SpecialKind;
    for (std::uint8_t s = 0; s < 5; ++s) {
        CHECK(pieces[s] == (editor::SpecialPiece{kGrowing, s, 5, Kind::Growing}));
    }
    CHECK(pieces[5] == (editor::SpecialPiece{kRipe, 0, 1, Kind::Ripe}));
    CHECK(pieces[6] == (editor::SpecialPiece{kWithered, 0, 1, Kind::Withered}));
    CHECK(pieces[7].id == kRuin);
    CHECK(pieces[8].id == kBeachRuin);
    CHECK(pieces[9] == (editor::SpecialPiece{kOutpost, 0, 1, Kind::Building}));
    CHECK(pieces[10] == (editor::SpecialPiece{kBig, 0, 1, Kind::Building}));
    for (const std::uint32_t id : {kGrowing, kRipe, kWithered, kRuin, kBeachRuin, kOutpost, kBig}) {
        CHECK(editor::is_special(island.buildings, *island.buildings.by_id(id)));
    }
    for (const std::uint32_t id : {kGround, kTree, kBeach}) {
        CHECK(!editor::is_special(island.buildings, *island.buildings.by_id(id)));
    }
}

TEST_CASE("editor special objects: a wild field grows from its stage, without owner, and goes again") {
    Island island;
    const auto pieces = editor::special_pieces(island.buildings);
    const editor::SpecialPiece field = piece_of(pieces, kGrowing, 2);
    auto placed = editor::place_special(island.world, island.buildings, 0, {3, 5}, field, 0);  // on forest
    REQUIRE(placed.ok());
    (*placed)->apply(island.world);
    const sim::MapCell& cell = island.world.islands()[0].map.top(3, 5);
    CHECK(island.at(3, 5)->id == kGrowing);
    CHECK(cell.anim_phase == 2);
    CHECK(cell.owner == sim::kNoPlayer);
    CHECK(cell.city == sim::MapCell::kNoCity);
    REQUIRE(island.world.growth().entries.size() == 1);
    CHECK(island.world.growth().entries[0].stage == 2);
    CHECK(island.world.growth().entries[0].x == 3);
    // Not twice on one tile.
    CHECK(editor::special_fits(island.world, island.buildings, 0, {3, 5}, field, 0) == editor::SpecialRefusal::Taken);
    // The remove tool takes it away: the forest comes back, the field grows no more.
    auto gone = editor::remove_building(island.world, island.buildings, 0, {3, 5});
    REQUIRE(gone.ok());
    (*gone)->apply(island.world);
    CHECK(island.at(3, 5)->id == kTree);
    CHECK(island.world.growth().entries.empty());
    (*gone)->revert(island.world);
    CHECK(island.at(3, 5)->id == kGrowing);
    CHECK(island.world.growth().entries.size() == 1);
    (*placed)->revert(island.world);
    CHECK(island.at(3, 5)->id == kTree);
    CHECK(island.world.growth().entries.empty());
}

TEST_CASE("editor special objects: ruins on land or beach, buildings on open land of the island") {
    Island island;
    const auto pieces = editor::special_pieces(island.buildings);
    using R = editor::SpecialRefusal;
    const auto fits = [&](std::uint32_t id, sim::TilePos at) {
        return editor::special_fits(island.world, island.buildings, 0, at, piece_of(pieces, id), 0);
    };
    CHECK(fits(kRuin, {4, 4}) == R::None);
    CHECK(fits(kRuin, {19, 4}) == R::Ground);
    CHECK(fits(kBeachRuin, {19, 4}) == R::None);
    CHECK(fits(kBeachRuin, {4, 4}) == R::Ground);
    CHECK(fits(kBig, {18, 4}) == R::Ground);  // half on the beach
    CHECK(fits(kBig, {19, 19}) == R::NoIsland);
    CHECK(fits(kBig, {-1, 0}) == R::NoIsland);
    CHECK(fits(kBig, {4, 4}) == R::None);  // over the forest too

    auto big = editor::place_special(island.world, island.buildings, 0, {4, 4}, piece_of(pieces, kBig), 1);
    REQUIRE(big.ok());
    (*big)->apply(island.world);
    CHECK(island.at(5, 5)->id == kBig);
    CHECK(island.world.islands()[0].map.top(5, 5).rotation == 1);
    const auto at = sim::building_at(island.world, island.buildings, 0, {5, 5});
    REQUIRE(at.has_value());
    CHECK(at->origin.x == 4);
    CHECK(at->origin.y == 4);
    CHECK(fits(kOutpost, {5, 4}) == R::Taken);
    CHECK(!editor::place_special(island.world, island.buildings, 0, {5, 4}, piece_of(pieces, kOutpost), 0).ok());
    auto gone = editor::remove_building(island.world, island.buildings, 0, {4, 5});
    REQUIRE(gone.ok());
    (*gone)->apply(island.world);
    CHECK(island.at(4, 4)->id == kGround);
    CHECK(island.at(4, 5)->id == kTree);
}
