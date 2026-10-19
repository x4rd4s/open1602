// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/form.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace o1602::ui {

namespace {

constexpr std::int32_t kBorder = 2;   ///< frame width, design units
constexpr std::int32_t kPadding = 8;  ///< text from the left or right edge of its box
constexpr std::int32_t kBox = 18;     ///< the box of a check box
constexpr double kFallbackText = 18;  ///< text height without a font

/// Width and height of text in design units.
double text_width(const Font* font, std::string_view s) {
    return font != nullptr ? font->width(s) / font->design_scale() : 9.0 * static_cast<double>(s.size());
}

double text_height(const Font* font) {
    return font != nullptr ? font->height() / font->design_scale() : kFallbackText;
}

Form::Widget make(Form::Widget::Kind kind, Form::Id id, Rect r, std::string text = {}) {
    Form::Widget w;
    w.kind = kind;
    w.id = id;
    w.rect = r;
    w.text = std::move(text);
    return w;
}

}  // namespace

std::string fit_text(const Font* font, std::string s, double width, bool keep_end) {
    if (text_width(font, s) <= width) {
        return s;
    }
    // Binary search for the cut: shorter text is never wider. The font measures up to the first
    // control character, so the start may move up to it at most (the rest is then 0 wide).
    if (keep_end) {
        const auto control =
            std::find_if(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
        std::size_t lo = 0;
        std::size_t hi = font != nullptr ? static_cast<std::size_t>(control - s.begin()) : s.size();
        if (text_width(font, std::string_view(s).substr(hi)) > width) {
            return {};
        }
        while (lo < hi) {  // the first start whose rest fits
            const std::size_t mid = lo + (hi - lo) / 2;
            if (text_width(font, std::string_view(s).substr(mid)) <= width) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return s.substr(lo);
    }
    std::string probe;
    const auto fits = [&](std::size_t n) {
        probe.assign(s, 0, n);
        probe += "...";
        return text_width(font, probe) <= width;
    };
    std::size_t lo = 0;
    std::size_t hi = s.size();
    while (lo < hi) {  // the longest start that fits with the dots, 0 if none does
        const std::size_t mid = lo + (hi - lo + 1) / 2;
        if (fits(mid)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    s.resize(lo);
    return s + "...";
}

void Form::panel(Rect r) {
    m_widgets.push_back(make(Widget::Kind::Panel, -1, r));
}

void Form::swatch(Rect r, std::uint8_t colour) {
    Widget w = make(Widget::Kind::Swatch, -1, r);
    w.colour = colour;
    m_widgets.push_back(std::move(w));
}

void Form::label(Rect r, std::string text, Align align, Tone tone) {
    Widget w = make(Widget::Kind::Label, -1, r, std::move(text));
    w.align = align;
    w.tone = tone;
    m_widgets.push_back(std::move(w));
}

void Form::paragraph(Rect r, std::string text, Tone tone, std::int32_t line) {
    Widget w = make(Widget::Kind::Paragraph, -1, r, std::move(text));
    w.tone = tone;
    w.line = std::max(line, 1);
    m_widgets.push_back(std::move(w));
}

void Form::button(Id id, Rect r, std::string text, bool enabled, bool selected) {
    Widget w = make(Widget::Kind::Button, id, r, std::move(text));
    w.align = Align::Centre;
    w.enabled = enabled;
    w.on = selected;
    m_widgets.push_back(std::move(w));
}

void Form::field(Id id, Rect r, const std::string& text, bool secret) {
    Widget w = make(Widget::Kind::Field, id, r, secret ? std::string(text.size(), '*') : text);
    w.on = secret;
    m_widgets.push_back(std::move(w));
}

void Form::check(Id id, Rect r, std::string text, bool on, bool enabled) {
    Widget w = make(Widget::Kind::Check, id, r, std::move(text));
    w.on = on;
    w.enabled = enabled;
    m_widgets.push_back(std::move(w));
}

void Form::row(Id id, Rect r, bool selected) {
    Widget w = make(Widget::Kind::Row, id, r);
    w.on = selected;
    m_widgets.push_back(std::move(w));
}

std::optional<Form::Id> Form::hit(std::int32_t x, std::int32_t y) const {
    // In design units; the last widget laid out is on top.
    const auto dx = static_cast<std::int32_t>(x / m_scale);
    const auto dy = static_cast<std::int32_t>(y / m_scale);
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
        if (it->id >= 0 && it->enabled && it->rect.contains(dx, dy)) {
            return it->id;
        }
    }
    return std::nullopt;
}

std::optional<Form::Id> Form::mouse(std::int32_t x, std::int32_t y, bool down) {
    const std::optional<Id> under = hit(x, y);
    m_hovered = under;
    if (down) {
        if (!m_pressed) {
            m_pressed = under ? under : std::optional<Id>(-1);  // -1: pressed on nothing
        }
        return std::nullopt;
    }
    const std::optional<Id> pressed = std::exchange(m_pressed, std::nullopt);
    if (pressed && under && *pressed == *under) {
        return under;
    }
    return std::nullopt;
}

std::int32_t Form::colour_of(Tone tone) const noexcept {
    switch (tone) {
        case Tone::Heading:
            return m_style.heading;
        case Tone::Dim:
            return m_style.dim;
        case Tone::Warning:
            return m_style.warning;
        case Tone::Normal:
            break;
    }
    return m_style.text;
}

std::vector<DrawItem> Form::draw(const std::array<const Font*, 3>& fonts) const {
    const Font* font = m_style.font >= 0 && m_style.font < 3 ? fonts[static_cast<std::size_t>(m_style.font)] : nullptr;
    const double s = m_scale;
    const auto px = [s](std::int32_t v) { return to_pixels(v, s); };
    std::vector<DrawItem> out;
    const auto fill = [&](Rect r, std::uint8_t colour) {
        DrawItem d;
        d.kind = DrawItem::Kind::Fill;
        d.x = px(r.x);
        d.y = px(r.y);
        d.w = px(r.x + r.w) - d.x;
        d.h = px(r.y + r.h) - d.y;
        d.colour = colour;
        out.push_back(d);
    };
    const auto framed = [&](Rect r, std::uint8_t inside) {
        fill(r, m_style.frame);
        fill(Rect{r.x + kBorder, r.y + kBorder, r.w - 2 * kBorder, r.h - 2 * kBorder}, inside);
    };
    const auto text = [&](Rect r, std::string t, Align align, std::int32_t colour, bool keep_end = false) {
        const double room = r.w - 2.0 * kPadding;
        DrawItem d;
        d.kind = DrawItem::Kind::Text;
        d.block = m_style.font;
        d.colour = colour;
        d.align = align;
        d.text = fit_text(font, std::move(t), std::max(room, 0.0), keep_end);
        const std::int32_t x = align == Align::Left    ? r.x + kPadding
                               : align == Align::Right ? r.x + r.w - kPadding
                                                       : r.x + r.w / 2;
        d.x = px(x);
        d.y = px(r.y + static_cast<std::int32_t>((r.h + text_height(font)) / 2));
        out.push_back(std::move(d));
    };
    const auto raster = [&](Rect r) {
        DrawItem d;
        d.kind = DrawItem::Kind::Raster;
        d.x = px(r.x);
        d.y = px(r.y);
        d.w = px(r.x + r.w) - d.x;
        d.h = px(r.y + r.h) - d.y;
        out.push_back(d);
    };
    const bool held = m_pressed && *m_pressed >= 0;
    for (const Widget& w : m_widgets) {
        const bool hover = w.id >= 0 && m_hovered == w.id && w.enabled;
        switch (w.kind) {
            case Widget::Kind::Panel:
                framed(w.rect, m_style.ground);
                break;
            case Widget::Kind::Swatch:
                fill(w.rect, w.colour);
                break;
            case Widget::Kind::Label:
                text(w.rect, w.text, w.align, colour_of(w.tone));
                break;
            case Widget::Kind::Paragraph: {
                std::vector<std::string> lines;
                if (font != nullptr) {
                    const double room = std::max(0.0, (w.rect.w - 2.0 * kPadding) * font->design_scale());
                    lines = wrap_text(*font, w.text, static_cast<std::uint32_t>(room));
                } else {
                    lines = {w.text};
                }
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    const auto y = w.rect.y + static_cast<std::int32_t>(i) * w.line;
                    if (y + w.line > w.rect.y + w.rect.h) {
                        break;
                    }
                    text(Rect{w.rect.x, y, w.rect.w, w.line}, lines[i], Align::Left, colour_of(w.tone));
                }
                break;
            }
            case Widget::Kind::Button: {
                const bool pressed = hover && held && *m_pressed == w.id;
                framed(w.rect, w.on || pressed ? m_style.selected : hover ? m_style.hover : m_style.ground);
                text(w.rect, w.text, Align::Centre, w.on ? m_style.heading : m_style.text);
                if (!w.enabled) {
                    raster(w.rect);
                }
                break;
            }
            case Widget::Kind::Field: {
                const bool focused = m_focus == w.id;
                framed(w.rect, m_style.field);
                text(w.rect, focused ? w.text + "_" : w.text, Align::Left, m_style.text, focused);
                break;
            }
            case Widget::Kind::Check: {
                const Rect box{w.rect.x, w.rect.y + (w.rect.h - kBox) / 2, kBox, kBox};
                framed(box, hover ? m_style.hover : m_style.field);
                if (w.on) {
                    fill(Rect{box.x + 5, box.y + 5, kBox - 10, kBox - 10}, m_style.frame);
                }
                text(Rect{w.rect.x + kBox, w.rect.y, w.rect.w - kBox, w.rect.h}, w.text, Align::Left,
                     w.enabled ? m_style.text : m_style.dim);
                break;
            }
            case Widget::Kind::Row:
                if (w.on || hover) {
                    fill(w.rect, w.on ? m_style.selected : m_style.hover);
                }
                break;
        }
    }
    return out;
}

}  // namespace o1602::ui
