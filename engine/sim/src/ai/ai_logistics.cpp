// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ships of the computer players: every 10 s the repair at the own
// shipyard, one supply run that brings goods home, the size of the fleet and new ships, and the
// patrols around the own islands (AI_Logistics).
#include <algorithm>
#include <array>
#include <cstdlib>

#include "ai_engine.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint32_t kHealthy = 0x5F;          // health * 128 / max above this: at least 75 %
constexpr std::uint32_t kWornPatrol = 64;         // a patrol below half its health goes off duty while the yard repairs
constexpr std::uint64_t kFleetHoldMs = 600'000;   // improved computers: a fleet target holds 10 min
constexpr std::uint16_t kSpareShipFactor = 0x40;  // a spare ship for half its price
constexpr std::uint16_t kFullPriceFactor = 128;   // what the yard asks
constexpr std::int32_t kFirstShipPriceLimit = 100000;  // offers from this price on are not looked at

bool carries_goods(const Ship& s) {
    for (const CargoSlot& c : s.cargo) {
        if (assets::is_good_number(c.ware) && c.amount != 0) {
            return true;
        }
    }
    return false;
}

/// Goods on board the home city has room for. What does not fit stays on board (S-097), and a ship
/// carrying only that is free for the next supply run; otherwise it would lie at home until room
/// came up.
bool carries_goods_for(const Ship& s, const City& home) {
    for (const CargoSlot& c : s.cargo) {
        if (assets::is_good_number(c.ware) && c.amount != 0 && home.stock(c.ware) < home.capacity) {
            return true;
        }
    }
    return false;
}

}  // namespace

ShipNote& Engine::note(AiPlayer& p, std::uint32_t ship) {
    // Called inside loops over the computer's own ships: a walk of the list each time made a
    // logistics pass quadratic in the fleet (TB-16).
    if (const auto it = p.ship_slots.find(ship);
        it != p.ship_slots.end() && it->second < p.ships.size() && p.ships[it->second].id == ship) {
        return p.ships[it->second];
    }
    for (std::size_t i = 0; i < p.ships.size(); ++i) {
        if (p.ships[i].id == ship) {
            p.ship_slots[ship] = static_cast<std::uint32_t>(i);
            return p.ships[i];
        }
    }
    ShipNote n;
    n.id = ship;
    p.ship_slots[ship] = static_cast<std::uint32_t>(p.ships.size());
    p.ships.push_back(n);
    return p.ships.back();
}

