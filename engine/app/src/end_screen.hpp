// SPDX-License-Identifier: GPL-3.0-or-later
//
// The players' score sheets (Screen_PlayerStatus; ENDE.GAD, docs/re/ui.md "Endbildschirm"): after
// a game is decided, and from the info panel's "Bewertung" while it runs. The labels are part of
// the background picture; the screen fills in a player's figures and points, the total, the name
// and the coat of arms, and offers the coats of arms of the other players to look at theirs.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "high_score_file.hpp"

#include "o1602/assets/gad.hpp"
#include "o1602/render/sdl_ui_renderer.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::app {

class EndScreen {
public:
    /// What the player chose.
    enum class Choice : std::uint8_t {
        MainMenu,  ///< "Hauptmenü"
        NewGame,   ///< "Neues Spiel": the same scenario's mission screen (single player)
        Resume,    ///< Escape: back to the game (the sheet while it runs; Open1602 after a win)
    };

    /// `ende`: the layout ENDE.GAD, nothing if the installation lacks it (the screen is black).
    explicit EndScreen(std::optional<assets::GadgetLayout> ende);

    /// Shows a decided game (`view_only` false: the buttons, no way back) or the sheets of a
    /// running one (Escape goes back). Starts with the local player's sheet.
    void open(sim::EndScreenSummary summary, bool view_only);
    void set_scale(double scale);

    /// Draws over the whole 1024 x 768 design, its top left at (x, y).
    void draw(render::SdlUiRenderer& renderer, std::int32_t x, std::int32_t y);
    /// The mouse relative to the design's top left, in pixels.
    std::optional<Choice> mouse(std::int32_t x, std::int32_t y, bool down);
    /// Escape: back to a running game, or into a won one (Open1602: the original has no keys on
    /// its final screen and ends the game).
    [[nodiscard]] std::optional<Choice> back() const noexcept;
    /// Left and right: the sheet of the previous or next player (Open1602: ENDE.GAD has coats of
    /// arms for four colours, a game may have twelve players).
    void step(int direction);

    [[nodiscard]] const sim::EndScreenSummary& summary() const noexcept { return m_summary; }
    [[nodiscard]] bool view_only() const noexcept { return m_view_only; }

private:
    void show(std::size_t row);

    std::optional<ui::Panel> m_panel;
    double m_scale = 1.0;
    std::int32_t m_arms = 0;  ///< sprite of the coat of arms of colour 0 (gadget 36005)
    sim::EndScreenSummary m_summary;
    std::size_t m_row = 0;  ///< the row shown
    bool m_view_only = false;
    /// The row of each colour's button, nothing for a colour nobody plays.
    std::array<std::optional<std::size_t>, 4> m_by_colour{};
};

}  // namespace o1602::app
