// SPDX-License-Identifier: GPL-3.0-or-later
//
// Improved computers spend their gold from one budget (ai_overview.hpp): a plan that costs gold
// is made only while the gold, less what plans under way set aside, stays above a cushion now,
// when the plan begins to pay and at the horizon. The computer reckons with what it sees in its
// own statistics windows, nothing else.
#include <algorithm>
#include <array>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/goods_statistics.hpp"

namespace o1602::sim::ai {

std::int64_t Budget::cushion() const noexcept {
    return std::int64_t{cushion_minutes} * std::max<std::int64_t>(0, outflow - reliable);
}

std::int64_t Budget::at(const Commitment& c, std::uint32_t minute) const noexcept {
    const std::int64_t m = minute;
    const std::int64_t paying = std::max<std::int64_t>(0, m - std::int64_t{c.delay});
    return cash - reserved - c.cost + m * (net - c.upkeep) + paying * c.value;
}

Refusal Budget::refusal(const Commitment& c) const noexcept {
    const std::int64_t keep = cushion();
    if (at(c, 0) < keep) {
        return Refusal::Now;
    }
    if (at(c, std::min(c.delay, horizon)) < keep) {
        return Refusal::Delivery;
    }
    return at(c, horizon) < keep ? Refusal::Horizon : Refusal::None;
}

bool Budget::allows(const Commitment& c) const noexcept {
    return refusal(c) == Refusal::None;
}

Budget Engine::gold_budget(const AiPlayer& p) const {
    const Balance& bal = m_world.balance();
    Budget b;
    b.cash = player(p).money;
    b.reserved = reserved(p);
    b.horizon = bal.ai_budget_horizon_minutes;
    b.cushion_minutes = bal.ai_budget_cushion_minutes;
    // The flows change with the minutes, not with each plan of a step: worked out once a step.
    if (m_budgets.size() <= p.player) {
        m_budgets.resize(std::size_t{p.player} + 1);
    }
    BudgetCache& cache = m_budgets[p.player];
    if (!cache.valid || cache.ms != now_ms()) {
        cache.valid = true;
        cache.ms = now_ms();
        cache.net = earned_income(p);
        cache.outflow = outflow(p);
        cache.reliable = reliable_income(p);
        // A young colony's expected costs stay in the projection until its statistics show them.
        const std::int64_t young = young_colonies_gap(p);
        cache.net -= young;
        cache.outflow += young;
    }
    b.net = cache.net;
    b.outflow = cache.outflow;
    b.reliable = cache.reliable;
    return b;
}

std::int64_t Engine::outflow(const AiPlayer& p) const {
    // What the computer pays out a minute without the trade it could do without: the running costs
    // of its cities, its army's upkeep and the inhabitants' goods it bought, a minute over the
    // horizon's last minutes (the tab "Handel").
    std::int64_t sum = military_cost(p);
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        if (m_world.cities()[i].owner == p.player) {
            sum += costs[i];
        }
    }
    std::array<bool, 256> eaten{};
    eaten[m_buildings.food_ware()] = true;
    for (const assets::ResidentClass& k : m_buildings.resident_classes()) {
        for (const assets::ResidentClass::Need& n : k.needs) {
            eaten[n.ware] = true;
        }
    }
    const GoodsScope scope{p.player, std::nullopt};
    for (std::size_t w = 0; w < eaten.size(); ++w) {
        if (eaten[w]) {
            const FlowTotals t =
                goods_totals(m_world, scope, static_cast<std::uint8_t>(w), m_world.balance().ai_budget_horizon_minutes);
            if (t.minutes != 0) {
                sum += static_cast<std::int64_t>(t.of(GoodsFlow::BoughtValue) / t.minutes);
            }
        }
    }
    return sum;
}

