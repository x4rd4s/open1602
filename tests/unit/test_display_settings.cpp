// SPDX-License-Identifier: GPL-3.0-or-later
// How the game is shown (engine/app/src/display_settings.hpp, display_menu.hpp): the display.*
// lines of settings.cfg, the window sizes and interface sizes offered, the window "Anzeige".

#include <string>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "display_menu.hpp"
#include "display_settings.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;

TEST_CASE("display settings: written to settings.cfg and read back") {
    app::AppSettings s;
    s.display.fullscreen = true;
    s.display.window_width = 1600;
    s.display.window_height = 900;
    s.display.ui_scale = 150;
    s.display.free_zoom = false;
    s.display.smooth_zoom = false;
    s.display.vsync = false;
    s.display.goods_trend = false;
    s.display.rival_wars = false;
    s.display.classic_graphics = true;
    const std::string text = app::write_settings(s);
    CHECK(text.find("display.window = 1600x900\n") != std::string::npos);
    CHECK(text.find("display.graphics = classic\n") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.display == s.display);
    // The defaults: a window, the interface as large as fits, the free zoom drawn smooth.
    const app::AppSettings none = app::parse_settings("", app::AppSettings{});
    CHECK(!none.display.fullscreen);
    CHECK(none.display.window_width == 0);
    CHECK(none.display.ui_scale == 0);
    CHECK(none.display.free_zoom);
    CHECK(none.display.smooth_zoom);
    CHECK(none.display.vsync);
    CHECK(none.display.goods_trend);
    CHECK(none.display.rival_wars);
    CHECK(!none.display.classic_graphics);  // Open1602's look
}

TEST_CASE("display settings: damaged lines keep what was there") {
    app::DisplaySettings d;
    CHECK(!app::read_display_line(d, "window", "big"));
    CHECK(!app::read_display_line(d, "window", "1600x"));
    CHECK(!app::read_display_line(d, "fullscreen", "maybe"));
    CHECK(!app::read_display_line(d, "unknown", "on"));
    CHECK(!app::read_display_line(d, "graphics", "on"));
    CHECK(d == app::DisplaySettings{});
    CHECK(app::read_display_line(d, "window", "100x50"));  // kept within limits
    CHECK(d.window_width == 320);
    CHECK(d.window_height == 240);
    CHECK(app::read_display_line(d, "ui_scale", "30"));  // below half: automatic
    CHECK(d.ui_scale == 0);
    CHECK(app::read_display_line(d, "ui_scale", "900"));
    CHECK(d.ui_scale == 400);
}

TEST_CASE("display settings: the window sizes that fit the screen") {
    const auto all = app::window_sizes_for(0, 0);
    CHECK(all.size() == app::kWindowSizes.size());
    const auto hd = app::window_sizes_for(1920, 1040);  // a task bar takes 40 rows
    CHECK(!hd.empty());
    for (const app::WindowSize& s : hd) {
        CHECK(s.width <= 1920);
        CHECK(s.height <= 1040);
    }
    CHECK(app::window_sizes_for(800, 600).size() == 1);  // the smallest always
    // The next size: the following one in the list, a dragged size the next larger.
    CHECK(app::next_window_size(all, {1280, 720}) == (app::WindowSize{1280, 800}));
    CHECK(app::next_window_size(all, {1300, 900}) == (app::WindowSize{1600, 900}));
    CHECK(app::next_window_size(all, app::kWindowSizes.back()) == app::kWindowSizes.front());
    // The previous size: the one before in the list, a dragged size the next smaller.
    CHECK(app::previous_window_size(all, {1280, 800}) == (app::WindowSize{1280, 720}));
    CHECK(app::previous_window_size(all, {1300, 900}) == (app::WindowSize{1366, 768}));
    CHECK(app::previous_window_size(all, app::kWindowSizes.front()) == app::kWindowSizes.back());
    CHECK(app::previous_window_size(all, {200, 100}) == app::kWindowSizes.back());
    for (const app::WindowSize& s : all) {
        CHECK(app::previous_window_size(all, app::next_window_size(all, s)) == s);
    }
}

