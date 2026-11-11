// SPDX-License-Identifier: GPL-3.0-or-later
//
// The city planner of the computer players: task 5 refreshes the
// current city (Kontor settings, production switches, money) or runs the handler of its stage,
// which asks a fixed list of Needs and queues the first building that is missing.
#include <algorithm>
#include <array>
#include <limits>
#include <span>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/mission.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint64_t kRefreshMs = 5100;  // a refresh every 5.1 s
constexpr std::size_t kLockFromClass = 2;   // the citizens
constexpr std::int64_t kLockMoney = 0;      // improved computers lock the material only in debt
constexpr std::int64_t kUnlockMoney = 3000;
constexpr std::int64_t kLeanMoney = -1000;      // improved computers below this cut running costs
constexpr std::int64_t kShedMoney = -5000;      // ... and below this tear down producers nobody needs,
constexpr std::size_t kShedPerVisit = 4;        // ... a few per visit of the city
constexpr std::int64_t kShedFarMoney = -10000;  // ... and below this whatever they refund

}  // namespace

// ---------------------------------------------------------------------------------------------
// Task 5

void Engine::task_develop(AiPlayer& p) {
    if (now_ms() - p.refresh_ms >= kRefreshMs) {
        refresh_city(p);
        return;  // the task stays; the next step runs the stage handler
    }
    pop(p);
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || peaceful(p)) {
        return;
    }
    if (p.yard_wanted) {
        p.yard_wanted = false;
        if (!p.yard_valid && need_shipyard(p)) {
            return;
        }
    }
    run_stage(p, *c, *r);
}

Cheats Engine::cheats() const {
    return cheats_of(m_world.balance());
}

void Engine::tutorial_money(AiPlayer& p) {
    // At most once a second, money below 3791 gets 379.
    if (now_ms() - p.refresh_ms <= 1000) {
        return;
    }
    p.refresh_ms = now_ms();
    Player& pl = player(p);
    if (pl.money < cheats().steady_below) {
        pl.money = money_plus(pl.money, std::int64_t{cheats().steady_money});
    }
}

bool Engine::peaceful(AiPlayer& p) {
    // A computer a scenario keeps quiet builds nothing while every
    // human holds a peace treaty with it and it likes each of them at least -30. Once one does not,
    // the switch goes for good.
    Player& self = player(p);
    if (!self.scenario.has(scenario_flag::kPassive)) {
        return false;
    }
    const Diplomacy& d = m_world.diplomacy();
    for (std::size_t h = 0; h < m_world.players().size(); ++h) {
        const Player& other = m_world.players()[h];
        if (other.role != PlayerRole::Human || other.status != PlayerStatus::Playing) {
            continue;
        }
        const auto b = static_cast<PlayerId>(h);
        if (d.relation(p.player, b).peace != Treaty::Signed || d.mood(m_world, p.player, b, 0, now_ms()) < -30) {
            self.scenario.flags &= ~scenario_flag::kPassive;
            log(p, "the scenario's peace is over");
            return false;
        }
    }
    return true;
}

void Engine::refresh_city(AiPlayer& p) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c != nullptr && r != nullptr) {
        buy_settings(p, *c, *r);
        sell_settings(p, *c, *r);
        production_switches(p, *c);
        shed_producers(p, *c);
        tax_rates(p, *c);
        material_lock(p, *c);
    }
    p.refresh_ms = now_ms();
    // The original's steady money and subsidy (cheats), on every difficulty; improved computers
    // live from what they earn, as a player does.
    if (!gifts()) {
        return;
    }
    Player& pl = player(p);
    if (pl.money < cheats().steady_below) {
        pl.money = money_plus(pl.money, std::int64_t{cheats().steady_money});
    }
    if (!threatened(p) && behind(p)) {
        subsidy(p, 1);
    }
}

void Engine::subsidy(AiPlayer& p, std::int32_t m) {
    // Money and goods into the current city while the computer is behind.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || !gifts()) {
        return;
    }
    Player& pl = player(p);
    if (pl.money < cheats().subsidy_poor_below) {
        pl.money = money_plus(pl.money, static_cast<std::int64_t>(cheats().subsidy_poor * m));
    } else if (pl.money < cheats().subsidy_below && civ_level(p) >= 1) {
        pl.money = money_plus(pl.money, static_cast<std::int64_t>(cheats().subsidy * m));
    }
    const auto& mat = m_buildings.materials();
    const auto u = static_cast<std::uint32_t>(m);
    if (p.score >= 1 && p.present[kSlotWoodcutter] && c->stock(mat.wood) < cheats().subsidy_wood * kStockUnitsPerTon) {
        add_goods(*c, mat.wood, cheats().subsidy_wood * kStockUnitsPerTon * u);
    }
    if (p.score >= 2 && c->stock(mat.tools) < cheats().subsidy_tools * kStockUnitsPerTon) {
        add_goods(*c, mat.tools, cheats().subsidy_tools * kStockUnitsPerTon * u);
    }
    // The bricks of the original need a score of 20, which never comes.
    if (population(*c, 2) >= 50) {
        for (const ComputerSlot& k : slots()) {
            if (k.subsidy_tons != 0 && r->slots[k.number].count > k.subsidy_above &&
                c->stock(k.subsidy_ware) < 3 * kTon &&
                (k.subsidy_people == 0 || population(*c, k.subsidy_class) >= k.subsidy_people)) {
                add_goods(*c, k.subsidy_ware, k.subsidy_tons * kTon * u);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Kontor settings and production switches

void Engine::buy_settings(AiPlayer& p, City& c, CityPlan& r) {
    const std::uint32_t level = kontor_level(c);
    if (level == 0 || !c.warehouse_x || !c.warehouse_y) {
        return;
    }
    const Tile k = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    const std::uint32_t max_entries = k.valid() ? k.type->max_wares2 : 0;
    struct Want {
        std::uint8_t ware;
        std::uint32_t limit;
    };
    std::vector<Want> wanted;
    std::uint32_t counted = 0;
    const auto want = [&](std::uint8_t w, std::uint32_t target, bool counts = true) {
        if (counts && counted >= max_entries) {
            return;
        }
        if (std::any_of(wanted.begin(), wanted.end(), [&](const Want& x) { return x.ware == w; })) {
            return;  // already wanted, by an improved computer's need
        }
        if (c.stock(w) < target) {
            wanted.push_back({w, target + target / 4});
            counted += counts ? 1U : 0U;
        }
    };
    // Luxury goods by demand: ten minutes' worth, at least 21 t (below 20 t it is raised, as in the original).
    const auto lux = [&](std::uint8_t w, std::uint32_t factor) {
        std::uint32_t t = factor * demand(c, w);
        if (t < 20 * kTon) {
            t = 21 * kTon;
        }
        return t;
    };
    const std::uint8_t food = m_buildings.food_ware();
    const auto& m = m_buildings.materials();
    const std::uint32_t civ = civ_level(p);
    if (r.stage != stage::kColony) {
        want(food, std::max(8 * demand(c, food), std::uint32_t{8 * kTon}));
    }
    want(m.tools, (population(c) >= 2 && r.slots[kSlotToolsmith].count == 0) ? 20U * kTon : 10U * kTon);
    if (r.slots[kSlotSmelter].count < 2 && c.stock(m.wood) < 20 * kTon && counted < max_entries) {
        wanted.push_back({m.wood, 25 * kTon});
        ++counted;
    }
    if (r.slots[kSlotStonemason].count < 2) {
        want(m.bricks, (civ + 1) * 10 * kTon);
    }
    if (population(c) >= 25) {
        want(ware::kCloth, std::max(8 * demand(c, ware::kCloth), std::uint32_t{10 * kTon}));
    }
    if (rivals()) {
        // Open1602: an improved computer buys the goods its people use and it makes too little of
        // as soon as they use them, the one that runs out first ahead, before ore and iron take the
        // Kontor's few buying places: alcohol came last and its settlers went without for hours.
        struct Lack {
            std::uint8_t ware;
            std::uint64_t lasts;  ///< stock * 256 / demand
        };
        std::vector<Lack> lacks;
        for (const std::uint8_t w :
             {ware::kAlcohol, ware::kTobaccoGoods, ware::kSpices, ware::kCocoa, ware::kClothes}) {
            const std::uint32_t used = demand(c, w);
            if (used != 0 && supply(c, w) < used) {
                lacks.push_back({w, std::uint64_t{c.stock(w)} * 256 / used});
            }
        }
        std::stable_sort(lacks.begin(), lacks.end(), [](const Lack& a, const Lack& b) { return a.lasts < b.lasts; });
        for (const Lack& l : lacks) {
            want(l.ware, lux(l.ware, 10));
        }
    }
    if (population(c, 1) >= 170) {
        if (player(p).money > 2000) {
            // Open1602: with its veins running low, twice as much in store before they are gone.
            const std::uint32_t more = ore_running_out(p) ? 2U : 1U;
            want(ware::kOre, more * (r.slots[kSlotSmelter].count != 0 ? 15U : 10U) * kTon);
            want(ware::kIron, more * (r.slots[kSlotToolsmith].count != 0 ? 15U : 10U) * kTon);
        }
        want(ware::kSpices, lux(ware::kSpices, 10));
        want(ware::kTobaccoGoods, lux(ware::kTobaccoGoods, 10));
        want(ware::kAlcohol, lux(ware::kAlcohol, 10));
    }
    if (population(c, 2) >= 350) {
        want(ware::kCocoa, lux(ware::kCocoa, 10));
    }
    if (population(c, 3) >= 400) {
        want(ware::kClothes, lux(ware::kClothes, 10));
        if (c.stock(ware::kGold) < 11 * kTon && counted < max_entries) {
            wanted.push_back({ware::kGold, 13 * kTon + 3 * kTon / 4});
            ++counted;
        }
        want(ware::kJewellery, lux(ware::kJewellery, 4), false);
    }
    for (const std::uint8_t w : m_world.goods().trade_wares()) {
        const City::Trade* t = c.trade_of(w);
        const auto it = std::find_if(wanted.begin(), wanted.end(), [&](const Want& x) { return x.ware == w; });
        TradeSetting s;
        s.player = p.player;
        s.island = c.island;
        s.city = c.index;
        s.ware = w;
        if (it == wanted.end()) {
            if (t != nullptr && t->buy) {
                s.mode = TradeSetting::Mode::None;
                execute(s);
            }
            continue;
        }
        // The original writes the limit only when it switches buying on (S-101); Open1602 keeps it
        // up to date. The original lets ships sell beyond the storage room (S-075), Open1602 does
        // not: a limit above the room would show the ware as wanted to visiting ships although
        // the full Kontor takes none, so it never asks for more than the room.
        const std::uint32_t limit = c.capacity != 0 ? std::min(it->limit, c.capacity) : it->limit;
        if (t == nullptr || !t->buy || t->limit != limit) {
            s.mode = TradeSetting::Mode::Buy;
            s.amount = limit;
            execute(s);
        }
    }
}

void Engine::sell_settings(AiPlayer& p, City& c, CityPlan& r) {
    (void)r;
    if (!player(p).unlocked(infra::kLevel2C)) {
        return;
    }
    if (kontor_level(c) == 0 || !c.warehouse_x || !c.warehouse_y) {
        return;
    }
    const Tile k = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    const std::uint32_t max_entries = k.valid() ? k.type->max_wares : 0;
    struct Offer {
        std::uint8_t ware;
        std::uint32_t keep;
    };
    std::vector<Offer> offers;
    std::uint32_t counted = 0;
    const auto offer = [&](std::uint8_t w, std::uint32_t keep, std::uint32_t above, bool counts = true) {
        if (counts && counted >= max_entries) {
            return;
        }
        const City::Trade* t = c.trade_of(w);
        if (t != nullptr && t->buy) {
            return;  // never sells what it buys
        }
        if (c.stock(w) > above) {
            offers.push_back({w, keep});
            counted += counts ? 1U : 0U;
        }
    };
    // Open1602: an improved rival keeps also what its houses will use once they rise (demand_ahead),
    // the store ascent_ready waits for.
    const auto by_demand = [&](std::uint8_t w) {
        return std::max(15 * (rivals() ? demand_ahead(c, w) : demand(c, w)), std::uint32_t{17 * kTon});
    };
    const std::uint32_t civ = civ_level(p);
    struct Lux {
        std::uint8_t ware;
        std::uint8_t slot;
    };
    constexpr std::array<Lux, 5> kLux{{{ware::kTobaccoGoods, kSlotTobaccoGoods},
                                       {ware::kSpices, kSlotSpices},
                                       {ware::kAlcohol, kSlotAlcohol},
                                       {ware::kCocoa, kSlotCocoa},
                                       {ware::kClothes, kSlotTailor}}};
    for (const Lux& l : kLux) {
        if (p.present[l.slot]) {
            const std::uint32_t keep = by_demand(l.ware);
            offer(l.ware, keep, keep + 8 * kTon);
        }
    }
    const auto& m = m_buildings.materials();
    if (p.present[kSlotToolsmith]) {
        const std::uint32_t keep = (civ + 1) * 15 * kTon;
        offer(m.tools, keep, keep + 8 * kTon);
    }
    offer(ware::kOre, (civ + 1) * 10 * kTon, (civ + 1) * 10 * kTon + 8 * kTon);
    offer(ware::kIron, (civ + 1) * 15 * kTon, (civ + 1) * 15 * kTon + 8 * kTon);
    offer(ware::kGold, 25 * kTon, 30 * kTon);
    offer(ware::kJewellery, 25 * kTon, 30 * kTon);
    offer(ware::kCloth, 17 * kTon, 25 * kTon);
    offer(m.bricks, (civ + 1) * 15 * kTon, (civ + 1) * 15 * kTon + 8 * kTon);
    offer(m.wood, (civ + 1) * 15 * kTon, (civ + 1) * 15 * kTon + 8 * kTon);
    const std::uint8_t food = m_buildings.food_ware();
    offer(food, by_demand(food), by_demand(food) + 8 * kTon, false);
    for (const std::uint8_t w : m_world.goods().trade_wares()) {
        const City::Trade* t = c.trade_of(w);
        const auto it = std::find_if(offers.begin(), offers.end(), [&](const Offer& x) { return x.ware == w; });
        TradeSetting s;
        s.player = p.player;
        s.island = c.island;
        s.city = c.index;
        s.ware = w;
        if (it == offers.end()) {
            if (t != nullptr && t->sell) {
                s.mode = TradeSetting::Mode::None;
                execute(s);
            }
            continue;
        }
        if (t == nullptr || !t->sell || t->keep != it->keep) {
            s.mode = TradeSetting::Mode::Sell;
            s.amount = it->keep;
            execute(s);
        }
    }
}

std::int64_t Engine::stage_gold(std::uint32_t gold) const {
    // The original's computers hoard their gold before a city moves on, which the steady money
    // makes easy. Improved computers live from their taxes and trade and move on with a part of it,
    // as a player does, who puts what comes in back into the city.
    return rivals() ? std::int64_t{gold} * m_world.balance().ai_rival_money_percent / 100 : std::int64_t{gold};
}

void Engine::tax_rates(AiPlayer& p, City& c) {
    // Improved computers set their taxes as a player does: as high as each class stays fully
    // content with a step to spare, so growth never stops for it. Short of money, a class that is
    // fully supplied pays up to where it is just content enough not to move out; with money to
    // spare, a class short of goods pays less so that it still grows. The original's computers
    // keep the normal rate.
    if (!rivals()) {
        return;
    }
    constexpr std::uint8_t kNormal = 128;
    constexpr std::uint8_t kStep = 8;
    constexpr std::uint8_t kStable = 112;  // above the 107 below which houses lose people
    const auto& classes = m_buildings.resident_classes();
    const std::int64_t money = player(p).money;
    // Only in debt does a computer tax its people below full content: a city at 120 stops growing,
    // and with little gold but no debt it kept that rate for hours.
    const bool broke = money < 0 && net_income(p) < 0;
    for (std::size_t k = 0; k < classes.size() && k < c.tax_rate.size() && k < c.inhabitants.size(); ++k) {
        if (c.inhabitants[k] == 0) {
            continue;
        }
        const auto content = [&](std::uint32_t rate) {
            return satisfaction_of(c, classes[k], static_cast<std::uint8_t>(std::min<std::uint32_t>(rate, 255)));
        };
        std::uint32_t rate = CityOrder::kMinTax;
        for (std::uint32_t r = CityOrder::kMinTax; r <= CityOrder::kMaxTax; r += kStep) {
            if (content(r + kStep) >= kFullSupply) {
                rate = r;
            }
        }
        if (rate < kNormal) {
            rate = money >= 4000 ? std::max<std::uint32_t>(rate, kNormal - 4 * kStep) : kNormal;
        }
        if (broke) {
            for (std::uint32_t r = rate + kStep; r <= CityOrder::kMaxTax; r += kStep) {
                if (content(r) >= kStable) {
                    rate = r;
                }
            }
        }
        const auto chosen = static_cast<std::uint8_t>(rate);
        if (c.tax_rate[k] != chosen) {
            c.tax_rate[k] = chosen;
            if (k < c.satisfaction.size()) {
                c.satisfaction[k] = content(chosen);
            }
            log(p, "city %u/%u: tax of class %zu at %u", c.island, c.index, k, unsigned{chosen});
        }
    }
}

void Engine::material_lock(AiPlayer& p, City& c) {
    if (!rivals()) {
        return;
    }
    // Houses that rise without the goods of their class only lose their people again, and with
    // them the taxes that pay for the city. So the material stays locked while the city has no
    // food left, or none of a good that its classes from the citizens on want and that it does
    // not make enough of itself. Only in debt: a computer with gold in
    // hand lets its city grow and builds or buys what is missing. A lock holds until the gold is back above 3000.
    if (player(p).money >= (c.material_lock ? kUnlockMoney : kLockMoney)) {
        if (c.material_lock) {
            set_material_lock(p, c, false);
        }
        return;
    }
    const std::uint8_t food = m_buildings.food_ware();
    bool lack = c.stock(food) < kTon;
    const auto& classes = m_buildings.resident_classes();
    for (std::size_t k = kLockFromClass; !lack && k < classes.size() && k < c.inhabitants.size(); ++k) {
        if (c.inhabitants[k] == 0) {
            continue;
        }
        for (const assets::ResidentClass::Need& n : classes[k].needs) {
            if (c.stock(n.ware) < kTon && supply(c, n.ware) < demand(c, n.ware)) {
                lack = true;
                break;
            }
        }
    }
    if (lack != c.material_lock) {
        set_material_lock(p, c, lack);
    }
}

void Engine::set_material_lock(AiPlayer& p, City& c, bool on) {
    CityOrder o;
    o.player = p.player;
    o.island = c.island;
    o.city = c.index;
    o.op = CityOrder::Op::MaterialLock;
    o.on = on;
    execute(o);
    log(p, "city %u/%u: material %s", c.island, c.index, on ? "locked, goods are missing" : "free again");
}

Engine::WareSet Engine::needed_wares(const AiPlayer& p, bool soon) const {
    // The wares the computer needs: building material and food, what its inhabitants want and
    // what its own buildings work into such a ware, down the chain, and the weapons while it arms
    // or is at war. With `soon` also what the next class of each of its cities will want.
    WareSet needed{};
    const auto& m = m_buildings.materials();
    for (const std::uint8_t w : {m.wood, m.tools, m.bricks, m_buildings.food_ware()}) {
        needed[w] = true;
    }
    const auto& classes = m_buildings.resident_classes();
    for (const City& o : m_world.cities()) {
        if (o.owner != p.player) {
            continue;
        }
        std::size_t top = 0;
        for (std::size_t k = 0; k < classes.size() && k < o.inhabitants.size(); ++k) {
            if (o.inhabitants[k] != 0) {
                top = k + 1;
                for (const assets::ResidentClass::Need& n : classes[k].needs) {
                    needed[n.ware] = true;
                }
            }
        }
        if (soon && top != 0 && top < classes.size()) {
            for (const assets::ResidentClass::Need& n : classes[top].needs) {
                needed[n.ware] = true;
            }
        }
    }
    // Swords always, for the army it keeps to defend itself; the rest of the weapons while it arms
    // or is at war.
    needed[ware::kSwords] = true;
    if (p.strategy.goal == goal::kMilitary || p.strategy.war_target != kNoPlayer) {
        for (const std::uint8_t w : {ware::kMuskets, m.cannons}) {
            needed[w] = true;
        }
    }
    std::vector<const assets::BuildingType*> made;  // the computer's producers, once per type
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island >= m_world.islands().size()) {
            continue;
        }
        const IslandMap& map = island_of(pr.island).map;
        if (!map.contains(pr.x, pr.y) || map.top(pr.x, pr.y).empty()) {
            continue;
        }
        const City* o = m_world.city(pr.island, map.top(pr.x, pr.y).city);
        const assets::BuildingType* b = m_buildings.by_sprite(map.top(pr.x, pr.y).sprite);
        if (o != nullptr && o->owner == p.player && b != nullptr && b->raw_ware != 0 &&
            std::find(made.begin(), made.end(), b) == made.end()) {
            made.push_back(b);
        }
    }
    for (bool more = true; more;) {
        more = false;
        for (const assets::BuildingType* b : made) {
            if (needed[b->ware] && !needed[b->raw_ware]) {
                needed[b->raw_ware] = true;
                more = true;
            }
        }
    }
    return needed;
}

bool Engine::saving_costs(const AiPlayer& p) const {
    // Deep in debt with the gold still falling, or arming with an army that is at its budget.
    if (!rivals() || p.pirates) {
        return false;
    }
    if (player(p).money < kLeanMoney && net_income(p) < 0) {
        return true;
    }
    const Strategy& st = p.strategy;
    const std::int32_t upkeep = military_cost(p);
    return (st.goal == goal::kMilitary || st.war_target != kNoPlayer) && upkeep != 0 &&
           upkeep + unit_upkeep_estimate(p) > army_budget(p);
}

void Engine::shed_producers(AiPlayer& p, City& c) {
    // Open1602: an improved computer deep in debt and still losing gold gets rid of the producers
    // of the city whose ware none of its cities needs (needed_wares). It weighs switching off
    // against tearing down as a player does: one whose ware it will want soon (the next class of a
    // city, weapons at war) only goes off (production_switches); another is torn down where the
    // idle costs it would still cost and what the game refunds outweigh building it again later
    // (worth_demolishing, by the game's refund rule), else it stays switched off. A few at a time.
    if (!rivals() || p.pirates || player(p).money >= kShedMoney || net_income(p) >= 0) {
        return;
    }
    // Far in debt nothing is kept for later: what no city needs now goes, whatever it refunds.
    const bool far = player(p).money < kShedFarMoney;
    const WareSet soon = needed_wares(p, !far);
    const std::uint32_t refund = m_sim.rules().refund_share();
    const IslandMap& map = island_of(c.island).map;
    std::vector<std::pair<std::int32_t, std::int32_t>> shed;
    for (const Production& pr : m_world.productions()) {
        if (shed.size() >= kShedPerVisit) {
            break;
        }
        if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty()) {
            continue;
        }
        const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->ware == 0 || soon[b->ware] || b->prod_kind == prod::kMarket ||
            b->prod_kind == prod::kKontor || b->prod_kind < prod::kWorkshop || b->prod_kind > prod::kFisher) {
            continue;
        }
        if (!far && !worth_demolishing(b->idle_cost, build_value(b->build_cost), refund, true)) {
            continue;  // cheaper to keep switched off
        }
        shed.emplace_back(pr.x, pr.y);
    }
    std::size_t done = 0;
    for (const auto& [x, y] : shed) {
        done += demolish_tile(p, x, y) != 0 ? 1U : 0U;
    }
    if (done != 0) {
        log(p, "city %u/%u: tears down %zu producers nobody needs in debt (gold %d)", c.island, c.index, done,
            player(p).money);
    }
}

