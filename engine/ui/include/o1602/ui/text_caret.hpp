// SPDX-License-Identifier: GPL-3.0-or-later
//
// The caret and the selection of a text being typed: where the next letter goes, what is marked,
// and the keys that move or delete. Every text input of the game shares it (the fields of
// Open1602's own windows, ui::Form, and the names of ships and cities in their windows), so that
// they all answer the same keys. The owner keeps the text. Pure logic.
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace o1602::ui {

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

class TextCaret {
public:
    /// The caret at the end of the text, nothing selected (a new text to type).
    void reset() noexcept {
        m_caret = std::string::npos;
        m_anchor.reset();
    }
    /// Ctrl+A: the whole text is selected; what is typed next replaces it, Backspace removes it.
    void select_all() noexcept {
        m_anchor = 0;
        m_caret = std::string::npos;
    }
    [[nodiscard]] bool all_selected() const noexcept { return m_anchor == 0 && m_caret == std::string::npos; }
    /// Whether the whole text was selected; the selection is gone afterwards (any key ends it).
    bool take_all_selected() noexcept {
        const bool all = all_selected();
        m_anchor.reset();
        return all;
    }

    /// Where the caret stands in `text` (its end, unless moved).
    [[nodiscard]] std::size_t caret(std::string_view text) const noexcept { return std::min(m_caret, text.size()); }
    /// The part of `text` that is selected: [first, second), empty (both at the caret) if nothing is.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selection(std::string_view text) const noexcept;
    /// Text typed into `text`: it takes the selection's place, or goes in at the caret, as far as
    /// `most` bytes in all allow; the caret follows it. The owner leaves out what the text does not
    /// take before. Nothing typed leaves the selection as it is.
    void insert(std::string& text, std::string_view typed, std::size_t most);
    /// A key for `text`: Ctrl+A (kKeySelectAll) selects it all, Backspace and Delete remove the
    /// selection or the byte before or after the caret, Left, Right, Home and End move the caret,
    /// with kKeyShift selecting on the way. False for any other key, which the owner handles.
    bool edit(std::string& text, int key);

private:
    std::size_t m_caret = std::string::npos;  ///< in the text; npos: at its end
    std::optional<std::size_t> m_anchor;      ///< where the selection began; none: nothing selected
};

}  // namespace o1602::ui
