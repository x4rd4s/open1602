// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gadget layouts at work (docs/re/ui.md): where the regions of the screen are, what a layout
// draws in which state, and how it answers the mouse (press, release, toggles, radio groups,
// select-on-drag, sliders, menu buttons) as the original does (0x00406660, 0x00407A70). Pure logic:
// a panel makes a list of things to draw, the frontend draws them.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/gad.hpp"
#include "o1602/ui/font.hpp"

namespace o1602::ui {

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    [[nodiscard]] bool contains(std::int32_t px, std::int32_t py) const noexcept {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// Design size of the original's screens: every layout is in units of a 1024 x 768 screen.
inline constexpr std::int32_t kDesignWidth = 1024;
inline constexpr std::int32_t kDesignHeight = 768;

/// The regions layouts belong to (0x00440EF0): the panel on the right, the status bar below the
/// game view, the tutorial window above it, and the whole screen (menus).
struct ScreenRegions {
    double scale = 1.0;  ///< screen width / 1024
    Rect side_panel;
    Rect status_bar;
    Rect tutor;
    Rect full;
    Rect game_view;  ///< what is left for the map
};
[[nodiscard]] ScreenRegions screen_regions(std::int32_t width, std::int32_t height) noexcept;

/// The regions at a chosen scale (the original: width / 1024; Open1602 keeps the whole panel on
/// screen with min(width / 1024, height / 768)).
[[nodiscard]] ScreenRegions screen_regions(std::int32_t width, std::int32_t height, double scale) noexcept;

/// Design units to pixels: trunc((v + 0.5) * scale), as the original rounds every element.
[[nodiscard]] std::int32_t to_pixels(std::int32_t v, double scale) noexcept;

/// Something a panel draws, in pixels relative to its region.
struct DrawItem {
    enum class Kind : std::uint8_t {
        Sprite,        ///< sprite `gfx` of sheet `block` with its top left at (x, y)
        SpriteMask,    ///< the sprite's shape in palette colour `colour`
        SpriteRaster,  ///< the shape as a checkerboard of `colour`
        Text,          ///< `text` in font `block`, text colour `colour`, aligned at x, cell bottom at y
        Fill,          ///< rectangle (x, y, w, h) in palette colour `colour`
        Raster,        ///< checkerboard over (x, y, w, h) (disabled gadgets), black
    };
    Kind kind = Kind::Sprite;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    std::int32_t block = 0;
    std::int32_t gfx = 0;
    std::int32_t colour = 0;
    Align align = Align::Left;
    bool half = false;  ///< drawn as a 50 % checkerboard (Alphaflg)
    std::string text;
};

/// What the mouse did to a panel.
struct PanelEvent {
    enum class Kind : std::uint8_t {
        Pressed,  ///< a menu gadget was pressed (Menuflg: the original's callback)
        Clicked,  ///< released on the gadget it was pressed on
        Slid,     ///< a slider moved (every frame while held)
    };
    Kind kind = Kind::Clicked;
    std::int32_t id = 0;
};

class Panel {
public:
    Panel(assets::GadgetLayout layout, double scale);

    [[nodiscard]] const assets::GadgetLayout& layout() const noexcept { return m_layout; }
    [[nodiscard]] double scale() const noexcept { return m_scale; }
    void set_scale(double scale) noexcept { m_scale = scale; }

    // State the game sets (0x00406EC0..0x004079B0); unknown Ids are ignored.
    void set_text(std::int32_t id, std::string text);
    void set_selected(std::int32_t id, bool on);
    [[nodiscard]] bool selected(std::int32_t id) const;
    void set_hidden(std::int32_t id, bool on);
    /// Not clickable (Gad_SetNoSel 0x004072D0).
    void set_no_select(std::int32_t id, bool on);
    /// Hidden and not clickable, or both undone: what the original's windows do with gadgets that
    /// do not apply (hidden ones would still answer the mouse).
    void set_visible(std::int32_t id, bool on);
    void set_disabled(std::int32_t id, bool on);
    /// Slider value scaled to `range` (0x004074F0: (value * range + size / 2) / size) and back.
    [[nodiscard]] std::int32_t slider(std::int32_t id, std::size_t axis, std::int32_t range) const;
    void set_slider(std::int32_t id, std::size_t axis, std::int32_t value, std::int32_t range);
    /// First line of a flowing text shown (TEXTFL scrolling).
    void set_scroll(std::int32_t id, std::int32_t line);
    /// Last first line that still fills the flowing text's box (0 if the text fits); callers clamp
    /// their scroll to it so "down" past the end does not blank the window (UI-12).
    [[nodiscard]] std::int32_t max_scroll(std::int32_t id, const std::array<const Font*, 3>& fonts) const;
    /// Another sprite and pressed step for a gadget (0x004077C0, 0x00407840): message icons.
    void set_graphic(std::int32_t id, std::int32_t gfx, std::int32_t press_offset);
    /// Another draw offset (Posoffs) for a gadget: art centred in its box, groups moved together.
    void set_offset(std::int32_t id, std::int32_t x, std::int32_t y);
    /// Moves a gadget, click area and picture (Open1602 rearranges a few windows, e.g. U-013).
    void set_position(std::int32_t id, std::int32_t x, std::int32_t y);
    /// Another size for a gadget (a flowing text in a window that grew with the screen).
    void set_size(std::int32_t id, std::int32_t w, std::int32_t h);
    /// Top left of a gadget's click area, nothing if the layout has no such gadget.
    [[nodiscard]] std::optional<std::array<std::int32_t, 2>> position(std::int32_t id) const;

    /// The mouse at (x, y) in pixels relative to the region, button down or up.
    std::vector<PanelEvent> mouse(std::int32_t x, std::int32_t y, bool down);
    /// Gadget Id under the mouse at the last call (for the status line's help text).
    [[nodiscard]] std::optional<std::int32_t> hovered() const;
    /// Gadget Id held down by the mouse now (a slider being dragged), nothing if none.
    [[nodiscard]] std::optional<std::int32_t> held() const;

    /// Everything to draw, in paint order. `fonts` are the three font slots (for flowing text).
    [[nodiscard]] std::vector<DrawItem> draw(const std::array<const Font*, 3>& fonts) const;

private:
    struct State {
        bool selected = false;
        bool hidden = false;
        bool disabled = false;
        bool no_select = false;
        std::array<std::int32_t, 2> slider{};
        std::int32_t scroll = 0;
        std::string text;
        std::optional<std::pair<std::int32_t, std::int32_t>> graphic;  ///< gfx, press offset
    };
    /// A flowing text's wrapped lines and how many fit its box; nothing if it draws no text.
    [[nodiscard]] std::optional<std::pair<std::vector<std::string>, std::int32_t>> flow_lines(
        std::size_t index, const std::array<const Font*, 3>& fonts) const;
    [[nodiscard]] std::optional<std::size_t> hit(std::int32_t x, std::int32_t y) const;
    void draw_gadget(std::vector<DrawItem>& out, std::size_t index, std::uint8_t state,
                     const std::array<const Font*, 3>& fonts) const;
    void slide(std::size_t index, std::int32_t gx, std::int32_t gy);
    [[nodiscard]] std::uint8_t state_of(std::size_t index) const;

    assets::GadgetLayout m_layout;
    std::vector<State> m_state;
    double m_scale;
    std::optional<std::size_t> m_pressed;
    std::optional<std::size_t> m_hovered;
    bool m_down = false;
};

}  // namespace o1602::ui
