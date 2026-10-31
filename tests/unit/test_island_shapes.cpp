// SPDX-License-Identifier: GPL-3.0-or-later
// Islands of their own shape (world generator stage 2, docs/re/worldgen.md "Island shapes").

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/sim/island_shapes.hpp"

using namespace o1602;
using sim::shapes::Coast;
namespace ids = sim::shapes::ids;

namespace {

template <std::size_t N>
bool one_of(std::uint32_t id, const std::array<std::uint32_t, N>& list) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

/// What the tile with this id is, as far as the coast goes.
std::optional<Coast> coast_of(std::uint32_t id) {
    if (one_of(id, ids::kOpenSea)) {
        return Coast::OpenSea;
    }
    if (one_of(id, ids::kSurf)) {
        return Coast::Surf;
    }
    if (one_of(id, ids::kSurfCorner)) {
        return Coast::SurfCorner;
    }
    if (one_of(id, ids::kSurfBay)) {
        return Coast::SurfBay;
    }
    if (one_of(id, ids::kBeach)) {
        return Coast::Beach;
    }
    if (one_of(id, ids::kOuterCorner)) {
        return Coast::OuterCorner;
    }
    if (one_of(id, ids::kInnerCorner)) {
        return Coast::InnerCorner;
    }
    return std::nullopt;  // ground, forest, mountains
}

int footprint(std::uint32_t id) {
    if (one_of(id, ids::kBigMountain)) {
        return ids::kBigMountainSize;
    }
    if (one_of(id, ids::kSmallMountain)) {
        return ids::kSmallMountainSize;
    }
    return 1;
}

bool is_sea(Coast c) {
    return c == Coast::OpenSea || c == Coast::Surf || c == Coast::SurfCorner || c == Coast::SurfBay;
}

struct Checked {
    std::size_t land = 0;
    std::size_t big = 0;
    std::size_t small = 0;
};

/// Every tile is laid once, every coast tile is the one its neighbours ask for.
Checked check_island(const assets::Island& island, const sim::shapes::ShapeRequest& r) {
    const int w = island.header.width;
    const int h = island.header.height;
    CHECK(w == r.width);
    CHECK(h == r.height);
    CHECK(island.header.template_number == assets::IslandHeader::kNoTemplate);
    CHECK(island.header.stored_complete);
    CHECK(island.header.template_path().empty());
    REQUIRE(island.layers.size() == 1);
    std::vector<int> laid(static_cast<std::size_t>(w * h), 0);
    std::vector<std::optional<Coast>> coast(static_cast<std::size_t>(w * h));
    std::vector<std::uint8_t> rotation(static_cast<std::size_t>(w * h), 0);
    Checked out;
    for (const assets::IslandTile& t : island.layers[0]) {
        CHECK(t.island_number() == r.number);
        CHECK(t.city() == assets::IslandTile::kNoCity);
        const int size = footprint(t.cod_id());
        out.big += size == ids::kBigMountainSize ? 1U : 0U;
        out.small += size == ids::kSmallMountainSize ? 1U : 0U;
        for (int y = t.y; y < t.y + size; ++y) {
            for (int x = t.x; x < t.x + size; ++x) {
                REQUIRE(x < w);
                REQUIRE(y < h);
                const auto i = static_cast<std::size_t>(y * w + x);
                ++laid[i];
                coast[i] = coast_of(t.cod_id());
                rotation[i] = t.rotation();
            }
        }
    }
    CHECK(std::all_of(laid.begin(), laid.end(), [](int n) { return n == 1; }));
    const auto land = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return false;
        }
        const auto& c = coast[static_cast<std::size_t>(y * w + x)];
        return !c || !is_sea(*c);
    };
    constexpr std::array<int, 8> dx = {0, 1, 1, 1, 0, -1, -1, -1};
    constexpr std::array<int, 8> dy = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t n = 0;
            for (std::size_t d = 0; d < 8; ++d) {
                n = static_cast<std::uint8_t>(n | (land(x + dx[d], y + dy[d]) ? 1U << d : 0U));
            }
            const auto want = sim::shapes::coast_tile(land(x, y), n);
            REQUIRE(want.has_value());
            const auto& has = coast[static_cast<std::size_t>(y * w + x)];
            if (want->coast == Coast::Inland) {
                CHECK(!has.has_value());
            } else {
                REQUIRE(has.has_value());
                CHECK(*has == want->coast);
                if (want->coast != Coast::OpenSea) {
                    CHECK(rotation[static_cast<std::size_t>(y * w + x)] == want->rotation);
                }
            }
            if (land(x, y)) {
                ++out.land;
                CHECK((x >= 2 && y >= 2 && x < w - 2 && y < h - 2));
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("island shapes: the coast tiles of the templates") {
    using sim::shapes::coast_tile;
    // Land: with land all around, sea to the east (rotation 3), sea to the south and west (an
    // outer corner, rotation 0), sea only to the north-east (an inner corner, rotation 0).
    CHECK(coast_tile(true, 255)->coast == Coast::Inland);
    CHECK((coast_tile(true, 241)->coast == Coast::Beach && coast_tile(true, 241)->rotation == 3));
    CHECK((coast_tile(true, 7)->coast == Coast::OuterCorner && coast_tile(true, 7)->rotation == 0));
    CHECK((coast_tile(true, 253)->coast == Coast::InnerCorner && coast_tile(true, 253)->rotation == 0));
    // Sea: land to the west (rotation 0), only to the north-east (21207, rotation 0), to the north
    // and east (rotation 0).
    CHECK(coast_tile(false, 0)->coast == Coast::OpenSea);
    CHECK((coast_tile(false, 224)->coast == Coast::Surf && coast_tile(false, 224)->rotation == 0));
    CHECK((coast_tile(false, 2)->coast == Coast::SurfCorner && coast_tile(false, 2)->rotation == 0));
    CHECK((coast_tile(false, 7)->coast == Coast::SurfBay && coast_tile(false, 7)->rotation == 0));
    // What the templates have no tile for.
    CHECK(!coast_tile(true, 1 | 16));          // land one tile wide
    CHECK(!coast_tile(true, 255 & ~2 & ~32));  // the sea on two diagonals
    CHECK(!coast_tile(false, 1 | 16));         // water one tile wide
    CHECK(!coast_tile(false, 2 | 128));        // water between two corners
    CHECK(!coast_tile(false, 1 | 4 | 16));     // a bay three sides deep
    // Each side of the sea has its own beach rotation.
    std::array<bool, 4> seen{};
    for (unsigned n = 0; n < 256; ++n) {
        if (auto c = coast_tile(true, static_cast<std::uint8_t>(n)); c && c->coast == Coast::Beach) {
            seen[c->rotation] = true;
        }
    }
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

TEST_CASE("island shapes: every size makes an island whose coast fits, with its mountains") {
    struct Size {
        std::uint16_t width, height;
        std::uint8_t size, big, small;
    };
    constexpr std::array<Size, 5> kSizes = {
        {{30, 30, 0, 0, 1}, {40, 40, 1, 0, 1}, {50, 52, 2, 1, 1}, {70, 60, 3, 1, 2}, {100, 90, 4, 2, 3}}};
    for (const Size& s : kSizes) {
        for (std::uint64_t seed = 0; seed < 40; ++seed) {
            sim::shapes::ShapeRequest r;
            r.width = s.width;
            r.height = s.height;
            r.size = s.size;
            r.climate = static_cast<std::uint8_t>(seed & 1U);
            r.number = static_cast<std::uint8_t>(seed + 3);
            r.big_mountains = s.big;
            r.small_mountains = s.small;
            r.seed = seed * 7919U + s.width;
            const assets::Island island = sim::shapes::make_island(r);
            const Checked c = check_island(island, r);
            const std::size_t area = std::size_t{s.width} * s.height;
            CHECK(c.land * 4 >= area);  // the templates are 40..60 % land
            CHECK(c.land * 4 <= area * 3);
            CHECK(c.big == s.big);
            CHECK(c.small == s.small);
            CHECK(island.header.template_size == s.size);
            CHECK(island.header.climate == r.climate);
        }
    }
}

TEST_CASE("island shapes: the same request gives the same island, another seed another") {
    sim::shapes::ShapeRequest r;
    r.width = 70;
    r.height = 60;
    r.size = 3;
    r.big_mountains = 1;
    r.small_mountains = 2;
    r.number = 5;
    r.seed = 1602;
    const auto a = sim::shapes::make_island(r);
    const auto b = sim::shapes::make_island(r);
    REQUIRE(a.layers.size() == 1);
    REQUIRE(b.layers.size() == 1);
    REQUIRE(a.layers[0].size() == b.layers[0].size());
    CHECK(std::equal(a.layers[0].begin(), a.layers[0].end(), b.layers[0].begin(),
                     [](const assets::IslandTile& x, const assets::IslandTile& y) {
                         return x.id == y.id && x.x == y.x && x.y == y.y && x.bits == y.bits;
                     }));
    r.seed = 1603;
    const auto c = sim::shapes::make_island(r);
    const bool same = c.layers[0].size() == a.layers[0].size() &&
                      std::equal(a.layers[0].begin(), a.layers[0].end(), c.layers[0].begin(),
                                 [](const assets::IslandTile& x, const assets::IslandTile& y) {
                                     return x.id == y.id && x.x == y.x && x.y == y.y;
                                 });
    CHECK(!same);
}

TEST_CASE("island shapes: every coast, outline and share of land, from the smallest to the largest side") {
    using sim::shapes::Form;
    using sim::shapes::Roughness;
    struct Size {
        std::uint16_t width, height;
    };
    constexpr std::array<Size, 4> kSizes = {{{16, 16}, {64, 24}, {180, 120}, {250, 250}}};
    for (const Size& s : kSizes) {
        for (const auto roughness : {Roughness::Smooth, Roughness::Natural, Roughness::Rugged}) {
            for (const auto form : {Form::Compact, Form::Branched}) {
                for (const std::uint16_t land : std::array<std::uint16_t, 2>{0, 740}) {
                    sim::shapes::ShapeRequest r;
                    r.width = s.width;
                    r.height = s.height;
                    r.size = 4;
                    r.number = 9;
                    r.roughness = roughness;
                    r.form = form;
                    r.land = land;
                    r.big_mountains = s.width >= 100 ? 6 : 0;
                    r.small_mountains = s.width >= 100 ? 8 : 0;
                    r.seed = std::uint64_t{s.width} * 31U + static_cast<std::uint64_t>(roughness) * 7U +
                             static_cast<std::uint64_t>(form) + land;
                    const assets::Island island = sim::shapes::make_island(r);
                    const Checked c = check_island(island, r);
                    const std::size_t area = std::size_t{s.width} * s.height;
                    CHECK(c.land * 5 >= area);  // never a speck
                    if (land >= 700 && form == Form::Compact && s.width >= 100) {
                        CHECK(c.land * 100 >= area * 58);  // a world of much land: fuller islands
                    }
                    CHECK(c.big == r.big_mountains);
                    CHECK(c.small == r.small_mountains);
                }
            }
        }
    }
}
