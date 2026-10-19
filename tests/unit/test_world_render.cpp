// SPDX-License-Identifier: GPL-3.0-or-later
// World map, animation clock, world draw list and sprite atlas.

#include <algorithm>
#include <array>
#include <bit>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/camera.hpp"
#include "o1602/render/ship_trail.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/sprite_hit_mask.hpp"
#include "o1602/render/text_cache.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// Open sea (Kind 19 with production kind 9, 4 ripple frames of 100 ms) and a static 2x2 building.
assets::BuildingTable make_table() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"HAUS_PRODTYP", 2}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n"
        "  Id: 21201\n  Gfx: 10\n  Size: 1, 1\n  Kind: 19\n"
        "  AnimAnz: 4\n  AnimAdd: 1\n  AnimTime: 100\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n  EndObj\n"
        "  @Nummer: +1\n"
        "  Id: 20100\n  Gfx: 100\n  Size: 2, 2\n  Rotate: 4\n  Posoffs: 20\n  Kind: 14\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

sim::WorldIsland make_island(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(w, h), x, y};
    return island;
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

/// BSH file with solid sprites; pixel (x, y) of sprite i has palette index i * 16 + x + y.
std::vector<std::uint8_t> make_bsh(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes) {
    std::vector<std::vector<std::uint8_t>> records;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto [w, h] = sizes[i];
        std::vector<std::uint8_t> rle;
        for (std::uint32_t y = 0; y < h; ++y) {
            rle.push_back(0);
            rle.push_back(static_cast<std::uint8_t>(w));
            for (std::uint32_t x = 0; x < w; ++x) {
                rle.push_back(static_cast<std::uint8_t>(i * 16 + x + y));
            }
            rle.push_back(y + 1 == h ? 0xFF : 0xFE);
        }
        std::vector<std::uint8_t> rec;
        put_u32(rec, w);
        put_u32(rec, h);
        put_u32(rec, 1);
        put_u32(rec, static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3}));
        rec.insert(rec.end(), rle.begin(), rle.end());
        rec.resize((rec.size() + 3) & ~std::size_t{3}, 0);
        records.push_back(std::move(rec));
    }
    std::vector<std::uint8_t> payload;
    auto offset = static_cast<std::uint32_t>(4 * records.size());
    for (const auto& rec : records) {
        put_u32(payload, offset);
        offset += static_cast<std::uint32_t>(rec.size());
    }
    for (const auto& rec : records) {
        payload.insert(payload.end(), rec.begin(), rec.end());
    }
    std::vector<std::uint8_t> file(16, 0);
    std::copy_n("BSH", 3, file.begin());
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

assets::Palette make_palette() {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 256; ++i) {
        payload.insert(payload.end(), {static_cast<std::uint8_t>(i), 1, 2, 0});
    }
    std::vector<std::uint8_t> file(16, 0);
    std::copy_n("COL", 3, file.begin());
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
    auto chunks = assets::ChunkFile::from_bytes(file);
    auto palette = assets::Palette::from_chunk_file(*chunks);
    return std::move(palette).value();
}

}  // namespace

TEST_CASE("building table: open sea record") {
    const auto table = make_table();
    REQUIRE(table.open_sea() != nullptr);
    CHECK(table.open_sea()->id == 21201);
    CHECK(table.index_of(*table.open_sea()) == 0);
    const auto copy = table;  // the open sea is stored as an index, copies stay valid
    CHECK(copy.open_sea() == &copy.types()[0]);
}

TEST_CASE("world: islands occupy their cells, overlap and out of bounds are rejected") {
    sim::World world;
    CHECK(world.cell(0, 0).island == sim::World::kSea);
    REQUIRE(world.add_island(make_island(10, 20, 4, 3)).ok());
    CHECK(world.cell(10, 20).island == 0);
    CHECK(world.cell(13, 22).island == 0);
    CHECK(world.cell(13, 22).local_x == 3);
    CHECK(world.cell(13, 22).local_y == 2);
    CHECK(world.cell(14, 22).island == sim::World::kSea);
    CHECK(world.cell(13, 23).island == sim::World::kSea);

    CHECK(!world.add_island(make_island(12, 21, 4, 4)).ok());   // overlap
    CHECK(!world.add_island(make_island(498, 0, 4, 4)).ok());   // leaves the map
    CHECK(world.cell(14, 21).island == sim::World::kSea);       // failed adds leave no trace
    REQUIRE(world.add_island(make_island(14, 20, 2, 2)).ok());  // directly adjacent is fine
    CHECK(world.cell(14, 20).island == 1);
    CHECK(world.islands().size() == 2);
}

