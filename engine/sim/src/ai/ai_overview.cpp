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

const Overview& Engine::overview(const AiPlayer& p) const {
    // The figures come from the cities' last closed minutes (the statistics step closes a minute
    // after the computers' step of its tick), so they stay the same from one close to the next: built
    // once for them and for the computer's cities of the moment, read from the cache until either
    // changes. A game loaded in between builds the same overview again.
    const std::uint64_t t = now_ms();
    const std::uint64_t minute = t == 0 ? 0 : (t - 1) / kGoodsMinuteMs + 1;
    std::uint64_t cities = 1469598103934665603ULL;  // FNV-1a over the indices of its cities
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        if (m_world.cities()[i].owner == p.player) {
            cities = (cities ^ (i + 1)) * 1099511628211ULL;
        }
    }
    if (m_overviews.size() <= p.player) {
        m_overviews.resize(std::size_t{p.player} + 1);
    }
    OverviewCache& cache = m_overviews[p.player];
    if (!cache.valid || cache.minute != minute || cache.cities != cities) {
        cache.valid = true;
        cache.minute = minute;
        cache.cities = cities;
        cache.view = build_overview(p);
    }
    // The army's upkeep is the player's and counts as it is now.
    cache.view.all.now.military = military_cost(p);
    cache.view.all.before.military = cache.view.all.now.military;
    return cache.view;
}

Overview Engine::build_overview(const AiPlayer& p) const {
    Overview o;
    // Now: the last closed minute. An hour ago: the oldest quarter of the last hour, once every city
    // of the area has kTrendMinutes of history (a young colony's would weigh as if it had none).
    const auto area = [&](std::optional<std::uint16_t> island) {
        AreaView a;
        a.island = island;
        std::uint32_t shortest = UINT32_MAX;
        for (const City& c : m_world.cities()) {
            if (c.owner != p.player || (island && c.island != *island)) {
                continue;
            }
            ++a.cities;
            const std::vector<CitySample> last = c.history.last_minutes();
            shortest = std::min(shortest, static_cast<std::uint32_t>(last.size()));
            if (last.empty()) {
                continue;  // founded within the minute
            }
            const CitySample& s = last.back();
            std::uint32_t people = 0;
            for (std::size_t k = 0; k < a.classes.size(); ++k) {
                const auto n = static_cast<std::uint32_t>(std::max(0, s.of_class(k)));
                a.classes[k] += n;
                people += n;
            }
            a.inhabitants += people;
            a.peopled += people != 0 ? 1U : 0U;
            add(a.now, s);
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