std::int64_t Engine::reliable_income(const AiPlayer& p) const {
    // The taxes a computer can count on: those of its cities whose inhabitants have their food. A
    // growing city whose taxes pay its outflow keeps no cushion; one whose taxes would fall with a
    // famine keeps what the outflow takes beyond the rest.
    std::int64_t sum = 0;
    for (const City& c : m_world.cities()) {
        if (c.owner == p.player && population(c) > 0 && !food_short(c)) {
            sum += tax_income(c, m_buildings);
        }
    }
    return sum;
}

std::int64_t Engine::class_taxes(const City& city, std::size_t k, std::uint64_t people) const {
    const auto& classes = m_buildings.resident_classes();
    const std::int64_t rate = k < city.tax_rate.size() ? city.tax_rate[k] : kFullSupply;
    return static_cast<std::int64_t>(std::uint64_t{classes[k].tax} * people * 6 / 32) * rate / kFullSupply;
}

bool Engine::young_colony(const City& c) const {
    // A city whose statistics do not yet cover the horizon: its build-out and supply still come.
    return c.ledger.minutes_closed() < m_world.balance().ai_budget_horizon_minutes;
}

std::int64_t Engine::young_colonies_gap(const AiPlayer& p) const {
    // What the young colonies will cost a minute beyond what they cost now: the upkeep their
    // founding was weighed with (colony_commitment, from the grown colonies) less their deficit
    // now. Without it the budget saw a fresh colony at its Kontor's running costs and founded the
    // next one minutes later; five within 80 minutes took a computer into debt.
    if (!rivals() || p.pirates) {
        return 0;
    }
    std::int64_t expected = colony_deficit(p);
    if (const assets::BuildingType* b = by_id(id::kontor(1)); b != nullptr) {
        expected = std::max<std::int64_t>(expected, b->running_cost);
    }
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    std::int64_t gap = 0;
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        const City& c = m_world.cities()[i];
        if (c.owner != p.player || is_home(p, c) || !young_colony(c)) {
            continue;
        }
        const std::int64_t now = std::int64_t{costs[i]} - tax_income(c, m_buildings);
        gap += std::max<std::int64_t>(0, expected - now);
    }
    return gap;
}

std::int64_t Engine::reserved(const AiPlayer& p) const {
    // A founding under way keeps the colony's gold until its Kontor stands (colony_savings): the
    // ship that is called home to load or sails with the load.
    if (!rivals() || p.pirates) {
        return 0;
    }
    // G4b: and from when the colony is planned (colony_plan).
    return p.settle.active || p.settle.ship != 0 || p.settle.plan_crop != 0 ? std::int64_t{colony_money(p)} : 0;
}

bool Engine::can_commit(const AiPlayer& p, const Commitment& c, Spend what) const {
    const Budget b = gold_budget(p);
    const Refusal r = b.refusal(c);
    if (r != Refusal::None && logging()) {
        // Logged once a game minute per kind of plan, to measure which check refuses what.
        static constexpr std::array<const char*, kSpends> kWhat{
            "building", "ship", "colony", "room colony", "goal colony", "unit", "tower", "war", "army"};
        static constexpr std::array<const char*, 4> kWhy{"", "now", "delivery", "horizon"};
        if (m_refusals_logged.size() <= p.player) {
            m_refusals_logged.resize(std::size_t{p.player} + 1);
        }
        std::uint64_t& last = m_refusals_logged[p.player][static_cast<std::size_t>(what)];
        const std::uint64_t minute = now_ms() / 60'000 + 1;
        if (last != minute) {
            last = minute;
            log(p,
                "budget refuses %s (%s): cash %lld reserved %lld cost %lld upkeep %lld value %lld net %lld "
                "cushion %lld war %d at0 %lld atD %lld atH %lld",
                kWhat[static_cast<std::size_t>(what)], kWhy[static_cast<std::size_t>(r)],
                static_cast<long long>(b.cash), static_cast<long long>(b.reserved), static_cast<long long>(c.cost),
                static_cast<long long>(c.upkeep), static_cast<long long>(c.value), static_cast<long long>(b.net),
                static_cast<long long>(b.cushion()), 0, static_cast<long long>(b.at(c, 0)),
                static_cast<long long>(b.at(c, std::min(c.delay, b.horizon))),
                static_cast<long long>(b.at(c, b.horizon)));
        }
    }
    return r == Refusal::None;
}

