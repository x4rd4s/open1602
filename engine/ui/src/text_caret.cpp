// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/text_caret.hpp"

namespace o1602::ui {

std::pair<std::size_t, std::size_t> TextCaret::selection(std::string_view text) const noexcept {
    const std::size_t c = caret(text);
    if (!m_anchor) {
        return {c, c};
    }
    const std::size_t a = std::min(*m_anchor, text.size());
    return {std::min(a, c), std::max(a, c)};
}

void TextCaret::insert(std::string& text, std::string_view typed, std::size_t most) {
    if (typed.empty()) {
        return;
    }
    const auto [first, last] = selection(text);
    text.erase(first, last - first);
    typed = typed.substr(0, most > text.size() ? most - text.size() : 0);
    text.insert(first, typed);
    m_caret = first + typed.size();
    m_anchor.reset();
}

bool TextCaret::edit(std::string& text, int key) {
    const bool shift = (key & kKeyShift) != 0;
    const int k = key & ~kKeyShift;
    const auto [first, last] = selection(text);
    const std::size_t c = caret(text);
    switch (k) {
        case kKeySelectAll:
            select_all();
            return true;
        case kKeyBackspace:
        case kKeyDelete:
            if (first < last) {
                text.erase(first, last - first);
                m_caret = first;
            } else if (k == kKeyBackspace && c > 0) {
                text.erase(c - 1, 1);
                m_caret = c - 1;
            } else if (k == kKeyDelete && c < text.size()) {
                text.erase(c, 1);
                m_caret = c;
            }
            m_anchor.reset();
            return true;
        case kKeyLeft:
        case kKeyRight:
        case kKeyHome:
        case kKeyEnd: {
            std::size_t to = 0;
            if (!shift && first < last && (k == kKeyLeft || k == kKeyRight)) {
                to = k == kKeyLeft ? first : last;  // the selection ends at its side
            } else if (k == kKeyLeft) {
                to = c > 0 ? c - 1 : 0;
            } else if (k == kKeyRight) {
                to = std::min(c + 1, text.size());
            } else if (k == kKeyEnd) {
                to = text.size();
            }
            if (!shift) {
                m_anchor.reset();
            } else if (!m_anchor) {
                m_anchor = c;
            }
            m_caret = to;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace o1602::ui
