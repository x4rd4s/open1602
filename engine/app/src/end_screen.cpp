// SPDX-License-Identifier: GPL-3.0-or-later
#include "end_screen.hpp"

#include <algorithm>
#include <utility>

#include "o1602/core/text.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

// ENDE.GAD (docs/re/ui.md "Endbildschirm").
constexpr std::int32_t kMainMenu = 36001;
constexpr std::int32_t kNewGame = 36002;
constexpr std::int32_t kBack = 36003;  // commented out in the shipped file
constexpr std::int32_t kTotal = 36004;
constexpr std::int32_t kArms = 36005;
constexpr std::int32_t kName = 36006;
constexpr std::int32_t kFigures = 36011;  // the status column, 11 lines
constexpr std::int32_t kPoints = 36031;   // the points column, 9 lines
constexpr std::int32_t kColours = 36051;  // the coats of arms to choose a player, 4

// The hint of Open1602's way back into a won game, over the buttons.
constexpr std::int32_t kHintX = 512;
constexpr std::int32_t kHintY = 680;
constexpr std::int32_t kText = 4;  // text colour of the original (ui::text_colour)

}  // namespace

EndScreen::EndScreen(std::optional<assets::GadgetLayout> ende) {
    if (ende) {
        if (const assets::Gadget* g = ende->find(kArms)) {
            m_arms = g->gfx;
        }
        m_panel.emplace(std::move(*ende), m_scale);
    }
}

void EndScreen::open(sim::EndScreenSummary summary, bool view_only) {
    m_summary = std::move(summary);
    m_view_only = view_only;
    m_by_colour = {};
    for (std::size_t i = 0; i < m_summary.rows.size(); ++i) {
        const sim::BannerId banner = m_summary.rows[i].banner;
        if (banner < m_by_colour.size() && !m_by_colour[banner]) {
            m_by_colour[banner] = i;
        }
    }
    if (!m_panel) {
        return;
    }
    ui::Panel& p = *m_panel;
    p.set_visible(kMainMenu, !view_only);
    p.set_visible(kNewGame, !view_only);
    p.set_visible(kBack, view_only);
    // Screen_PlayerStatus: a coat of arms for every colour somebody plays, with two players or more.
    for (std::size_t c = 0; c < m_by_colour.size(); ++c) {
        p.set_visible(kColours + static_cast<std::int32_t>(c),
                      !view_only && m_by_colour[c].has_value() && m_summary.rows.size() >= 2);
    }
    show(0);  // the local player is the first row
}

void EndScreen::show(std::size_t row) {
    if (!m_panel || row >= m_summary.rows.size()) {
        return;
    }
    ui::Panel& p = *m_panel;
    m_row = row;
    const sim::EndScreenRow& r = m_summary.rows[row];
    const sim::ScoreSheet& s = r.sheet;
    const std::array<std::int64_t, 10> figures{s.cities,     s.inhabitants,       s.monuments,     s.money,
                                               s.islands,    s.soldiers_killed,   s.soldiers_lost, s.ships_sunk,
                                               s.ships_lost, s.opponents_defeated};
    for (std::size_t i = 0; i < figures.size(); ++i) {
        p.set_text(kFigures + static_cast<std::int32_t>(i), std::to_string(figures[i]));
    }
    p.set_text(kFigures + static_cast<std::int32_t>(figures.size()),
               std::to_string(s.bonus_percent) + "%");  // [GAME] 48 "%d%%"
    const std::array<std::int64_t, 9> points{s.city_points,  s.subjects_points, s.monument_points,
                                             s.money_points, s.land_points,     s.soldier_points,
                                             s.ship_points,  s.defeat_points,   s.bonus};
    for (std::size_t i = 0; i < points.size(); ++i) {
        p.set_text(kPoints + static_cast<std::int32_t>(i), std::to_string(points[i]));
    }
    p.set_text(kTotal, std::to_string(s.total));
    p.set_text(kName, r.name.empty() ? ui::tr("end.player", {std::to_string(r.player + 1)}) : r.name);
    // The four colours of the original have coats of arms; the further players show none.
    p.set_visible(kArms, r.banner <= 3);
    p.set_graphic(kArms, m_arms + sim::four_colour_index(r.banner), 0);
    for (std::size_t c = 0; c < m_by_colour.size(); ++c) {
        p.set_selected(kColours + static_cast<std::int32_t>(c), m_by_colour[c] == row);
    }
}

void EndScreen::step(int direction) {
    const std::size_t n = m_summary.rows.size();
    if (n < 2 || m_view_only) {
        return;
    }
    show(direction < 0 ? (m_row + n - 1) % n : (m_row + 1) % n);
}

void EndScreen::set_scale(double scale) {
    m_scale = scale;
    if (m_panel) {
        m_panel->set_scale(scale);
    }
}

void EndScreen::draw(render::SdlUiRenderer& renderer, std::int32_t x, std::int32_t y) {
    if (m_panel) {
        renderer.draw(m_panel->draw(renderer.fonts()), x, y, m_scale);
    }
    if (!m_view_only && m_summary.outcome == sim::GameOutcome::Won) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 0;
        d.colour = kText;
        d.align = ui::Align::Centre;
        d.x = ui::to_pixels(kHintX, m_scale);
        d.y = ui::to_pixels(kHintY, m_scale);
        d.text = ui::tr("end.continue_hint");
        renderer.draw({std::move(d)}, x, y, m_scale);
    }
}

std::optional<EndScreen::Choice> EndScreen::mouse(std::int32_t x, std::int32_t y, bool down) {
    if (!m_panel) {
        return std::nullopt;
    }
    std::optional<Choice> choice;
    for (const ui::PanelEvent& e : m_panel->mouse(x, y, down)) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        if (e.id == kMainMenu) {
            choice = Choice::MainMenu;
        } else if (e.id == kNewGame) {
            choice = Choice::NewGame;
        } else if (e.id == kBack) {
            choice = Choice::Resume;
        } else if (e.id >= kColours && e.id < kColours + static_cast<std::int32_t>(m_by_colour.size())) {
            if (const auto row = m_by_colour[static_cast<std::size_t>(e.id - kColours)]) {
                show(*row);
            }
        }
    }
    return choice;
}

std::optional<EndScreen::Choice> EndScreen::back() const noexcept {
    if (m_view_only || m_summary.outcome == sim::GameOutcome::Won) {
        return Choice::Resume;
    }
    return std::nullopt;
}

}  // namespace o1602::app
