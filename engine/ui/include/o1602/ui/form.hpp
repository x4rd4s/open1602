// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows of Open1602's own that have no layout in the original's files (the multiplayer menu):
// framed panels, labels, buttons, text fields, check boxes, sliders and list rows in the original's fonts
// and the colours of Open1602's other windows (gold frame, dark ground). The owner lays the form
// out anew whenever its state changes (design units of the 1024 x 768 screen, like the layouts);
// the form draws it and answers the mouse. Pure logic, like ui::Panel.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    /// Grounds of buttons, fields and rows at half strength over what lies below (a parchment), and
    /// buttons that cannot be pressed faded instead of rastered.
    bool translucent = false;
};

/// Text in a form's box starts this far (design units) from its left or right edge.
inline constexpr std::int32_t kTextPadding = 8;

/// The text cut to `width` design units: its end replaced by "..." (or, with `keep_end`, for a
/// field being typed in, its start left out, so that the caret stays in sight). Without a font
/// every byte is 9 units wide.
[[nodiscard]] std::string fit_text(const Font* font, std::string text, double width, bool keep_end);
/// The text broken into lines of at most `width` design units, at its spaces; a word wider than a
/// line is cut where the line is full. Unlike wrap_text() the text stays as it is (nothing is read
/// as a hyphen or a line break), as it is meant for what players write. Without a font every byte is
/// 9 units wide. Empty text is one empty line.
[[nodiscard]] std::vector<std::string> wrap_lines(const Font* font, std::string_view text, double width);
/// The width of the text in design units. Without a font every byte is 9 units wide.
[[nodiscard]] double text_width(const Font* font, std::string_view text);
/// The lines that Form::paragraph() shows of the text in a box `width` design units wide (all of
/// them, as if the box were high enough). Without a font the text is split at its line breaks only.
[[nodiscard]] std::vector<std::string> paragraph_lines(const Font* font, std::string_view text, std::int32_t width);
/// Where the bottom of the cell of text goes, design units, so that its ink sits in the middle of
/// the line from `y`, `height` high (the digits' ink, Font::ink_drop()).
[[nodiscard]] std::int32_t text_bottom(const Font* font, std::int32_t y, std::int32_t height);

/// The keys of a field being typed in that are no text (Hud::type_key()).
inline constexpr int kKeySelectAll = 1;  ///< Ctrl+A
inline constexpr int kKeyBackspace = 8;
inline constexpr int kKeyTab = 9;
inline constexpr int kKeyLineBreak = 10;  ///< Shift+Enter
inline constexpr int kKeyEnter = 13;
inline constexpr int kKeyEscape = 27;
inline constexpr int kKeyLeft = 0x101;
inline constexpr int kKeyRight = 0x102;
inline constexpr int kKeyHome = 0x103;
inline constexpr int kKeyEnd = 0x104;
inline constexpr int kKeyDelete = 0x105;
/// Added to kKeyLeft, kKeyRight, kKeyHome or kKeyEnd: Shift is held, the caret selects on its way.
inline constexpr int kKeyShift = 0x1000;

/// The mouse button of a press or release.
enum class MouseButton : std::uint8_t { Left, Right };

class Form {
public:
    using Id = std::int32_t;
    enum class Tone : std::uint8_t { Normal, Heading, Dim, Warning };

    explicit Form(FormStyle style = {}) : m_style(style) {}

    void set_style(const FormStyle& style) { m_style = style; }
    void set_scale(double scale) noexcept { m_scale = scale; }
    [[nodiscard]] double scale() const noexcept { return m_scale; }
    /// The part of the screen the form may draw on, in design units of its own coordinates
    /// (1024 x 768 from 0, 0 unless set): tooltips stay inside it. A form laid out from a column
    /// that starts further right has the screen's left edge at a negative x.
    void set_bounds(Rect bounds) noexcept { m_bounds = bounds; }
    [[nodiscard]] const Rect& bounds() const noexcept { return m_bounds; }
    /// The font of all text out of `fonts` (FormStyle::font), if there is one.
    [[nodiscard]] const Font* font(const std::array<const Font*, kFontSlots>& fonts) const noexcept;

    /// Takes all widgets away for a new layout; the pressed and the focused widget stay by Id.
    void clear() { m_widgets.clear(); }
    /// Forgets the button held: its release clicks nothing (the widgets changed beneath it).
    void cancel_press() noexcept { m_pressed.reset(); }

