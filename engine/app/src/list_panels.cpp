// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ship list and the city list (SHIPLIST.GAD, STADTLST.GAD; ShipList_Refresh 0x00434020,
// CityList_Refresh 0x00434200; docs/re/ui.md): opened from the player status or with L and
// Shift+L, 13 rows a page; a click on a name goes there (a ship is also selected).
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "panel_common.hpp"

namespace o1602::app {

namespace {

constexpr std::size_t kRows = 13;

class ListPanel final : public SelectionPanel {
public:
    ListPanel(assets::GadgetLayout layout, bool ships)
        : m_panel(std::move(layout), 1.0), m_ships(ships), m_base(ships ? 61000 : 63000) {
        const assets::GadgetLayout& l = m_panel.layout();
        if (const auto g = l.index_of(m_base + kArms)) {
            m_arms = l.gadgets[*g].gfx;
        }
        if (const auto g = l.index_of(m_base + kIcon)) {
            m_icons = l.gadgets[*g].gfx;
        }
    }

    void set_scale(double scale) override { m_panel.set_scale(scale); }
    ui::Panel& panel() override { return m_panel; }

    void refresh(const Selection& /*selection*/, const PanelContext& c) override {
        collect(c);
        // The page is kept within the list (it shrinks when ships sink or cities change hands).
        const std::size_t last = m_rows.empty() ? 0 : (m_rows.size() - 1) / kRows * kRows;
        m_first = std::min(m_first, last);
        // The original leaves the heading empty; Open1602 names the list and counts its rows.
        m_panel.set_text(m_base + kHeading,
                         panel_detail::help_line(c, m_ships ? 214 : 215) + " (" + std::to_string(m_rows.size()) + ")");
        const sim::Player* local = c.world().player(c.local);
        const std::int32_t colour = local != nullptr ? sim::four_colour_index(local->banner) : 0;
        m_panel.set_graphic(m_base + kArms, m_arms + colour, 0);
        m_panel.set_graphic(m_base + kArmsShadow, m_arms + colour, 0);
        m_panel.set_visible(m_base + kPageDown, m_first + kRows < m_rows.size());
        m_panel.set_visible(m_base + kPageUp, m_first > 0);
        for (std::size_t k = 0; k < kRows; ++k) {
            const std::int32_t row = static_cast<std::int32_t>(k);
            const bool shown = m_first + k < m_rows.size();
            for (const std::int32_t id : {kBoard, kName, kIcon}) {
                m_panel.set_visible(m_base + id + row, shown);
            }
            if (!shown) {
                continue;
            }
            const Row& r = m_rows[m_first + k];
            m_panel.set_text(m_base + kName + row, r.name);
            m_panel.set_graphic(m_base + kIcon + row, m_icons + r.icon, 0);
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& /*selection*/, const PanelContext& /*c*/,
                 std::vector<sim::Command>& /*out*/) override {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        if (e.id == m_base + kPageDown && m_first + kRows < m_rows.size()) {
            m_first += kRows;
        } else if (e.id == m_base + kPageUp) {
            m_first -= std::min(m_first, kRows);
        } else if (e.id >= m_base + kName && e.id < m_base + kName + static_cast<std::int32_t>(kRows)) {
            const std::size_t i = m_first + static_cast<std::size_t>(e.id - m_base - kName);
            if (i < m_rows.size()) {
                const Row& r = m_rows[i];
                Request q;
                q.kind = m_ships ? Request::Kind::SelectShip : Request::Kind::Centre;
                q.ship = r.ship;
                q.x = r.x;
                q.y = r.y;
                m_request = q;
            }
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& /*selection*/,
                                                  const PanelContext& c) const override {
        const std::int32_t k = id - m_base;
        if (k >= kName && k < kName + static_cast<std::int32_t>(kRows)) {
            return panel_detail::help_line(c, m_ships ? 221 : 223);
        }
        if (k >= kIcon && k < kIcon + static_cast<std::int32_t>(kRows)) {
            const std::size_t i = m_first + static_cast<std::size_t>(k - kIcon);
            if (!m_ships) {
                return panel_detail::help_line(c, 222);
            }
            if (i < m_rows.size()) {
                // 218 on a trade route, 219 under way, 220 at anchor.
                constexpr std::array<std::size_t, 3> kLines = {218, 220, 219};
                return panel_detail::help_line(c, kLines[static_cast<std::size_t>(std::clamp(m_rows[i].icon, 0, 2))]);
            }
        }
        return std::nullopt;
    }

    /// Every opening starts at the first page (0x0043CE00).
    void opened() override { m_first = 0; }

    [[nodiscard]] std::optional<Request> take_request() override { return std::exchange(m_request, std::nullopt); }

private:
    static constexpr std::int32_t kHeading = 2;
    static constexpr std::int32_t kArmsShadow = 3;
    static constexpr std::int32_t kArms = 4;
    static constexpr std::int32_t kPageDown = 5;  ///< the next 13
    static constexpr std::int32_t kPageUp = 6;
    static constexpr std::int32_t kBoard = 11;
    static constexpr std::int32_t kName = 31;
    static constexpr std::int32_t kIcon = 51;

    struct Row {
        std::string name;
        std::int32_t icon = 0;  ///< ships: 0 trade route, 1 anchored, 2 under way; cities: 4 + colour
        std::uint32_t ship = 0;
        double x = 0.0;  ///< world tile to go to
        double y = 0.0;
    };

    void collect(const PanelContext& c) {
        m_rows.clear();
        const sim::World& w = c.world();
        if (m_ships) {
            // All own ships that are not sinking, in the order of the ship table (0x0044F000).
            for (const sim::Ship& s : w.ships()) {
                if (s.owner != c.local || s.sinking) {
                    continue;
                }
                const sim::ShipMotion& m = s.motion;
                const bool moving = m.phase == sim::ShipMotion::Phase::Run || m.phase == sim::ShipMotion::Phase::Turn ||
                                    m.path_index < m.path.size();
                Row r;
                r.name = s.name;
                r.icon = s.route.active && s.route.has_stops() ? 0 : (moving ? 2 : 1);
                r.ship = s.id;
                r.x = s.x;
                r.y = s.y;
                m_rows.push_back(std::move(r));
            }
            return;
        }
        // Cities of humans and computers: the player's own and those of trade partners.
        for (const sim::City& city : w.cities()) {
            const sim::Player* owner = w.player(city.owner);
            if (owner == nullptr || city.island >= w.islands().size() ||
                (owner->role != sim::PlayerRole::Human && owner->role != sim::PlayerRole::Computer)) {
                continue;
            }
            if (city.owner != c.local && (c.local == sim::kNoPlayer || !w.diplomacy().may_trade(c.local, city.owner))) {
                continue;
            }
            const sim::WorldIsland& island = w.islands()[city.island];
            Row r;
            r.name = city.name;
            r.icon = 4 + sim::four_colour_index(owner->banner);
            r.x = island.x + (city.warehouse_x ? *city.warehouse_x : island.map.width() / 2.0);
            r.y = island.y + (city.warehouse_y ? *city.warehouse_y : island.map.height() / 2.0);
            m_rows.push_back(std::move(r));
        }
    }

    ui::Panel m_panel;
    bool m_ships;
    std::int32_t m_base;
    std::int32_t m_arms = 548;
    std::int32_t m_icons = 622;
    std::size_t m_first = 0;
    std::vector<Row> m_rows;
    std::optional<Request> m_request;
};

}  // namespace

std::unique_ptr<SelectionPanel> make_list_panel(assets::GadgetLayout layout, bool ships) {
    return std::make_unique<ListPanel>(std::move(layout), ships);
}

}  // namespace o1602::app