TEST_CASE("animation clock: steps once AnimTime is reached, at most one frame per update") {
    const auto table = make_table();
    const auto& sea = *table.open_sea();
    const auto& house = *table.by_id(20100);
    render::AnimationClock clock(table);
    CHECK(clock.frame(sea) == 0);
    clock.advance(60);  // 60 + 30 < 100
    CHECK(clock.frame(sea) == 0);
    clock.advance(60);  // 120 + 30 >= 100
    CHECK(clock.frame(sea) == 1);
    clock.advance(10);  // accumulator was reset
    CHECK(clock.frame(sea) == 1);
    clock.advance(1000);  // long frame: still a single step (original behaviour)
    CHECK(clock.frame(sea) == 2);
    CHECK(clock.frame(house) == 0);  // AnimTime 0: never animates
}

TEST_CASE("world renderer: open sea ripple pattern") {
    const auto table = make_table();
    const auto& sea = *table.open_sea();
    CHECK(render::sea_sprite(sea, 0, 0, 0) == 10);
    CHECK(render::sea_sprite(sea, 0, 1, 0) == 13);  // 3x
    CHECK(render::sea_sprite(sea, 0, 0, 1) == 11);  // + y
    CHECK(render::sea_sprite(sea, 1, 1, 0) == 10);  // (1 + 3) % 4
    CHECK(render::sea_sprite(sea, 0, 2, 3) == 11);  // (6 + 3) % 4
}

