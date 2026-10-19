// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows of Open1602's own that have no layout in the original's files (the multiplayer menu):
// framed panels, labels, buttons, text fields, check boxes and list rows in the original's fonts
// and the colours of Open1602's other windows (gold frame, dark ground). The owner lays the form
// out anew whenever its state changes (design units of the 1024 x 768 screen, like the layouts);
// the form draws it and answers the mouse. Pure logic, like ui::Panel.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "o1602/ui/font.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::ui {

/// Palette colours and text colours of a form.
struct FormStyle {
    std::uint8_t frame = 215;    ///< borders: gold (202, 172, 52) in STADTFLD.COL
    std::uint8_t ground = 28;    ///< panels: dark brown (35, 23, 12)
    std::uint8_t field = 0;      ///< text fields and lists
    std::uint8_t hover = 28;     ///< a button or row under the mouse
    std::uint8_t selected = 28;  ///< a chosen row or button
    std::int32_t font = 0;       ///< font slot of all text
    std::int32_t text = 4;       ///< text colours (ui::text_colour): normal, headings, hints, warnings
    std::int32_t heading = 3;
    std::int32_t dim = 1;
    std::int32_t warning = 9;
};

/// The text cut to `width` design units: its end replaced by "..." (or, with `keep_end`, for a
/// field being typed in, its start left out, so that the caret stays in sight). Without a font
/// every byte is 9 units wide.
[[nodiscard]] std::string fit_text(const Font* font, std::string text, double width, bool keep_end);

class Form {
public:
    using Id = std::int32_t;
    enum class Tone : std::uint8_t { Normal, Heading, Dim, Warning };

    explicit Form(FormStyle style = {}) : m_style(style) {}

    void set_style(const FormStyle& style) { m_style = style; }
    void set_scale(double scale) noexcept { m_scale = scale; }
    [[nodiscard]] double scale() const noexcept { return m_scale; }

    /// Takes all widgets away for a new layout; the pressed and the focused widget stay by Id.
    void clear() { m_widgets.clear(); }

    /// A framed background.
    void panel(Rect r);
    /// A plain rectangle of a palette colour (a banner's colour).
    void swatch(Rect r, std::uint8_t colour);
    /// Text in the box, cut to its width; vertically centred.
    void label(Rect r, std::string text, Align align = Align::Left, Tone tone = Tone::Normal);
    /// Text wrapped into the lines of the box (`line` design units apart); what does not fit is
    /// left out.
    void paragraph(Rect r, std::string text, Tone tone = Tone::Normal, std::int32_t line = 22);
    void button(Id id, Rect r, std::string text, bool enabled = true, bool selected = false);
    /// A text field: the owner keeps the text; the focused one shows the caret. `secret` shows
    /// stars.
    void field(Id id, Rect r, const std::string& text, bool secret = false);
    void check(Id id, Rect r, std::string text, bool on, bool enabled = true);
    /// A row of a list: clickable, highlighted when `selected`; labels go on top of it.
    void row(Id id, Rect r, bool selected);

    /// The field that takes the keys, if any.
    [[nodiscard]] std::optional<Id> focus() const noexcept { return m_focus; }
    void set_focus(std::optional<Id> id) noexcept { m_focus = id; }

    /// The mouse at (x, y) in pixels of the form's region: the widget clicked (pressed and
    /// released on it), if any.
    std::optional<Id> mouse(std::int32_t x, std::int32_t y, bool down);
    /// The widget under the mouse at the last call.
    [[nodiscard]] std::optional<Id> hovered() const noexcept { return m_hovered; }

    /// Everything to draw, in pixels of the region. `fonts` cut texts to their boxes.
    [[nodiscard]] std::vector<DrawItem> draw(const std::array<const Font*, 3>& fonts) const;

    /// The widgets' boxes and texts as laid out, for tests and the owner.
    struct Widget {
        enum class Kind : std::uint8_t { Panel, Swatch, Label, Paragraph, Button, Field, Check, Row };
        Kind kind = Kind::Label;
        Id id = 0;
        Rect rect;
        std::string text;
        Align align = Align::Left;
        Tone tone = Tone::Normal;
        std::uint8_t colour = 0;
        std::int32_t line = 0;  ///< paragraph: line height
        bool enabled = true;
        bool on = false;  ///< selected, checked, secret
    };
    [[nodiscard]] const std::vector<Widget>& widgets() const noexcept { return m_widgets; }

private:
    [[nodiscard]] std::optional<Id> hit(std::int32_t x, std::int32_t y) const;
    [[nodiscard]] std::int32_t colour_of(Tone tone) const noexcept;

    FormStyle m_style;
    double m_scale = 1.0;
    std::vector<Widget> m_widgets;
    std::optional<Id> m_pressed;
    std::optional<Id> m_hovered;
    std::optional<Id> m_focus;
};

}  // namespace o1602::ui
