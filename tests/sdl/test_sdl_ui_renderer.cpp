// SPDX-License-Identifier: GPL-3.0-or-later
// The SDL backend of the interface (engine/render/sdl/sdl_ui_renderer.cpp) on a software renderer,
// with a synthetic game folder: one sheet (TOOLS.BSH) and one font (ZEI20V.ZEI) of the large art.

#include <filesystem>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "../unit/test_support.hpp"
#include "sdl_support.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/core/file.hpp"
#include "o1602/render/sdl_ui_renderer.hpp"
#include "o1602/ui/font.hpp"

using namespace o1602;

namespace {

const assets::Rgba kBackground{0, 0, 200, 255};

/// A game folder with what GameDirectory::open looks for, block 0 of the large art (sprite 0:
/// 6 x 4 pixels, sprite 1: 9 x 2, both colour 60) and font slot 0 (96 glyphs of 4 x 6, index 1).
struct GameFolder {
    std::filesystem::path root = test::temp_path("o1602-sdl-ui");

    GameFolder() {
        std::filesystem::remove_all(root);
        for (const char* name : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            write(name, std::vector<std::uint8_t>{0});
        }
        write("TOOLGFX/TOOLS.BSH", test::sheet_bytes({{6, 4}, {9, 2}}, 60));
        write("TOOLGFX/ZEI20V.ZEI",
              test::sheet_bytes(std::vector<std::pair<std::uint32_t, std::uint32_t>>(96, {4, 6})));
    }
    ~GameFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    GameFolder(const GameFolder&) = delete;
    GameFolder& operator=(const GameFolder&) = delete;

    void write(const std::string& name, const std::vector<std::uint8_t>& bytes) const {
        std::filesystem::create_directories((root / name).parent_path());
        REQUIRE(core::write_file(root / name, bytes).ok());
    }
    [[nodiscard]] assets::GameDirectory game() const {
        auto game = assets::GameDirectory::open(root);
        REQUIRE(game.ok());
        return std::move(game).value();
    }
};

/// The pieces every test needs: folder, canvas and renderer with the large art.
struct Setup {
    GameFolder folder;
    assets::Palette palette = test::make_palette();
    test::Canvas canvas{64, 48};
    render::SdlUiRenderer ui{canvas.renderer(), folder.game(), palette, ui::ArtSize::Large};