    /// A framed background.
    void panel(Rect r);
    /// A plain rectangle of a palette colour or a colour of its own (ui::rgb_ink): a banner's colour,
    /// which the palette does not have for every banner (no pink, no violet).
    void swatch(Rect r, std::int32_t colour);
    /// Text in the box, cut to its width; vertically centred.
    void label(Rect r, std::string text, Align align = Align::Left, Tone tone = Tone::Normal);
    /// A line of a chat, left aligned and cut to the box as one text: the speaker's name in palette
    /// colour `ink` (their banner's colour), what follows it (": Hallo") in the tone.
    void speech(Rect r, std::string speaker, std::int32_t ink, std::string text, Tone tone = Tone::Normal);
    /// A label on the left and its value on the right of one box, as a list of settings shown
    /// only: the value gets the room it needs, the label what is left.
    void entry(Rect r, std::string label, std::string value, Tone label_tone = Tone::Normal,
               Tone value_tone = Tone::Normal);
    /// Text wrapped into the lines of the box (`line` design units apart); what does not fit is
    /// left out. With a `fallback`, that is shown instead where `text` does not fit whole.
    void paragraph(Rect r, std::string text, Tone tone = Tone::Normal, std::int32_t line = 22,
                   std::string fallback = {});
    void button(Id id, Rect r, std::string text, bool enabled = true, bool selected = false);
    /// A button that goes through values: the left button chooses the next, the right one the
    /// previous (clicked_with() tells which). Other widgets answer the left button only.
    void cycle_button(Id id, Rect r, std::string text, bool enabled = true);
    /// A text field: the owner keeps the text; the focused one shows the caret. `secret` shows
    /// stars.
    void field(Id id, Rect r, const std::string& text, bool secret = false);
    void check(Id id, Rect r, std::string text, bool on, bool enabled = true);
    /// A row of a list: clickable, highlighted when `selected`; labels go on top of it.
    void row(Id id, Rect r, bool selected);
    /// A slider with its knob at `position` (0: left end, 1: right end). Pressed on it, the knob
    /// follows the mouse while the button is held; letting go, wherever, clicks it, and
    /// slid_to() tells where the knob was let go.
    void slider(Id id, Rect r, double position, bool enabled = true);
    /// A bar that fills from the left as something goes on: `done` of it (0..1), in the frame's
    /// gold inside a field's ground.
    void progress(Rect r, double done);
    /// A box that explains what lies under the mouse (a setting's tooltip): framed like a panel,
    /// opaque, the text wrapped to at most kTooltipWidth, below `anchor` (the thing it explains)
    /// or above it where the screen ends, always within the 1024 x 768 design. Laid out last, it
    /// lies over everything; it takes no clicks.
    void tooltip(Rect anchor, std::string text);
    static constexpr std::int32_t kTooltipWidth = 420;

    /// The field that takes the keys, if any.
    [[nodiscard]] std::optional<Id> focus() const noexcept { return m_focus; }
    /// Another field (or none) takes the keys: its caret is at the end of its text, nothing selected.
    void set_focus(std::optional<Id> id) noexcept {
        if (id != m_focus) {
            m_caret = std::string::npos;
            m_anchor.reset();
        }
        m_focus = id;
    }
    /// Ctrl+A: the whole text of the field with the focus is selected (shown on the selected
    /// colour); what is typed next replaces it, Backspace removes it.
    void select_all() noexcept {
        if (m_focus) {
            m_anchor = 0;
            m_caret = std::string::npos;
        }
    }
    [[nodiscard]] bool all_selected() const noexcept {
        return m_focus.has_value() && m_anchor == 0 && m_caret == std::string::npos;
    }
    /// Whether the whole text was selected; the selection is gone afterwards (any key ends it).
    bool take_all_selected() noexcept {
        const bool all = all_selected();
        m_anchor.reset();
        return all;
    }

    /// Where the caret stands in `text`, the focused field's text (its end, unless moved).
    [[nodiscard]] std::size_t caret(std::string_view text) const noexcept { return std::min(m_caret, text.size()); }
    /// The part of `text`, the focused field's text, that is selected: [first, second), empty
    /// (both at the caret) if nothing is.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection(std::string_view text) const noexcept;
    /// Text typed into `text`, the focused field's: it takes the selection's place, or goes in at the
    /// caret, as far as `most` bytes in all allow; the caret follows it. The owner leaves out what
    /// the field does not take before. Nothing typed leaves the selection as it is.
    void insert(std::string& text, std::string_view typed, std::size_t most);
    /// A key for `text`, the focused field's: Ctrl+A (kKeySelectAll) selects it all, Backspace and
    /// Delete remove the selection or the byte before or after the caret, Left, Right, Home and End
    /// move the caret, with kKeyShift selecting on the way. False for any other key (and without
    /// a focus), which the owner handles.
    bool edit(std::string& text, int key);
    /// The selected text of the focused field as last laid out (Ctrl+C); empty for a secret one.
    [[nodiscard]] std::string selected_text() const;

