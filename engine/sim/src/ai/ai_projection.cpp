// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602 (P2.4): a computer city projected minute by minute. The copy runs the game's own steps
// (advance_city, advance_houses) for the city alone, with the ledger's inflow of its goods: the
// supply levels, satisfaction, growth, ascents and descents follow each other as in the game.

#include <algorithm>
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
    // The inflow of each ware a minute: all that came in and went out but what the inhabitants
    // ate, over the budget's horizon (in 1/32 t, with what a minute leaves over carried on).
    const std::uint32_t horizon = m_world.balance().ai_budget_horizon_minutes;
    std::vector<std::int64_t> inflow(city->goods.size(), 0);
    std::vector<std::int64_t> carried(city->goods.size(), 0);
    for (std::size_t w = 0; w < city->goods.size(); ++w) {
        const auto ware = static_cast<std::uint8_t>(w);
        if (!city->ledger.used(ware)) {
            continue;
        }
        const FlowTotals t = city->ledger.totals(ware, horizon);
        if (t.minutes != 0) {
            inflow[w] = (t.net() + static_cast<std::int64_t>(t.of(GoodsFlow::Residents))) /
                        static_cast<std::int64_t>(t.minutes);
        }
    }
    std::uint32_t city_ms = m_sim.m_city_ms;
    out.minutes.reserve(minutes);
    for (std::uint32_t m = 0; m < minutes; ++m) {
        for (std::uint32_t ms = 0; ms < kCityTicksPerMinute * kCityTickMs; ms += Simulation::kTickMs) {
            city_ms += Simulation::kTickMs;
            if (city_ms >= kCityTickMs) {
                city_ms -= kCityTickMs;
                // A sixth of the minute's inflow before the city takes its goods, as the producers'
                // goods come before the city tick.
                for (std::size_t w = 0; w < inflow.size(); ++w) {
                    if (inflow[w] == 0) {
                        continue;
                    }
                    carried[w] += inflow[w];
                    const std::int64_t now = carried[w] / kCityTicksPerMinute;
                    carried[w] -= now * kCityTicksPerMinute;
                    const std::int64_t stock = std::clamp<std::int64_t>(std::int64_t{city->goods[w]} + now, 0,
                                                                        std::max(city->capacity, city->goods[w]));
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
        out.minutes.push_back(std::move(pm));
    }
    return out;
}

}  // namespace o1602::sim::ai