std::int64_t Engine::price_paid(const AiPlayer& p, std::uint8_t w, std::uint64_t amount) const {
    // What `amount` (1/32 t) of the good costs bought: at the price its cities paid over the
    // horizon, else at the price every Kontor starts with.
    const FlowTotals t =
        goods_totals(m_world, GoodsScope{p.player, std::nullopt}, w, m_world.balance().ai_budget_horizon_minutes);
    if (const std::uint64_t bought = t.of(GoodsFlow::Bought); bought != 0) {
        return static_cast<std::int64_t>(amount * t.of(GoodsFlow::BoughtValue) / bought);
    }
    return static_cast<std::int64_t>(amount * base_price(w) / kTon);
}

std::int64_t Engine::gate_value(const AiPlayer& p, std::uint8_t good) const {
    // What the home city gains a minute once it has `good`, which its cities make too little of: the
    // taxes of half the class below each class that asks for it beyond the class below (as
    // ascent_extra counts the rise), less what those risen inhabitants' other new goods cost bought
    // where the computer's cities do not make them.
    if (!p.home_valid || good == 0 || goods_ahead(p.player, good) == 0) {
        return 0;
    }
    const City* home = city(p.home_island, p.home_city);
    if (home == nullptr || home->owner != p.player) {
        return 0;
    }
    const auto& classes = m_buildings.resident_classes();
    const auto taxes = [&](std::size_t k, std::uint64_t people) -> std::int64_t {
        return class_taxes(*home, k, people);
    };
    std::int64_t value = 0;
    for (std::size_t k = 1; k < classes.size() && k - 1 < home->inhabitants.size(); ++k) {
        if (ascent_extra(*home, k, good) == 0) {
            continue;
        }
        const std::uint64_t risers = home->inhabitants[k - 1] / 2;
        value += taxes(k, risers) - taxes(k - 1, risers);
        for (const assets::ResidentClass::Need& n : classes[k].needs) {
            if (n.ware == good) {
                continue;
            }
            const std::uint32_t more = ascent_extra(*home, k, n.ware);
            const std::uint32_t bought = std::min(more, goods_ahead(p.player, n.ware));
            value -= price_paid(p, n.ware, bought);
        }
    }
    return value;
}

namespace {

/// The good a colony's crop is grown for, 0 for one that opens no class (ore, gold, cotton).
std::uint8_t crop_good(std::uint8_t crop) {
    switch (crop) {
        case ware::kWineField:
        case ware::kSugarField:
            return ware::kAlcohol;
        case ware::kTobaccoField:
            return ware::kTobaccoGoods;
        case ware::kSpiceField:
            return ware::kSpices;
        case ware::kCocoaField:
            return ware::kCocoa;
        default:
            return 0;
    }
}

}  // namespace

Commitment Engine::colony_commitment(const AiPlayer& p, std::uint8_t crop) const {
    // A colony costs its gold now and, a minute, what one of the computer's colonies costs beyond
    // its taxes (at least its Kontor's running costs); it brings what its crop's good opens in the
    // home city (gate_value), once it delivers, half the horizon from now.
    Commitment c;
    c.cost = colony_money(p);
    std::int64_t kontor = 0;
    if (const assets::BuildingType* b = by_id(id::kontor(1)); b != nullptr) {
        kontor = b->running_cost;
    }
    c.upkeep = std::max(colony_deficit(p), kontor);
    c.value = gate_value(p, crop_good(crop));
    // Interim: half the horizon. When a colony delivers follows from its Kontor's and fields' build
    // time, the crop's cycle and the sail home, which are not known before the island is chosen.
    c.delay = m_world.balance().ai_budget_horizon_minutes / 2;
    return c;
}

}  // namespace o1602::sim::ai