void Engine::production_switches(AiPlayer& p, City& c) {
    // Every producer of the current city, by the ware it makes. Each check looks at the
    // building's switch first: one that is on can only go off, one that is off only on.
    // An off building is not ticked, so its count of cycles without raw
    // material stays until it is switched on again and must not keep it off.
    const IslandMap& map = island_of(c.island).map;
    const std::uint32_t cap = c.capacity;
    const auto& m = m_buildings.materials();
    std::vector<std::pair<std::uint32_t, bool>> changes;  // production index, on; in the original's order
    // Deep in debt an improved computer also saves the running costs of its stores: whatever has
    // 10 t of its ware in stock goes off and stays off until the stock is down to 4 t.
    const bool lean = rivals() && player(p).money < kLeanMoney && net_income(p) < 0;
    // Saving for its army or in debt it also switches off what none of its cities needs now
    // (needed_wares), until it can afford them again.
    const bool saving = saving_costs(p);
    const WareSet needed = saving ? needed_wares(p, false) : WareSet{};
    std::uint32_t saved = 0;
    std::uint32_t idled = 0;
    // Open1602: the food of the city and what its producers work into food (cattle, grain, flour)
    // keep running in debt; without it the people leave faster than any saving pays back.
    WareSet feeds{};
    if (rivals()) {
        feeds[m_buildings.food_ware()] = true;
        std::vector<const assets::BuildingType*> types;
        for (const Production& pr : m_world.productions()) {
            if (pr.used && pr.island == c.island && map.contains(pr.x, pr.y) && map.top(pr.x, pr.y).city == c.index &&
                !map.top(pr.x, pr.y).empty()) {
                if (const assets::BuildingType* b = m_buildings.by_sprite(map.top(pr.x, pr.y).sprite);
                    b != nullptr && b->raw_ware != 0) {
                    types.push_back(b);
                }
            }
        }
        for (bool more = true; more;) {
            more = false;
            for (const assets::BuildingType* b : types) {
                if (feeds[b->ware] && !feeds[b->raw_ware]) {
                    feeds[b->raw_ware] = true;
                    more = true;
                }
            }
        }
    }
    for (std::size_t i = 0; i < m_world.productions().size(); ++i) {
        const Production& pr = m_world.productions()[i];
        if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty()) {
            continue;
        }
        const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor ||
            b->prod_kind < prod::kWorkshop || b->prod_kind > prod::kFisher) {
            continue;
        }
        const auto index = static_cast<std::uint32_t>(i);
        bool enabled = pr.enabled;
        // Open1602: what its cities go short of (critical_short) keeps running in debt; the ships
        // carry the stock to where it is missing.
        const bool short_of = rivals() && b->ware != 0 && (feeds[b->ware] || goods_short(p.player, b->ware) != 0);
        const bool idle = saving && b->ware != 0 && !needed[b->ware];
        const bool hold = (lean && !short_of && c.stock(b->ware) >= 4 * kTon) || idle;
        const auto set = [&](bool on) {
            if (on && hold) {
                return;
            }
            changes.emplace_back(index, on);
            enabled = on;
        };
        // NoRawGoods: a Maxnorohst of 0 counts as starved at once.
        const bool starved = pr.idle_cycles >= b->max_idle_cycles;
        const std::uint8_t w = b->ware;
        const auto raw = [&] {
            // Raw materials: off when the warehouse is full, on below its room less 4 t.
            if (enabled) {
                if (c.stock(w) >= cap) {
                    set(false);
                }
            } else if (c.stock(w) + 4 * kTon < cap) {
                set(true);
            }
        };
        const auto workshop = [&] {
            // Workshops: off when starved, on with more than 4 t of raw material. A starved one with
            // raw material goes off now and on at the next refresh, which clears its idle count.
            if (enabled) {
                if (starved) {
                    set(false);
                }
            } else if (c.stock(b->raw_ware) > static_cast<std::uint32_t>(short_of ? kTon : 4 * kTon)) {
                set(true);  // Open1602: for a good its cities go short of, with the first ton
            }
        };
        const auto capped = [&](std::uint32_t limit) {
            if (enabled) {
                if (starved || c.stock(w) >= limit) {
                    set(false);
                }
            } else if (c.stock(b->raw_ware) > kTon && c.stock(w) < limit) {
                set(true);
            }
        };
        if (w == ware::kIron) {
            capped(60 * kTon);
        } else if (w == ware::kSwords || w == ware::kMuskets) {
            capped(10 * kTon);
        } else if (w == m.cannons) {
            capped(25 * kTon);
        } else if (w == m.tools) {
            capped(40 * kTon);
        } else if (b->prod_kind == prod::kWorkshop && b->raw_ware != 0 && b->raw_ware < ware::kCornField &&
                   w != m.bricks) {
            workshop();
            if (w == ware::kAlcohol) {
                raw();  // the rum distillery is asked twice, as workshop and as producer of alcohol
            }
        } else {
            raw();  // farms, pastures, mines, hunters, fishers and the workshops without a raw ware
        }
        if (lean && !short_of && enabled && c.stock(w) >= 10 * kTon) {
            set(false);
            ++saved;
        } else if (idle && enabled) {
            set(false);
            ++idled;
        }
    }
    if (saved != 0) {
        log(p, "city %u/%u: switches %u full producers off in debt", c.island, c.index, saved);
    }
    if (idled != 0) {
        log(p, "city %u/%u: switches %u producers off nobody needs now (gold %d, income %d)", c.island, c.index, idled,
            player(p).money, net_income(p));
    }
    for (const auto& [index, on] : changes) {
        const Production& pr = m_world.productions()[index];
        ProductionSwitch s;
        s.player = p.player;
        s.island = pr.island;
        s.x = pr.x;
        s.y = pr.y;
        s.on = on;
        execute(s);
    }
}

