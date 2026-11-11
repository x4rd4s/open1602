// SPDX-License-Identifier: GPL-3.0-or-later
//
// Improved computers read their cities as a player reads the statistics windows (ai_overview.hpp).
#include "ai_overview.hpp"

#include <algorithm>

#include "ai_engine.hpp"

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/goods_statistics.hpp"

namespace o1602::sim::ai {

std::int64_t AreaView::net_ahead(std::uint32_t minutes) const noexcept {
    // `before` is about kForecastMinutes old: its change to now goes on at the same pace.
    if (history < kTrendMinutes) {
        return now.net();
    }
    const std::int64_t change = now.net() - before.net();
    return now.net() + change * std::int64_t{minutes} / std::int64_t{kForecastMinutes};
}

std::int64_t AreaView::net_safe(std::uint32_t minutes) const noexcept {
    return std::min(now.net(), net_ahead(minutes));
}

std::optional<std::uint32_t> WareView::lasts() const noexcept {
    if (balance >= 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(stock / static_cast<std::uint64_t>(-balance), UINT32_MAX));
}

std::int64_t WareView::ahead(std::uint32_t after) const noexcept {
    return static_cast<std::int64_t>(stock) + balance * std::int64_t{after};
}

std::uint32_t WareView::met() const noexcept {
    if (wanted == 0) {
        return 100;
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(100, eaten * 100 / wanted));
}

const AreaView* Overview::island(std::uint16_t i) const noexcept {
    const auto it = std::ranges::find_if(islands, [&](const AreaView& a) { return a.island == i; });
    return it != islands.end() ? &*it : nullptr;
}

namespace {

void add(Finances& f, const CitySample& s) {
    f.taxes += s.of(CityFigure::Taxes);
    f.costs += s.of(CityFigure::RunningCosts);
    f.sales += s.of(CityFigure::Sales);
    f.purchases += s.of(CityFigure::Purchases);
}

}  // namespace

Overview Engine::overview(const AiPlayer& p) const {
    Overview o;
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    // The history's samples of an hour ago: the oldest quarter of its last hour, once every city
    // of the area has kTrendMinutes of it (a young colony's history would weigh as if it had none).
    const auto area = [&](std::optional<std::uint16_t> island) {
        AreaView a;
        a.island = island;
        std::uint32_t shortest = UINT32_MAX;
        for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
            const City& c = m_world.cities()[i];
            if (c.owner != p.player || (island && c.island != *island)) {
                continue;
            }
            ++a.cities;
            const std::uint32_t people = population(c);
            a.inhabitants += people;
            a.peopled += people != 0 ? 1U : 0U;
            for (std::size_t k = 0; k < c.inhabitants.size() && k < a.classes.size(); ++k) {
                a.classes[k] += c.inhabitants[k];
            }
            a.now.taxes += tax_income(c, m_buildings);
            a.now.costs += i < costs.size() ? costs[i] : 0;
            a.now.sales += c.sales / 480;
            a.now.purchases += c.purchases / 480;
            const std::vector<CitySample> last = c.history.last_minutes();
            shortest = std::min(shortest, static_cast<std::uint32_t>(last.size()));
            if (last.size() >= kTrendMinutes) {
                Finances old;
                const std::size_t n = last.size() - kTrendMinutes + 1;  // the oldest minutes
                for (std::size_t k = 0; k < n; ++k) {
                    add(old, last[k]);
                }
                const auto div = static_cast<std::int64_t>(n);
                a.before.taxes += old.taxes / div;
                a.before.costs += old.costs / div;
                a.before.sales += old.sales / div;
                a.before.purchases += old.purchases / div;
            }
        }
        a.history = a.cities != 0 && shortest != UINT32_MAX ? shortest : 0;
        if (a.history < kTrendMinutes) {
            a.before = a.now;
        }
        return a;
    };
    o.all = area(std::nullopt);
    // The army is the player's, not an island's; its trend is not kept (it counts as it is).
    o.all.now.military = military_cost(p);
    o.all.before.military = o.all.now.military;
    for (const std::uint16_t island : goods_islands(m_world, p.player)) {
        o.islands.push_back(area(island));
    }
    return o;
}

WareView Engine::ware_view(const AiPlayer& p, std::uint8_t ware, std::optional<std::uint16_t> island) const {
    WareView v;
    GoodsScope scope{p.player, island};
    v.stock = goods_stock(m_world, scope, ware);
    const FlowTotals t = goods_totals(m_world, scope, ware, kViewMinutes);
    v.minutes = t.minutes;
    if (t.minutes != 0) {
        const std::uint64_t m = t.minutes;
        v.made = t.production() / m;
        v.eaten = t.of(GoodsFlow::Residents) / m;
        v.wanted = t.of(GoodsFlow::Demand) / m;
        v.balance = t.balance() / static_cast<std::int64_t>(m);
    }
    for (const GoodsProducers& k : goods_producers(m_world, m_buildings, scope, ware)) {
        v.producers += k.count;
        v.working += k.working;
        v.short_of_raw += k.short_of_raw;
        v.waiting += k.waiting_for_room;
        v.off += k.off;
    }
    return v;
}

}  // namespace o1602::sim::ai