void Engine::forget_ships(AiPlayer& p) {
    std::erase_if(p.ships, [&](const ShipNote& n) {
        const Ship* s = m_world.ship(n.id);
        return s == nullptr || s->owner != p.player || s->sinking;
    });
    p.ship_slots.clear();
    for (std::size_t i = 0; i < p.ships.size(); ++i) {
        p.ship_slots[p.ships[i].id] = static_cast<std::uint32_t>(i);
    }
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
    // The white flag does not count (the original tests the wrong bit):
    // a plundered ship gets its next job, and the job strikes the flag.
    const ShipNote& n = note(p, s.id);
    return !ship_moving(s) && !s.route.active && n.mission == 0 && !s.repairing && s.explore_steps == 0 &&
           s.id != p.settle.ship;
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

void Engine::stop_ship(AiPlayer& p, Ship& s) {
    ShipOrder stop;
    stop.player = p.player;
    stop.ship = s.id;
    execute(stop);
    ShipNote& n = note(p, s.id);
    n.patrol = false;
    n.mission = 0;
    n.counter = 0;
}

void Engine::arm_one(City& c, Ship& s) {
    // One cannon from the city's stock onto the ship.
    const assets::FigureType* t = ship_type(s);
    const std::uint8_t cannons = m_buildings.materials().cannons;
    if (t != nullptr && s.cannons < t->ship.max_cannons && c.stock(cannons) > kTon) {
        ++s.cannons;
        remove_goods(c, cannons, kTon);
    }
}

// ---------------------------------------------------------------------------------------------

void Engine::colony_load_timeout(AiPlayer& p) {
    // Open1602: a colony ship called home to load whose city never gathers the wood, tools and
    // bricks lay idle at the Kontor for good, kept from every other job. After kColonyLoadMs it goes
    // back to work, and the next colony starts over later.
    constexpr std::uint64_t kColonyLoadMs = 10ULL * 60 * 1000;
    Settlement& s = p.settle;
    if (s.load_ms == 0) {
        return;
    }
    if (s.ship == 0 || s.active) {
        s.load_ms = 0;
        return;
    }
    if (now_ms() - s.load_ms < kColonyLoadMs) {
        return;
    }
    log(p, "colony ship %u: no load after %llu min, back to work", s.ship,
        static_cast<unsigned long long>((now_ms() - s.load_ms) / 60'000));
    s.ship = 0;
    s.load_ms = 0;
    s.last_ms = now_ms();
}

void Engine::logistics(AiPlayer& p) {
    // A scenario can keep the computer's ships out of supply and patrols.
    if (p.pirates || player(p).scenario.has(scenario_flag::kNoFleet)) {
        return;
    }
    forget_ships(p);
    continue_colony_load(p);
    colony_load_timeout(p);
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
        // Open1602: an improved computer that lost its last city calls its patrols off, so that a
        // ship is free to found anew; before, it sailed round the empty island for good and the
        // search waited for it.
        if (s != nullptr && n.patrol && rivals() && p.cities == 0) {
            RouteEdit off;
            off.player = p.player;
            off.ship = s->id;
            off.op = RouteEdit::Op::Stop;
            execute(off);
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
        cart_supply(p);
        if (Ship* s = pick_logistics_ship(p, healthy)) {
            // As in the original, a ship the computer offered for sale is withdrawn when it gets
            // this job (a patrol keeps the offer).
            if (s->for_sale) {
                ShipCommand withdraw;
                withdraw.player = p.player;
                withdraw.ship = s->id;
                withdraw.op = ShipCommand::Op::WithdrawSale;
                execute(withdraw);
            }
            if (!carries_goods_for(*s, *home) || s->route.active) {
                City* to = rivals() ? supply_destination(p, *home) : home;
                if (!home_supply_route(p, *s, *to) && to != home) {
                    home_supply_route(p, *s, *home);
                }
            } else if (ship_at_city(*s, *home)) {
                unload_at(*home, *s, false, false);  // the original throws away what does not fit (S-097)
                arm_one(*home, *s);
            } else {
                send_to_city(p, *s, *home);
            }
        }
        update_fleet_target(p);
        // Open1602: an improved computer orders no further ship while one of its own lies idle.
        const bool spare = rivals() && std::ranges::any_of(own_ships(p), [&](const Ship* s) {
                               return ship_idle(p, *s) && !s->for_sale;
                           });
        if (static_cast<std::int32_t>(own_ships(p).size()) < p.fleet_target && !spare && !buy_ship(p)) {
            order_ship(p);
        }
    }
    missions(p);
    // Open1602: an improved computer's ship with nothing to carry explores before it patrols, as a
    // player sends his first ship around the islands.
    if (rivals() && p.cities != 0) {
        scout(p);
    }
    for (const City& c : m_world.cities()) {
        if (c.owner != p.player) {
            continue;
        }
        if (static_cast<std::int32_t>(patrols(p, c.island)) < p.ships_per_city) {
            Ship* s = pick_patrol_ship(p, healthy);
            if (s == nullptr) {
                break;
            }
            patrol_route(p, *s, c.island);
        }
    }
    if (rivals()) {
        guard_idle_ship(p, healthy);
    }
}

void Engine::guard_idle_ship(AiPlayer& p, bool healthy) {
    // Open1602: a ship left without a job (more ships than the supply takes, once the fleet target fell
    // back) guards the home island instead of lying at the yard for a quarter of an hour; as a patrol
    // the supply takes it again at the end of a leg. One a look.
    const City* home = home_city(p);
    if (home == nullptr) {
        return;
    }
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        if (ship_idle(p, *s) && n.cooldown == 0 && !s->for_sale && !carries_goods(*s) &&
            (!healthy || health128(*s) > kHealthy)) {
            log(p, "ship %u has no job: guards island %u", s->id, home->island);
            patrol_route(p, *s, home->island);
            return;
        }
    }
}

void Engine::scout(AiPlayer& p) {
    // Open1602: an improved computer sees only what its ships saw. A ship with nothing else to do
    // (no supply run; even the only one, which would otherwise patrol the home island) sails to the
    // nearest island it has not explored and explores it, one ship at a time, so that the island
    // search knows the crops when the city wants them.
    const std::vector<Ship*> ships = own_ships(p);
    if (ships.empty() ||
        std::any_of(p.ships.begin(), p.ships.end(), [](const ShipNote& n) { return n.mission == 10; })) {
        return;
    }
    Ship* ship = nullptr;
    for (Ship* s : ships) {
        // Not with goods on board: the founding goods of a new city would sail away with it.
        if (ship_idle(p, *s) && note(p, s->id).cooldown == 0 && health128(*s) > kHealthy && !carries_goods(*s)) {
            ship = s;
            break;
        }
    }
    if (ship == nullptr) {
        return;
    }
    std::int64_t best = -1;
    Target goal;
    std::uint16_t target = 0;
    for (std::size_t i = 0; i < m_world.islands().size(); ++i) {
        const auto island = static_cast<std::uint16_t>(i);
        if (knows_crops(p, island) || island_size(island) == 0) {
            continue;
        }
        if (island_of(island).discovered_by(p.player) && !island_allowed(p, island)) {
            continue;
        }
        if (rim_goal(*ship, island, best, goal)) {
            target = island;
        }
    }
    if (best < 0) {
        return;
    }
    ShipOrder order;
    order.player = p.player;
    order.ship = ship->id;
    order.target = goal;
    if (!execute(order).accepted) {
        return;
    }
    ShipNote& n = note(p, ship->id);
    n.patrol = false;
    n.mission = 10;
    n.param = target;
    n.counter = 2;
    log(p, "scouting island %u with ship %u", target, ship->id);
}

bool Engine::yard_service(AiPlayer& p) {
    // One ship at a time into the yard for repair.
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
            // A ship already in reach of the home Kontor stays idle: sent there again on every pass
            // it never became free for the other jobs (a new ship from a yard next to the Kontor).
            if (!ship_at_city(*s, *home)) {
                send_to_city(p, *s, *home);
            }
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
    // An idle ship with the fewest cannons, else the patrol with the fewest stops left
    // that lies at the end of a leg; a ship under way is never taken.
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
        // Taking patrols while they sailed turned them into supply runs about twice as often as
        // in the original (62 against 33 new jobs in game10.gam over 31 minutes, 2026-10-27).
        if (!n.patrol || ship_moving(*s) || !s->route.active || (healthy && health128(*s) <= kHealthy)) {
            continue;
        }
        if (n.counter < best_counter) {
            best = s;
            best_counter = n.counter;
        }
    }
    if (best != nullptr || !rivals()) {
        return best;
    }
    // Open1602: an improved computer whose ships all patrol takes the one with the fewest cannons
    // under way: a patrol never lies at the end of a leg long enough, so a computer that had set
    // every ship to guard its island sent no supply run for six hours while its colony held
    // 99 t of the tobacco its home city waited for.
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        if (!n.patrol || !s->route.active || (healthy && health128(*s) <= kHealthy)) {
            continue;
        }
        if (s->cannons < best_cannons) {
            best = s;
            best_cannons = s->cannons;
        }
    }
    return best;
}

