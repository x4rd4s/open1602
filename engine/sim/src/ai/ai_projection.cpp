// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602 (P2.4): a computer city projected minute by minute. The copy runs the game's own steps
// (advance_city, advance_houses) for the city alone, with the ledger's inflow of its goods: the
// supply levels, satisfaction, growth, ascents and descents follow each other as in the game.

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "ai_engine.hpp"

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::sim::ai {

CityProjection Engine::project_city(const AiPlayer& p, const City& c, std::uint32_t minutes) const {
    (void)p;
    CityProjection out;
    // ai-view: eigen: the copy keeps only the own city's houses and runs only the own city; nothing
    // of another player's is read from it. Its steps raise no events (nullptr). Their only draws pick
    // a picture (a risen or fallen house's variant, a ruin's), never an outcome; still, the copy
    // draws from streams of the default seed, not from the game's own, so that it cannot foresee
    // the game's next numbers, and the game's streams stay as they are.
    World copy = m_world;
    copy.reseed(kDefaultSimulationSeed);
    std::vector<House> others;
    for (const House& h : copy.houses()) {
        if (h.island != c.island || h.city != c.index) {
            others.push_back(h);
        }
    }
    for (const House& h : others) {
        copy.remove_house(h.island, h.x, h.y);
    }
    City* city = copy.city(c.island, c.index);
    if (city == nullptr) {
        return out;
    }
    out.running_cost = running_cost(copy, m_buildings, *city);
    const std::uint32_t workers = city_workers(copy, m_buildings, *city);
    // Each ware's flows a minute (1/32 t, what a minute leaves over carried on). The steady ones
    // (production, workshops, what overflows) run at the budget's horizon rate. Trade at the Kontor
    // follows its own settings: sales only take stock above `keep`, purchases fill only up to
    // `limit` (P2.5). Own ships come and go in batches about as long apart as the horizon, so they
    // run at the rate of the ledger's whole minute window.
    enum Flow : std::size_t { kSteady, kSold, kBought, kShips, kFlows };
    const std::uint32_t horizon = m_world.balance().ai_budget_horizon_minutes;
    std::vector<std::array<std::int64_t, kFlows>> rate(city->goods.size());
    std::vector<std::array<std::int64_t, kFlows>> carried(city->goods.size());
    std::vector<FlowTotals> window(city->goods.size());  // for the price the Kontor got and paid
    for (std::size_t w = 0; w < city->goods.size(); ++w) {
        const auto ware = static_cast<std::uint8_t>(w);
        if (!city->ledger.used(ware)) {
            continue;
        }
        const FlowTotals t = city->ledger.totals(ware, horizon);
        const FlowTotals l = city->ledger.totals(ware, GoodsLedger::kMinutes);
        const auto of = [](const FlowTotals& f, GoodsFlow k) { return static_cast<std::int64_t>(f.of(k)); };
        if (t.minutes != 0) {
            const std::int64_t n = t.minutes;
            const std::int64_t trade = of(t, GoodsFlow::Bought) - of(t, GoodsFlow::Sold);
            const std::int64_t ships = of(t, GoodsFlow::ShippedIn) - of(t, GoodsFlow::ShippedOut);
            // Building and recruiting are new decisions, not the city's own flow (P2.6 plans them).
            const std::int64_t decided = of(t, GoodsFlow::Construction) + of(t, GoodsFlow::Military);
            rate[w][kSteady] = (t.net() + of(t, GoodsFlow::Residents) + decided - trade - ships) / n;
            rate[w][kSold] = of(t, GoodsFlow::Sold) / n;
            rate[w][kBought] = of(t, GoodsFlow::Bought) / n;
            window[w] = t;
        }
        if (l.minutes != 0) {
            rate[w][kShips] = (of(l, GoodsFlow::ShippedIn) - of(l, GoodsFlow::ShippedOut)) / l.minutes;
        }
    }
    // A sixth of each flow's minute, with what is left over carried to the next tick.
    const auto share = [&](std::size_t w, Flow f) {
        carried[w][f] += rate[w][f];
        const std::int64_t now = carried[w][f] / kCityTicksPerMinute;
        carried[w][f] -= now * kCityTicksPerMinute;
        return now;
    };
    std::uint32_t city_ms = m_sim.m_city_ms;
    out.minutes.reserve(minutes);
    for (std::uint32_t m = 0; m < minutes; ++m) {
        struct Traded {
            std::int64_t sold = 0;
            std::int64_t bought = 0;
        };
        std::vector<Traded> traded(rate.size());  // this minute
        for (std::uint32_t ms = 0; ms < kCityTicksPerMinute * kCityTickMs; ms += Simulation::kTickMs) {
            city_ms += Simulation::kTickMs;
            if (city_ms >= kCityTickMs) {
                city_ms -= kCityTickMs;
                // The flows before the city takes its goods, as the producers' goods come before
                // the city tick.
                for (std::size_t w = 0; w < rate.size(); ++w) {
                    const std::int64_t room = std::max(city->capacity, city->goods[w]);
                    std::int64_t stock = std::clamp<std::int64_t>(
                        std::int64_t{city->goods[w]} + share(w, kSteady) + share(w, kShips), 0, room);
                    const City::Trade* k = w < city->trade.size() ? &city->trade[w] : nullptr;
                    const std::int64_t sold = std::clamp<std::int64_t>(
                        share(w, kSold), 0, k != nullptr && k->sell ? std::max<std::int64_t>(0, stock - k->keep) : 0);
                    stock -= sold;
                    const std::int64_t bought = std::clamp<std::int64_t>(
                        share(w, kBought), 0,
                        k != nullptr && k->buy
                            ? std::max<std::int64_t>(0, std::min<std::int64_t>(k->limit, room) - stock)
                            : 0);
                    stock += bought;
                    traded[w].sold += sold;
                    traded[w].bought += bought;
                    city->goods[w] = static_cast<std::uint32_t>(stock);
                }
                advance_city(copy, m_buildings, *city, out.running_cost, workers, nullptr);
            }
            advance_houses(copy, m_buildings, Simulation::kTickMs, nullptr);
        }
        ProjectedMinute pm;
        pm.stock = city->goods;
        for (const City::Supply& s : city->supply) {
            pm.ratio.push_back(s.ratio);
        }
        pm.inhabitants = city->inhabitants;
        pm.satisfaction = city->satisfaction;
        pm.taxes = tax_income(*city, m_buildings);
        for (std::size_t w = 0; w < traded.size(); ++w) {
            // At the window's prices: its gold per ton sold and bought.
            const FlowTotals& t = window[w];
            if (t.of(GoodsFlow::Sold) != 0) {
                pm.trade += traded[w].sold * static_cast<std::int64_t>(t.of(GoodsFlow::SoldValue)) /
                            static_cast<std::int64_t>(t.of(GoodsFlow::Sold));
            }
            if (t.of(GoodsFlow::Bought) != 0) {
                pm.trade -= traded[w].bought * static_cast<std::int64_t>(t.of(GoodsFlow::BoughtValue)) /
                            static_cast<std::int64_t>(t.of(GoodsFlow::Bought));
            }
        }
        out.minutes.push_back(std::move(pm));
    }
    return out;
}

void add_city_gold(std::vector<std::int64_t>& gold, const CityProjection& f) {
    // Each city adds its own running sum; summing onto gold[m - 1] would drop the cities before.
    std::int64_t sum = 0;
    for (std::size_t m = 1; m < gold.size(); ++m) {
        if (m <= f.minutes.size()) {
            sum += f.minutes[m - 1].taxes - f.running_cost + f.minutes[m - 1].trade;
        }
        gold[m] += sum;
    }
}

}  // namespace o1602::sim::ai