TEST_CASE("world renderer: islands replace the sea, buildings use their elevation") {
    const auto table = make_table();
    sim::World world;
    auto island = make_island(10, 20, 2, 2);
    REQUIRE(island.map.place(*table.by_id(20100), 0, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    const render::AnimationClock clock(table);

    render::WorldView view;
    view.zoom = render::Zoom::Small;  // 16 x 8 px tiles
    render::Viewport all;
    render::world_pixel_size(view, all.width, all.height);
    CHECK(all.width == 850 * 8);
    CHECK(all.height == 850 * 4);
    const auto commands = render::build_world_draw_list(world, table, clock, view, all, render::SpriteExtent{16, 8});
    CHECK(commands.size() == std::size_t{500} * 350);
    std::size_t house = 0;
    for (const auto& c : commands) {
        if (c.sprite >= 100 && c.sprite < 104) {
            ++house;
        }
    }
    CHECK(house == 4);

    // Tile (10, 20) at rotation 0: left edge (10 - 20 + 349) * 8, row 30 bottom 30 * 4 + 7, raised by 20 * 16 / 64.
    const auto it = std::find_if(commands.begin(), commands.end(), [](const auto& c) { return c.sprite == 100; });
    REQUIRE(it != commands.end());
    CHECK(it->x == 339 * 8);
    CHECK(it->y == 30 * 4 + 7 - 5);

    std::int32_t cx = 0;
    std::int32_t cy = 0;
    render::world_tile_center(view, 10, 20, cx, cy);
    CHECK(cx == it->x + 8);
    CHECK(cy == 30 * 4 + 4);
}

TEST_CASE("world renderer: marked tiles get a diamond on top of everything") {
    const auto table = make_table();
    sim::World world;
    auto island = make_island(10, 20, 2, 2);
    REQUIRE(island.map.place(*table.by_id(20100), 0, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    const render::AnimationClock clock(table);
    render::WorldView view;
    view.zoom = render::Zoom::Small;
    render::Viewport all;
    render::world_pixel_size(view, all.width, all.height);
    const std::vector<render::TileMark> marks{{0, 1, 0, assets::Rgba{220, 30, 30, 140}},
                                              {0, 0, 1, assets::Rgba{220, 30, 30, 140}}};
    render::WorldLayers layers;
    layers.extent = render::SpriteExtent{16, 8};
    layers.tile_marks = &marks;
    std::vector<render::DrawCommand> commands;
    render::build_world_draw_list(world, table, clock, view, all, layers, commands);
    REQUIRE(commands.size() == std::size_t{500} * 350 + 2);
    for (std::size_t i = commands.size() - 2; i < commands.size(); ++i) {
        CHECK(commands[i].sprite == render::DrawCommand::kOverlayDiamond);
        CHECK(commands[i].tint.r == 220);
        CHECK(commands[i].tint_size == 16);
    }
}

TEST_CASE("world renderer: a hatched tile lies on the ground, under what stands on it") {
    const auto table = make_table();
    sim::World world;
    auto island = make_island(10, 20, 2, 2);
    REQUIRE(island.map.place(*table.by_id(20100), 0, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    const render::AnimationClock clock(table);
    render::WorldView view;
    view.zoom = render::Zoom::Small;
    render::Viewport all;
    render::world_pixel_size(view, all.width, all.height);
    render::TileMark hatched{0, 1, 1, render::kDragHatch};
    hatched.hatched = true;
    const std::vector<render::TileMark> marks{hatched, {0, 0, 0, assets::Rgba{220, 30, 30, 140}}};
    render::WorldLayers layers;
    layers.extent = render::SpriteExtent{16, 8};
    layers.tile_marks = &marks;
    std::vector<render::DrawCommand> commands;
    render::build_world_draw_list(world, table, clock, view, all, layers, commands);
    REQUIRE(commands.size() == std::size_t{500} * 350 + 2);
    // Only the red mark goes last; the hatched one follows its row's ground.
    CHECK(commands.back().sprite == render::DrawCommand::kOverlayDiamond);
    const auto it = std::find_if(commands.begin(), commands.end(), [](const render::DrawCommand& c) {
        return c.sprite == render::DrawCommand::kOverlayHatch;
    });
    REQUIRE(it != commands.end());
    CHECK(it < commands.end() - 1);
    CHECK(it->tint == render::kDragHatch);
    CHECK(it->tint_size == 16);
    CHECK(it->overlay());
}

TEST_CASE("world renderer: a tile marked twice shows its last mark; kept working memory changes nothing") {
    const auto table = make_table();
    sim::World world;
    auto island = make_island(10, 20, 2, 2);
    REQUIRE(island.map.place(*table.by_id(20100), 0, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    const render::AnimationClock clock(table);
    render::WorldView view;
    view.zoom = render::Zoom::Small;
    render::Viewport all;
    render::world_pixel_size(view, all.width, all.height);
    render::TileMark hatched{0, 0, 0, render::kDragHatch};
    hatched.hatched = true;
    const std::vector<render::TileMark> marks{{0, 1, 0, assets::Rgba{220, 30, 30, 140}},
                                              {0, 0, 1, assets::Rgba{30, 30, 220, 140}},
                                              hatched,
                                              {0, 1, 0, assets::Rgba{30, 220, 30, 140}}};
    render::WorldLayers layers;
    layers.extent = render::SpriteExtent{16, 8};
    layers.tile_marks = &marks;
    std::vector<render::DrawCommand> fresh;
    render::build_world_draw_list(world, table, clock, view, all, layers, fresh);
    REQUIRE(fresh.size() == std::size_t{500} * 350 + 3);
    CHECK(std::count_if(fresh.begin(), fresh.end(), [](const render::DrawCommand& c) {
              return c.sprite == render::DrawCommand::kOverlayHatch;
          }) == 1);
    std::set<std::uint8_t> reds;
    for (std::size_t i = fresh.size() - 2; i < fresh.size(); ++i) {
        CHECK(fresh[i].sprite == render::DrawCommand::kOverlayDiamond);
        reds.insert(fresh[i].tint.r);
    }
    CHECK((reds == std::set<std::uint8_t>{30}));  // green and blue, not the first red

    // The same lists with working memory kept from a frame with other marks and from itself.
    const auto same = [](const std::vector<render::DrawCommand>& a, const std::vector<render::DrawCommand>& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                          [](const render::DrawCommand& x, const render::DrawCommand& y) {
                              return x.sprite == y.sprite && x.x == y.x && x.y == y.y && x.tint == y.tint &&
                                     x.tint_size == y.tint_size && x.mode == y.mode && x.cut_bottom == y.cut_bottom &&
                                     x.edges == y.edges;
                          });
    };
    render::DrawListScratch scratch;
    std::vector<render::DrawCommand> kept;
    const std::vector<render::TileMark> other{{0, 1, 1, assets::Rgba{1, 2, 3, 4}}};
    layers.tile_marks = &other;
    render::build_world_draw_list(world, table, clock, view, all, layers, kept, scratch);
    std::vector<render::DrawCommand> other_fresh;
    render::build_world_draw_list(world, table, clock, view, all, layers, other_fresh);
    CHECK(same(kept, other_fresh));
    layers.tile_marks = &marks;
    for (int frame = 0; frame < 2; ++frame) {
        render::build_world_draw_list(world, table, clock, view, all, layers, kept, scratch);
        CHECK(same(kept, fresh));
    }
}

TEST_CASE("world renderer: a catchment circle lights the land, its border is dashed over land and sea") {
    const auto table = make_table();
    sim::World world;
    auto island = make_island(10, 20, 2, 2);
    REQUIRE(island.map.place(*table.by_id(20100), 0, 0, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    const render::AnimationClock clock(table);
    sim::Catchment area;  // around the island's corner: half on land, half on the open sea
    area.centre = sim::TilePos{10, 20};
    area.radius = 3;
    const render::TileLighting lighting = render::TileLighting::circle(0, area);
    CHECK(!lighting.outlined());  // a clicked building: only the lit land shows its circle
    CHECK(lighting.in_circle(10, 17));
    CHECK(!lighting.in_circle(10, 16));
    sim::MapCell own;
    own.sprite = 100;
    own.owner = 0;
    sim::MapCell other = own;
    other.owner = 2;
    CHECK(!lighting.dimmed(10, 20, &own));
    CHECK(lighting.dimmed(10, 20, &other));   // not the player's land
    CHECK(lighting.dimmed(10, 21, nullptr));  // water, no harbour
    CHECK(lighting.dimmed(10, 16, &own));     // outside the circle
    CHECK(!render::TileLighting::territory(0).dimmed(10, 16, &own));
    CHECK(!render::TileLighting::territory(0).outline(area).dimmed(10, 16, &own));  // only the border

    for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
        render::WorldView view;
        view.zoom = render::Zoom::Small;
        view.rotation = rotation;
        render::Viewport all;
        render::world_pixel_size(view, all.width, all.height);
        render::WorldLayers layers;
        layers.extent = render::SpriteExtent{16, 8};
        render::TileLighting placing = lighting;  // as while placing: the border too
        placing.outline(area);
        layers.lighting = &placing;
        std::vector<render::DrawCommand> commands;
        render::build_world_draw_list(world, table, clock, view, all, layers, commands);
        // One edge for each pair of a tile inside and its neighbour outside.
        std::size_t expected = 0;
        for (std::int32_t y = 10; y < 30; ++y) {
            for (std::int32_t x = 0; x < 20; ++x) {
                if (lighting.in_circle(x, y)) {
                    expected += static_cast<std::size_t>(!lighting.in_circle(x - 1, y)) +
                                static_cast<std::size_t>(!lighting.in_circle(x + 1, y)) +
                                static_cast<std::size_t>(!lighting.in_circle(x, y - 1)) +
                                static_cast<std::size_t>(!lighting.in_circle(x, y + 1));
                }
            }
        }
        std::size_t edges = 0;
        std::size_t dimmed = 0;
        for (const auto& c : commands) {
            if (c.sprite == render::DrawCommand::kOverlayOutline) {
                edges += static_cast<std::size_t>(std::popcount(static_cast<unsigned>(c.edges)));
                CHECK(c.tint.r == render::kCircleOutline.r);
            } else if (c.mode == render::DrawCommand::Mode::Dimmed) {
                ++dimmed;
            }
        }
        CHECK(edges == expected);
        CHECK(dimmed == std::size_t{500} * 350 - 4);  // all but the player's building
    }
}

TEST_CASE("world renderer: viewport culling keeps exactly the visible tiles (all rotations)") {
    const auto table = make_table();
    const sim::World world;
    const render::AnimationClock clock(table);
    for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
        render::WorldView view;
        view.zoom = render::Zoom::Medium;  // 32 x 16 px tiles
        view.rotation = rotation;
        render::Viewport all;
        render::world_pixel_size(view, all.width, all.height);
        const render::SpriteExtent extent{32, 16};
        const auto full = render::build_world_draw_list(world, table, clock, view, all, extent);
        CHECK(full.size() == std::size_t{500} * 350);

        // Centre of the map and the corners of the diamond (partly outside the map).
        std::vector<render::Viewport> viewports{{all.width / 2 - 320, all.height / 2 - 240, 640, 480}};
        for (const auto& [tx, ty] :
             {std::pair{0U, 0U}, std::pair{499U, 0U}, std::pair{0U, 349U}, std::pair{499U, 349U}}) {
            std::int32_t cx = 0;
            std::int32_t cy = 0;
            render::world_tile_center(view, tx, ty, cx, cy);
            viewports.push_back(render::Viewport{cx - 150, cy - 100, 300, 200});
        }
        for (const render::Viewport& vp : viewports) {
            const auto part = render::build_world_draw_list(world, table, clock, view, vp, extent);
            std::set<std::tuple<std::uint32_t, std::int32_t, std::int32_t>> got;
            for (const auto& c : part) {
                got.emplace(c.sprite, c.x, c.y);
            }
            std::size_t visible = 0;
            for (const auto& c : full) {  // sprite rectangle: [x, x + 32) x [y - 16, y)
                const bool intersects =
                    c.x < vp.left + vp.width && c.x + 32 > vp.left && c.y - 16 < vp.top + vp.height && c.y > vp.top;
                if (intersects) {
                    ++visible;
                    CHECK(got.contains({c.sprite, c.x, c.y}));
                }
            }
            CHECK(visible > 0);
            CHECK(part.size() < visible * 2 + 400);  // conservative, but not the whole map
        }
    }
}

TEST_CASE("sprite atlas: packs sprites without overlap, splits pages, blits back") {
    auto chunks = assets::ChunkFile::from_bytes(make_bsh({{5, 3}, {2, 7}, {6, 6}, {1, 1}, {4, 2}}));
    REQUIRE(chunks.ok());
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    REQUIRE(sheet.ok());
    REQUIRE(sheet->size() == 5);
    const auto palette = make_palette();

    CHECK(!render::SpriteAtlas::build(*sheet, palette, 6).ok());  // 6x6 + padding does not fit

    auto atlas = render::SpriteAtlas::build(*sheet, palette, 10);
    REQUIRE(atlas.ok());
    CHECK(atlas->pages().size() >= 2);
    CHECK(atlas->max_sprite_width() == 6);
    CHECK(atlas->max_sprite_height() == 7);
    for (std::uint32_t i = 0; i < 5; ++i) {
        const auto& r = atlas->rect(i);
        CHECK(r.width == sheet->info(i).width);
        CHECK(r.height == sheet->info(i).height);
        CHECK(r.x + r.width <= 10);
        CHECK(r.y + r.height <= 10);
        for (std::uint32_t j = i + 1; j < 5; ++j) {
            const auto& o = atlas->rect(j);
            const bool overlap = r.page == o.page && r.x < o.x + o.width && o.x < r.x + r.width &&
                                 r.y < o.y + o.height && o.y < r.y + r.height;
            CHECK(!overlap);
        }
        // Pixel (1, 0) of sprite i has palette index i * 16 + 1 -> red channel.
        if (r.width > 1) {
            const auto& page = atlas->pages()[r.page].pixels;
            const std::size_t p = (std::size_t{r.y} * 10 + r.x + 1) * 4;
            CHECK(page[p] == i * 16 + 1);
            CHECK(page[p + 3] == 255);
        }
    }
    CHECK(atlas->rect(99).empty());

    // Blit sprite 2 (6x6) anchored bottom-left at (2, 8) into a 5x5 target at origin (3, 3):
    // visible sprite pixels x 1..5, y 1..5 (clipped on all sides that leave the target).
    render::RgbaImage target;
    target.width = 5;
    target.height = 5;
    target.pixels.assign(5 * 5 * 4, 0);
    render::blit_draw_list({render::DrawCommand{2, 2, 8}, render::DrawCommand{99, 0, 0}}, *atlas, target, 3, 3);
    // target (0, 0) = world (3, 3) = sprite pixel (1, 1) -> index 2 * 16 + 2
    CHECK(target.pixels[0] == 34);
    // target (4, 4) = world (7, 7) = sprite pixel (5, 5) -> 32 + 10
    CHECK(target.pixels[(4 * 5 + 4) * 4] == 42);
    CHECK(target.pixels[(4 * 5 + 4) * 4 + 3] == 255);

    // A hatched diamond 16 wide lights every second of its 80 pixels, on the world's checkerboard,
    // wherever the target's origin is.
    for (const std::int32_t origin : {0, 1}) {
        render::RgbaImage hatch;
        hatch.width = 16;
        hatch.height = 8;
        hatch.pixels.assign(16 * 8 * 4, 0);
        render::DrawCommand c{render::DrawCommand::kOverlayHatch, origin, 8 + origin, render::kDragHatch, 16};
        render::blit_draw_list({c}, *atlas, hatch, origin, origin);
        int lit = 0;
        for (std::int32_t y = 0; y < 8; ++y) {
            for (std::int32_t x = 0; x < 16; ++x) {
                if (hatch.pixels[static_cast<std::size_t>(y * 16 + x) * 4] == 202) {
                    ++lit;
                    CHECK(render::hatch_pixel(x + origin, y + origin));
                }
            }
        }
        CHECK(lit == 40);
    }
}

TEST_CASE("camera: world_tile_at inverts world_tile_center for all zooms and rotations") {
    for (int z = 0; z < 3; ++z) {
        for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
            const render::WorldView view{static_cast<render::Zoom>(z), rotation};
            const std::int32_t hw = render::tile_width(view.zoom) / 2;
            const std::int32_t hh = render::tile_height(view.zoom) / 2;
            for (const auto& [x, y] : {std::pair{0U, 0U}, std::pair{499U, 349U}, std::pair{0U, 349U},
                                       std::pair{499U, 0U}, std::pair{123U, 77U}, std::pair{250U, 300U}}) {
                std::int32_t px = 0;
                std::int32_t py = 0;
                render::world_tile_center(view, x, y, px, py);
                const render::TilePos expected{x, y};
                CHECK(render::world_tile_at(view, px, py) == expected);
                // Points just inside the diamond corners belong to the same tile.
                CHECK(render::world_tile_at(view, px - hw + 1.5, py) == expected);
                CHECK(render::world_tile_at(view, px + hw - 1.5, py) == expected);
                CHECK(render::world_tile_at(view, px, py - hh + 0.75) == expected);
                CHECK(render::world_tile_at(view, px, py + hh - 0.75) == expected);
            }
            CHECK(!render::world_tile_at(view, 0, 0).has_value());  // corner of the bounding box
            CHECK(!render::world_tile_at(view, -100, -100).has_value());
        }
    }
}

TEST_CASE("camera: zoom, rotation and pixel scale keep the anchored tile in place") {
    render::Camera camera;
    camera.set_screen(1280, 800, 1);
    camera.center_on(render::TilePos{200, 150});
    CHECK(camera.tile_at_screen(640, 400) == (render::TilePos{200, 150}));

    // Zoom at the mouse cursor: whatever is under the cursor stays there.
    const auto under_mouse = camera.tile_at_screen(900, 250);
    REQUIRE(under_mouse.has_value());
    for (const auto zoom : {render::Zoom::Medium, render::Zoom::Small, render::Zoom::Large}) {
        camera.set_zoom(zoom, 900, 250);
        CHECK(camera.view().zoom == zoom);
        CHECK(camera.tile_at_screen(900, 250) == under_mouse);
    }

    // Rotation around the screen centre keeps the centre tile.
    const auto centre = camera.tile_at_screen(640, 400);
    for (std::uint8_t r = 1; r <= 4; ++r) {
        camera.set_rotation(r, 640, 400);
        CHECK(camera.view().rotation == (r & 3));
        CHECK(camera.tile_at_screen(640, 400) == centre);
    }

    camera.set_pixel_scale(3, 100, 700);
    const auto scaled = camera.tile_at_screen(100, 700);
    camera.set_pixel_scale(2, 100, 700);
    CHECK(camera.pixel_scale() == 2);
    CHECK(camera.tile_at_screen(100, 700) == scaled);
    const render::Viewport vp = camera.viewport();
    CHECK(vp.width == 641);  // 1280 / 2 + 1
    CHECK(vp.height == 401);
}

TEST_CASE("camera: scrolling is clamped to the map") {
    render::Camera camera;
    camera.set_screen(800, 600, 1);
    camera.center_on(render::TilePos{250, 175});
    // Whichever way the view is pushed, the screen centre stays on the map's diamond, not in the
    // black corners of its bounding box.
    const std::array<std::pair<double, double>, 8> pushes = {
        std::make_pair(-1e6, 0.0),  std::make_pair(1e6, 0.0), std::make_pair(0.0, -1e6), std::make_pair(0.0, 1e6),
        std::make_pair(-1e6, -1e6), std::make_pair(1e6, 1e6), std::make_pair(1e6, -1e6), std::make_pair(-1e6, 1e6)};
    for (const auto& [dx, dy] : pushes) {
        for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
            camera.set_rotation(rotation, 400, 300);
            camera.center_on(render::TilePos{250, 175});
            camera.scroll(dx, dy);
            const auto [x, y] = camera.point_at_screen(400, 300);
            CHECK(x >= -1e-6);
            CHECK(y >= -1e-6);
            CHECK(x <= 500 + 1e-6);
            CHECK(y <= 350 + 1e-6);
        }
    }
    // Straight up from the middle ends at the top corner of the map.
    camera.set_rotation(0, 400, 300);
    camera.center_on(render::TilePos{250, 175});
    camera.scroll(0, -1e6);
    const auto [x, y] = camera.point_at_screen(400, 300);
    CHECK(x + y < 2.0);
}

TEST_CASE("rules: defaults fix the original, original() restores it (ADR 0006)") {
    constexpr auto open = sim::Rules::open1602();
    constexpr auto orig = sim::Rules::original();
    CHECK(open.max_ships_per_player == 100);  // L-001
    CHECK(orig.max_ships_per_player == 33);
    CHECK(open.guarantee_resources);  // S-019
    CHECK(!orig.guarantee_resources);
    CHECK(orig.max_game_speed == 4);  // L-004
    CHECK(!(open == orig));
}

TEST_CASE("world: more than 255 islands (the original's limit, L-003)") {
    sim::World world;
    std::uint32_t added = 0;
    for (std::uint32_t y = 0; y + 2 <= world.height() && added < 300; y += 2) {
        for (std::uint32_t x = 0; x + 2 <= world.width() && added < 300; x += 2) {
            if (world.add_island(make_island(x, y, 2, 2)).ok()) {
                ++added;
            }
        }
    }
    CHECK(added == 300);
    CHECK(world.cell(1, 2).island == 250);  // second row of islands
}

TEST_CASE("world: the size is a property of the map (ADR 0007)") {
    sim::World big(1200, 900);
    CHECK(big.width() == 1200);
    CHECK(big.height() == 900);
    CHECK(big.add_island(make_island(1100, 850, 4, 3)).ok());  // outside the original 500 x 350
    CHECK(!big.add_island(make_island(1198, 0, 4, 3)).ok());   // leaves the map
    CHECK(big.cell(1103, 852).island == 0);

    // Views take the size from the world; the camera from set_world_size.
    const render::WorldView view = render::fit_view(render::WorldView{render::Zoom::Small, 0}, big);
    std::int32_t w = 0;
    std::int32_t h = 0;
    render::world_pixel_size(view, w, h);
    CHECK(w == (1200 + 900) * 8);
    CHECK(h == (1200 + 900) * 4);
    const auto tile = render::world_tile_at(view, 0.0, 0.0);
    CHECK(!tile.has_value());
    std::int32_t px = 0;
    std::int32_t py = 0;
    render::world_tile_center(view, 1103, 852, px, py);
    const auto back = render::world_tile_at(view, px, py);
    REQUIRE(back.has_value());
    CHECK(back->x == 1103);
    CHECK(back->y == 852);
}

namespace {

/// Two sprites: 0 a 4x3 ring (the middle pixels transparent), 1 a 2x2 full square.
class RingSource final : public render::SpriteSource {
public:
    [[nodiscard]] std::uint32_t count() const override { return 2; }
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override {
        width = i == 0 ? 4 : 2;
        height = i == 0 ? 3 : 2;
    }
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        size(i, w, h);
        rgba.assign(std::size_t{w} * h * 4, 255);
        if (i == 0) {
            for (const std::uint32_t x : {1U, 2U}) {
                rgba[(std::size_t{1} * w + x) * 4 + 3] = 0;
            }
        }
        return true;
    }
};

}  // namespace

TEST_CASE("sprite hit mask: units are picked by their visible pixels") {
    const RingSource source;
    const render::SpriteSource* sources[] = {&source};
    auto atlas = render::SpriteAtlas::build(sources, 16);
    REQUIRE(atlas.ok());
    const auto mask = render::SpriteHitMask::build(*atlas, 0);
    CHECK(mask.visible(0, 0, 0));
    CHECK(mask.visible(0, 3, 1));
    CHECK(!mask.visible(0, 1, 1));  // the hole
    CHECK(!mask.visible(0, 2, 1));
    CHECK(!mask.visible(0, 4, 0));  // outside
    CHECK(!mask.visible(0, -1, 0));
    CHECK(mask.visible(1, 1, 1));
    // Sprites before `first` have no mask: their whole rectangle counts.
    const auto partial = render::SpriteHitMask::build(*atlas, 1);
    CHECK(partial.visible(0, 1, 1));
    CHECK(partial.visible(1, 0, 0));
    // A command: left / bottom anchor (10, 20), so the ring covers x 10..13, y 17..19.
    render::DrawCommand c;
    c.sprite = 0;
    c.x = 10;
    c.y = 20;
    const auto b = render::command_bounds(c, *atlas);
    REQUIRE(b.has_value());
    CHECK((*b == std::array<std::int32_t, 4>{10, 17, 4, 3}));
    CHECK(render::command_hit(c, *atlas, mask, 10, 17));
    CHECK(!render::command_hit(c, *atlas, mask, 11, 18));  // the hole
    CHECK(render::command_hit(c, *atlas, mask, 13, 19));
    CHECK(!render::command_hit(c, *atlas, mask, 13, 20));
    // Rows cut at the bottom (a figure in a field) do not count.
    c.cut_bottom = 1;
    CHECK(!render::command_hit(c, *atlas, mask, 10, 19));
    CHECK(render::command_hit(c, *atlas, mask, 10, 18));
    render::DrawCommand overlay;
    overlay.sprite = render::DrawCommand::kOverlayDiamond;
    CHECK(!render::command_bounds(overlay, *atlas));
}

TEST_CASE("render: a ship is drawn on its way between two ticks, a jump is not smoothed") {
    sim::World world(40, 40);
    sim::Ship ship;
    ship.pos_x = 10 * sim::kShipUnit;
    ship.pos_y = 10 * sim::kShipUnit;
    world.add_ship(ship);
    render::ShipTrail trail;
    trail.capture(world);
    sim::Ship& s = world.ships().back();
    s.pos_x += sim::kShipUnit;  // one tile on during the tick
    trail.set_fraction(0.25);
    double x = 0.0;
    double y = 0.0;
    trail.position(s, x, y);
    CHECK(x == 10.25);
    CHECK(y == 10.0);
    s.pos_x += 5 * sim::kShipUnit;  // far away at once: drawn where it is
    trail.position(s, x, y);
    CHECK(x == 16.0);
    // A ship the trail has not seen is drawn where it is.
    sim::Ship other;
    other.pos_x = 3 * sim::kShipUnit;
    other.pos_y = 4 * sim::kShipUnit;
    world.add_ship(other);
    trail.position(world.ships().back(), x, y);
    CHECK(x == 3.0);
    CHECK(y == 4.0);
}

TEST_CASE("render: the text cache gives up the least recently used texts, never those of this frame") {
    render::TextCache<int> cache(3);
    std::vector<int> evicted;
    const auto evict = [&evicted](int v) { evicted.push_back(v); };
    // A text by text, font and colour; the lookup copies nothing and needs no std::string.
    cache.insert("gold", 0, 4, 1, evict);
    cache.insert("gold", 1, 4, 2, evict);
    cache.insert("gold", 0, 3, 3, evict);
    const std::string_view gold = "gold and more";
    REQUIRE(cache.find(gold.substr(0, 4), 1, 4) != nullptr);
    CHECK(*cache.find(gold.substr(0, 4), 1, 4) == 2);
    CHECK(cache.find("tools", 0, 4) == nullptr);
    // All three were used in this frame: the cache grows instead of evicting.
    cache.insert("tools", 0, 4, 4, evict);
    CHECK(cache.size() == 4);
    CHECK(evicted.empty());
    // Next frame: (gold, 1, 4) and "tools" are used again; inserting evicts the least recently
    // used of the rest, oldest first, down to the capacity.
    cache.next_frame();
    CHECK(cache.find("gold", 1, 4) != nullptr);
    CHECK(cache.find("tools", 0, 4) != nullptr);
    cache.insert("wood", 0, 4, 5, evict);
    CHECK((evicted == std::vector<int>{1, 3}));
    CHECK(cache.size() == 3);
    CHECK(cache.find("gold", 0, 4) == nullptr);
    CHECK(cache.find("gold", 0, 3) == nullptr);
    // A frame later the least recently used of all goes first: (gold, 1, 4).
    cache.next_frame();
    CHECK(cache.find("wood", 0, 4) != nullptr);
    CHECK(cache.find("tools", 0, 4) != nullptr);
    cache.insert("fish", 0, 4, 6, evict);
    CHECK((evicted == std::vector<int>{1, 3, 2}));
    std::vector<int> left;
    cache.for_each([&left](int v) { left.push_back(v); });
    std::sort(left.begin(), left.end());
    CHECK((left == std::vector<int>{4, 5, 6}));
}