// ---------------------------------------------------------------------------------------------
// Stages and their handlers

void Engine::set_stage(AiPlayer& p, CityPlan& r, std::uint8_t s) {
    log(p, "city %u/%u: stage 0x%x -> 0x%x", r.island, r.city, r.stage, s);
    r.stage = s;
    r.stage_ms = now_ms();
    if (s > p.max_stage) {
        p.max_stage = s;
        if (s > stage::kPioneers) {
            p.home_valid = true;
            p.home_island = r.island;
            p.home_city = r.city;
        }
    }
    const std::uint8_t score = s >= stage::kMerchants2  ? 4
                               : s >= stage::kCitizens2 ? 3
                               : s >= stage::kSecond    ? 2
                               : s >= stage::kSettlers  ? 1
                                                        : 0;
    p.score = std::max(p.score, score);
}

void Engine::nothing_to_do(AiPlayer& p) {
    // With several cities the visit ends.
    if (p.cities > 1) {
        p.budget = 0;
    }
}

std::int32_t Engine::civil_income(const AiPlayer& p) const {
    // Taxes less the running costs of all own cities, per minute.
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    std::int32_t sum = 0;
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        const City& c = m_world.cities()[i];
        if (c.owner == p.player) {
            sum += tax_income(c, m_buildings) - costs[i];
        }
    }
    return sum;
}

std::int32_t Engine::military_cost(const AiPlayer& p) const {
    // What its army costs a minute; nothing for the original's computers (pays_upkeep).
    if (m_sim.figures() == nullptr) {
        return 0;
    }
    const std::vector<std::uint32_t> upkeep = military_upkeep(m_world, *m_sim.figures());
    return p.player < upkeep.size() ? static_cast<std::int32_t>(upkeep[p.player]) : 0;
}

std::int32_t Engine::unit_upkeep_estimate(const AiPlayer& p) const {
    // What one more soldier costs a minute: the average of its army, else of the unit kinds.
    const assets::FigureTable* f = m_sim.figures();
    if (f == nullptr || !pays_upkeep(m_world, player(p))) {
        return 0;
    }
    const auto counts = army_counts(p.player);
    const std::uint32_t units = counts[0] + counts[1] + counts[2] + counts[3];
    if (units != 0) {
        return std::max<std::int32_t>(1, military_cost(p) / static_cast<std::int32_t>(units));
    }
    std::uint32_t sum = 0;
    std::uint32_t n = 0;
    for (std::int32_t k = 0; k < 4; ++k) {
        if (const std::uint32_t u = f->unit_upkeep(k); u != 0) {
            sum += u;
            ++n;
        }
    }
    return n != 0 ? static_cast<std::int32_t>(sum / n) : 0;
}

std::int32_t Engine::net_income(const AiPlayer& p) const {
    return civil_income(p) - military_cost(p);
}

std::int32_t Engine::earned_income(const AiPlayer& p) const {
    // What the computer's gold does a minute with its trade: its purchases of goods for the
    // inhabitants count against it.
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(std::int64_t{net_income(p)} + trade_balance(player(p)),
                                                              std::numeric_limits<std::int32_t>::min(),
                                                              std::numeric_limits<std::int32_t>::max()));
}

bool Engine::incomes_ok(const AiPlayer& p, std::int32_t minimum) const {
    // Every inhabited city of the computer earns at least `minimum` per minute.
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        const City& c = m_world.cities()[i];
        if (c.owner == p.player && population(c) > 0 && tax_income(c, m_buildings) - costs[i] < minimum) {
            return false;
        }
    }
    return true;
}

std::int32_t Engine::land_rank(const AiPlayer& p) const {
    // -1 another own city has no land measured, 0 another has more, 1 the largest.
    const CityPlan* here = nullptr;
    for (const CityPlan& r : p.plans) {
        if (r.island == p.island && r.city == p.city) {
            here = &r;
        }
    }
    if (here == nullptr) {
        return 1;
    }
    std::int32_t rank = 1;
    for (const CityPlan& r : p.plans) {
        if (&r == here) {
            continue;
        }
        const City* c = city(r.island, r.city);
        if (c == nullptr || c->owner != p.player) {
            continue;
        }
        // Open1602: an improved computer's young colony (before its settlers) does not make the
        // older city a second one; were it given up, the older one would stay a second city for good.
        if (!classic() && r.stage < stage::kSettlers) {
            continue;
        }
        if (r.land == 0) {
            return -1;
        }
        if (r.land > here->land) {
            rank = 0;
        }
    }
    // Open1602: a city with land enough for a main city stays one, however much more another city
    // of the computer has. The original makes the city with less land a second city that grows
    // goods for the other; on large islands far apart that leaves the first city short of food
    // while its land lies empty.
    if (rank == 0 && !classic() && here->land >= kGrowFrom) {
        rank = 1;
    }
    return rank;
}