    /// The mouse at (x, y) in pixels of the form's region: the widget clicked (pressed and
    /// released on it with the same button), if any. The right button clicks cycle buttons only;
    /// a button pressed while another is held is not seen.
    std::optional<Id> mouse(std::int32_t x, std::int32_t y, bool down, MouseButton button = MouseButton::Left);
    /// Where the knob of slider `id` is dragged now, if it is held (to show its value meanwhile).
    [[nodiscard]] std::optional<double> sliding(Id id) const noexcept {
        return m_pressed == id && m_pressed_with == MouseButton::Left ? std::optional<double>(m_slide) : std::nullopt;
    }
    /// Where the slider of the last click mouse() returned was let go (0..1).
    [[nodiscard]] double slid_to() const noexcept { return m_slide; }
    /// The button of the last click mouse() returned.
    [[nodiscard]] MouseButton clicked_with() const noexcept { return m_clicked_with; }
    /// The widget under the mouse at the last call.
    [[nodiscard]] std::optional<Id> hovered() const noexcept { return m_hovered; }
    /// Whether the last call let go of a press (on a widget or beside them), and not only moved
    /// the mouse: a click beside the fields ends typing, the mouse merely leaving one does not.
    [[nodiscard]] bool released() const noexcept { return m_released; }

    /// Everything to draw, in pixels of the region. `fonts` cut texts to their boxes.
    [[nodiscard]] std::vector<DrawItem> draw(const std::array<const Font*, kFontSlots>& fonts) const;

    /// The widgets' boxes and texts as laid out, for tests and the owner.
    struct Widget {
        enum class Kind : std::uint8_t {
            Panel,
            Swatch,
            Label,
            Speech,
            Entry,
            Paragraph,
            Button,
            Field,
            Check,
            Row,
            Slider,
            Progress,
            Tooltip
        };
        Kind kind = Kind::Label;
        Id id = 0;
        Rect rect;
        std::string text;
        std::string value;  ///< entry: the text on the right; speech: what follows the name; paragraph: the fallback
        Align align = Align::Left;
        Tone tone = Tone::Normal;
        Tone value_tone = Tone::Normal;  ///< entry
        std::int32_t colour = 0;         ///< swatch: its colour (a palette colour or ui::rgb_ink)
        std::int32_t ink = -1;           ///< speech: the name's (a palette colour or ui::rgb_ink)
        std::int32_t line = 0;           ///< paragraph: line height
        double position = 0.0;           ///< slider: where the knob is; progress: how far (0..1)
        bool enabled = true;
        bool on = false;      ///< selected, checked, secret
        bool cycles = false;  ///< button: the right button chooses the previous value
    };
    [[nodiscard]] const std::vector<Widget>& widgets() const noexcept { return m_widgets; }

private:
    [[nodiscard]] std::optional<Id> hit(std::int32_t x, std::int32_t y, MouseButton button) const;
    [[nodiscard]] std::int32_t colour_of(Tone tone) const noexcept;
    /// The slider held now, if a slider is pressed.
    [[nodiscard]] const Widget* held_slider() const noexcept;
    /// The knob's position for the mouse at x pixels over `slider`.
    [[nodiscard]] double slider_position(const Widget& slider, std::int32_t x) const noexcept;

    FormStyle m_style;
    double m_scale = 1.0;
    Rect m_bounds{0, 0, 1024, 768};
    std::vector<Widget> m_widgets;
    std::optional<Id> m_pressed;
    MouseButton m_pressed_with = MouseButton::Left;
    MouseButton m_clicked_with = MouseButton::Left;
    std::optional<Id> m_hovered;
    std::optional<Id> m_focus;
    std::size_t m_caret = std::string::npos;  ///< in the focused field's text; npos: at its end
    std::optional<std::size_t> m_anchor;      ///< where the selection began; none: nothing selected
    bool m_released = false;
    double m_slide = 0.0;  ///< the held slider's knob, then where it was let go
};

}  // namespace o1602::ui