Ship* Engine::pick_patrol_ship(AiPlayer& p, bool healthy) {
    // An idle ship with the most cannons; worn-out patrols go home.
    const std::vector<Ship*> ships = own_ships(p);
    const City* home = home_city(p);
    Ship* best = nullptr;
    std::int32_t best_cannons = -1;
    for (Ship* s : ships) {
        ShipNote& n = note(p, s->id);
        const bool worn = healthy && health128(*s) < kWornPatrol;
        if (n.patrol && (n.counter == 0 || worn)) {
            if (n.counter != 0 && !classic() && (home == nullptr || !carries_goods_for(*s, *home))) {
                // Open1602: a damaged patrol with nothing for home stops where it is and waits there
                // until the yard calls it. Sent home, it came straight back out on the next patrol and
                // was sent home again, so that the damaged ships of a long war crowded the home Kontor.
                stop_ship(p, *s);
            } else if (home != nullptr) {
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
        if (worn && !classic()) {
            continue;  // Open1602: it would be called back at the next look; it waits for the yard
        }
        if (ships.size() == 1 && (home == nullptr ? carries_goods(*s) : carries_goods_for(*s, *home))) {
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
    // The corners of the island's rectangle, a little apart each time, in a loop.
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
                                                 std::uint16_t* price, const City* to) {
    // An own city on another island with goods to spare, else the cheapest Kontor
    // of a partner with a trade treaty.
    const auto& m = m_buildings.materials();
    const std::uint32_t reserve = (w == m.tools || w == m.bricks || w == m.wood) ? 28 * kTon : 5 * kTon;
    const auto& cities = m_world.cities();
    if (rivals()) {
        // Open1602: an improved computer weighs the way. Its nearest own city with goods to spare,
        // else the partner whose price, raised by the hours at sea, is lowest: ten minutes away
        // count as the price twice.
        const City* home = to != nullptr ? to : home_city(p);
        if (home == nullptr) {
            return std::nullopt;
        }
        const auto at = city_point(*home);
        std::optional<std::size_t> nearest;
        std::uint32_t nearest_s = 0;
        std::optional<std::size_t> best;
        std::uint64_t best_cost = 0;
        std::uint16_t best_price = 1000;
        for (std::size_t i = 0; i < cities.size(); ++i) {
            const City& c = cities[i];
            if (c.island == home_island) {
                continue;
            }
            if (c.owner == p.player) {
                if (own_spare(c, w, reserve, is_home(p, c)) != 0) {
                    const std::uint32_t sec = sail_seconds(p, at, city_point(c));
                    if (!nearest || sec < nearest_s) {
                        nearest = i;
                        nearest_s = sec;
                    }
                }
                continue;
            }
            if (m_world.player(c.owner) == nullptr ||
                m_world.diplomacy().relation(p.player, c.owner).trade != Treaty::Signed) {
                continue;
            }
            const City::Trade* t = c.trade_of(w);
            if (t == nullptr || t->sell_price >= 1000 || partner_spare(p, c, w) == 0) {
                continue;
            }
            const std::uint64_t cost = std::uint64_t{t->sell_price} * (600 + sail_seconds(p, at, city_point(c)));
            if (!best || cost < best_cost) {
                best = i;
                best_cost = cost;
                best_price = t->sell_price;
            }
        }
        if (nearest) {
            *price = 0;
            return nearest;
        }
        *price = best_price;
        return best;
    }
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
        // Any partner with a trade treaty, the pirates' nest and the natives' villages too, as in the
        // original.
        if (c.owner == p.player || o == nullptr) {
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

std::vector<std::uint8_t> Engine::supply_wares(const AiPlayer& p, const City& home, std::uint32_t* target) const {
    std::vector<std::uint8_t> wares{m_buildings.food_ware(), m_buildings.materials().tools,
                                    m_buildings.materials().bricks, m_buildings.materials().wood, ware::kCloth};
    *target = 10 * kTon;
    // Open1602: an improved rival also the goods its houses will want once they rise (demand_ahead).
    const auto wanted = [&](std::uint8_t w) { return (rivals() ? demand_ahead(home, w) : demand(home, w)) > 0; };
    if (wanted(ware::kSpices) || wanted(ware::kTobaccoGoods) || p.max_stage > 30) {
        wares.insert(wares.end(), {ware::kTobaccoGoods, ware::kSpices, ware::kAlcohol, ware::kOre, ware::kIron});
        *target = 20 * kTon;
    }
    if (wanted(ware::kCocoa) || p.max_stage > 39) {
        wares.insert(wares.end(), {ware::kCocoa, ware::kClothes});
        *target = 30 * kTon;
    }
    if (p.max_stage > 49) {
        wares.insert(wares.end(), {ware::kGold, ware::kJewellery});
        *target = 40 * kTon;
    }
    // Open1602: an improved computer brings first what runs out soonest (supply_urgency), so that
    // cloth or alcohol at the end of the list waits no longer behind building materials in store.
    if (rivals()) {
        const std::uint32_t keep = *target;
        std::ranges::stable_sort(wares, {}, [&](std::uint8_t w) { return supply_urgency(home, w, keep); });
    }
    return wares;
}

namespace {

/// The wares a route unloads (at `to` only, when given).
void unloaded_wares(const TradeRoute& route, std::vector<std::uint8_t>& out, const Target* to = nullptr) {
    for (const RouteStop& stop : route.stops) {
        if (to != nullptr && !(stop.target == *to)) {
            continue;
        }
        for (const RouteEntry& e : stop.entries) {
            if (e.ware != 0 && e.unload) {
                out.push_back(e.ware);
            }
        }
    }
}

}  // namespace

void Engine::cart_supply(AiPlayer& p) {
    // Open1602's computers: a city buys what it lacks from a partner's city on its own island with
    // its trade cart, as players do, before a ship sails for it. The original's computers never
    // send a cart out.
    if (classic()) {
        return;
    }
    for (std::size_t h = 0; h < m_world.cities().size(); ++h) {
        const City& home = m_world.cities()[h];
        if (home.owner != p.player || !home.warehouse_x || !home.warehouse_y) {
            continue;
        }
        const Ship* cart = trade_cart_of(m_world, p.player, home.island);
        if (cart != nullptr && (cart->route.active || ship_moving(*cart))) {
            continue;  // on its way
        }
        std::uint32_t target = 0;
        const std::vector<std::uint8_t> wares = supply_wares(p, home, &target);
        // Natives give their goods for goodwill, which goods they take bring: cloth or alcohol from
        // home, a cart load of it (10 t) while home keeps 5 t; nothing where they take neither.
        const auto natives_gift = [&](const City& c) -> std::uint8_t {
            for (const std::uint8_t g : {ware::kCloth, ware::kAlcohol}) {
                const City::Trade* t = c.trade_of(g);
                if (t != nullptr && t->buy && c.stock(g) + kTon <= t->limit && home.stock(g) >= 15 * kTon) {
                    return g;
                }
            }
            return 0;
        };
        const auto goodwill = [&](const City& c) { return p.player < c.goodwill.size() ? c.goodwill[p.player] : 0U; };
        // A partner the cart cannot drive to and back from stays with the supply ships: forests or
        // walls can part two cities of an island.
        const WorldIsland& home_isl = island_of(home.island);
        const auto cart_gets_there = [&](const City& c) {
            const Target to = Target::city_of(c.island, c.index);
            Fleet& fleet = m_sim.m_fleet;
            return fleet.cart_way(p.player, home_isl.x + *home.warehouse_x, home_isl.y + *home.warehouse_y, to) &&
                   (cart == nullptr || fleet.cart_way(p.player, cart->x, cart->y, to));
        };
        std::optional<std::size_t> source;
        std::uint8_t ware = 0;
        for (const std::uint8_t w : wares) {
            if (home.stock(w) >= target) {
                continue;
            }
            std::uint16_t best_price = 1000;
            for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
                const City& c = m_world.cities()[i];
                if (c.island != home.island || c.owner == p.player || !c.warehouse_x || !c.warehouse_y ||
                    m_world.player(c.owner) == nullptr ||
                    m_world.diplomacy().relation(p.player, c.owner).trade != Treaty::Signed) {
                    continue;
                }
                const City::Trade* t = c.trade_of(w);
                if (t == nullptr || !t->sell || c.stock(w) <= t->keep + 5 * kTon || t->sell_price >= best_price) {
                    continue;
                }
                // Not to natives who would give nothing (too little goodwill for a ton) and take no gift.
                if (m_world.acts_as(c.owner, FactionBehaviour::Natives) &&
                    goodwill(c) < std::max<std::uint32_t>(1, m_world.goods().price(w) * 42U / 128U) &&
                    natives_gift(c) == 0) {
                    continue;
                }
                if (!cart_gets_there(c)) {
                    continue;
                }
                source = i;
                best_price = t->sell_price;
            }
            if (source) {
                ware = w;
                break;
            }
        }
        if (!source) {
            continue;
        }
        const City& from = m_world.cities()[*source];
        const std::uint8_t home_index = home.index;
        const std::uint16_t island = home.island;
        const Target load = Target::city_of(from.island, from.index);
        const std::uint32_t amount = std::max<std::uint32_t>(target - home.stock(ware), 5 * kTon);
        // A gift only while the goodwill there would not pay for the load.
        const std::uint64_t needed =
            std::uint64_t{std::max<std::uint32_t>(1, m_world.goods().price(ware) * 42U / 128U)} * amount / kTon;
        const std::uint8_t gift =
            m_world.acts_as(from.owner, FactionBehaviour::Natives) && goodwill(from) < needed ? natives_gift(from) : 0;
        std::uint32_t id = cart != nullptr ? cart->id : 0;
        if (cart == nullptr) {
            const WorldIsland& isl = island_of(island);
            CartRequest request;
            request.player = p.player;
            request.island = island;
            request.city = home_index;
            request.x = static_cast<std::uint16_t>(isl.x + *from.warehouse_x);
            request.y = static_cast<std::uint16_t>(isl.y + *from.warehouse_y);
            if (!execute(request).accepted) {
                continue;
            }
            const Ship* made = trade_cart_of(m_world, p.player, island);
            if (made == nullptr) {
                continue;
            }
            id = made->id;
            log(p, "trade cart %u on island %u", id, island);
        }
        // Load at the partner, unload at home, once.
        const auto edit = [&](RouteEdit e) {
            e.player = p.player;
            e.ship = id;
            execute(e);
        };
        RouteEdit clear;
        clear.op = RouteEdit::Op::ClearStop;
        clear.stop = 0xFF;
        edit(clear);
        // To natives first the gift from home, unloaded there before the ware is loaded.
        struct Stop {
            Target target;
            std::vector<RouteEntry> entries;
        };
        std::vector<Stop> stops;
        const Target home_stop = Target::city_of(island, home_index);
        if (gift != 0) {
            stops.push_back(Stop{home_stop, {RouteEntry{gift, false, kCartHoldCapacity}}});
            stops.push_back(Stop{load, {RouteEntry{gift, true, kCartHoldCapacity}, RouteEntry{ware, false, amount}}});
        } else {
            stops.push_back(Stop{load, {RouteEntry{ware, false, amount}}});
        }
        // Home again: the ware, and what natives left of a gift, back into the home city's stock.
        Stop back{home_stop, {RouteEntry{ware, true, 50 * kTon}}};
        for (const std::uint8_t g : {assets::ware::kCloth, assets::ware::kAlcohol}) {
            const bool on_board = cart != nullptr && std::any_of(cart->cargo.begin(), cart->cargo.end(),
                                                                 [g](const CargoSlot& c) { return c.ware == g; });
            if (g != ware && (g == gift || on_board)) {
                back.entries.push_back(RouteEntry{g, true, kCartHoldCapacity});
            }
        }
        stops.push_back(std::move(back));
        for (std::size_t i = 0; i < stops.size(); ++i) {
            RouteEdit stop;
            stop.op = RouteEdit::Op::SetStop;
            stop.stop = static_cast<std::uint8_t>(i);
            stop.target = stops[i].target;
            edit(stop);
            for (std::size_t e = 0; e < stops[i].entries.size(); ++e) {
                RouteEdit entry;
                entry.op = RouteEdit::Op::SetEntry;
                entry.stop = static_cast<std::uint8_t>(i);
                entry.entry = static_cast<std::uint8_t>(e);
                entry.value = stops[i].entries[e];
                edit(entry);
            }
        }
        RouteEdit way;
        way.op = RouteEdit::Op::OneWay;
        way.on = true;
        edit(way);
        RouteEdit start;
        start.op = RouteEdit::Op::Start;
        edit(start);
        log(p, "trade cart %u fetches ware %u from city %u/%u, gift %u", id, ware, load.island, load.city, gift);
    }
}

std::vector<std::uint8_t> Engine::routed_wares(AiPlayer& p, std::uint32_t ship, const City& to) {
    // Open1602: an improved computer's supply ships sail to several of its cities; a ware on the
    // way to one of them is not on the way to another.
    const Target at = Target::city_of(to.island, to.index);
    const Target* only = rivals() ? &at : nullptr;
    std::vector<std::uint8_t> routed;
    for (const ShipNote& n : p.ships) {
        const Ship* o = m_world.ship(n.id);
        if (o == nullptr || o->id == ship || !n.supply || !o->route.active) {
            continue;
        }
        unloaded_wares(o->route, routed, only);
    }
    if (const Ship* cart = trade_cart_of(m_world, p.player, to.island);
        cart != nullptr && cart->route.active && !classic()) {
        unloaded_wares(cart->route, routed);
    }
    return routed;
}

std::uint32_t Engine::own_spare(const City& c, std::uint8_t w, std::uint32_t reserve, bool home) const {
    // Open1602: what an improved computer's city can give of a ware to another of its cities. One
    // where people live gives only what it makes beyond their use, keeping 20 t, so that a city the
    // runs bring a ware to does not pass it on (it ran dry itself). The home city also keeps what
    // its people use in kSpareKeepMinutes: a home that gave its alcohol to a new colony down to
    // 18 t ran out of it within the hour. A colony does not: kept back there, the cloth and alcohol
    // it made for the home city lay in its Kontor while the home city went without. A city where
    // nobody lives gives all beyond the reserve.
    std::uint32_t keep = reserve;
    if (population(c) != 0 && demand(c, w) != 0) {
        if (supply(c, w) <= demand(c, w)) {
            // A colony that makes less than it uses still gives what lasts it beyond
            // kSpareKeepMinutes: it held its alcohol in store while the home city, which pays for
            // it, ran dry (2323 runs found no source so).
            if (home) {
                return 0;
            }
            keep = std::max<std::uint32_t>({reserve, 20 * kTon, kSpareKeepMinutes * demand(c, w)});
        } else {
            keep = std::max<std::uint32_t>({reserve, 20 * kTon, home ? kSpareKeepMinutes * demand(c, w) : 0U});
        }
    }
    // Less than kMinSupplyRun is not worth a run: ships sailed for a ton or four and crowded the
    // narrow harbours of the cities they left.
    return c.stock(w) >= keep + kMinSupplyRun ? c.stock(w) - keep : 0;
}

std::uint32_t Engine::partner_spare(AiPlayer& p, const City& c, std::uint8_t w) {
    // Open1602: what an improved computer can still buy of a ware at a partner's Kontor: what it
    // sells beyond what it keeps (and 5 t), less what the computer's own runs are already on their
    // way to load there; natives only for as much as the goodwill there pays for. Bought on what
    // the Kontor merely held, nearly two thirds of the runs came back empty.
    const City::Trade* t = c.trade_of(w);
    if (t == nullptr || !t->sell || c.stock(w) <= t->keep + 5 * kTon) {
        return 0;
    }
    std::uint64_t spare = c.stock(w) - t->keep - 5 * kTon;
    if (m_world.acts_as(c.owner, FactionBehaviour::Natives)) {
        const std::uint32_t goodwill = p.player < c.goodwill.size() ? c.goodwill[p.player] : 0U;
        const std::uint32_t per_ton = std::max<std::uint32_t>(1, m_world.goods().price(w) * 42U / 128U);
        spare = std::min<std::uint64_t>(spare, std::uint64_t{goodwill} / per_ton * kTon);
    }
    // Every ship's route that loads there, the computer's own and other buyers' (the trade routes
    // sail in plain sight): bought on its own runs alone, every other purchase at a computer's
    // Kontor still came back empty.
    const Target at = Target::city_of(c.island, c.index);
    std::uint64_t booked = 0;
    for (const Ship& o : m_world.ships()) {
        if (!o.route.active || o.sinking) {
            continue;
        }
        for (const RouteStop& stop : o.route.stops) {
            if (!(stop.target == at)) {
                continue;
            }
            for (const RouteEntry& e : stop.entries) {
                if (e.ware == w && !e.unload) {
                    booked += e.amount;
                }
            }
        }
    }
    return spare >= booked + kMinSupplyRun ? static_cast<std::uint32_t>(spare - booked) : 0;
}

std::uint32_t Engine::supply_load(AiPlayer& p) {
    // What a supply run brings of one ware: half the holds (a run takes two wares), by the
    // computer's ships on average, at least one hold.
    std::size_t holds = 0;
    std::size_t ships = 0;
    for (const Ship* s : own_ships(p)) {
        holds += usable_holds(*s, *m_sim.m_figures);
        ++ships;
    }
    const std::size_t half = ships != 0 ? holds / (2 * ships) : 1;
    return static_cast<std::uint32_t>(std::max<std::size_t>(half, 1) * kHoldCapacity);
}

std::uint32_t Engine::supply_ships_for(AiPlayer& p, const City& to, std::uint8_t w, std::size_t src,
                                       std::uint32_t seconds) {
    // Open1602: as many runs at a time as it takes to bring what the city uses beyond what it
    // makes while one run sails there and back: its shortfall a minute times the minutes of the
    // round trip (a minute at each end for the loading), over what one run brings of the ware. From
    // an own city no more than that city makes beyond its own use, so that the runs do not wait
    // there for goods. At least one, at most kMaxRunsPerWare.
    const City& from = m_world.cities()[src];
    const std::uint32_t used = demand(to, w);
    const std::uint32_t made = supply(to, w);
    std::uint64_t rate = used > made ? used - made : 0;  // 1/32 t a minute
    if (from.owner == p.player) {
        const std::uint32_t there_used = demand(from, w);
        const std::uint32_t there_made = supply(from, w);
        rate = std::min<std::uint64_t>(rate, there_made > there_used ? there_made - there_used : 0);
    }
    const std::uint64_t round_s = 2 * std::uint64_t{seconds} + 120;
    const std::uint64_t per_round = rate * round_s / 60;  // stock units over one round trip
    const std::uint64_t load = supply_load(p);
    const std::uint64_t runs = (per_round + load - 1) / load;
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(runs, 1, kMaxRunsPerWare));
}

std::uint64_t Engine::supply_urgency(const City& c, std::uint8_t w, std::uint32_t target) const {
    const auto& m = m_buildings.materials();
    if (w == m.tools || w == m.bricks || w == m.wood) {
        return c.stock(w) < target / 2 ? 0 : kNoUrgency;
    }
    // Open1602: an improved rival also brings what its houses will use once they rise (demand_ahead),
    // to have it in store before (ascent_ready).
    if (rivals() && demand(c, w) == 0) {
        const std::uint32_t ahead = demand_ahead(c, w);
        if (ahead != 0 && supply(c, w) < ahead) {
            return std::uint64_t{c.stock(w)} * 256 / ahead;
        }
    }
    // Food made only just as fast as it is eaten counts too: the farms' rate is what they make at
    // best, and a colony that made 220 against 207 ran dry within the hour.
    if (rivals() && w == m_buildings.food_ware()) {
        if (const std::uint32_t used = demand(c, w);
            used != 0 && 4 * std::uint64_t{supply(c, w)} < 5 * std::uint64_t{used}) {
            return std::uint64_t{c.stock(w)} * 256 / used;
        }
    }
    return stock_lasts(c, w);
}

std::uint64_t Engine::supply_need(AiPlayer& p, const City& c, std::uint8_t only) {
    std::uint32_t target = 0;
    const std::vector<std::uint8_t> wares = supply_wares(p, c, &target);
    const std::vector<std::uint8_t> routed = routed_wares(p, 0, c);
    for (const std::uint8_t w : wares) {  // the most urgent first
        if (only != 0 && w != only) {
            continue;
        }
        const std::uint64_t urgency = supply_urgency(c, w, target);
        if (urgency == kNoUrgency) {
            break;
        }
        std::uint16_t price = 0;
        const auto src = supply_source(p, c.island, w, &price, &c);
        if (src &&
            static_cast<std::uint32_t>(std::ranges::count(routed, w)) <
                supply_ships_for(p, c, w, *src, sail_seconds(p, city_point(c), city_point(m_world.cities()[*src])))) {
            return urgency;
        }
    }
    return kNoUrgency;
}

City* Engine::supply_destination(AiPlayer& p, City& home) {
    // Open1602: an improved computer supplies each of its cities where people live, not the home
    // city alone: the next ship goes where a ware runs out soonest; the home city keeps its runs
    // for the rest. While a ware of the home city lasts less than kHomeFirstMinutes, the ship goes
    // there: the home city pays for the others, and a run of alcohol that went to a new colony
    // while the home city had none took its merchants with it.
    City* best = &home;
    // Food first: while the home city's own food lasts, a city whose food runs out within
    // kHomeFirstMinutes gets the run before any good the home city waits for. A colony whose fields
    // had no room left starved with 220 t of food in the home city's Kontor, while the computer's
    // only ship brought cocoa to the home city run after run.
    const std::uint8_t food = m_buildings.food_ware();
    const std::uint64_t first = std::uint64_t{kHomeFirstMinutes} * 256;
    if (supply_need(p, home, food) >= first) {
        City* hungry = nullptr;
        std::uint64_t hungry_need = first;
        for (const City& c : m_world.cities()) {
            if (c.owner != p.player || &c == &home || c.island == home.island || !c.warehouse_x || population(c) == 0) {
                continue;
            }
            if (const std::uint64_t need = supply_need(p, c, food); need < hungry_need) {
                if (City* to = city(c.island, c.index); to != nullptr) {
                    hungry = to;
                    hungry_need = need;
                }
            }
        }
        if (hungry != nullptr) {
            return hungry;
        }
    }
    std::uint64_t best_need = supply_need(p, home);
    if (best_need < first) {
        return best;
    }
    for (const City& c : m_world.cities()) {
        if (c.owner != p.player || &c == &home || c.island == home.island || !c.warehouse_x || population(c) == 0) {
            continue;
        }
        const std::uint64_t need = supply_need(p, c);
        if (need < best_need) {
            if (City* to = city(c.island, c.index); to != nullptr) {
                best = to;
                best_need = need;
            }
        }
    }
    return best;
}

bool Engine::food_runs_out(AiPlayer& p) {
    for (const City& c : m_world.cities()) {
        if (c.owner == p.player && c.warehouse_x && population(c) != 0 &&
            supply_need(p, c, m_buildings.food_ware()) < std::uint64_t{kHomeFirstMinutes} * 256) {
            return true;
        }
    }
    return false;
}

bool Engine::home_supply_route(AiPlayer& p, Ship& s, City& home) {
    // One or two wares the home city lacks, from where they are, one-way home. An improved
    // computer's runs go to any of its cities (supply_destination); `home` is that city.
    std::uint32_t target = 0;
    const std::vector<std::uint8_t> wares = supply_wares(p, home, &target);
    // Wares other supply ships already bring, and the trade cart on the home island.
    const std::vector<std::uint8_t> routed = routed_wares(p, s.id, home);
    const bool own_home = &home == home_city(p);
    struct Pick {
        std::uint8_t ware = 0;
        std::size_t source = 0;
        std::uint32_t amount = 0;
        bool own = false;
        std::uint32_t seconds = 0;  ///< improved computers: the way there
    };
    const auto choose = [&](std::uint8_t exclude) -> std::optional<Pick> {
        for (const std::uint8_t w : wares) {
            const auto on_way = static_cast<std::uint32_t>(std::ranges::count(routed, w));
            if (w == exclude || (on_way != 0 && classic())) {
                continue;
            }
            if (classic() && home.stock(w) >= target) {
                continue;
            }
            if (!own_home && supply_urgency(home, w, target) == kNoUrgency) {
                break;  // another city gets only what runs out there (the wares come most urgent first)
            }
            std::uint16_t price = 0;
            const auto src = supply_source(p, home.island, w, &price, &home);
            if (!src) {
                continue;
            }
            std::uint32_t keep = target;
            std::uint32_t seconds = 0;
            if (rivals()) {
                // Open1602: the further the source, the more the ship brings: what the city needs
                // until the ship can be back, at most half the ship's holds.
                seconds = sail_seconds(p, city_point(home), city_point(m_world.cities()[*src]));
                keep = supply_buffer(target, 2 * seconds);
                // Food for kHomeFirstMinutes of what the city eats, up to half its Kontor: 40 t
                // lasted a colony of 450 people four minutes.
                if (w == m_buildings.food_ware()) {
                    const std::uint64_t eaten = std::uint64_t{kHomeFirstMinutes} * demand(home, w);
                    keep = std::max<std::uint32_t>(
                        keep, static_cast<std::uint32_t>(std::min<std::uint64_t>(eaten, home.capacity / 2)));
                }
                if (home.stock(w) >= keep || on_way >= supply_ships_for(p, home, w, *src, seconds)) {
                    continue;
                }
            }
            const std::uint32_t need = keep - home.stock(w);
            const bool own = m_world.cities()[*src].owner == p.player;
            std::uint32_t amount = std::max<std::uint32_t>(need, 20 * kTon);
            if (rivals()) {
                const auto half = static_cast<std::uint32_t>(
                    std::max<std::size_t>(1, usable_holds(s, *m_sim.m_figures) / 2) * kHoldCapacity);
                amount = std::min(std::max(amount, own ? 50 * kTon : amount), half);
                if (!own) {
                    amount = std::min(amount, partner_spare(p, m_world.cities()[*src], w));
                } else {
                    // no more than the city can spare (own_spare), so that it does not run dry
                    const auto& m = m_buildings.materials();
                    const std::uint32_t reserve = (w == m.tools || w == m.bricks || w == m.wood) ? 28 * kTon : 5 * kTon;
                    const City& from = m_world.cities()[*src];
                    amount = std::min(amount, own_spare(from, w, reserve, is_home(p, from)));
                }
            }
            return Pick{w, *src, amount, own, seconds};
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
    load1.entries[1] = RouteEntry{first->ware, false, first->own && classic() ? 50U * kTon : first->amount};
    stops.push_back(load1);
    if (second) {
        const City& src2 = m_world.cities()[second->source];
        Stop load2{Target::city_of(src2.island, src2.index), {}};
        // The original loads the first ware's amount here too (S-101).
        load2.entries[1] = RouteEntry{second->ware, false, second->own && classic() ? 50U * kTon : second->amount};
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
    if (stops.size() > TradeRoute::kOriginalStops) {  // the supply runs of the original
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
    if (rivals()) {
        log(p, "supply ship %u: ware %u (%u t) from city %u/%u to %u/%u, %u s away%s", s.id, first->ware,
            first->amount / kTon, src1.island, src1.index, home.island, home.index, first->seconds,
            second ? ", and a second ware" : "");
    }
    return true;
}

void Engine::update_fleet_target(AiPlayer& p) {
    // About one ship per city, more against strong human fleets once the computer
    // casts cannons.
    const auto ships = static_cast<std::int32_t>(own_ships(p).size());
    const auto n = static_cast<std::int32_t>(p.cities);
    p.ships_per_city = n > 1 ? std::max(1, ships / n + 1) : ships;
    // At most four as in the original, or as many as the computer may found (Rules::computer_cities).
    std::int32_t t = std::min(n, static_cast<std::int32_t>(city_limit()));
    std::int32_t supply = 0;
    if (rivals()) {
        // Open1602: an improved computer keeps one ship to guard each city and as many more as its
        // supply takes, by how much it lacks and how far it sails for it. The ships carry rather
        // than circle their islands.
        p.ships_per_city = 1;
        // In debt it wants no ships beyond its guards.
        if (const City* home = home_city(p); home != nullptr && player(p).money >= 0) {
            supply = supply_ships_wanted(p, *home);
            // Open1602: and one more for a colony it wants while no ship is free to sail for it.
            if (colony_crop_missing(p) && colony_ship_candidate(p) == 0) {
                ++supply;
            }
        }
        t = std::min(t + supply, std::max(n, 1) + static_cast<std::int32_t>(kMaxSupplyShips));
    }
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
    const std::int32_t before = p.fleet_target;
    p.fleet_target = limit != 0 ? std::min<std::int32_t>(t, static_cast<std::int32_t>(limit)) : t;
    if (rivals()) {
        // The target falls only once the supply wanted fewer ships for ten minutes.
        if (p.fleet_target >= before) {
            p.fleet_ms = std::max<std::uint64_t>(now_ms(), 1);
        } else if (now_ms() - p.fleet_ms < kFleetHoldMs) {
            p.fleet_target = before;
        }
    }
    if (rivals() && p.fleet_target != before) {
        log(p, "fleet target %d (ships %d, cities %d, for supply %d)", p.fleet_target, ships, n, supply);
    }
}

std::pair<std::int32_t, std::int32_t> Engine::city_point(const City& c) const {
    const WorldIsland& isl = island_of(c.island);
    if (c.warehouse_x && c.warehouse_y) {
        return {static_cast<std::int32_t>(isl.x + *c.warehouse_x), static_cast<std::int32_t>(isl.y + *c.warehouse_y)};
    }
    return {static_cast<std::int32_t>(isl.x + isl.map.width() / 2),
            static_cast<std::int32_t>(isl.y + isl.map.height() / 2)};
}

std::uint32_t Engine::sail_seconds(const AiPlayer& p, std::pair<std::int32_t, std::int32_t> from,
                                   std::pair<std::int32_t, std::int32_t> to) {
    const auto dx = static_cast<std::uint64_t>(std::abs(from.first - to.first));
    const auto dy = static_cast<std::uint64_t>(std::abs(from.second - to.second));
    const std::uint64_t diagonal = std::min(dx, dy);
    const std::uint64_t straight = std::max(dx, dy) - diagonal;
    // A ship of 500 sails a straight tile in 0.4 s (Fleet::speed_milli); the way round the coasts
    // is taken as a quarter longer.
    std::uint64_t us_straight = 400'000;
    std::uint64_t us_diagonal = 565'685;
    const std::vector<Ship*> ships = own_ships(p);
    if (!ships.empty()) {
        if (const std::uint32_t milli = m_sim.fleet().speed_milli(*ships.front()); milli != 0) {
            us_straight = 1'000'000'000ULL / milli;
            us_diagonal = us_straight * 1414 / 1000;
        }
    }
    const std::uint64_t us = (straight * us_straight + diagonal * us_diagonal) * 5 / 4;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(us / 1'000'000, 1'000'000));
}

std::uint32_t Engine::supply_buffer(std::uint32_t target, std::uint32_t seconds) {
    // Ten minutes there and back double what the city keeps, at most four times as much.
    return target + static_cast<std::uint32_t>(std::uint64_t{target} * std::min<std::uint32_t>(seconds, 1800) / 600);
}

std::int32_t Engine::supply_ships_wanted(AiPlayer& p, const City& home) {
    // A ship brings two wares. Open1602: an improved computer counts for each ware the runs it
    // takes at a time (supply_ships_for: what the city lacks a minute against the way there and
    // back), for a ware it lacks for good as well as for one low in store. At most kMaxSupplyShips.
    std::uint64_t runs = 0;
    const auto count = [&](const City& to, bool all) {
        std::uint32_t target = 0;
        const std::vector<std::uint8_t> wares = supply_wares(p, to, &target);
        const auto at = city_point(to);
        for (const std::uint8_t w : wares) {
            if (!all && supply_urgency(to, w, target) == kNoUrgency) {
                break;
            }
            std::uint16_t price = 0;
            const auto src = supply_source(p, to.island, w, &price, &to);
            if (!src) {
                continue;
            }
            const std::uint32_t seconds = sail_seconds(p, at, city_point(m_world.cities()[*src]));
            if (to.stock(w) >= supply_buffer(target, 2 * seconds) && demand(to, w) <= supply(to, w)) {
                continue;
            }
            runs += supply_ships_for(p, to, w, *src, seconds);
        }
    };
    count(home, true);
    // Open1602: the other cities where people live, for what runs out there (supply_destination).
    for (const City& c : m_world.cities()) {
        if (c.owner == p.player && &c != &home && c.island != home.island && c.warehouse_x && population(c) != 0) {
            count(c, false);
        }
    }
    return static_cast<std::int32_t>(std::min<std::uint64_t>((runs + 1) / 2, kMaxSupplyShips));
}

void Engine::order_ship(AiPlayer& p) {
    // A ship of a random type the yard can build, while the money covers half of it.
    // Never in the tutorial.
    if (tutorial()) {
        return;
    }
    if (!p.yard_valid) {
        // Without a yard the computer plans one (when a building task is on top of its
        // stack), whatever its stage; the stage lists ask for it only from the merchants
        // on. Without this a computer never had more than its first ship. The original plans it
        // right here and so replaces the search of the paused task (GB-2); Open1602 leaves the
        // request for the next develop step; the original's computers keep the quirk.
        if (top(p) > kSurvey) {
            if (classic()) {
                if (need_shipyard(p)) {
                    return;
                }
            } else {
                p.yard_wanted = true;
            }
            // Until the yard stands, a computer without any ship buys the cheapest one on offer, as
            // in the original.
            if (own_ships(p).empty()) {
                buy_first_ship(p);
            }
        }
        return;
    }
    const CostBuilding* yard = nullptr;
    for (const CostBuilding& b : m_world.cost_buildings()) {
        if (b.kind == CostBuilding::Kind::Shipyard && b.island == p.yard_island && b.x == p.yard_x && b.y == p.yard_y) {
            yard = &b;
        }
    }
    if (yard == nullptr) {
        return;
    }
    const std::vector<Ship*> ships = own_ships(p);
    Ship* at_yard = nullptr;
    for (Ship* s : ships) {
        if (ship_idle(p, *s) &&
            building_in_reach(m_world, m_buildings, p.yard_island, p.yard_x, p.yard_y, s->x, s->y)) {
            at_yard = s;
            break;
        }
    }
    if (yard->order == CostBuilding::kNoOrder && at_yard == nullptr) {
        const Tile t = tile(p.yard_island, p.yard_x, p.yard_y);
        const bool small = !t.valid() || t.type->size_x <= 3;
        const auto type = static_cast<std::uint8_t>((rng().next() & 1U) * 2U + (small ? 0U : 1U));
        // Open1602: a ship of a package of that type is built as often as the original's
        // (docs/modding/own-figures.md#ships); the original's computers know only theirs.
        std::uint32_t package_ship = 0;
        if (!classic()) {
            std::vector<std::uint32_t> choice{0};
            for (const auto& [number, ship_type] : m_sim.m_figures->package_ships()) {
                if (ship_type == type) {
                    choice.push_back(number);
                }
            }
            if (choice.size() > 1) {
                package_ship = choice[rng().next() % choice.size()];
            }
        }
        const assets::FigureType* f =
            m_sim.m_figures->get(package_ship != 0 ? package_ship : figure::ship_figure(type));
        const std::int64_t price = f != nullptr ? f->ship.price : 0;
        // Open1602: an improved computer pays for a ship only with the price in hand; the original's
        // orders with half of it and may run into debt. An improved rival's order also leaves its gold for the goods it
        // buys (buy_reserve).
        // Also the gold the costs eat in kCostlyMinutes while they exceed the income: three ships
        // ordered within half an hour took a computer to −16,706 gold.
        const std::int64_t drain =
            rivals() && !p.pirates ? kCostlyMinutes * std::max<std::int64_t>(0, -std::int64_t{earned_income(p)}) : 0;
        const std::int64_t needed = classic() ? price / 2 : price + buy_reserve(p) + drain;
        if (std::int64_t{player(p).money} > needed) {
            ShipyardOrder order;
            order.player = p.player;
            order.island = p.yard_island;
            order.x = p.yard_x;
            order.y = p.yard_y;
            order.op = ShipyardOrder::Op::Build;
            order.ship_type = type;
            order.figure = package_ship;
            if (execute(order).accepted && rivals()) {
                log(p, "ordered a ship of type %u at the yard on island %u (money %d)", type, p.yard_island,
                    player(p).money);
            }
            return;
        }
    }
    // The yard is busy, a ship lies idle at it, or the money is short: with at least as many ships
    // as cities and none on offer yet, the ship at the yard is offered for half its price if it has
    // no cannons and nothing on board (the others are told), and gets no job for four looks.
    if (ships.size() < p.cities || at_yard == nullptr || at_yard->cannons != 0 ||
        std::any_of(ships.begin(), ships.end(), [](const Ship* s) { return s->for_sale; }) ||
        std::any_of(at_yard->cargo.begin(), at_yard->cargo.end(),
                    [](const CargoSlot& c) { return c.ware != 0 && c.amount != 0; })) {
        return;
    }
    ShipCommand offer;
    offer.player = p.player;
    offer.ship = at_yard->id;
    offer.op = ShipCommand::Op::OfferForSale;
    offer.factor = kSpareShipFactor;
    execute(offer);
    note(p, at_yard->id).cooldown = 4;
    log(p, "ship %u offered for sale", at_yard->id);
}

void Engine::buy_first_ship(AiPlayer& p) {
    // The original's: the cheapest ship another player offers, if the price stays below the money.
    // The original asks for no peace and buys from anyone; Open1602 leaves out the offers its Buy
    // turns down (pirates, soldiers on board) so that they do not hide a ship it could buy.
    const std::int64_t money = player(p).money;
    const Ship* best = nullptr;
    std::int32_t best_price = kFirstShipPriceLimit;
    for (const Ship& s : m_world.ships()) {
        const Player* seller = m_world.player(s.owner);
        if (!s.for_sale || s.sinking || s.owner == p.player || seller == nullptr ||
            (seller->role != PlayerRole::Human && seller->role != PlayerRole::Computer) ||
            std::any_of(s.cargo.begin(), s.cargo.end(), [](const CargoSlot& c) { return is_soldier_ware(c.ware); })) {
            continue;
        }
        const std::int32_t price = m_sim.fleet().sale_price(s);
        if (price < best_price) {
            best = &s;
            best_price = price;
        }
    }
    if (best == nullptr || std::int64_t{best_price} >= money) {
        return;
    }
    ShipCommand buy;
    buy.player = p.player;
    buy.ship = best->id;
    buy.op = ShipCommand::Op::Buy;
    const std::uint32_t id = best->id;
    if (execute(buy).accepted) {
        log(p, "bought first ship %u for %d", id, best_price);
    }
}

bool Engine::buy_ship(AiPlayer& p) {
    // Open1602 (the History Edition's computers buy ships too; Rules::computers_buy_ships, off with
    // the original's settings): a computer short of ships buys one a partner at peace offers at no
    // more than its yard price, if it keeps half its money. Not while it offers one itself, so
    // that two computers do not pass a ship back and forth; the cheapest offer first, then the
    // larger ship.
    if (!m_sim.rules().computers_buy_ships || p.pirates || tutorial()) {
        return false;
    }
    const std::vector<Ship*> ships = own_ships(p);
    if (std::any_of(ships.begin(), ships.end(), [](const Ship* s) { return s->for_sale; })) {
        return false;
    }
    const std::int64_t money = player(p).money;
    const Ship* best = nullptr;
    std::int32_t best_price = 0;
    std::uint32_t best_value = 0;
    for (const Ship& s : m_world.ships()) {
        const Player* seller = m_world.player(s.owner);
        if (!s.for_sale || s.sinking || s.owner == p.player || seller == nullptr ||
            (seller->role != PlayerRole::Human && seller->role != PlayerRole::Computer) ||
            s.sale_factor > kFullPriceFactor ||
            m_world.diplomacy().relation(p.player, s.owner).peace != Treaty::Signed ||
            std::any_of(s.cargo.begin(), s.cargo.end(), [](const CargoSlot& c) { return is_soldier_ware(c.ware); })) {
            continue;
        }
        const std::int32_t price = m_sim.fleet().sale_price(s);
        const assets::FigureType* t = ship_type(s);
        const std::uint32_t value = t != nullptr ? t->ship.price : 0;
        if (value == 0 || std::int64_t{price} * 2 > money) {
            continue;
        }
        if (best == nullptr || s.sale_factor < best->sale_factor ||
            (s.sale_factor == best->sale_factor && value > best_value)) {
            best = &s;
            best_price = price;
            best_value = value;
        }
    }
    if (best == nullptr) {
        return false;
    }
    ShipCommand buy;
    buy.player = p.player;
    buy.ship = best->id;
    buy.op = ShipCommand::Op::Buy;
    const std::uint32_t id = best->id;
    if (!execute(buy).accepted) {
        return false;
    }
    log(p, "bought ship %u for %d", id, best_price);
    return true;
}

}  // namespace o1602::sim::ai