std::uint32_t Engine::luxury_in_stock(const AiPlayer& p, const City& c, const CityPlan& r, std::uint32_t level) const {
    // Luxury goods the city has enough of (4 t with an own producer, else 20 t).
    const auto t = [](bool own) { return (own ? 4U : 20U) * kTon; };
    std::uint32_t n = 0;
    if (level >= 2) {
        n += c.stock(ware::kSpices) > t(r.slots[kSlotSpices].count != 0) ? 1U : 0U;
        n += c.stock(ware::kTobaccoGoods) >= t(r.slots[kSlotTobaccoGoods].count != 0) ? 1U : 0U;
        n += c.stock(ware::kAlcohol) >= t(r.slots[kSlotWine].count != 0 || r.slots[kSlotRum].count != 0) ? 1U : 0U;
    }
    if (level >= 3) {
        n += c.stock(ware::kCocoa) >= t(r.slots[kSlotCocoa].count != 0) ? 1U : 0U;
    }
    if (level >= 4) {
        n += c.stock(ware::kClothes) >= t(p.present[kSlotTailor]) ? 1U : 0U;
        n += c.stock(ware::kJewellery) >= t(p.present[kSlotGoldsmith]) ? 1U : 0U;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// Build orders of the stages (task 5)
//
// Each stage of a city tries what it needs in a fixed order and stops at the first entry that
// finds something to do. The orders are data (computer_orders.hpp): the original's, or a package's
// from its computers.toml.

std::uint32_t Engine::grown(const CityPlan& r, std::uint32_t n, bool any_stage) const {
    if (classic() || (r.stage < stage::kSecond && !any_stage)) {
        return n;
    }
    return grown_count(r.land, n, any_stage || m_grow_from == 0 ? kGrowFrom : m_grow_from);
}

std::uint32_t Engine::houses_wanted(const City& c, const CityPlan& r, std::uint32_t n, bool home) const {
    // The home city grows from kGrowLand tiles on rather than kGrowFrom: most home islands give
    // their main city between 1000 and 2000 tiles, and it stood at the order's number of houses.
    const std::uint32_t wanted = home ? std::max(grown(r, n), grown_count(r.land, n, kGrowLand)) : grown(r, n);
    if (wanted <= n) {
        return n;
    }
    // Houses beyond the original's number only while the city's houses move up: a new house starts
    // with the pioneers, and a city that keeps building them grows wide instead of tall, its
    // goods spread over houses that never reach the next class.
    std::uint32_t houses = 0;
    std::uint32_t pioneers = 0;
    for (const House& h : m_world.houses()) {
        if (h.island == c.island && h.city == c.index) {
            ++houses;
            pioneers += h.resident_class == 0 ? 1U : 0U;
        }
    }
    return pioneers * 8 <= houses ? wanted : n;
}

bool Engine::first_need(AiPlayer& p, const City& c, const CityPlan& r, std::int32_t money,
                        std::span<const StageNeed> needs, bool grow) {
    const auto count = [&](std::uint32_t n) { return grow ? grown(r, n) : n; };
    // Open1602: an order's producers grow with the land only while the city has little of their
    // good; more would make goods nobody needs.
    const auto producers = [&](std::uint8_t slot, std::uint32_t n) {
        const ComputerSlot* k = computer_slot(slots(), slot);
        const std::uint8_t w = k == nullptr ? 0 : k->ware != 0 ? k->ware : k->count_ware;
        if (grow && !classic() && w != 0 && c.stock(w) >= kGrowStock * kStockUnitsPerTon) {
            return n;
        }
        return count(n);
    };
    const auto holds = [&](StageNeed::When w) {
        switch (w) {
            case StageNeed::When::Never:
                return false;
            case StageNeed::When::Always:
                return true;
            case StageNeed::When::Gold4000:
                return money >= stage_gold(4000);
            case StageNeed::When::Gold5000:
                return money >= stage_gold(5000);
            case StageNeed::When::Gold8000:
                return money >= stage_gold(8000);
        }
        return false;
    };
    for (const StageNeed& n : needs) {
        if (!holds(n.when)) {
            continue;
        }
        bool found = false;
        switch (n.kind) {
            case StageNeed::Kind::Building:
                found = need(p, n.slot, producers(n.slot, n.n));
                break;
            case StageNeed::Kind::Market:
                found = need_market(p, n.n);
                break;
            case StageNeed::Kind::Houses: {
                // Open1602: the home city of an improved computer that earns gold, from its
                // cities too, grows its houses with its land at once (houses_wanted): more
                // inhabitants pay more taxes. It stops while its supply does not carry more, and
                // its food and cloth grow ahead of the houses, and keep up with those it has.
                const bool home_city_here = rivals() && &c == home_city(p);
                const bool earns = home_city_here && earned_income(p) > 0 && civil_income(p) > 0 && !food_short(c);
                const bool wants_more = earns && houses_wanted(c, r, n.n, true) > r.slots[kSlotHouses].count;
                if (home_city_here && supply_ahead(p, c, r, wants_more)) {
                    found = true;
                    break;
                }
                // The same supply holds the home city once its order is through (`grow`): it went on
                // building houses whose settlers then lacked cloth and alcohol.
                const bool supplied = home_city_here ? supplies_growth(c) : true;
                const bool home = earns && supplied;
                // Nor its order's houses while a good it makes is used up and comes in short: their
                // people would only fall back (the order filled up a city whose cloth was gone).
                if (home_city_here && lacking_ware(c, true) != 0) {
                    break;
                }
                found = need_houses(p, (grow && supplied) || home ? houses_wanted(c, r, n.n, home) : n.n);
            } break;
            case StageNeed::Kind::Public:
                found = need_public(p, n.slot, count(n.n), holds(n.flag));
                break;
            case StageNeed::Kind::Kontor:
                found = need_kontor(p, n.n);
                break;
            case StageNeed::Kind::Castle:
                found = need_castle(p);
                break;
            case StageNeed::Kind::Monument:
                found = need_denkmal(p);
                break;
            case StageNeed::Kind::Shipyard:
                found = need_shipyard(p);
                break;
            case StageNeed::Kind::Processing:
                found = processing(p);
                break;
            case StageNeed::Kind::IronTools:
                found = iron_tools(p);
                break;
            case StageNeed::Kind::Weapons:
                found = weapons(p);
                break;
            case StageNeed::Kind::Luxury:
                found = luxury(p, count(n.n), holds(n.flag));
                break;
            case StageNeed::Kind::ColonyLuxury:
                found = colony_luxury(p, c, r, n.n);
                break;
            case StageNeed::Kind::Tower:
                found = need_tower(p, 1, count(n.n));
                break;
            case StageNeed::Kind::RoadsIfMason:
                found = roads_if_mason(p);
                break;
            case StageNeed::Kind::Colony:
                found = want_colony(p, static_cast<std::uint8_t>(n.n), false, false);
                break;
            case StageNeed::Kind::Package:
                found = package_order(p, c, r, n.slot, count(n.n));
                break;
            case StageNeed::Kind::Crop:
                break;  // first_crops only: nothing a stage builds
        }
        if (found) {
            if (logging()) {
                if (m_last_need.size() <= p.player) {
                    m_last_need.resize(std::size_t{p.player} + 1, -1);
                }
                m_last_need[p.player] = static_cast<std::int32_t>(n.kind) << 8 | n.slot;
            }
            return true;
        }
    }
    return false;
}

void Engine::unload_ship(AiPlayer& p, City& c) {
    if (!c.warehouse_x || !c.warehouse_y) {
        return;
    }
    for (Ship* s : own_ships(p)) {
        if (!s->target.empty() || s->total_load() == 0 || s->id == p.settle.ship) {
            continue;
        }
        if (building_in_reach(m_world, m_buildings, c.island, *c.warehouse_x, *c.warehouse_y, s->x, s->y)) {
            unload_at(c, *s, false, false);
            return;
        }
    }
}

bool Engine::colony_luxury(AiPlayer& p, const City& c, const CityPlan& r, std::uint32_t tiles_per_field) {
    // Luxury crops of a colony by the highest stage the player reached (stage 1, 0x17).
    const std::uint32_t n = r.land / tiles_per_field;
    const std::uint32_t q = full_crops(c.island);
    switch (p.max_stage) {
        case stage::kSettlers:
        case stage::kMain:
        case stage::kMain2:
            return luxury(p, std::min(n, grown(r, 4, true)), true);
        case stage::kCitizens:
            return luxury(p, std::min(n, grown(r, 6, true)), true);
        case stage::kCitizens2:
            return luxury(p, std::min(n, grown(r, 4, true)), false);
        case stage::kCitizensSmall:
        case stage::kMerchantsSmall:
        case stage::kMerchants:
        case stage::kMerchants2:
        case stage::kAristocrats:
        case stage::kAristocrats2: {
            const std::uint32_t cap = (p.max_stage == stage::kMerchants || p.max_stage == stage::kMerchants2) ? 6U
                                      : (p.max_stage == stage::kAristocrats || p.max_stage == stage::kAristocrats2)
                                          ? 10U
                                          : 8U;
            const std::uint32_t k = std::min(n, grown(r, cap, true));
            if (q == 1) {
                if (luxury(p, k, false)) {
                    return true;
                }
                const City* home = home_city(p);
                // Open1602: not for a home city with land enough to feed itself (more_food).
                if (!classic() && home != nullptr) {
                    for (const CityPlan& h : p.plans) {
                        if (h.island == home->island && h.city == home->index && h.land >= kGrowFrom) {
                            return false;
                        }
                    }
                }
                return home != nullptr && home->stock(m_buildings.food_ware()) < 8 * kTon && need(p, kSlotCattle, k);
            }
            return luxury(p, k, false);
        }
        default:
            return false;
    }
}

bool Engine::farm_animals(AiPlayer& p, const CityPlan& r) {
    // A second city without luxury crops keeps sheep and cattle, by the highest stage reached.
    const std::uint32_t n = r.land / 128;
    switch (p.max_stage) {
        case stage::kMain:
        case stage::kMain2:
            return need(p, kSlotSheep, std::min(n, grown(r, 4U, true)));
        case stage::kCitizens:
            return need(p, kSlotSheep, std::min(n, grown(r, 6U, true)));
        case stage::kCitizens2:
            return need(p, kSlotCattle, std::min(n, grown(r, 4U, true))) ||
                   need(p, kSlotSheep, std::min(n, grown(r, 4U, true)));
        case stage::kCitizensSmall:
        case stage::kMerchantsSmall:
        case stage::kAristocrats:
        case stage::kAristocrats2:
            return need(p, kSlotCattle, std::min(n, grown(r, 8U, true))) ||
                   need(p, kSlotSheep, std::min(n, grown(r, 8U, true)));
        case stage::kMerchants:
        case stage::kMerchants2:
            return need(p, kSlotCattle, std::min(n, grown(r, 6U, true))) ||
                   need(p, kSlotSheep, std::min(n, grown(r, 6U, true)));
        default:
            return false;
    }
}

void Engine::run_stage(AiPlayer& p, City& c, CityPlan& r) {
    const std::int32_t money = player(p).money;
    // When the city moves on (balance.toml [computer_stages]; the original's numbers by default).
    const Balance& at = m_world.balance();
    const auto has = [&](std::uint32_t gold) { return std::int64_t{money} >= stage_gold(gold); };
    // The stage's own Needs first, then the producers of the packages' goods its inhabitants want.
    // The lists this visit went through; once there is nothing to do, Open1602 goes through them
    // again with their numbers grown by the city's land, so that a large island fills up without
    // holding up the way to the next stage.
    std::vector<ComputerList> asked;
    const auto needs = [&](ComputerList list) {
        asked.push_back(list);
        return first_need(p, c, r, money, order_of(m_world.balance().computer_orders, list)) || package_chains(p, c, r);
    };
    // A city that stopped growing is given up after a while.
    const auto give_up = [&](std::uint32_t seconds) {
        if (now_ms() - r.stage_ms >= std::uint64_t{seconds} * 1000 && (r.flags & city_flag::kAbandon) == 0) {
            r.flags |= city_flag::kAbandon;
            if (!classic()) {
                p.left_islands.emplace_back(r.island, std::max<std::uint64_t>(now_ms(), 1));
            }
            log(p, "city %u/%u: given up, %u inhabitants after %u s at stage 0x%02x (money %d)", r.island, r.city,
                population(c), seconds, unsigned{r.stage}, money);
        }
    };
    const auto idle = [&] {
        if (!classic()) {
            // A city at its last stage that earns well grows on the original's large islands too
            // (from kGrowLand tiles); before, it would spend what its next stage needs.
            m_grow_from = r.stage == stage::kAristocrats2 && net_income(p) >= kGrowIncome ? kGrowLand : kGrowFrom;
            for (const ComputerList list : asked) {
                if (first_need(p, c, r, money, order_of(m_world.balance().computer_orders, list), true)) {
                    m_grow_from = 0;
                    return;
                }
            }
            m_grow_from = 0;
        }
        // Open1602: the home city, its orders done, settles a further island for room to grow
        // (Rules::computer_cities). Only from the home city, where the ship loads.
        // So too, improved, when the home city lacks food or cloth its island has no room for and
        // no colony makes.
        const bool short_home = &c == home_city(p) && (home_shortfall(p, m_buildings.food_ware()) != 0 ||
                                                       home_shortfall(p, ware::kCloth) != 0);
        if (&c == home_city(p) && (wants_room(p) || short_home) && want_colony(p, 0, short_home, true)) {
            return;
        }
        nothing_to_do(p);
    };

    // Open1602: a city that cannot feed its inhabitants builds food producers first. A founded city
    // and the pioneers follow their orders (the fisher before the houses); need_houses asks for
    // food there.
    if (r.stage != stage::kFounded && r.stage != stage::kPioneers && more_food(p, c, r)) {
        return;
    }
    // Open1602: a colony makes the food and cloth its home city has no room left to make.
    if (r.stage != stage::kFounded && r.stage != stage::kPioneers && &c != home_city(p) && grow_for_home(p, c, r)) {
        return;
    }
    // Open1602: an improved computer whose people want a good none of its islands grows the crop
    // for asks for the colony at every look of its home city, once the gold for it is saved
    // (colony_savings); the orders asked only at their steps, hours apart.
    if (&c == home_city(p) && colony_savings(p) != 0 && player(p).money >= colony_money(p) + buy_reserve(p) &&
        want_colony(p, 3, false, false)) {
        return;
    }
    // Open1602: a farm without room for its fields moves (move_farm_later).
    if (r.move_x >= 0 && !classic() && relocate_farm(p, c, r)) {
        return;
    }
    // Open1602: a second city with land enough for a main city becomes one (see land_rank), and so
    // does one that no longer has a larger city beside it.
    if ((r.stage == stage::kSecond || r.stage == stage::kSecondLuxury || r.stage == stage::kSecondFarming) &&
        !classic() && (r.land >= kGrowFrom || land_rank(p) == 1)) {
        set_stage(p, r, stage::kMain);
        return;
    }

    // Open1602: a colony moves its Kontor up when its room or trade list is too small (colony_kontor).
    if ((r.stage == stage::kColony || r.stage == stage::kSecond || r.stage == stage::kSecondLuxury ||
         r.stage == stage::kSecondFarming) &&
        !classic() && colony_kontor(p, c)) {
        return;
    }

    switch (r.stage) {
        case stage::kFounded: {
            unload_ship(p, c);
            r.land = free_land(c.island, c.index, &r.land_percent);
            if (population(c) >= at.ai_pioneers_inhabitants) {
                set_stage(p, r, stage::kPioneers);
                return;
            }
            std::uint32_t big_cities = 0;
            bool large = false;
            for (const City& o : m_world.cities()) {
                if (o.owner == p.player && &o != &c) {
                    big_cities += population(o) >= at.ai_colony_other_people ? 1U : 0U;
                    large = large || population(o) >= at.ai_colony_large_city;
                }
            }
            if (r.land < at.ai_colony_below_land || building_count(c) > at.ai_colony_buildings || big_cities > 1 ||
                large) {
                set_stage(p, r, stage::kColony);
                return;
            }
            if (needs(ComputerList::Founded)) {
                return;
            }
            give_up(at.ai_founded_abandon_seconds);
            return;
        }
        case stage::kColony: {
            if (needs(ComputerList::Colony)) {
                return;
            }
            if (full_crops(c.island) == 0) {
                const std::uint32_t n =
                    std::min(r.land / at.ai_colony_animal_tiles, grown(r, at.ai_colony_animals, true));
                if (need(p, kSlotSheep, n) || need(p, kSlotCattle, n)) {
                    return;
                }
            } else if (colony_luxury(p, c, r, at.ai_colony_luxury_tiles)) {
                return;
            }
            if (needs(ComputerList::ColonyMaterials)) {
                return;
            }
            idle();
            return;
        }
        case stage::kPioneers: {
            unload_ship(p, c);
            if (population(c) >= at.ai_settlers_inhabitants) {
                set_stage(p, r, stage::kSettlers);
                return;
            }
            if (needs(ComputerList::Pioneers)) {
                return;
            }
            give_up(at.ai_pioneers_abandon_seconds);
            return;
        }
        case stage::kSettlers: {
            const bool grown =
                (r.slots[kSlotHouses].count >= at.ai_grown_houses && population(c, 1) >= at.ai_grown_settlers) ||
                now_ms() - r.stage_ms > std::uint64_t{at.ai_grown_seconds} * 1000;
            if (grown) {
                // A first colony before the city grows on (the original waits for it forever if no
                // island is found, S-097; Open1602 goes on once the search has failed). Not if the
                // scenario forbids colonies.
                const bool colony_first = kontor_level(c) == 1 && p.cities < 2 && (!p.colony_failed || classic()) &&
                                          !player(p).scenario.has(scenario_flag::kNoColonies);
                // Open1602: an improved computer that cannot found its first colony yet (it has not
                // the gold) does not wait for it with its city at the settlers' houses; it moves on
                // and founds the colony later (the main city asks for one too).
                const bool waits = colony_first && (want_colony(p, 1, true, true) || classic() || p.settle.active);
                if (!waits &&
                    (has(at.ai_grown_money) || incomes_ok(p, static_cast<std::int32_t>(at.ai_grown_income)))) {
                    const std::int32_t rank = land_rank(p);
                    if (rank == 0) {
                        set_stage(p, r, stage::kSecond);
                        return;
                    }
                    if (rank == 1) {
                        set_stage(p, r, stage::kMain);
                        return;
                    }
                }
            }
            if (needs(ComputerList::Settlers)) {
                return;
            }
            if (population(c, 1) >= at.ai_settlers_luxury_settlers) {
                (void)(processing(p) || luxury(p, at.ai_settlers_luxury_fields, true));
            }
            return;
        }
        case stage::kSecond: {
            if (needs(ComputerList::Second)) {
                return;
            }
            set_stage(p, r, full_crops(c.island) != 0 ? stage::kSecondLuxury : stage::kSecondFarming);
            return;
        }
        case stage::kSecondLuxury: {
            if (needs(ComputerList::SecondLuxury)) {
                return;
            }
            idle();
            return;
        }
        case stage::kSecondFarming: {
            if (needs(ComputerList::SecondFarming) || farm_animals(p, r) || need_tower(p, 1, 6)) {
                return;
            }
            idle();
            return;
        }
        case stage::kMain: {
            if ((population(c, 1) >= at.ai_main2_settlers && has(at.ai_main2_money)) || has(at.ai_main2_rich_money)) {
                if (kontor_level(c) < 3 && p.cities < 3 && want_colony(p, 2, false, true)) {
                    return;
                }
                set_stage(p, r, stage::kMain2);
                return;
            }
            if (needs(ComputerList::Main) || needs(ComputerList::MainRich)) {
                return;
            }
            if (has(at.ai_main_idle_money)) {
                idle();
            }
            return;
        }
        case stage::kMain2: {
            // Open1602: an improved computer whose houses rose to citizens already, with an income,
            // moves on with them: before, the stage could trail the houses by hours, waiting for gold
            // or a luxury good in store while its lists built nothing new.
            const bool risen = rivals() && population(c, 2) >= at.ai_citizens2_citizens / 2 && net_income(p) > 0 &&
                               std::int64_t{money} >= stage_gold(at.ai_citizens_money) / 4;
            if (risen ||
                (has(at.ai_citizens_money) && !want_colony(p, 2, false, true) &&
                 (luxury_in_stock(p, c, r, 2) > at.ai_citizens_luxury || kontor_level(c) > at.ai_citizens_kontor))) {
                set_stage(p, r, stage::kCitizens);
                return;
            }
            if (needs(ComputerList::Main2)) {
                return;
            }
            idle();
            return;
        }
        case stage::kCitizens: {
            if ((population(c, 2) >= at.ai_citizens2_citizens && has(at.ai_citizens2_money)) ||
                has(at.ai_citizens2_rich_money)) {
                set_stage(p, r, stage::kCitizens2);
                return;
            }
            if (needs(ComputerList::Citizens)) {
                return;
            }
            idle();
            return;
        }
        case stage::kCitizens2: {
            if ((population(c, 2) >= at.ai_merchants_citizens && has(at.ai_merchants_money)) ||
                has(at.ai_merchants_rich_money)) {
                set_stage(p, r, r.land < at.ai_merchants_land ? stage::kCitizensSmall : stage::kMerchants);
                return;
            }
            if (needs(ComputerList::Citizens2)) {
                return;
            }
            idle();
            return;
        }
        case stage::kCitizensSmall: {
            if (needs(ComputerList::CitizensSmall)) {
                return;
            }
            idle();
            return;
        }
        case stage::kMerchants: {
            if (population(c, 3) >= at.ai_merchants2_merchants || kontor_level(c) >= at.ai_merchants2_kontor) {
                set_stage(p, r, stage::kMerchants2);
                return;
            }
            if (needs(ComputerList::Merchants)) {
                return;
            }
            if (luxury_in_stock(p, c, r, 3) > 2 && needs(ComputerList::MerchantsLuxury)) {
                return;
            }
            if (needs(ComputerList::MerchantsTail)) {
                return;
            }
            idle();
            return;
        }
        case stage::kMerchants2:
        case stage::kMerchantsSmall: {
            const bool small = r.stage == stage::kMerchantsSmall;
            if (!small && has(at.ai_aristocrats_money) && population(c, 3) >= at.ai_aristocrats_merchants) {
                set_stage(p, r, r.land < at.ai_aristocrats_land ? stage::kMerchantsSmall : stage::kAristocrats);
                return;
            }
            if (small ? needs(ComputerList::MerchantsSmall) || needs(ComputerList::MerchantsSmallTail)
                      : needs(ComputerList::Merchants2) || needs(ComputerList::Merchants2Tail)) {
                return;
            }
            idle();
            return;
        }
        case stage::kAristocrats: {
            if (want_colony(p, 4, false, false)) {
                return;
            }
            if (luxury_in_stock(p, c, r, 4) > at.ai_aristocrats2_luxury && has(at.ai_aristocrats2_money)) {
                set_stage(p, r, stage::kAristocrats2);
                return;
            }
            if (needs(ComputerList::Aristocrats)) {
                return;
            }
            idle();
            return;
        }
        case stage::kAristocrats2: {
            if (needs(ComputerList::Aristocrats2)) {
                return;
            }
            idle();
            return;
        }
        default:
            set_stage(p, r, stage::kFounded);
            return;
    }
}

// ---------------------------------------------------------------------------------------------
// Needs

bool Engine::plan_building(AiPlayer& p, std::uint8_t task, const assets::BuildingType& b, std::uint8_t slot, bool force,
                           bool from_start) {
    if (!push(p, task, force)) {
        return false;
    }
    start_search(p, b, slot, from_start);
    return true;
}

void Engine::reserve(AiPlayer& p, const assets::BuildingType& b, std::int64_t extra_money) {
    // The costs leave the player and the city while the site is searched. The
    // original overwrites the building's gold with the fields' gold for farms (S-096); Open1602
    // keeps both.
    refund(p);
    City* c = current_city(p);
    if (c == nullptr) {
        return;
    }
    const auto& m = m_buildings.materials();
    Reservation& r = p.reserved;
    r.island = c->island;
    r.city = c->index;
    r.money = std::int64_t{b.build_cost.money} + extra_money;
    player(p).money = money_plus(player(p).money, -std::int64_t{r.money});
    log(p, "city %u/%u: reserve %lld gold for building %u (money left %d)", r.island, r.city,
        static_cast<long long>(r.money), b.id, player(p).money);
    r.wood = remove_goods(*c, m.wood, b.build_cost.wood);
    r.bricks = remove_goods(*c, m.bricks, b.build_cost.bricks);
    r.tools = remove_goods(*c, m.tools, b.build_cost.tools);
    r.cannons = remove_goods(*c, m.cannons, b.build_cost.cannons);
}

void Engine::refund(AiPlayer& p) {
    Reservation& r = p.reserved;
    if (r.empty()) {
        return;
    }
    if (Player* pl = m_world.player(p.player)) {
        pl->money = money_plus(pl->money, std::int64_t{r.money});
    }
    if (City* c = city(r.island, r.city); c != nullptr && c->owner == p.player) {
        const auto& m = m_buildings.materials();
        add_goods(*c, m.wood, r.wood);
        add_goods(*c, m.bricks, r.bricks);
        add_goods(*c, m.tools, r.tools);
        add_goods(*c, m.cannons, r.cannons);
    }
    r = Reservation{};
}

std::uint32_t Engine::processors_fed(const City& c, const CityPlan& r, const ComputerSlot& k) const {
    if (k.process_from == kNoProducer || k.process_per == 0) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    const ComputerSlot* source = computer_slot(slots(), k.process_from);
    const std::uint8_t raw = source == nullptr ? 0 : source->ware != 0 ? source->ware : source->count_ware;
    return workshops_fed(r.slots[k.process_from].count, k.process_per, k.process_max,
                         raw != 0 && c.stock(raw) >= kGrowStock * kStockUnitsPerTon);
}

bool Engine::need(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov) {
    (void)cov;
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    // Some Needs choose another building first (ComputerSlot::instead): wool and cloth from cotton
    // where it grows fully.
    const std::vector<ComputerProducer>& changes = m_world.balance().computer_producers;
    if (const ComputerSlot* k = computer_slot(slots(), which);
        k != nullptr && k->instead != kNoProducer && fertility(c->island, k->instead_crop) == 128) {
        // With `instead_unlocked`, only once its building is unlocked.
        bool open = k->instead_unlocked == kNoProducer;
        if (!open) {
            const auto first = need_def(slots(), k->instead_unlocked, changes);
            const assets::BuildingType* b = first ? by_id(first->fixed) : nullptr;
            open = b != nullptr && unlocked(p, *b);
        }
        if (open) {
            which = k->instead;
        }
    }
    const ComputerSlot* slot = computer_slot(slots(), which);
    if (slot != nullptr && slot->deposit == ware::kOre) {
        return need_iron_mine(p, n);
    }
    if (slot != nullptr && slot->deposit == ware::kGold) {
        return need_gold_mine(p);
    }
    const auto d = need_def(slots(), which, changes);
    if (!d || slot == nullptr) {
        return false;
    }
    SlotState& s = r->slots[which];
    std::uint32_t wanted = n;
    if (slot->single_below_stock != 0) {
        // More than one only with the raw material in stock and as many producers that need it.
        std::uint32_t users = 0;
        for (const ComputerSlot& k : slots()) {
            users += k.after == which && r->slots[k.number].count != 0 ? 1U : 0U;
        }
        if (!(c->stock(slot->raw) >= slot->single_below_stock * kStockUnitsPerTon &&
              users >= slot->single_below_users)) {
            wanted = std::min<std::uint32_t>(wanted, 1);
        }
    }
    // Open1602: workshops only as many as the producers of their raw material keep busy (also a
    // mill, which the original builds without waiting for grain), unless a package's producer waits
    // for another or none.
    if (!classic() && slot->process_from != kNoProducer) {
        const ComputerProducer* own = producer_change(changes, which);
        if (own == nullptr || !own->after || *own->after == slot->process_from) {
            wanted = std::min(wanted, processors_fed(*c, *r, *slot));
        }
    }
    if (s.count >= wanted || s.attempts >= d->attempts) {
        return false;
    }
    // Processors only where their raw material is produced (NeedDef::after, a package may change
    // it). The original bounds the raw producers from above instead (ceil(raw / 2) <= n), so more
    // farms stop the workshop, and asks for bakeries even where no mill feeds them (S-098).
    const ComputerProducer* change = producer_change(changes, which);
    const bool original_bakery = which == kSlotBakery && classic() && (change == nullptr || !change->after);
    if (d->after != kNoProducer && r->slots[d->after].count == 0 && !original_bakery) {
        return false;
    }
    // Conditions besides the count that stay the original's.
    if (slot->process_max != 0 && slot->process_from != kNoProducer &&
        n > std::uint32_t{slot->process_max} * r->slots[slot->process_from].count) {
        return false;
    }
    if (slot->raw_stock != 0 && c->stock(slot->raw) <= slot->raw_stock * kStockUnitsPerTon) {
        return false;
    }
    if (d->fertile != 0 && fertility(c->island, d->fertile) == 0) {
        return false;
    }
    const assets::BuildingType* b = d->fixed != 0 ? by_id(d->fixed) : producer(d->ware, d->raw);
    if (b == nullptr) {
        return false;
    }
    // Open1602: no further producer of a good that fills the warehouse.
    if (!classic() && s.count != 0 && stock_full(*c, b->ware)) {
        return false;
    }
    if (!unlocked(p, *b)) {
        return d->type == NeedType::C;
    }
    std::int64_t field_money = 0;
    if (d->field != 0) {
        if (const assets::BuildingType* f = producer(d->field, 0)) {
            field_money = 64 * std::int64_t{f->build_cost.money};
        }
    }
    const bool afford =
        affordable(p, *c, *b) && (d->field == 0 || std::int64_t{player(p).money} > field_money + b->build_cost.money);
    if (!afford) {
        return d->type == NeedType::B || d->type == NeedType::C;
    }
    const bool pushed = plan_building(p, d->task, *b, which, false, d->from_start);
    if (pushed) {
        reserve(p, *b, field_money);
    }
    return d->type == NeedType::D ? pushed : true;
}

std::optional<std::uint8_t> Engine::package_slot(std::uint8_t w) const {
    for (const PackageNeed& n : m_package_needs) {
        if (n.ware == w) {
            return n.slot;
        }
    }
    return std::nullopt;
}

bool Engine::package_chains(AiPlayer& p, const City& c, const CityPlan& r) {
    if (m_package_needs.empty()) {
        return false;
    }
    std::vector<std::uint32_t> use(m_package_needs.size());
    for (std::size_t i = 0; i < use.size(); ++i) {
        use[i] = demand(c, m_package_needs[i].ware);
    }
    const std::vector<std::uint32_t> wanted = package_wanted(m_package_needs, use);
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        if (wanted[i] != 0 && package_need(p, c, r, m_package_needs[i], wanted[i])) {
            return true;
        }
    }
    return false;
}

bool Engine::package_order(AiPlayer& p, const City& c, const CityPlan& r, std::uint8_t w, std::uint32_t wanted) {
    // An order's entry for a good of a package: as many producers as it says, where the computers
    // have a producer for the good (package_needs).
    for (const PackageNeed& n : m_package_needs) {
        if (n.ware == w) {
            return package_need(p, c, r, n, wanted);
        }
    }
    return false;
}

bool Engine::package_need(AiPlayer& p, const City& c, const CityPlan& r, const PackageNeed& n, std::uint32_t wanted) {
    const SlotState& s = r.slots[n.slot];
    if (s.count >= wanted || s.attempts >= 2) {
        return false;
    }
    if (!classic() && s.count != 0 && stock_full(c, n.ware)) {
        return false;
    }
    // A workshop waits for the producer of its raw material. The original's Need of it is asked
    // for one; one of a package's comes in its own turn, as the city wants it too.
    if (n.raw_slot != kNoSlot && r.slots[n.raw_slot].count == 0) {
        if (n.raw_slot >= kSlots) {
            return false;
        }
        const std::size_t before = p.stack.size();
        (void)need(p, n.raw_slot, 1);
        return p.stack.size() > before;
    }
    if (n.fertile != 0 && fertility(c.island, n.fertile) == 0) {
        return false;
    }
    const assets::BuildingType* b = by_id(n.building);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    std::int64_t field_money = 0;
    if (n.field != 0) {
        if (const assets::BuildingType* f = producer(n.field, 0)) {
            field_money = 64 * std::int64_t{f->build_cost.money};
        }
    }
    if (!affordable(p, c, *b) || std::int64_t{player(p).money} <= field_money + b->build_cost.money) {
        return false;
    }
    if (!plan_building(p, n.task, *b, n.slot, false, false)) {
        return false;
    }
    reserve(p, *b, field_money);
    return true;
}

std::uint32_t Engine::made_per_minute(const City& c, const assets::BuildingType& b) const {
    // What the producers of `b` in the city made in their statistics windows, on average; for none
    // that has run a minute yet, half the full rate.
    const std::uint64_t full = b.interval == 0 ? 0 : std::uint64_t{b.produce_amount} * 60 / b.interval;
    const IslandMap& map = island_of(c.island).map;
    std::uint64_t sum = 0;
    std::uint32_t n = 0;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != c.island || pr.elapsed < 60 || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty() || m_buildings.by_sprite(cell.sprite) != &b) {
            continue;
        }
        sum += std::uint64_t{pr.produced} * 60 / pr.elapsed;
        ++n;
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(n == 0 ? full / 2 : sum / n, 0xFFFFFFFFU));
}

