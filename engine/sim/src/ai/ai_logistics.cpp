// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ships of the computer players (docs/re/ai.md, "Ships and trade"): every 10 s the repair at the own
// shipyard, one supply run that brings goods home, the size of the fleet and new ships, and the
// patrols around the own islands (AI_Logistics 0x00424BF0).
#include <algorithm>
#include <array>

#include "ai_engine.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint32_t kHealthy = 0x5F;  // health * 128 / max above this: at least 75 %

bool carries_goods(const Ship& s) {
    for (const CargoSlot& c : s.cargo) {
        if (c.ware != 0 && c.ware <= ware::kLastTrade && c.amount != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

ShipNote& Engine::note(AiPlayer& p, std::uint32_t ship) {
    for (ShipNote& n : p.ships) {
        if (n.id == ship) {
            return n;
        }
    }
    ShipNote n;
    n.id = ship;
    p.ships.push_back(n);
    return p.ships.back();
}

void Engine::forget_ships(AiPlayer& p) {
    std::erase_if(p.ships, [&](const ShipNote& n) {
        const Ship* s = m_world.ship(n.id);
        return s == nullptr || s->owner != p.player || s->sinking;
    });
}

const assets::FigureType* Engine::ship_type(const Ship& s) const {
    return m_sim.m_figures->get(s.figure);
}

std::uint32_t Engine::health128(const Ship& s) const {
    const assets::FigureType* t = ship_type(s);
    const std::uint32_t max = t != nullptr && t->ship.max_health != 0 ? t->ship.max_health : 1U;
    const std::uint32_t health = s.health != 0 ? std::min(s.health, max) : max;
    return health * 128U / max;
}

bool Engine::ship_at_city(const Ship& s, const City& c) const {
    return c.warehouse_x && c.warehouse_y &&
           building_in_reach(m_world, m_buildings, c.island, *c.warehouse_x, *c.warehouse_y, s.x, s.y);
}

bool Engine::ship_idle(AiPlayer& p, const Ship& s) {
    const ShipNote& n = note(p, s.id);
    return !ship_moving(s) && !s.route.active && n.mission == 0 && !s.repairing && s.id != p.settle.ship && !s.held;
}

void Engine::send_to_city(AiPlayer& p, Ship& s, const City& c) {
    ShipOrder order;
    order.player = p.player;
    order.ship = s.id;
    order.target = Target::city_of(c.island, c.index);
    execute(order);
    ShipNote& n = note(p, s.id);
    n.patrol = false;
    n.mission = 0;
    n.counter = 0;
}

void Engine::arm_one(City& c, Ship& s) {
    // 0x004234D0: one cannon from the city's stock onto the ship.
    const assets::FigureType* t = ship_type(s);
    const std::uint8_t cannons = m_buildings.materials().cannons;
    if (t != nullptr && s.cannons < t->ship.max_cannons && c.stock(cannons) > kTon) {
        ++s.cannons;
        remove_goods(c, cannons, kTon);
    }
}

// ---------------------------------------------------------------------------------------------

void Engine::logistics(AiPlayer& p) {
    if (p.pirates) {
        return;
    }
    forget_ships(p);
    // Patrol stops passed since the last look.
    for (ShipNote& n : p.ships) {
        const Ship* s = m_world.ship(n.id);
        if (s != nullptr && n.patrol && s->route.active && s->route.current != n.last_stop) {
            n.last_stop = s->route.current;
            if (n.counter > 0) {
                --n.counter;
            }
        }
        if (s != nullptr && n.patrol && !s->route.active) {
            n.patrol = false;
        }
        if (s != nullptr && n.supply && !s->route.active && !ship_moving(*s)) {
            n.supply = false;
        }
    }
    bool healthy = false;
    City* home = home_city(p);
    if (home != nullptr && p.cities != 0) {
        if (p.yard_valid) {
            healthy = yard_service(p);
        }
        if (Ship* s = pick_logistics_ship(p, healthy)) {
            if (!carries_goods(*s) || s->route.active) {
                home_supply_route(p, *s, *home);
            } else if (ship_at_city(*s, *home)) {
                unload_at(*home, *s, false, false);  // the original throws away what does not fit (S-097)
                arm_one(*home, *s);
            } else {
                send_to_city(p, *s, *home);
            }
        }
        update_fleet_target(p);
        if (static_cast<std::int32_t>(own_ships(p).size()) < p.fleet_target) {
            order_ship(p);
        }
    }
    missions(p);
    for (const City& c : m_world.cities()) {
        if (c.owner != p.player) {
            continue;
        }
        if (static_cast<std::int32_t>(patrols(p, c.island)) < p.ships_per_city) {
            Ship* s = pick_patrol_ship(p, healthy);
            if (s == nullptr) {
                return;
            }
            patrol_route(p, *s, c.island);
        }
    }
}

bool Engine::yard_service(AiPlayer& p) {
    // 0x004243C0: one ship at a time into the yard for repair.
    const Tile t = tile(p.yard_island, p.yard_x, p.yard_y);
    if (!t.valid() || t.prod != prod::kShipyard || t.owner != p.player) {
        p.yard_valid = false;
        return false;
    }
    const CostBuilding* yard = nullptr;
    for (const CostBuilding& b : m_world.cost_buildings()) {
        if (b.kind == CostBuilding::Kind::Shipyard && b.island == p.yard_island && b.x == p.yard_x && b.y == p.yard_y) {
            yard = &b;
        }
    }
    if (yard == nullptr) {
        return false;
    }
    if (yard->repair_ship != 0 && m_world.ship(yard->repair_ship) != nullptr) {
        return true;
    }
    for (Ship* s : own_ships(p)) {
        if (!ship_idle(p, *s) ||
            !building_in_reach(m_world, m_buildings, p.yard_island, p.yard_x, p.yard_y, s->x, s->y)) {
            continue;
        }
        if (health128(*s) <= kHealthy) {
            ShipyardOrder order;
            order.player = p.player;
            order.island = p.yard_island;
            order.x = p.yard_x;
            order.y = p.yard_y;
            order.op = ShipyardOrder::Op::Repair;
            order.ship = s->id;
            execute(order);
            return false;
        }
        if (City* home = home_city(p)) {
            send_to_city(p, *s, *home);
            arm_one(*home, *s);
        }
    }
    // The weakest ship that needs it comes to the yard.
    Ship* weakest = nullptr;
    for (Ship* s : own_ships(p)) {
        const std::uint32_t h = health128(*s);
        if (h > kHealthy || s->repairing || s->id == p.settle.ship) {
            continue;
        }
        if (h < 32 || ship_idle(p, *s)) {
            if (weakest == nullptr || h < health128(*weakest)) {
                weakest = s;
            }
        }
    }
    if (weakest != nullptr) {
        ShipOrder order;
        order.player = p.player;
        order.ship = weakest->id;
        order.target = Target::building(p.yard_island, p.yard_x, p.yard_y);
        execute(order);
        ShipNote& n = note(p, weakest->id);
        n.patrol = false;
        n.mission = 0;
    }
    return true;
}

Ship* Engine::pick_logistics_ship(AiPlayer& p, bool healthy) {
    // 0x00423BC0: an idle ship with the fewest cannons, else the patrol with the fewest stops left.
    Ship* best = nullptr;
    std::uint32_t best_cannons = 50;
    for (Ship* s : own_ships(p)) {
        ShipNote& n = note(p, s->id);
        if (!ship_idle(p, *s)) {
            continue;
        }
        if (n.cooldown > 0) {
            --n.cooldown;
            continue;
        }
        if (healthy && health128(*s) <= kHealthy) {
            continue;
        }
        if (s->cannons < best_cannons) {
            best = s;
            best_cannons = s->cannons;
        }
    }
    if (best != nullptr) {
        return best;
    }
    std::uint32_t best_counter = 9;
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        if (!n.patrol || n.mission != 0 || (healthy && health128(*s) <= kHealthy)) {
            continue;
        }
        if (n.counter < best_counter) {
            best = s;
            best_counter = n.counter;
        }
    }
    return best;
}

Ship* Engine::pick_patrol_ship(AiPlayer& p, bool healthy) {
    // 0x00423E80: an idle ship with the most cannons; worn-out patrols go home.
    const std::vector<Ship*> ships = own_ships(p);
    Ship* best = nullptr;
    std::int32_t best_cannons = -1;
    for (Ship* s : ships) {
        ShipNote& n = note(p, s->id);
        if (n.patrol && (n.counter == 0 || (healthy && health128(*s) < 64))) {
            if (City* home = home_city(p)) {
                send_to_city(p, *s, *home);
            }
            continue;
        }
        if (!ship_idle(p, *s)) {
            continue;
        }
        if (n.cooldown > 0) {
            --n.cooldown;
            continue;
        }
        if (ships.size() == 1 && carries_goods(*s)) {
            continue;
        }
        if (static_cast<std::int32_t>(s->cannons) > best_cannons) {
            best = s;
            best_cannons = s->cannons;
        }
    }
    return best;
}

std::uint32_t Engine::patrols(AiPlayer& p, std::uint16_t island) {
    std::uint32_t n = 0;
    for (const ShipNote& note_ : p.ships) {
        const Ship* s = m_world.ship(note_.id);
        n += s != nullptr && note_.patrol && note_.mission == 0 && note_.patrol_island == island && s->route.active
                 ? 1U
                 : 0U;
    }
    return n;
}

void Engine::patrol_route(AiPlayer& p, Ship& s, std::uint16_t island) {
    // 0x00424A50: the corners of the island's rectangle, a little apart each time, in a loop.
    const WorldIsland& isl = island_of(island);
    const std::int32_t r1 = static_cast<std::int32_t>(rng().next() & 3U);
    const std::int32_t r2 = static_cast<std::int32_t>(rng().next() & 3U);
    const auto x0 = static_cast<std::int32_t>(isl.x) - r1;
    const auto y0 = static_cast<std::int32_t>(isl.y) - r2;
    const std::int32_t x1 = static_cast<std::int32_t>(isl.x + isl.map.width()) - 1 + r2;
    const std::int32_t y1 = static_cast<std::int32_t>(isl.y + isl.map.height()) - 1 + r1;
    const auto clamp_x = [&](std::int32_t x) {
        return static_cast<std::uint32_t>(
            std::clamp<std::int32_t>(x, 0, static_cast<std::int32_t>(m_world.width()) - 1));
    };
    const auto clamp_y = [&](std::int32_t y) {
        return static_cast<std::uint32_t>(
            std::clamp<std::int32_t>(y, 0, static_cast<std::int32_t>(m_world.height()) - 1));
    };
    const std::array<Target, 4> corners{
        Target::position(clamp_x(x0), clamp_y(y0)), Target::position(clamp_x(x1), clamp_y(y0)),
        Target::position(clamp_x(x1), clamp_y(y1)), Target::position(clamp_x(x0), clamp_y(y1))};
    RouteEdit e;
    e.player = p.player;
    e.ship = s.id;
    e.op = RouteEdit::Op::ClearStop;
    e.stop = 0xFF;
    execute(e);
    for (std::uint8_t i = 0; i < 4; ++i) {
        RouteEdit stop;
        stop.player = p.player;
        stop.ship = s.id;
        stop.op = RouteEdit::Op::SetStop;
        stop.stop = i;
        stop.target = corners[i];
        execute(stop);
    }
    RouteEdit way;
    way.player = p.player;
    way.ship = s.id;
    way.op = RouteEdit::Op::OneWay;
    way.on = false;
    execute(way);
    RouteEdit start;
    start.player = p.player;
    start.ship = s.id;
    start.op = RouteEdit::Op::Start;
    execute(start);
    ShipNote& n = note(p, s.id);
    n.patrol = true;
    n.patrol_island = island;
    n.mission = 0;
    n.counter = s.cannons < 4 ? 3 : static_cast<std::uint8_t>(8 - r1 - r2);
    n.last_stop = s.route.current;
}

std::optional<std::size_t> Engine::supply_source(AiPlayer& p, std::uint16_t home_island, std::uint8_t w,
                                                 std::uint16_t* price) {
    // 0x00424560: an own city on another island with goods to spare, else the cheapest Kontor
    // of a partner with a trade treaty.
    const auto& m = m_buildings.materials();
    const std::uint32_t reserve = (w == m.tools || w == m.bricks || w == m.wood) ? 28 * kTon : 5 * kTon;
    const auto& cities = m_world.cities();
    for (std::size_t i = 0; i < cities.size(); ++i) {
        const City& c = cities[i];
        if (c.owner == p.player && c.island != home_island && c.stock(w) > reserve) {
            *price = 0;
            return i;
        }
    }
    std::optional<std::size_t> best;
    std::uint16_t best_price = 1000;
    for (std::size_t i = 0; i < cities.size(); ++i) {
        const City& c = cities[i];
        const Player* o = m_world.player(c.owner);
        // Only human and computer partners (the original also takes natives and pirates, S-101).
        if (c.owner == p.player || o == nullptr || (o->role != PlayerRole::Human && o->role != PlayerRole::Computer)) {
            continue;
        }
        if (m_world.diplomacy().relation(p.player, c.owner).trade != Treaty::Signed) {
            continue;
        }
        const City::Trade* t = c.trade_of(w);
        if (t == nullptr || !t->sell || c.stock(w) <= t->keep + 5 * kTon) {
            continue;
        }
        if (t->sell_price < best_price) {
            best = i;
            best_price = t->sell_price;
        }
    }
    *price = best_price;
    return best;
}

bool Engine::home_supply_route(AiPlayer& p, Ship& s, City& home) {
    // 0x00424850: one or two wares the home city lacks, from where they are, one-way home.
    std::vector<std::uint8_t> wares{m_buildings.food_ware(), m_buildings.materials().tools,
                                    m_buildings.materials().bricks, m_buildings.materials().wood, ware::kCloth};
    std::uint32_t target = 10 * kTon;
    if (demand(home, ware::kSpices) > 0 || p.max_stage > 30) {
        wares.insert(wares.end(), {ware::kTobaccoGoods, ware::kSpices, ware::kAlcohol, ware::kOre, ware::kIron});
        target = 20 * kTon;
    }
    if (demand(home, ware::kCocoa) > 0 || p.max_stage > 39) {
        wares.insert(wares.end(), {ware::kCocoa, ware::kClothes});
        target = 30 * kTon;
    }
    if (p.max_stage > 49) {
        wares.insert(wares.end(), {ware::kGold, ware::kJewellery});
        target = 40 * kTon;
    }
    // Wares other supply ships already bring.
    std::vector<std::uint8_t> routed;
    for (const ShipNote& n : p.ships) {
        const Ship* o = m_world.ship(n.id);
        if (o == nullptr || o->id == s.id || !n.supply || !o->route.active) {
            continue;
        }
        for (const RouteStop& stop : o->route.stops) {
            for (const RouteEntry& e : stop.entries) {
                if (e.ware != 0 && e.unload) {
                    routed.push_back(e.ware);
                }
            }
        }
    }
    struct Pick {
        std::uint8_t ware = 0;
        std::size_t source = 0;
        std::uint32_t amount = 0;
        bool own = false;
    };
    const auto choose = [&](std::uint8_t exclude) -> std::optional<Pick> {
        for (const std::uint8_t w : wares) {
            if (w == exclude || std::find(routed.begin(), routed.end(), w) != routed.end()) {
                continue;
            }
            if (home.stock(w) >= target) {
                continue;
            }
            std::uint16_t price = 0;
            const auto src = supply_source(p, home.island, w, &price);
            if (!src) {
                continue;
            }
            const std::uint32_t need = target - home.stock(w);
            const bool own = m_world.cities()[*src].owner == p.player;
            return Pick{w, *src, std::max<std::uint32_t>(need, 20 * kTon), own};
        }
        return std::nullopt;
    };
    const auto first = choose(0);
    if (!first) {
        return false;
    }
    const auto second = choose(first->ware);
    struct Stop {
        Target target;
        std::array<RouteEntry, 3> entries{};
    };
    std::vector<Stop> stops;
    const City& src1 = m_world.cities()[first->source];
    // Cannons for an own colony on the way out.
    const std::uint8_t cannons = m_buildings.materials().cannons;
    if (first->own && ship_at_city(s, home) && src1.stock(cannons) < kTon && home.stock(cannons) > 2 * kTon) {
        Stop out{Target::city_of(home.island, home.index), {}};
        out.entries[0] = RouteEntry{cannons, false, 2 * kTon};
        stops.push_back(out);
    }
    Stop load1{Target::city_of(src1.island, src1.index), {}};
    if (!stops.empty()) {
        load1.entries[0] = RouteEntry{cannons, true, 2 * kTon};
    }
    load1.entries[1] = RouteEntry{first->ware, false, first->own ? 50U * kTon : first->amount};
    stops.push_back(load1);
    if (second) {
        const City& src2 = m_world.cities()[second->source];
        Stop load2{Target::city_of(src2.island, src2.index), {}};
        // The original loads the first ware's amount here too (S-101).
        load2.entries[1] = RouteEntry{second->ware, false, second->own ? 50U * kTon : second->amount};
        if (load2.target == load1.target) {
            stops.back().entries[2] = load2.entries[1];
        } else {
            stops.push_back(load2);
        }
    }
    Stop back{Target::city_of(home.island, home.index), {}};
    back.entries[0] = RouteEntry{first->ware, true, 50 * kTon};
    if (second) {
        back.entries[1] = RouteEntry{second->ware, true, 50 * kTon};
    }
    stops.push_back(back);
    if (stops.size() > TradeRoute::kStops) {
        stops.erase(stops.begin());
    }
    RouteEdit clear;
    clear.player = p.player;
    clear.ship = s.id;
    clear.op = RouteEdit::Op::ClearStop;
    clear.stop = 0xFF;
    execute(clear);
    for (std::size_t i = 0; i < stops.size(); ++i) {
        RouteEdit stop;
        stop.player = p.player;
        stop.ship = s.id;
        stop.op = RouteEdit::Op::SetStop;
        stop.stop = static_cast<std::uint8_t>(i);
        stop.target = stops[i].target;
        execute(stop);
        for (std::uint8_t k = 0; k < 3; ++k) {
            if (stops[i].entries[k].ware == 0) {
                continue;
            }
            RouteEdit entry;
            entry.player = p.player;
            entry.ship = s.id;
            entry.op = RouteEdit::Op::SetEntry;
            entry.stop = static_cast<std::uint8_t>(i);
            entry.entry = k;
            entry.value = stops[i].entries[k];
            execute(entry);
        }
    }
    RouteEdit way;
    way.player = p.player;
    way.ship = s.id;
    way.op = RouteEdit::Op::OneWay;
    way.on = true;
    execute(way);
    RouteEdit start;
    start.player = p.player;
    start.ship = s.id;
    start.op = RouteEdit::Op::Start;
    execute(start);
    ShipNote& n = note(p, s.id);
    n.patrol = false;
    n.supply = true;
    n.mission = 0;
    n.counter = 0;
    return true;
}

void Engine::update_fleet_target(AiPlayer& p) {
    // 0x00423710: about one ship per city, more against strong human fleets once the computer
    // casts cannons.
    const auto ships = static_cast<std::int32_t>(own_ships(p).size());
    const auto n = static_cast<std::int32_t>(p.cities);
    p.ships_per_city = n > 1 ? std::max(1, ships / n + 1) : ships;
    std::int32_t t = std::min(n, 4);
    if (p.present[kSlotCannons]) {
        std::vector<std::int32_t> cannons(m_world.players().size(), 0);
        for (const Ship& s : m_world.ships()) {
            if (s.owner < cannons.size() && !s.sinking) {
                cannons[s.owner] += s.cannons;
            }
        }
        std::int32_t h = 0;
        for (std::size_t q = 0; q < m_world.players().size(); ++q) {
            if (m_world.players()[q].role == PlayerRole::Human) {
                h = std::max(h, cannons[q]);
            }
        }
        switch (difficulty()) {
            case 0:
                h = h * 3 / 4;
                break;
            case 1:
                break;
            case 2:
                h = h * 3 / 2;
                break;
            default:
                h *= 2;
                break;
        }
        const City* home = home_city(p);
        const std::int32_t own = cannons[p.player];
        if (t > 0 && own < h && home != nullptr && home->stock(m_buildings.materials().cannons) > kTon && own / t > 3) {
            t = h / 8;
        }
        t = std::max(t, 2 * n);
        if (t > ships) {
            t = ships + 1;
        }
    }
    const std::uint32_t limit = m_sim.rules().max_ships_per_player;
    p.fleet_target = limit != 0 ? std::min<std::int32_t>(t, static_cast<std::int32_t>(limit)) : t;
}

void Engine::order_ship(AiPlayer& p) {
    // 0x00422150: a ship of a random type the yard can build, while the money covers half of it.
    if (!p.yard_valid) {
        return;
    }
    const CostBuilding* yard = nullptr;
    for (const CostBuilding& b : m_world.cost_buildings()) {
        if (b.kind == CostBuilding::Kind::Shipyard && b.island == p.yard_island && b.x == p.yard_x && b.y == p.yard_y) {
            yard = &b;
        }
    }
    if (yard == nullptr || yard->order != CostBuilding::kNoOrder) {
        return;
    }
    for (Ship* s : own_ships(p)) {
        if (ship_idle(p, *s) &&
            building_in_reach(m_world, m_buildings, p.yard_island, p.yard_x, p.yard_y, s->x, s->y)) {
            return;
        }
    }
    const Tile t = tile(p.yard_island, p.yard_x, p.yard_y);
    const bool small = !t.valid() || t.type->size_x <= 3;
    const auto type = static_cast<std::uint8_t>((rng().next() & 1U) * 2U + (small ? 0U : 1U));
    const assets::FigureType* f = m_sim.m_figures->get(figure::kShips[type]);
    const std::int64_t price = f != nullptr ? f->ship.price : 0;
    if (std::int64_t{player(p).money} <= price / 2) {
        return;
    }
    ShipyardOrder order;
    order.player = p.player;
    order.island = p.yard_island;
    order.x = p.yard_x;
    order.y = p.yard_y;
    order.op = ShipyardOrder::Op::Build;
    order.ship_type = type;
    execute(order);
}

}  // namespace o1602::sim::ai