    Setup() { canvas.clear(kBackground); }
};

ui::DrawItem item(ui::DrawItem::Kind kind, std::int32_t x, std::int32_t y) {
    ui::DrawItem d;
    d.kind = kind;
    d.x = x;
    d.y = y;
    return d;
}

/// Whether the rectangle [x0, x1) x [y0, y1) has the colour and its one pixel wide border not.
bool box(const test::Canvas& canvas, int x0, int y0, int x1, int y1, assets::Rgba colour) {
    for (int y = y0 - 1; y <= y1; ++y) {
        for (int x = x0 - 1; x <= x1; ++x) {
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if ((canvas.at(x, y) == colour) != inside) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("sdl ui renderer: loads the sheets and fonts that exist, and knows the sprites' sizes") {
    Setup s;
    CHECK(s.ui.art_size() == ui::ArtSize::Large);
    CHECK(s.ui.sprite_size(0, 0) == (std::pair<std::int32_t, std::int32_t>{6, 4}));
    CHECK(s.ui.sprite_size(0, 1) == (std::pair<std::int32_t, std::int32_t>{9, 2}));
    CHECK(!s.ui.sprite_size(0, 2).has_value());
    CHECK(!s.ui.sprite_size(1, 0).has_value());  // no START.BSH in the folder
    CHECK(!s.ui.sprite_size(7, 0).has_value());
    const auto fonts = s.ui.fonts();
    REQUIRE(fonts[0] != nullptr);
    CHECK(fonts[0]->height() == 6);
    CHECK(fonts[1] == nullptr);
    CHECK(fonts[2] == nullptr);
}

TEST_CASE("sdl ui renderer: a sprite is drawn at the region's offset, stretched from the art's scale") {
    Setup s;
    s.ui.draw({item(ui::DrawItem::Kind::Sprite, 3, 2)}, 10, 10, 1.0);
    s.canvas.read();
    CHECK(box(s.canvas, 13, 12, 19, 16, assets::Rgba{60, 0, 0, 255}));

    s.canvas.clear(kBackground);
    s.ui.draw({item(ui::DrawItem::Kind::Sprite, 3, 2)}, 10, 10, 2.0);
    s.canvas.read();
    CHECK(box(s.canvas, 13, 12, 25, 20, assets::Rgba{60, 0, 0, 255}));

    // Missing sprites and sheets draw nothing.
    s.canvas.clear(kBackground);
    auto missing = item(ui::DrawItem::Kind::Sprite, 0, 0);
    missing.gfx = 5;
    auto other_sheet = item(ui::DrawItem::Kind::Sprite, 0, 0);
    other_sheet.block = 1;
    s.ui.draw({missing, other_sheet}, 0, 0, 1.0);
    s.canvas.read();
    CHECK(s.canvas.at(2, 2) == kBackground);
}

TEST_CASE("sdl ui renderer: shapes of sprites in one colour, solid translucent or as a checkerboard") {
    Setup s;
    auto mask = item(ui::DrawItem::Kind::SpriteMask, 0, 0);
    mask.colour = 30;
    auto raster = item(ui::DrawItem::Kind::SpriteRaster, 20, 0);
    raster.colour = 30;
    s.ui.draw({mask, raster}, 0, 0, 1.0);
    s.canvas.read();
    // The mask has opacity 110 of 255.
    CHECK(test::near(s.canvas.at(0, 0), assets::Rgba{13, 0, 114, 255}));
    CHECK(test::near(s.canvas.at(5, 3), assets::Rgba{13, 0, 114, 255}));
    CHECK(s.canvas.at(6, 0) == kBackground);
    CHECK(s.canvas.at(20, 0) == (assets::Rgba{30, 0, 0, 255}));
    CHECK(s.canvas.at(21, 0) == kBackground);
    CHECK(s.canvas.at(21, 1) == (assets::Rgba{30, 0, 0, 255}));
}

TEST_CASE("sdl ui renderer: text in the font's colour, its cell's bottom at y, aligned at x") {
    Setup s;
    const assets::Rgba body = s.palette[ui::text_recolour(s.palette, 3)[1]];
    REQUIRE(body != kBackground);
    auto text = item(ui::DrawItem::Kind::Text, 10, 20);
    text.text = "AB";
    text.colour = 3;
    s.ui.draw({text}, 0, 0, 1.0);
    s.canvas.read();
    // Two glyphs of 4 x 6 pixels: (10, 14) to (18, 20).
    CHECK(box(s.canvas, 10, 14, 18, 20, body));

    s.canvas.clear(kBackground);
    text.align = ui::Align::Right;
    text.x = 40;
    s.ui.draw({text}, 0, 0, 1.0);
    s.canvas.read();
    // The texture is two pixels wider than the text (overhang): right aligned it ends at x.
    CHECK(box(s.canvas, 30, 14, 38, 20, body));

    s.canvas.clear(kBackground);
    text.align = ui::Align::Centre;
    s.ui.draw({text}, 0, 0, 1.0);
    s.canvas.read();
    CHECK(box(s.canvas, 35, 14, 43, 20, body));

    // A font slot without a font draws nothing.
    s.canvas.clear(kBackground);
    text.block = 1;
    s.ui.draw({text}, 0, 0, 1.0);
    s.canvas.read();
    CHECK(s.canvas.at(36, 17) == kBackground);
}

TEST_CASE("sdl ui renderer: filled rectangles, solid or half transparent, and the checkerboard") {
    Setup s;
    s.ui.fill(ui::Rect{2, 2, 10, 5}, 100);
    auto fill = item(ui::DrawItem::Kind::Fill, 0, 0);
    fill.w = 8;
    fill.h = 4;
    fill.colour = 100;
    fill.half = true;
    auto raster = item(ui::DrawItem::Kind::Raster, 0, 10);
    raster.w = 6;
    raster.h = 4;
    s.ui.draw({fill, raster}, 20, 20, 1.0);
    s.canvas.read();
    CHECK(box(s.canvas, 2, 2, 12, 7, assets::Rgba{100, 0, 0, 255}));
    CHECK(test::near(s.canvas.at(20, 20), assets::Rgba{50, 0, 100, 255}));
    CHECK(test::near(s.canvas.at(27, 23), assets::Rgba{50, 0, 100, 255}));
    CHECK(s.canvas.at(28, 23) == kBackground);
    CHECK(s.canvas.at(20, 30) == (assets::Rgba{0, 0, 0, 255}));
    CHECK(s.canvas.at(21, 30) == kBackground);
    CHECK(s.canvas.at(25, 33) == (assets::Rgba{0, 0, 0, 255}));
    CHECK(s.canvas.at(26, 30) == kBackground);
    CHECK(s.canvas.at(20, 34) == kBackground);
}

TEST_CASE("sdl ui renderer: widened art fills its rectangle and is clipped to it") {
    Setup s;
    // Sprite 1 is 9 x 2: made 20 wide it covers exactly the rectangle's width.
    s.ui.draw_widened(0, 1, ui::Rect{4, 4, 20, 10}, 1.0);
    s.canvas.read();
    CHECK(box(s.canvas, 4, 4, 24, 6, assets::Rgba{60, 0, 0, 255}));

    // Narrower than the art: cut off at the rectangle's right edge, and the clip is reset after.
    s.canvas.clear(kBackground);
    s.ui.draw_widened(0, 1, ui::Rect{4, 4, 5, 10}, 1.0);
    s.ui.fill(ui::Rect{40, 40, 2, 2}, 100);
    s.canvas.read();
    CHECK(box(s.canvas, 4, 4, 9, 6, assets::Rgba{60, 0, 0, 255}));
    CHECK(s.canvas.at(40, 40) == (assets::Rgba{100, 0, 0, 255}));
}

TEST_CASE("sdl ui renderer: an image is kept under its key and updated when asked") {
    Setup s;
    // 2 x 2: red, green / blue, white.
    const std::vector<std::uint8_t> first = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    s.ui.image(1, first, 2, 2, ui::Rect{10, 10, 4, 4}, false);
    s.canvas.read();
    CHECK(s.canvas.at(10, 10) == (assets::Rgba{255, 0, 0, 255}));
    CHECK(s.canvas.at(13, 10) == (assets::Rgba{0, 255, 0, 255}));
    CHECK(s.canvas.at(10, 13) == (assets::Rgba{0, 0, 255, 255}));
    CHECK(s.canvas.at(13, 13) == (assets::Rgba{255, 255, 255, 255}));
    CHECK(s.canvas.at(14, 13) == kBackground);

    std::vector<std::uint8_t> second(16, 0);  // all green
    for (std::size_t i = 0; i < second.size(); i += 4) {
        second[i + 1] = 255;
        second[i + 3] = 255;
    }
    s.ui.image(1, second, 2, 2, ui::Rect{10, 10, 4, 4}, false);  // kept as it was
    s.canvas.read();
    CHECK(s.canvas.at(10, 10) == (assets::Rgba{255, 0, 0, 255}));
    s.ui.image(1, second, 2, 2, ui::Rect{10, 10, 4, 4}, true);
    s.canvas.read();
    CHECK(s.canvas.at(10, 10) == (assets::Rgba{0, 255, 0, 255}));

    // Another size makes a new texture from the pixels given.
    const std::vector<std::uint8_t> blue = {0, 0, 255, 255};
    s.ui.image(1, blue, 1, 1, ui::Rect{10, 10, 4, 4}, false);
    s.canvas.read();
    CHECK(s.canvas.at(10, 10) == (assets::Rgba{0, 0, 255, 255}));
}

TEST_CASE("sdl ui renderer: after the textures are released everything is drawn again") {
    Setup s;
    auto text = item(ui::DrawItem::Kind::Text, 10, 40);
    text.text = "A";
    auto raster = item(ui::DrawItem::Kind::Raster, 30, 30);
    raster.w = 2;
    raster.h = 2;
    const std::vector<ui::DrawItem> items = {item(ui::DrawItem::Kind::Sprite, 0, 0), text, raster};
    s.ui.draw(items, 0, 0, 1.0);
    s.ui.next_frame();
    s.ui.release_textures();
    s.canvas.clear(kBackground);
    s.ui.draw(items, 0, 0, 1.0);
    s.canvas.read();
    CHECK(s.canvas.at(0, 0) == (assets::Rgba{60, 0, 0, 255}));
    CHECK(s.canvas.at(10, 39) != kBackground);
    CHECK(s.canvas.at(30, 30) == (assets::Rgba{0, 0, 0, 255}));
}