bool Engine::food_ahead(const City& c) const {
    const std::uint8_t food = m_buildings.food_ware();
    return std::uint64_t{supply(c, food)} * 4 >= std::uint64_t{demand(c, food)} * 5;
}

bool Engine::supplies_growth(const City& c) const {
    // A stock alone fills new houses that starve once it is eaten: the home city grows beyond its
    // order only while its own food comes in with a quarter to spare and every class living in it
    // is content (kContentForGrowth) with every good of theirs in store or coming in. A settler
    // without cloth or a citizen without alcohol turns discontent, the houses fall back and pay a
    // fraction; a good merely low in store, with the people content, holds nothing up.
    // Open1602: for an improved rival, so too while its own food covers three quarters and the
    // stock feeds them for kFoodAheadMinutes: a home island with no room left for farms got the
    // rest by ship and sat for hours on 20,000 gold and more, its houses never built.
    const std::uint8_t food = m_buildings.food_ware();
    const bool stocked = rivals() && std::uint64_t{supply(c, food)} * 4 >= std::uint64_t{demand(c, food)} * 3 &&
                         std::uint64_t{c.stock(food)} >= std::uint64_t{demand(c, food)} * kFoodAheadMinutes;
    if (!food_ahead(c) && !stocked) {
        return false;
    }
    for (std::size_t k = 0; k < c.inhabitants.size() && k < c.satisfaction.size(); ++k) {
        if (c.inhabitants[k] != 0 && c.satisfaction[k] < kContentForGrowth) {
            return false;
        }
    }
    // Contentment follows a missing good only after a while: nor while one of their goods that the
    // city makes is used up and comes in short.
    if (goods_lacking(c, true)) {
        return false;
    }
    return true;
}

bool Engine::goods_lacking(const City& c, bool content_too) const {
    return lacking_ware(c, content_too) != 0;
}

std::uint64_t Engine::stock_lasts(const City& c, std::uint8_t w) const {
    // A good its people do not use, or that the city makes enough of, lasts for good.
    const std::uint32_t used = demand(c, w);
    if (used == 0 || supply(c, w) >= used) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return std::uint64_t{c.stock(w)} * 256 / used;
}

std::uint32_t Engine::critical_short(PlayerId owner, std::uint8_t w) const {
    // Open1602: alcohol and cloth, what all the owner's cities use beyond what they make (1/32 t a
    // minute); the ships carry it between them. Food each city grows for itself (more_food).
    if (w != ware::kAlcohol && w != ware::kCloth) {
        return 0;
    }
    return goods_short(owner, w);
}

std::uint32_t Engine::goods_short(PlayerId owner, std::uint8_t w) const {
    // Open1602: what all the owner's cities use of a ware beyond what they make (1/32 t a minute).
    // Where it is short, the supply runs cannot bring enough however many ships sail: the
    // computer makes more of it (luxury) instead. Measured afresh at each look, so that a colony
    // that a rival takes or burns shows at once.
    std::uint64_t used = 0;
    std::uint64_t made = 0;
    for (const City& c : m_world.cities()) {
        if (c.owner == owner) {
            used += demand(c, w);
            made += supply(c, w);
        }
    }
    return used > made ? static_cast<std::uint32_t>(std::min<std::uint64_t>(used - made, 0xFFFFFFFFU)) : 0;
}

std::uint32_t Engine::ascent_extra(const City& c, std::size_t k, std::uint8_t w) const {
    // Open1602: half the people of class k - 1 risen into class k use what class k wants of the
    // good beyond what they used before.
    const auto& classes = m_buildings.resident_classes();
    if (k == 0 || k >= classes.size() || k - 1 >= c.inhabitants.size() || c.inhabitants[k - 1] == 0) {
        return 0;
    }
    const auto amount = [&](std::size_t of) {
        std::uint64_t a = 0;
        for (const assets::ResidentClass::Need& n : classes[of].needs) {
            a += n.ware == w ? n.amount : 0U;
        }
        return a;
    };
    const std::uint64_t up = amount(k);
    const std::uint64_t before = amount(k - 1);
    if (up <= before) {
        return 0;
    }
    const std::uint64_t risers = c.inhabitants[k - 1] / 2;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>((risers * (up - before) * 6) >> 8, 0xFFFFFFFFU));
}

