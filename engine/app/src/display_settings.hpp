// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the game is shown (the window "Anzeige" of the settings): window or full screen, the
// window's size, the size of the interface, the free zoom and how it draws. Kept in settings.cfg
// as display.* lines. Pure logic: the viewer applies it to the window.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::app {

struct DisplaySettings {
    bool fullscreen = false;  ///< the whole screen at the desktop's resolution (no mode change)
    /// The window's size in window coordinates when it was last left; 0: the default (1280 x 800).
    std::int32_t window_width = 0;
    std::int32_t window_height = 0;
    /// The interface's size in percent of the desktop's scale (100: the original's pixels on a
    /// normal screen, twice that on a HiDPI one); 0: as large as fits (the original's behaviour).
    std::int32_t ui_scale = 0;
    /// Free zoom (any magnification, the wheel zooms finely); off: the original's three levels.
    bool free_zoom = true;
    /// A magnification between whole scales is drawn sharp and smooth (render::SdlMapCanvas);
    /// off: straight with uneven pixels (cheapest).
    bool smooth_zoom = true;
    bool vsync = true;  ///< frames wait for the screen
    /// The goods trend at the Kontor: under each good of its stock what comes in more than goes
    /// out per minute, green, or the other way round, red (docs/architecture/0012-goods-statistics.md).
    bool goods_trend = true;
    /// The status bar tells when two other players go to war or make peace (the original tells
    /// only the player concerned).
    bool rival_wars = true;
    /// The original's graphics instead of Open1602's: shadows and chimney smoke dithered (every
    /// second pixel) instead of blended (render::Dither), and no dotted border around other players'
    /// land while building. One switch for everything where the two differ in look, not in what is
    /// shown.
    bool classic_graphics = false;

    friend bool operator==(const DisplaySettings&, const DisplaySettings&) = default;
};

/// The interface sizes offered, in percent (0: automatic).
inline constexpr std::array<std::int32_t, 9> kUiScales = {0, 75, 100, 125, 150, 175, 200, 250, 300};

struct WindowSize {
    std::int32_t width = 0;
    std::int32_t height = 0;
    friend bool operator==(const WindowSize&, const WindowSize&) = default;
};
/// The window sizes offered (any other comes from dragging the window's edge).
inline constexpr std::array<WindowSize, 10> kWindowSizes = {{{1024, 768},
                                                             {1280, 720},
                                                             {1280, 800},
                                                             {1366, 768},
                                                             {1600, 900},
                                                             {1680, 1050},
                                                             {1920, 1080},
                                                             {1920, 1200},
                                                             {2560, 1440},
                                                             {3840, 2160}}};
/// The offered sizes that fit into the desktop's usable area (window coordinates); at least the
/// smallest. Unknown area (0): all.
[[nodiscard]] std::vector<WindowSize> window_sizes_for(std::int32_t usable_width, std::int32_t usable_height);
/// The size after `current` in `sizes` (the first that is larger in area; after the last the first).
[[nodiscard]] WindowSize next_window_size(const std::vector<WindowSize>& sizes, WindowSize current);
/// The size before `current` in `sizes` (the last that is smaller in area; before the first the last).
[[nodiscard]] WindowSize previous_window_size(const std::vector<WindowSize>& sizes, WindowSize current);

/// The interface's scale (screen pixels per pixel of the 1024 x 768 design) in a window of
/// `width` x `height` physical pixels: automatic, the largest that fits; else the setting times the
/// desktop's scale, never larger than fits.
[[nodiscard]] double ui_scale_for(std::int32_t ui_scale, std::int32_t width, std::int32_t height,
                                  float display_scale) noexcept;
/// The setting after `current` in kUiScales (after the last the first).
[[nodiscard]] std::int32_t next_ui_scale(std::int32_t current) noexcept;
/// The setting before `current` in kUiScales (before the first the last).
[[nodiscard]] std::int32_t previous_ui_scale(std::int32_t current) noexcept;

/// A "display.*" line of settings.cfg (`key` without the prefix); false if the key is unknown.
bool read_display_line(DisplaySettings& s, std::string_view key, std::string_view value);
/// The display.* lines of settings.cfg.
[[nodiscard]] std::string write_display(const DisplaySettings& s);

}  // namespace o1602::app