TEST_CASE("display settings: the interface's scale") {
    // Automatic: as large as the 1024 x 768 design fits.
    CHECK(app::ui_scale_for(0, 1024, 768, 1.0F) == 1.0);
    CHECK(app::ui_scale_for(0, 2560, 1440, 1.0F) == 1440.0 / 768.0);
    // A size: in percent of the desktop's scale, never larger than fits.
    CHECK(app::ui_scale_for(100, 2560, 1440, 1.0F) == 1.0);
    CHECK(app::ui_scale_for(100, 3840, 2160, 2.0F) == 2.0);
    CHECK(app::ui_scale_for(150, 1280, 800, 1.0F) == 800.0 / 768.0);
    CHECK(app::ui_scale_for(75, 1920, 1080, 1.0F) == 0.75);
    CHECK(app::next_ui_scale(0) == 75);
    CHECK(app::next_ui_scale(300) == 0);
    CHECK(app::next_ui_scale(33) == 0);
    CHECK(app::previous_ui_scale(75) == 0);
    CHECK(app::previous_ui_scale(0) == 300);
    CHECK(app::previous_ui_scale(33) == 300);
    for (const std::int32_t s : app::kUiScales) {
        CHECK(app::previous_ui_scale(app::next_ui_scale(s)) == s);
    }
}

TEST_CASE("display menu: each row chooses its next value") {
    ui::set_language(ui::kFallbackLanguage);
    app::DisplayModel model;
    model.window = {1280, 800};
    model.sizes = app::window_sizes_for(0, 0);
    using Item = app::DisplayMenu::Item;
    CHECK(app::DisplayMenu::value_text(model, Item::WindowSize) == "1280 \xd7 800");  // Windows-1252
    CHECK(app::DisplayMenu::choose_next(model, Item::WindowSize));
    CHECK(model.window == (app::WindowSize{1366, 768}));
    CHECK(model.settings.window_width == 1366);
    CHECK(app::DisplayMenu::choose_next(model, Item::Mode));
    CHECK(model.settings.fullscreen);
    CHECK(!app::DisplayMenu::choose_next(model, Item::WindowSize));  // full screen: the screen's size
    CHECK(app::DisplayMenu::value_text(model, Item::UiScale) == "as large as fits");
    CHECK(app::DisplayMenu::choose_next(model, Item::UiScale));
    CHECK(app::DisplayMenu::value_text(model, Item::UiScale) == "75 %");
    CHECK(app::DisplayMenu::choose_next(model, Item::Zoom));
    CHECK(!model.settings.free_zoom);
    CHECK(!app::DisplayMenu::choose_next(model, Item::ZoomDrawing));  // the original's levels: whole scales
    CHECK(app::DisplayMenu::choose_next(model, Item::Vsync));
    CHECK(!model.settings.vsync);
    CHECK(app::DisplayMenu::value_text(model, Item::GoodsTrend) == "on");
    CHECK(app::DisplayMenu::choose_next(model, Item::GoodsTrend));
    CHECK(!model.settings.goods_trend);
    CHECK(app::DisplayMenu::choose_next(model, Item::RivalWars));
    CHECK(!model.settings.rival_wars);
    CHECK(app::DisplayMenu::value_text(model, Item::RivalWars) == "off");
    CHECK(app::DisplayMenu::value_text(model, Item::Graphics) == "modern");
    CHECK(app::DisplayMenu::choose_next(model, Item::Graphics));
    CHECK(model.settings.classic_graphics);
    CHECK(app::DisplayMenu::value_text(model, Item::Graphics) == "as in the original");
}

TEST_CASE("display menu: a click on a row changes it, the button closes the window") {
    ui::set_language(ui::kFallbackLanguage);
    using Item = app::DisplayMenu::Item;
    app::DisplayMenu menu;
    menu.set_scale(1.0);
    app::DisplayModel model;
    model.window = {1280, 800};
    model.sizes = app::window_sizes_for(0, 0);
    (void)menu.draw(model, {});
    // The first row's button (the mode) on the right of the window.
    const std::int32_t x = app::DisplayMenu::kWidth - 60;
    const std::int32_t y = 64 + 15;
    CHECK(menu.mouse(x, y, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(x, y, false, model) == app::DisplayMenu::Result::Changed);
    CHECK(model.settings.fullscreen);
    CHECK(app::DisplayMenu::choose_next(model, Item::Mode));  // back to the window
    // A right click on the window's size takes the previous one.
    const std::int32_t size_y = 64 + 40 + 15;
    CHECK(menu.mouse(x, size_y, true, model, ui::MouseButton::Right) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(x, size_y, false, model, ui::MouseButton::Right) == app::DisplayMenu::Result::Changed);
    CHECK(model.window == (app::WindowSize{1280, 720}));
    CHECK(model.settings.window_width == 1280);
    CHECK(model.settings.window_height == 720);
    CHECK(app::DisplayMenu::choose_previous(model, Item::UiScale));
    CHECK(model.settings.ui_scale == 300);
    const std::int32_t cx = app::DisplayMenu::kWidth / 2;
    const std::int32_t cy = app::DisplayMenu::kHeight - 34;
    CHECK(menu.mouse(cx, cy, true, model) == app::DisplayMenu::Result::None);
    CHECK(menu.mouse(cx, cy, false, model) == app::DisplayMenu::Result::Close);
}