std::uint32_t Engine::demand_ahead(const City& c, std::uint8_t w) const {
    // What the city uses now and what the next rise of each class adds (ascent_extra), where the
    // class below lives in the city.
    std::uint64_t sum = demand(c, w);
    const std::size_t n = m_buildings.resident_classes().size();
    for (std::size_t k = 1; k < n; ++k) {
        sum += ascent_extra(c, k, w);
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(sum, 0xFFFFFFFFU));
}

std::uint32_t Engine::goods_ahead(PlayerId owner, std::uint8_t w) const {
    // Open1602: goods_short with the rise of the classes to come (demand_ahead): an improved
    // rival makes its goods before its houses rise, not once its new citizens go without.
    std::uint64_t used = 0;
    std::uint64_t made = 0;
    for (const City& c : m_world.cities()) {
        if (c.owner == owner) {
            used += demand_ahead(c, w);
            made += supply(c, w);
        }
    }
    return used > made ? static_cast<std::uint32_t>(std::min<std::uint64_t>(used - made, 0xFFFFFFFFU)) : 0;
}

bool Engine::ascent_ready(AiPlayer& p, const City& c, std::size_t k) {
    // Open1602: an improved rival lets its houses rise into class k only once that class's goods
    // are ready: each made by its cities for what they use after the rise (goods_ahead), or bought
    // from a partner with twice the store, and kAscentStockMinutes of what the city will use in
    // store (at most half its Kontor); food grown for its people. Risen without them, five hundred
    // citizens left again within the hour, and their taxes with them.
    const auto& classes = m_buildings.resident_classes();
    if (k >= classes.size()) {
        return true;
    }
    const std::uint8_t food = m_buildings.food_ware();
    if (food_short(c) || (supply(c, food) < demand(c, food) && stock_lasts(c, food) < 2ULL * kAscentStockMinutes * 256)) {
        return false;
    }
    for (const assets::ResidentClass::Need& n : classes[k].needs) {
        const std::uint8_t w = n.ware;
        if (w == food) {
            continue;
        }
        const std::uint64_t use = std::uint64_t{demand(c, w)} + ascent_extra(c, k, w);
        if (use == 0) {
            continue;
        }
        const std::uint64_t cap = c.capacity / 2;
        const std::uint64_t store = std::min<std::uint64_t>(std::max<std::uint64_t>(20 * kTon, kAscentStockMinutes * use), cap);
        if (c.stock(w) < store) {
            return false;
        }
        if (goods_ahead(p.player, w) != 0) {
            std::uint16_t price = 0;
            if (!supply_source(p, c.island, w, &price, &c) || c.stock(w) < std::min<std::uint64_t>(2 * store, cap)) {
                return false;
            }
        }
    }
    return true;
}

std::size_t Engine::gate_class(const ComputerSlot& def) const {
    // The lowest class from the citizens on whose houses cannot do without the building: all the
    // other public buildings and the market do not serve it.
    House h;
    h.market = true;
    h.services = static_cast<assets::ServiceBits>(~def.service);
    const std::size_t n = m_buildings.resident_classes().size();
    for (std::size_t k = kLockFromClass; k < n; ++k) {
        if (!has_services(h, m_buildings, static_cast<std::uint8_t>(k))) {
            return k;
        }
    }
    return 0;
}

std::uint8_t Engine::lacking_ware(const City& c, bool content_too) const {
    // A class that lives in the city (discontent, unless `content_too`) with one of its goods (not
    // food) under a ton in stock and short in supply. With `content_too` only goods the city makes
    // itself count: one it has no producer for yet lacks from the start and shows in the
    // contentment; one it makes but too little of is used up before the people turn discontent.
    const auto& classes = m_buildings.resident_classes();
    for (std::size_t k = 0; k < classes.size() && k < c.inhabitants.size(); ++k) {
        if (c.inhabitants[k] == 0 ||
            (!content_too && k < c.satisfaction.size() && c.satisfaction[k] >= kContentForGrowth)) {
            continue;
        }
        for (const assets::ResidentClass::Need& n : classes[k].needs) {
            if (n.ware == m_buildings.food_ware() || c.stock(n.ware) >= kTon) {
                continue;
            }
            const std::uint32_t made = supply(c, n.ware);
            if (made < demand(c, n.ware) && (!content_too || made > 0)) {
                return n.ware;
            }
        }
    }
    return 0;
}

bool Engine::supply_ahead(AiPlayer& p, const City& c, const CityPlan& r, bool growing) {
    // Short of gold, only where its own supply covers less than four fifths of the need: the rest
    // waits for gold to spare and, for food, for the stock to run low (a new farm's yield shows only
    // after a while). Ships and the trader bring some, but a city that lives on them starves when
    // they are late.
    const bool spare = std::int64_t{player(p).money} >= stage_gold(2000);
    const auto dire = [&](std::uint8_t w) { return std::uint64_t{supply(c, w)} * 5 < std::uint64_t{demand(c, w)} * 4; };
    // Food: while what comes in falls short of what its people eat (with a quarter more while it is
    // to grow).
    const std::uint8_t food = m_buildings.food_ware();
    const bool covered = growing ? food_ahead(c) : supply(c, food) >= demand(c, food);
    if (!covered && (dire(food) || (spare && c.stock(food) < 2 * kFoodStock * kStockUnitsPerTon)) &&
        more_food(p, c, r, true)) {
        return true;
    }
    // Cloth: one more sheep farm (cotton where it grows fully) while the cloth falls short and runs
    // out, once the weaving caught up with the farms (processing: one for every two), so that the
    // farms do not run ahead of a weaving whose yield has not shown yet.
    const std::uint32_t cloth = demand(c, ware::kCloth);
    const std::uint32_t farms = std::uint32_t{r.slots[kSlotSheep].count} + r.slots[kSlotCotton].count;
    const std::uint32_t weaving = std::uint32_t{r.slots[kSlotWebstube].count} + r.slots[kSlotWeberei].count;
    if (cloth != 0 && supply(c, ware::kCloth) < cloth && !food_lasts(c.stock(ware::kCloth), cloth) &&
        (spare || dire(ware::kCloth)) && farms < 2 * weaving + 2) {
        const std::size_t before = p.stack.size();
        (void)need(p, kSlotSheep, std::uint32_t{r.slots[kSlotSheep].count} + 1);
        if (p.stack.size() > before) {
            log(p, "city %u/%u: cloth short (%u/%u), a farm for it", r.island, r.city, supply(c, ware::kCloth), cloth);
            return true;
        }
    }
    return false;
}

bool Engine::more_food(AiPlayer& p, const City& c, const CityPlan& r, bool ahead) {
    if (classic() || population(c) == 0 || (!ahead && !food_short(c))) {
        return false;
    }
    const std::vector<ComputerProducer>& changes = m_world.balance().computer_producers;
    const auto building = [&](std::uint8_t slot) -> const assets::BuildingType* {
        const auto d = need_def(slots(), slot, changes);
        return !d ? nullptr : d->fixed != 0 ? by_id(d->fixed) : producer(d->ware, d->raw);
    };
    const auto upkeep = [&](std::uint8_t slot) -> std::uint64_t {
        const assets::BuildingType* b = building(slot);
        return b != nullptr ? b->running_cost : 0;
    };
    const auto fed = [&](std::uint8_t slot) {
        const ComputerSlot* k = computer_slot(slots(), slot);
        return k != nullptr && r.slots[slot].count < processors_fed(c, r, *k);
    };
    // The ways to more food, each by what it adds a minute and what it costs a minute to run: a
    // workshop whose raw material the city already makes costs only its own upkeep; a new chain
    // the upkeep of its workshop and of the farms that feed it (one for every two). What a building
    // makes is what the city's own buildings of its kind make (a fisher by a sea with few fish
    // makes little), so the computer picks what pays best on this island.
    struct Way {
        std::uint8_t slot;       ///< what is built next
        std::uint8_t end;        ///< what makes the food
        std::uint64_t food = 0;  ///< 1/32 t a minute
        std::uint64_t cost = 0;  ///< gold a minute
    };
    std::vector<Way> ways;
    // `end` makes the food; `links` are its raw material's producers, the next first, each with
    // how many of them one `end` needs.
    const auto add = [&](std::uint8_t end, std::initializer_list<std::pair<std::uint8_t, std::uint32_t>> links) {
        const assets::BuildingType* b = building(end);
        if (b == nullptr) {
            return;
        }
        Way w{end, end, made_per_minute(c, *b), upkeep(end)};
        if (w.food == 0) {
            return;  // the city's own buildings of its kind make nothing here (a fisher by a sea without fish)
        }
        if (links.size() != 0 && !fed(end)) {
            // A whole new chain; built from the first link whose own raw material is there.
            for (const auto& [slot, per] : links) {
                w.cost += per * upkeep(slot);
            }
            for (const auto& [slot, per] : links) {
                w.slot = slot;
                if (fed(slot)) {
                    break;
                }
            }
        }
        ways.push_back(w);
    };
    add(kSlotFisher, {});
    add(kSlotButcher, {{kSlotCattle, 2}});
    add(kSlotBakery, {{kSlotMill, 2}, {kSlotGrain, 4}});
    // Not only the best on paper: each way counts for less the more of it the city has, and one of
    // those nearly as good as the best is taken at random, so cities mix fishers, pastures and
    // fields instead of building one kind only.
    const auto score = [&](const Way& w) { return food_way_score(w.food, w.cost, r.slots[w.end].count); };
    std::ranges::stable_sort(ways, [&](const Way& a, const Way& b) { return score(a) > score(b); });
    std::size_t close = 0;
    while (close < ways.size() && score(ways[close]) * 3 >= score(ways[0]) * 2) {
        ++close;
    }
    if (close > 1) {
        std::swap(ways[0], ways[rng().below(static_cast<std::uint32_t>(close))]);
    }
    for (const Way& w : ways) {
        const std::size_t before = p.stack.size();
        (void)need(p, w.slot, std::uint32_t{r.slots[w.slot].count} + 1);
        if (p.stack.size() > before) {
            log(p, "city %u/%u: %s, slot %u (%llu/min for %llu gold)", r.island, r.city,
                ahead && !food_short(c) ? "food ahead" : "short of food", w.slot,
                static_cast<unsigned long long>(w.food), static_cast<unsigned long long>(w.cost));
            // A way whose building failed already does not hold up the stage's list as well: the
            // city goes on with it while the food producer is tried again.
            return r.slots[w.slot].fail_level == 0;
        }
    }
    return false;
}

bool Engine::need_iron_mine(AiPlayer& p, std::uint32_t n) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    const std::optional<NeedDef> found = need_def(slots(), kSlotIronMine, m_world.balance().computer_producers);
    if (!found) {
        return false;  // a package removed the iron mine
    }
    const NeedDef& def = *found;
    SlotState& s = r->slots[kSlotIronMine];
    const std::uint32_t wanted = civ_level(p) >= 2 ? n : 1;
    const std::uint8_t blocked = std::max<std::uint8_t>(2, def.attempts);
    if (s.count >= wanted || s.attempts >= def.attempts) {
        return false;
    }
    if (c->stock(ware::kOre) >= 8 * kTon) {
        s.attempts = blocked;  // enough ore
        return false;
    }
    bool usable = false;
    std::int32_t free_x = -1;
    std::int32_t free_y = -1;
    for (const auto& d : island_of(c->island).header.deposits) {
        if (d.ware == ware::kOre && d.amount > 0 &&
            (d.kind == 1 || d.amount >= 120 * kTon || player(p).unlocked(infra::kLevel3F))) {
            usable = usable || reachable_deposit(p, d, free_x, free_y);
        }
    }
    if (!usable && free_x < 0) {
        s.attempts = blocked;
        return false;
    }
    const assets::BuildingType* b = by_id(def.fixed);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!usable) {
        return claim_land(p, kSlotIronMine, free_x, free_y);
    }
    if (!affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, kIronMine, *b, kSlotIronMine, false, true)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_gold_mine(AiPlayer& p) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    const std::optional<NeedDef> found = need_def(slots(), kSlotGoldMine, m_world.balance().computer_producers);
    if (!found) {
        return false;  // a package removed the gold mine
    }
    const NeedDef& def = *found;
    SlotState& s = r->slots[kSlotGoldMine];
    if (s.count != 0 || s.attempts >= def.attempts) {
        return false;
    }
    bool usable = false;
    std::int32_t free_x = -1;
    std::int32_t free_y = -1;
    for (const auto& d : island_of(c->island).header.deposits) {
        if (d.ware == ware::kGold && d.amount > 0) {
            usable = usable || reachable_deposit(p, d, free_x, free_y);
        }
    }
    if (!usable && free_x < 0) {
        s.attempts = std::max<std::uint8_t>(2, def.attempts);
        return false;
    }
    const assets::BuildingType* b = by_id(def.fixed);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!usable) {
        return claim_land(p, kSlotGoldMine, free_x, free_y);
    }
    if (!affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, kGoldMine, *b, kSlotGoldMine, false, true)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::reachable_deposit(AiPlayer& p, const assets::IslandHeader::Deposit& d, std::int32_t& free_x,
                               std::int32_t& free_y) {
    // The original's mine search binds any deposit of the island to the mountain it finds (S-093),
    // so for it every deposit counts. Open1602's stays where it lies: one in another city's land
    // is not searched for, one on free land is claimed first (claim_land).
    if (classic()) {
        return true;
    }
    std::int32_t x = -1;
    std::int32_t y = -1;
    switch (deposit_access(p, d, x, y)) {
        case DepositAccess::Own:
            return true;
        case DepositAccess::Free:
            if (free_x < 0) {
                free_x = x;
                free_y = y;
            }
            return false;
        case DepositAccess::Closed:
            return false;
    }
    return false;
}

bool Engine::claim_land(AiPlayer& p, std::uint8_t slot, std::int32_t x, std::int32_t y) {
    // Open1602: a market that brings the free slope (x, y) into the city's land, or comes nearer
    // to it (task kClaimLand). A failed search counts against the mine's slot, so a deposit that
    // cannot be reached is given up like a mine without a site.
    City* c = current_city(p);
    const assets::BuildingType* b = by_id(id::kMarket);
    if (c == nullptr || b == nullptr || !affordable(p, *c, *b)) {
        return false;
    }
    if (!push(p, kClaimLand, false)) {
        return false;
    }
    start_search(p, *b, slot, true);
    p.search.target_x = x;
    p.search.target_y = y;
    p.search.extra = market_distance(p, x, y);
    reserve(p, *b);
    log(p, "claim land towards %d,%d for slot %u (nearest market %d)", x, y, slot, p.search.extra);
    return true;
}

bool Engine::claim_fields(AiPlayer& p, std::uint8_t slot) {
    // Open1602: open land of the island for a farm whose slot found no site in the city's land:
    // the place with the most unclaimed land fields can take about it, nearest to the city's
    // markets, which a market then brings in (claim_land). Islands with a thousand free tiles and
    // more were left to the forest while the city starved.
    std::int32_t x = -1;
    std::int32_t y = -1;
    if (!field_land(p, x, y)) {
        return false;
    }
    log(p, "fields: open land at %d,%d for slot %u", x, y, unsigned{slot});
    City* c = current_city(p);
    const assets::BuildingType* market = by_id(id::kMarket);
    if (c != nullptr && market != nullptr && affordable(p, *c, *market) && claim_by_path(p, x, y)) {
        // The farm searches its sites anew in the land the market brought in.
        if (CityPlan* r = current_plan(p)) {
            r->slots[slot].attempts = 0;
            r->slots[slot].fail_level = 0;
        }
        return true;
    }
    return claim_land(p, slot, x, y);
}

bool Engine::need_houses(AiPlayer& p, std::uint32_t n) {
    // Houses while affordable, with half a ton of food and more than 1000 gold.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    SlotState& s = r->slots[kSlotHouses];
    if (s.count >= n || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kHouse);
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!affordable(p, *c, *b)) {
        // Open1602: a city whose houses wait for wood gets another woodcutter, one for every four
        // houses (a town's first roads and markets take the wood its one woodcutter makes).
        const auto& m = m_buildings.materials();
        if (!classic() && c->stock(m.wood) < b->build_cost.wood &&
            std::int64_t{player(p).money} >= b->build_cost.money && c->stock(m.tools) >= b->build_cost.tools) {
            const std::uint32_t woodcutters = r->slots[kSlotWoodcutter].count;
            const std::size_t before = p.stack.size();
            if (woodcutters < std::min<std::uint32_t>(1 + s.count / 4, kMostWoodcutters)) {
                (void)need(p, kSlotWoodcutter, woodcutters + 1);
            }
            if (p.stack.size() > before) {
                log(p, "city %u/%u: houses wait for wood, woodcutter %u", r->island, r->city, woodcutters + 1);
                return true;
            }
        }
        return false;
    }
    // Open1602: an improved computer builds houses with a quarter of the original's 1000 gold in
    // hand (stage_gold), as it lives from its taxes.
    if (c->stock(m_buildings.food_ware()) <= kTon / 2 || std::int64_t{player(p).money} <= stage_gold(1000)) {
        return false;
    }
    // Open1602: an improved computer that wants its first yard keeps the gold for it; its houses took
    // every coin above 250, and some never had a yard after hours.
    if (const std::int64_t yard = yard_savings(p);
        yard != 0 && std::int64_t{player(p).money} < yard + stage_gold(1000)) {
        return false;
    }
    // Open1602: so too for a colony whose crop its home island does not grow (colony_savings).
    if (const std::int64_t colony = colony_savings(p);
        colony != 0 && std::int64_t{player(p).money} < colony + stage_gold(1000)) {
        return false;
    }
    // Open1602: no new inhabitants a city cannot feed; its food producers come first (more_food).
    // A city that has no way to more food (no site for a fisher, its farms not yet open to it, as
    // in a town just founded) builds its houses all the same while its stock lasts, instead of
    // standing still until the computer gives it up.
    if (!classic() && food_short(*c)) {
        if (more_food(p, *c, *r)) {
            return true;
        }
        // Counted with every house full, the new one too: houses fill up after they are built.
        const auto& classes = m_buildings.resident_classes();
        const auto room = [&](std::uint8_t k) -> std::uint64_t {
            return k < classes.size() ? classes[k].max_per_house : 0;
        };
        std::uint64_t full = room(0);
        for (const House& h : m_world.houses()) {
            if (h.island == c->island && h.city == c->index) {
                full += room(h.resident_class);
            }
        }
        const std::uint64_t people = std::max<std::uint64_t>(population(*c), full);
        const auto eat = static_cast<std::uint32_t>(
            std::min<std::uint64_t>((people * m_buildings.food_need() * 6) >> 8, 0xFFFFFFFFU));
        if (!food_lasts(c->stock(m_buildings.food_ware()), eat)) {
            return false;
        }
    }
    if (plan_building(p, kHouses, *b, kSlotHouses, false)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_market(AiPlayer& p, std::uint32_t n) {
    // A market while fewer than n stand or the market flag is clear; no unlock test,
    // pushed even without budget.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    SlotState& s = r->slots[kSlotMarket];
    if (!(s.count < n || (r->flags & city_flag::kMarket) == 0) || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kMarket);
    if (b == nullptr) {
        return false;
    }
    if (!affordable(p, *c, *b)) {
        // The original waits for the market, even in a city without a woodcutter that lacks the wood
        // for it: the woodcutter comes later in the order, so the city waits for good. Open1602 goes
        // on with the order then.
        return classic() || r->slots[kSlotWoodcutter].count != 0;
    }
    if (plan_building(p, kMarket, *b, kSlotMarket, true)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_kontor(AiPlayer& p, std::uint32_t level) {
    // The next Kontor up to `level`.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || level > 4) {
        return false;
    }
    const std::uint32_t have = kontor_level(*c);
    if (have == 0 || have >= level || r->slots[kSlotKontor].attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kontor(have + 1));
    if (b == nullptr || !unlocked(p, *b)) {
        return false;
    }
    if (!affordable(p, *c, *b)) {
        return true;
    }
    if (push(p, kKontorUpgrade, false)) {
        start_search(p, *b, kSlotKontor, false);
        reserve(p, *b);
    }
    return true;
}

bool Engine::colony_kontor(AiPlayer& p, const City& c) {
    // The level of the computer's best Kontor in another city: its main city's, as a rule.
    std::uint32_t best = 0;
    for (const City& o : m_world.cities()) {
        if (o.owner == p.player && &o != &c) {
            best = std::max(best, kontor_level(o));
        }
    }
    const std::uint32_t have = kontor_level(c);
    if (have == 0 || have >= best) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kontor(have + 1));
    if (b == nullptr || !unlocked(p, *b) || !affordable(p, c, *b)) {
        return false;  // no waiting for it: the colony goes on with its order
    }
    const std::int64_t money = player(p).money;
    if (money < std::int64_t{b->build_cost.money} + kColonyKontorReserve) {
        return false;
    }
    // Of use when a good fills the warehouse (more room) or when the computer can well afford it
    // (more room and more goods on the Kontor's trade list).
    bool full = false;
    for (const std::uint32_t stock : c.goods) {
        full = full || std::uint64_t{stock} * 16 >= std::uint64_t{c.capacity} * kFullStock;
    }
    if (c.capacity == 0 || (!full && money < kColonyKontorMoney)) {
        return false;
    }
    return need_kontor(p, have + 1);
}

bool Engine::need_stone_roads(AiPlayer& p) {
    // Paving with three quarters of the warehouse in bricks and money to spare.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    const assets::BuildingType* road = by_id(id::kRoadStone);
    if (c == nullptr || r == nullptr || road == nullptr || r->slots[kSlotStoneRoads].attempts >= 2) {
        return false;
    }
    const auto& m = m_buildings.materials();
    const std::int32_t money = player(p).money;
    if (c->stock(m.bricks) < c->capacity * 3 / 4 || money <= 2000 || money <= 7 * road->build_cost.money ||
        c->stock(m.bricks) <= 7 * road->build_cost.bricks || !unlocked(p, *road)) {
        return false;
    }
    return push(p, kPaving, false);
}

bool Engine::roads_if_mason(AiPlayer& p) {
    const CityPlan* r = current_plan(p);
    return r != nullptr && r->slots[kSlotStonemason].count != 0 && need_stone_roads(p);
}

std::int64_t Engine::colony_savings(AiPlayer& p) {
    // The gold of a colony while an improved computer wants one for a crop none of its islands
    // grows (colony_crop_missing), else 0. Without it, the houses took every coin and the colony
    // came hours late.
    if (!rivals() || p.pirates || p.settle.failed || player(p).scenario.has(scenario_flag::kNoColonies)) {
        return 0;
    }
    // A settlement under way keeps the gold until the Kontor stands: spent meanwhile, the ship
    // arrived to find too little to found the city.
    // So too while its ship goes home to load: the gold fell from 3600 to 130 in those minutes.
    if (p.settle.active || p.settle.ship != 0) {
        return colony_money(p);
    }
    // Only with a ship to sail: without one the gold lay idle (update_fleet_target orders one).
    return colony_crop_missing(p) && colony_ship_candidate(p) != 0 ? colony_money(p) : 0;
}

std::uint32_t Engine::home_shortfall(AiPlayer& p, std::uint8_t w) {
    if (!rivals() || p.pirates || p.cities < 1) {
        return 0;
    }
    const City* home = home_city(p);
    if (home == nullptr || population(*home) == 0) {
        return 0;
    }
    const CityPlan& r = plan_of(p, home->island, home->index);
    const auto blocked = [&](std::uint8_t slot) { return r.slots[slot].fail_level >= 2; };
    std::uint64_t wanted = 0;
    if (w == m_buildings.food_ware()) {
        // The fishers alone feed no large city: the pastures and fields have no site left.
        if (!blocked(kSlotCattle) || !blocked(kSlotGrain)) {
            return 0;
        }
        wanted = std::uint64_t{demand(*home, w)} * 5 / 4;
    } else if (w == ware::kCloth) {
        if (!blocked(kSlotSheep) && !blocked(kSlotCotton)) {
            return 0;
        }
        wanted = demand(*home, w);
    } else {
        return 0;
    }
    const std::uint64_t made = supply(*home, w);
    if (made >= wanted) {
        return 0;
    }
    std::uint64_t missing = wanted - made;
    // What the other cities make beyond their own use comes by ship (home_supply_route).
    for (const City& o : m_world.cities()) {
        if (o.owner != p.player || &o == home) {
            continue;
        }
        const std::uint32_t own = supply(o, w);
        const std::uint32_t used = demand(o, w);
        if (own > used) {
            missing -= std::min<std::uint64_t>(missing, own - used);
        }
    }
    return static_cast<std::uint32_t>(missing);
}

bool Engine::grow_for_home(AiPlayer& p, const City& c, const CityPlan& r) {
    if (!rivals() || population(c) == 0) {
        return false;
    }
    if (home_shortfall(p, m_buildings.food_ware()) != 0 && more_food(p, c, r, true)) {
        log(p, "city %u/%u: food for the home city", r.island, r.city);
        return true;
    }
    if (home_shortfall(p, ware::kCloth) != 0) {
        // Farms only as far as the weaving keeps up (supply_ahead); processing() adds the weaving.
        const std::uint32_t farms = std::uint32_t{r.slots[kSlotSheep].count} + r.slots[kSlotCotton].count;
        const std::uint32_t weaving = std::uint32_t{r.slots[kSlotWebstube].count} + r.slots[kSlotWeberei].count;
        if (farms < 2 * weaving + 2) {
            const std::size_t before = p.stack.size();
            (void)need(p, kSlotSheep, std::uint32_t{r.slots[kSlotSheep].count} + 1);
            if (p.stack.size() > before) {
                log(p, "city %u/%u: cloth for the home city", r.island, r.city);
                return true;
            }
        }
    }
    return false;
}

bool Engine::colony_crop_missing(AiPlayer& p) {
    if (!rivals() || p.pirates || p.settle.active || p.settle.failed || p.cities >= city_limit() ||
        player(p).scenario.has(scenario_flag::kNoColonies)) {
        return false;
    }
    const City* home = home_city(p);
    // Only once settlers live there and the city has grown to kColonySaveFrom: saving from the
    // first settler held the houses that bring the gold, and the town stood still for two hours.
    if (home == nullptr || population(*home, 1) == 0 || population(*home) < kColonySaveFrom) {
        return false;
    }
    struct Need {
        std::uint8_t good;
        std::array<std::uint8_t, 2> crops;
    };
    static constexpr std::array<Need, 4> kNeeds{{{ware::kAlcohol, {ware::kWineField, ware::kSugarField}},
                                                 {ware::kTobaccoGoods, {ware::kTobaccoField, 0}},
                                                 {ware::kSpices, {ware::kSpiceField, 0}},
                                                 {ware::kCocoa, {ware::kCocoaField, 0}}}};
    return std::ranges::any_of(kNeeds, [&](const Need& n) {
        return demand(*home, n.good) != 0 &&
               std::ranges::none_of(n.crops, [&](std::uint8_t crop) { return crop != 0 && crop_available(p, crop); });
    });
}

std::int64_t Engine::yard_savings(AiPlayer& p) {
    // The gold of the small yard while an improved computer wants another ship, has no yard and its
    // home city could build one (a weaving mill, as need_shipyard asks), else 0.
    if (classic() || p.yard_valid || !p.home_valid ||
        static_cast<std::int32_t>(own_ships(p).size()) >= p.fleet_target) {
        return 0;  // only while it wants another ship
    }
    const CityPlan& r = plan_of(p, p.home_island, p.home_city);
    if (r.slots[kSlotShipyard].count != 0 || r.slots[kSlotShipyard].attempts > 3 ||
        (r.slots[kSlotWebstube].count == 0 && r.slots[kSlotWeberei].count == 0)) {
        return 0;
    }
    const assets::BuildingType* small = by_id(id::kShipyardSmall);
    return small != nullptr && unlocked(p, *small) ? small->build_cost.money : 0;
}

bool Engine::need_shipyard(AiPlayer& p) {
    // A yard in the home city once the computer has ships and a weaving mill.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    if (!own_ships(p).empty() && !(p.home_valid && p.home_island == p.island && p.home_city == p.city)) {
        return false;
    }
    if (r->slots[kSlotWebstube].count == 0 && r->slots[kSlotWeberei].count == 0) {
        return false;
    }
    SlotState& s = r->slots[kSlotShipyard];
    if (s.attempts > 3) {
        return false;
    }
    const assets::BuildingType* big = by_id(id::kShipyardBig);
    const assets::BuildingType* small = by_id(id::kShipyardSmall);
    if (s.count == 0) {
        if (slot_present(p, kSlotShipyard) != 0) {
            return false;  // a yard elsewhere (or not every city surveyed yet)
        }
        if (small == nullptr || !unlocked(p, *small)) {
            return false;
        }
        const assets::BuildingType* b = small;
        if (big != nullptr && unlocked(p, *big) && s.attempts <= 1 && affordable(p, *c, *big)) {
            b = big;
        }
        if (!affordable(p, *c, *b)) {
            return false;
        }
        if (plan_building(p, kShipyard, *b, kSlotShipyard, false)) {
            reserve(p, *b);
        }
        return true;
    }
    // Open1602: an improved computer builds the big yard on a site of its own and counts what
    // demolishing the small one gives back (Rules::demolish_refund); the original's upgrade tore the
    // small one down first, and where the big one then did not fit or the gold fell short, the
    // computer was left without a yard.
    const auto affordable_big = [&] {
        if (affordable(p, *c, *big)) {
            return true;
        }
        const std::uint32_t share = m_sim.rules().refund_share();
        if (classic() || share == 0 || small == nullptr) {
            return false;
        }
        const auto& m = m_buildings.materials();
        const auto& want = big->build_cost;
        const auto& back = small->build_cost;
        const auto has = [&](std::int64_t have, std::int64_t returned, std::int64_t cost) {
            return have + returned * share / 100 >= cost;
        };
        return has(player(p).money, back.money, want.money) && has(c->stock(m.wood), back.wood, want.wood) &&
               has(c->stock(m.bricks), back.bricks, want.bricks) && has(c->stock(m.tools), back.tools, want.tools) &&
               has(c->stock(m.cannons), back.cannons, want.cannons);
    };
    if (s.count == 1 && big != nullptr && unlocked(p, *big) && affordable_big()) {
        if (plan_building(p, classic() ? kShipyardUpgrade : kShipyard, *big, kSlotShipyard, false)) {
            reserve(p, *big);
        }
        return true;
    }
    return false;
}

bool Engine::need_tower(AiPlayer& p, std::uint8_t type, std::uint32_t n) {
    // 0 wooden, 1 stone, 2 pirate tower.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    const std::uint8_t slot = type == 0 ? kSlotWoodTower : type == 1 ? kSlotStoneTower : kSlotPirateTower;
    const std::uint32_t building = type == 0 ? id::kWoodTower : type == 1 ? id::kStoneTower : id::kPirateTower;
    SlotState& s = r->slots[slot];
    if (s.count >= n || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(building);
    if (b == nullptr || !unlocked(p, *b) || (!p.pirates && !affordable(p, *c, *b))) {
        return false;
    }
    // Open1602: a cannon tower costs upkeep like a soldier; only within the army's budget.
    if (!p.pirates && rivals() && pays_upkeep(m_world, player(p)) && m_sim.figures() != nullptr &&
        military_cost(p) + static_cast<std::int32_t>(m_sim.figures()->tower_upkeep()) > army_budget(p)) {
        return false;
    }
    if (!push(p, kTowers, false)) {
        return false;
    }
    start_search(p, *b, slot, false);
    if (!p.pirates) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_denkmal(AiPlayer& p) {
    // While the player has monuments allowed.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    const Player& pl = player(p);
    if (c == nullptr || r == nullptr || pl.monuments_built >= pl.monuments_allowed ||
        r->slots[kSlotDenkmal].attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(id::kDenkmal);
    if (b == nullptr || !unlocked(p, *b) || !affordable(p, *c, *b)) {
        return false;
    }
    if (plan_building(p, kDenkmal, *b, kSlotDenkmal, false)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::need_castle(AiPlayer& p) {
    // A castle in the home city while the computer trails the leading human. Open1602 (rivals): also
    // as the home city grows, a castle for every so many inhabitants while the army is short of its
    // target (Balance's [computer_military]).
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || (m_world.game_settings().flags & World::GameSettings::kNoMilitary) != 0) {
        return false;
    }
    if (!(p.home_valid && p.home_island == p.island && p.home_city == p.city)) {
        return false;
    }
    if (p.castles.size() >= 3 || r->slots[kSlotCastle].attempts >= 2) {
        return false;
    }
    const Balance& bal = m_world.balance();
    const auto castles = static_cast<std::uint32_t>(p.castles.size());
    // Rivals: grown enough for one more castle, and the army short of its target (the first castle
    // in any case, so that the computer can recruit at all).
    const bool grown = rivals() && bal.ai_castle_inhabitants != 0 &&
                       population(*c) >= std::uint64_t{bal.ai_castle_inhabitants} * (castles + 1U) &&
                       (castles == 0 || weighted_army(p.player) < army_target(p));
    if (!p.castles.empty() && !grown) {
        bool war = false;
        for (std::size_t h = 0; h < m_world.players().size(); ++h) {
            if (m_world.players()[h].role == PlayerRole::Human &&
                m_world.diplomacy().relation(p.player, static_cast<PlayerId>(h)).peace != Treaty::Signed) {
                war = true;
            }
        }
        if (p.castles.size() > 1 || !war) {
            return false;
        }
    }
    const assets::BuildingType* b = nullptr;
    for (const std::uint32_t id : {id::kCastle3, id::kCastle2, id::kCastle1}) {
        const assets::BuildingType* t = by_id(id);
        if (t != nullptr && unlocked(p, *t)) {
            b = t;
            break;
        }
    }
    if (b == nullptr || !affordable(p, *c, *b)) {
        return false;
    }
    if (grown) {
        // Gold to spare after the castle, as a player keeps some for running costs.
        if (player(p).money < std::int64_t{b->build_cost.money} + std::int64_t{bal.ai_castle_reserve}) {
            return false;
        }
    } else if (!behind(p)) {
        return false;
    }
    const assets::BuildingType* square = by_id(id::kSquare);
    const std::uint32_t ring = square != nullptr ? (b->size_x + b->size_y + 2) * 2 * square->build_cost.bricks : 0;
    const auto& m = m_buildings.materials();
    if (c->stock(m.bricks) < b->build_cost.bricks + ring) {
        return false;
    }
    if (plan_building(p, kCastle, *b, kSlotCastle, false)) {
        reserve(p, *b);
        p.reserved.bricks += remove_goods(*c, m.bricks, ring);
    }
    return true;
}

bool Engine::need_public(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov) {
    // A public building while fewer than n stand or houses lack it.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    // The public building of the slot (computer_slots.hpp); none for a removed one.
    const ComputerSlot* def = computer_slot(slots(), which);
    if (def == nullptr || def->kind != ComputerSlot::Kind::Public) {
        return false;
    }
    // The Kapelle always checks its flag; gallows and fountains ignore `cov`.
    if (which == kSlotKapelle || which == kSlotGalgen || which == kSlotBrunnen) {
        cov = true;
    }
    if (!cov) {
        r->flags |= def->flag;  // count only
    }
    SlotState& s = r->slots[which];
    if (!(s.count < n || (r->flags & def->flag) == 0) || s.attempts >= 2) {
        return false;
    }
    const assets::BuildingType* b = by_id(def->building);
    if (b == nullptr || !unlocked(p, *b) || !affordable(p, *c, *b)) {
        return false;
    }
    // Open1602: no costly public building (a church, a bath, a college) while the people already
    // living in the city run out of one of their goods: the gold goes to the goods first, and a
    // building that lifts houses to a class whose goods are missing only makes them fall back.
    if (rivals() && b->build_cost.money >= kCostlyPublic && goods_lacking(*c)) {
        return false;
    }
    // Open1602: nor one that lets houses rise into a class (a tavern, a theatre) before that
    // class's goods are made and in store (ascent_ready): until then it would only cost its gold
    // and running costs, and then lift houses whose people leave again.
    if (rivals()) {
        if (const std::size_t g = gate_class(*def); g != 0 && !ascent_ready(p, *c, g)) {
            return false;
        }
    }
    if (plan_building(p, def->task, *b, which, false)) {
        reserve(p, *b);
    }
    return true;
}

bool Engine::processing(AiPlayer& p) {
    // Workshops for the raw materials the city has.
    const CityPlan* r = current_plan(p);
    if (r == nullptr) {
        return false;
    }
    // In the order of their ranks (of equal ranks by number): one for every so many producers of
    // their raw material.
    std::vector<const ComputerSlot*> by_rank;
    for (const ComputerSlot& k : slots()) {
        if (k.process_rank != 0 && k.process_from != kNoProducer && k.process_per != 0) {
            by_rank.push_back(&k);
        }
    }
    std::ranges::stable_sort(by_rank, {}, &ComputerSlot::process_rank);
    for (const ComputerSlot* k : by_rank) {
        const std::uint32_t from = r->slots[k->process_from].count;
        const std::uint32_t want =
            k->process_round_up ? (from + k->process_per - 1) / k->process_per : from / k->process_per;
        if (r->slots[k->number].count < want) {
            return need(p, k->number, want);
        }
    }
    return false;
}

bool Engine::iron_tools(AiPlayer& p) {
    const City* c = current_city(p);
    if (c == nullptr || c->stock(ware::kOre) <= kTon) {
        return false;
    }
    return need(p, kSlotSmelter, 2) || need(p, kSlotToolsmith, 1);
}

bool Engine::weapons(AiPlayer& p) {
    // The weapon workshops in the home city.
    if ((m_world.game_settings().flags & World::GameSettings::kNoMilitary) != 0 ||
        !(p.home_valid && p.home_island == p.island && p.home_city == p.city)) {
        return false;
    }
    if (!p.castles.empty() && (need(p, kSlotSwords, 1) || need(p, kSlotMuskets, 1))) {
        return true;
    }
    return need(p, kSlotCannons, 1);
}

bool Engine::luxury(AiPlayer& p, std::uint32_t n, bool exclusive) {
    // Luxury raw materials by the island's fertility, what the home city lacks and
    // what the computer can afford to run.
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    City* home = home_city(p);
    if (c == nullptr || r == nullptr || home == nullptr || p.max_stage < stage::kSettlers) {
        return false;
    }
    std::int64_t budget = net_income(p);
    if (budget < std::int64_t{p.cities} * 25) {
        if (player(p).money < 7000) {
            return false;
        }
        budget = player(p).money / 100;
    }
    const std::uint32_t cap = home->capacity / 2;
    const auto upkeep = [&](std::uint8_t w, std::uint8_t raw) -> std::int64_t {
        const assets::BuildingType* b = producer(w, raw);
        return b != nullptr ? b->running_cost : 0;
    };
    // 1. Iron
    if (p.score >= 2 && fertility(c->island, ware::kOre) >= 64 && home->stock(ware::kOre) < 10 * kTon) {
        static constexpr std::array<std::uint32_t, 4> kMines{1, 2, 2, 3};
        if (need_iron_mine(p, kMines[std::min<std::uint32_t>(difficulty(), 3)])) {
            return true;
        }
    }
    // 2. Gold
    if (p.score >= 4 && fertility(c->island, ware::kGold) >= 64) {
        const auto gold = need_def(slots(), kSlotGoldMine, m_world.balance().computer_producers);
        const assets::BuildingType* mine = gold ? by_id(gold->fixed) : nullptr;
        if (mine != nullptr && mine->running_cost < budget && need_gold_mine(p)) {
            return true;
        }
    }
    struct Crop {
        std::uint8_t slot;
        std::uint8_t field;
        std::uint8_t good;  ///< what the home city must lack
        std::int64_t upkeep;
        bool eligible;
    };
    bool wine_island = false;
    const std::array<Crop, 6> crops{{
        {kSlotSpices, ware::kSpiceField, ware::kSpices, upkeep(ware::kSpices, ware::kSpiceField), true},
        {kSlotTobacco, ware::kTobaccoField, ware::kTobaccoGoods,
         upkeep(ware::kTobacco, ware::kTobaccoField) + upkeep(ware::kTobaccoGoods, ware::kTobacco), true},
        {kSlotWine, ware::kWineField, ware::kAlcohol, upkeep(ware::kAlcohol, ware::kWineField), true},
        {kSlotCocoa, ware::kCocoaField, ware::kCocoa, upkeep(ware::kCocoa, ware::kCocoaField),
         p.score >= 3 || m_state.max_score >= 3},
        {kSlotCotton, ware::kCottonField, ware::kCloth,
         upkeep(ware::kWool, ware::kCottonField) + upkeep(ware::kCloth, ware::kWool), p.max_stage >= stage::kCitizens},
        {kSlotSugar, ware::kSugarField, ware::kAlcohol,
         upkeep(ware::kSugar, ware::kSugarField) + upkeep(ware::kAlcohol, ware::kSugar), true},
    }};
    // Open1602: an improved computer plants first what its people go without (critical_short),
    // then what its home city's people run out of soonest; for a critical good as many fields as
    // make up what its cities lack, beyond the order's number.
    std::array<Crop, 6> order = crops;
    if (rivals()) {
        wine_island = fertility(c->island, ware::kWineField) == 128;
        std::ranges::stable_sort(order, {}, [&](const Crop& crop) {
            if (critical_short(p.player, crop.good) != 0) {
                return std::uint64_t{0};
            }
            return goods_ahead(p.player, crop.good) != 0 ? 1 : 2 + stock_lasts(*home, crop.good) / 2;
        });
    }
    for (const Crop& crop : order) {
        if (!crop.eligible || fertility(c->island, crop.field) != 128) {
            continue;
        }
        if (crop.slot == kSlotWine) {
            wine_island = true;
        }
        if (crop.slot == kSlotSugar && wine_island) {
            continue;
        }
        // At most two beyond the order in a city, so that the fields do not take its houses' land.
        std::uint32_t want = n;
        // What its cities will lack once their houses rise (goods_ahead): the goods are made before.
        const std::uint32_t lacking = rivals() ? goods_ahead(p.player, crop.good) : 0;
        if (lacking != 0) {
            const assets::BuildingType* b = producer(crop.good, crop.field);
            const std::uint32_t per = b != nullptr ? std::max<std::uint32_t>(made_per_minute(*c, *b), 1) : 0;
            if (per != 0) {
                want = std::max(want, std::min(r->slots[crop.slot].count + (lacking + per - 1) / per, n + 2));
            }
        }
        if (r->slots[crop.slot].count >= want || home->stock(crop.good) >= cap || crop.upkeep >= budget) {
            continue;
        }
        if (need(p, crop.slot, want)) {
            return true;
        }
        if (exclusive && crop.slot != kSlotSugar) {
            return false;
        }
    }
    return false;
}

}  // namespace o1602::sim::ai
