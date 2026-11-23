// SPDX-License-Identifier: GPL-3.0-or-later
//
// Military, diplomacy and the pirates' nest of the computer players:
// recruiting and guards every 6 s, ship missions with the logistics every 10 s, a look at one
// foreign city after every step, the soldiers on one island per tick, the treaties every 30 s.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <tuple>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

#include "o1602/sim/combat.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim::ai {

namespace {

/// Whether `t` is a tower, as the survey counts them (task_survey).
bool is_tower(const Tile& t) {
    return t.kind == kind::kTower || t.kind == kind::kTowerBeach || t.prod == prod::kTower;
}

/// Whether a ship lies within 16 tiles of the island's rectangle: farther out it reaches no beach
/// tile of it (landing_beach's danger, and the ships off the island the landing log follows).
bool near_island(const WorldIsland& isl, const Ship& o) {
    const auto ix0 = static_cast<std::int32_t>(isl.x);
    const auto iy0 = static_cast<std::int32_t>(isl.y);
    const auto ix1 = ix0 + static_cast<std::int32_t>(isl.map.width()) - 1;
    const auto iy1 = iy0 + static_cast<std::int32_t>(isl.map.height()) - 1;
    return o.x >= ix0 - 16 && o.x <= ix1 + 16 && o.y >= iy0 - 16 && o.y <= iy1 + 16;
}

/// Counts one unit of `figure` in a list of figure and count (only the log).
void tally(std::vector<std::pair<std::uint16_t, std::uint32_t>>& list, std::uint16_t figure) {
    auto it = std::ranges::find(list, figure, &std::pair<std::uint16_t, std::uint32_t>::first);
    if (it == list.end()) {
        list.emplace_back(figure, 1);
    } else {
        ++it->second;
    }
}

/// A list of figure and count as the log writes it: "figure x count " each.
std::string tallied(const std::vector<std::pair<std::uint16_t, std::uint32_t>>& list) {
    std::string out;
    for (const auto& [figure, count] : list) {
        out += std::to_string(figure) + "x" + std::to_string(count) + " ";
    }
    return out;
}

/// How often the log follows a landing party: once a minute of game time (only the log).
constexpr std::uint64_t kLandingLogMs = 60'000;

/// Guard posts kept per city.
constexpr std::size_t kMaxPosts = 64;

/// The pirate nest's offer comes again after more than 42 s (0x1A4 tenths, task 5).
constexpr std::uint64_t kNestOfferMs = 42'000;
constexpr std::uint32_t kDisbandPaybackMinutes = 30;  // a soldier is disbanded only if its upkeep pays the cost back

/// The index in figure::kUnitFamilies of a unit's family, for a unit of a package that of the
/// original's unit of its kind (Kind, as its hold shows it); 0 for other figures.
std::uint32_t family_index(const assets::FigureTable* figures, std::uint32_t figure) {
    const auto v = assets::FigureTable::unit_variant(figure);
    if (!v && figures != nullptr) {
        if (const auto own = figures->unit_of(figure)) {
            if (const assets::FigureType* t = figures->get(own->family)) {
                return static_cast<std::uint32_t>(std::clamp<std::int32_t>(t->kind, 1, 4) - 1);
            }
        }
    }
    const std::uint32_t family = v ? v->family : figure;
    for (std::uint32_t i = 0; i < figure::kUnitFamilies.size(); ++i) {
        if (figure::kUnitFamilies[i] == family) {
            return i;
        }
    }
    return 0;
}

std::uint32_t city_param(const City& c) {
    return std::uint32_t{c.island} << 8 | c.index;
}

/// A ship's strength for a job against a city: health and cannons.
std::uint32_t ship_strength(std::uint32_t health128, std::uint32_t cannons) {
    return health128 / 16 + 2U * cannons;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Armies

const Engine::Census* Engine::census() const {
    if (!m_in_tick) {
        return nullptr;
    }
    Census& c = m_census;
    if (c.valid) {
        return &c;
    }
    c.players = m_world.players().size();
    c.islands = m_world.islands().size();
    c.on_island.assign(c.players * c.islands, 0);
    c.army.assign(c.players, {});
    c.aboard.assign(c.players, {});
    c.cannons.assign(c.players, 0);
    c.own_ships.resize(c.players);
    for (auto& list : c.own_ships) {
        list.clear();
    }
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner >= c.players) {
            continue;
        }
        if (!s.away() && s.island < c.islands) {
            ++c.on_island[std::size_t{s.owner} * c.islands + s.island];
        }
        if (!s.dying()) {
            ++c.army[s.owner][family_index(m_sim.m_figures, s.figure)];
        }
    }
    const auto& ships = m_world.ships();
    for (std::size_t i = 0; i < ships.size(); ++i) {
        const Ship& s = ships[i];
        if (s.owner >= c.players) {
            continue;
        }
        for (const CargoSlot& slot : s.cargo) {
            if (is_soldier_ware(slot.ware)) {
                ++c.aboard[s.owner][family_index(m_sim.m_figures, unit_of_ware(slot.ware))];
            }
        }
        if (!s.sinking) {
            c.cannons[s.owner] += s.cannons;
            if (!s.trader && !s.cart()) {
                c.own_ships[s.owner].push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    c.valid = true;
    return &c;
}

std::vector<Ship*> Engine::own_ships(const AiPlayer& p) {
    std::vector<Ship*> out;
    if (const Census* c = census(); c != nullptr && p.player < c->players) {
        out.reserve(c->own_ships[p.player].size());
        for (const std::uint32_t i : c->own_ships[p.player]) {
            out.push_back(&m_world.ships()[i]);
        }
        return out;
    }
    for (Ship& s : m_world.ships()) {
        if (s.owner == p.player && !s.sinking && !s.trader && !s.cart()) {
            out.push_back(&s);
        }
    }
    return out;
}

std::array<std::uint32_t, 4> Engine::army_counts(PlayerId owner) const {
    const Census* census = this->census();
    std::array<std::uint32_t, 4> n{};
    if (census != nullptr && owner < census->players) {
        n = census->army[owner];
    } else {
        for (const Soldier& s : m_world.soldiers()) {
            if (s.owner == owner && !s.dying()) {
                ++n[family_index(m_sim.m_figures, s.figure)];
            }
        }
    }
    for (const CostBuilding& b : m_world.cost_buildings()) {
        if (b.kind != CostBuilding::Kind::Castle || b.island >= m_world.islands().size() ||
            m_world.islands()[b.island].map.top(b.x, b.y).owner != owner) {
            continue;
        }
        for (const CastleSlot& slot : b.slots) {
            if (slot.figure != 0) {
                ++n[family_index(m_sim.m_figures, slot.figure)];
            }
        }
    }
    if (census != nullptr && owner < census->players) {
        for (std::size_t i = 0; i < 4; ++i) {
            n[i] += census->aboard[owner][i];
        }
        return n;
    }
    for (const Ship& s : m_world.ships()) {
        if (s.owner != owner) {
            continue;
        }
        for (const CargoSlot& c : s.cargo) {
            if (is_soldier_ware(c.ware)) {
                ++n[family_index(m_sim.m_figures, unit_of_ware(c.ware))];
            }
        }
    }
    return n;
}

std::array<std::uint32_t, 4> Engine::unit_weights() const {
    // Damage per second of each unit: Hitpoint / Worktime (sword 40, cavalry 51, musket 38, cannon 49).
    std::array<std::uint32_t, 4> w{};
    for (std::size_t i = 0; i < figure::kUnitFamilies.size(); ++i) {
        const assets::FigureType* t = m_sim.m_figures->get(figure::kUnitFamilies[i]);
        if (t != nullptr && t->work_time_ms != 0) {
            w[i] = static_cast<std::uint32_t>(std::uint64_t{t->combat.hit} * 1000U / (32U * t->work_time_ms));
        }
    }
    return w;
}

std::uint32_t Engine::weighted_army(PlayerId owner) const {
    const auto n = army_counts(owner);
    const auto w = unit_weights();
    std::uint32_t s = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        s += n[i] * w[i];
    }
    return s;
}

std::uint32_t Engine::army_target(const AiPlayer& p, bool whole) const {
    // A scenario can ask for a fixed strength instead: a hundred cannons' worth,
    // on every level.
    if (player(p).scenario.has(scenario_flag::kFixedArmy)) {
        return unit_weights()[3] * 100;
    }
    // 1.5 times the strongest human army, more on harder levels.
    std::uint32_t best = 0;
    for (std::size_t h = 0; h < m_world.players().size(); ++h) {
        if (m_world.players()[h].role != PlayerRole::Human) {
            continue;
        }
        const auto human = static_cast<PlayerId>(h);
        std::uint32_t s = seen_army(whole ? human : p.player, human);
        s += s / 2;
        best = std::max(best, s);
    }
    switch (difficulty()) {
        case 0:
            break;
        case 1:
            best += best / 4;
            break;
        case 2:
            best += best / 2;
            break;
        default:
            best *= 2;
            break;
    }
    return rivals() ? std::max(best, rival_army_target(p, whole)) : best;
}

std::uint32_t Engine::rival_army_target(const AiPlayer& p, bool whole) const {
    // A swordsman's strength per so many inhabitants, and a share of the strongest army of the other
    // computers, at peace or not: a peace can end, as a player knows. The share stays below the whole, so that two
    // computers arming against each other settle at their inhabitants' army instead of outbidding each other.
    const Balance& bal = m_world.balance();
    std::uint32_t own = 0;
    if (bal.ai_inhabitants_per_unit != 0) {
        own = static_cast<std::uint32_t>(std::uint64_t{unit_weights()[0]} * total_population(p.player) /
                                         bal.ai_inhabitants_per_unit);
    }
    std::uint32_t rival = 0;
    for (std::size_t i = 0; i < m_world.players().size(); ++i) {
        const auto other = static_cast<PlayerId>(i);
        const Player& pl = m_world.players()[i];
        if (other == p.player || pl.role != PlayerRole::Computer || pl.status != PlayerStatus::Playing ||
            m_world.behaviour(pl) != FactionBehaviour::Settlers) {
            continue;
        }
        rival = std::max(rival, seen_army(whole ? other : p.player, other));
    }
    const std::uint32_t rival_whole = rival;
    rival = static_cast<std::uint32_t>(std::uint64_t{rival} * bal.ai_rival_army_percent / 100U);
    // The strategy: more while arming or as a conqueror; a trader keeps less of the rivals' share,
    // never less than its inhabitants' army.
    const std::uint32_t pct = army_percent(p);
    own = static_cast<std::uint32_t>(std::uint64_t{own} * std::max<std::uint32_t>(pct, 100) / 100U);
    // Never beyond the rival's whole army, so that two computers do not outbid each other.
    rival = std::min(static_cast<std::uint32_t>(std::uint64_t{rival} * pct / 100U), std::max(rival_whole, rival));
    return std::max(own, rival);
}

bool Engine::stronger(PlayerId a, PlayerId b) const {
    // The original compares the armies and the cannons of the fleets. The original takes b's fleet
    // on both sides, so only the armies count (S-102).
    return strength(a, a) >= strength(a, b);
}

bool Engine::sees_all() const {
    return !rivals() || difficulty() + 1 >= NewGameOptions::kDifficulties;
}

std::uint32_t Engine::seen_army(PlayerId viewer, PlayerId owner) const {
    if (viewer == owner || sees_all()) {
        return weighted_army(owner);
    }
    // A human sees the soldiers on every island it discovered, and nothing of the others.
    const auto w = unit_weights();
    std::uint32_t s = 0;
    for (const Soldier& u : m_world.soldiers()) {
        if (u.owner == owner && !u.away() && u.island < m_world.islands().size() &&
            island_of(u.island).discovered_by(viewer)) {
            s += w[family_index(m_sim.m_figures, u.figure)];
        }
    }
    return s;
}

bool Engine::warship(std::uint32_t figure) const {
    for (std::size_t type = 0; type < figure::kShips.size(); ++type) {
        if (figure::kShips[type] == figure) {
            return figure::warship_type(type);
        }
    }
    for (const auto& [number, type] : m_sim.m_figures->package_ships()) {
        if (number == figure) {
            return figure::warship_type(type);
        }
    }
    return false;
}

std::uint32_t Engine::seen_cannons(PlayerId viewer, PlayerId owner) const {
    const bool all = viewer == owner || sees_all();
    if (const Census* c = census(); all && c != nullptr && owner < c->players) {
        return c->cannons[owner];
    }
    // Ships are drawn everywhere, so a human sees every ship and its type, but not its cannons.
    // Open1602: merchant ships carry cannons too; the computer counts its own, so it guesses those of
    // others as well, each class by its own ships of that class (they are armed very differently).
    std::uint32_t war_mounted = 0;
    std::uint32_t war_most = 0;
    std::uint32_t merchant_mounted = 0;
    std::uint32_t merchant_most = 0;
    if (!all) {
        own_cannons(viewer, true, war_mounted, war_most);
        own_cannons(viewer, false, merchant_mounted, merchant_most);
    }
    std::uint32_t n = 0;
    for (const Ship& s : m_world.ships()) {
        if (s.owner != owner || s.sinking) {
            continue;
        }
        if (all) {
            n += s.cannons;
        } else if (const assets::FigureType* t = armable(s)) {
            const bool war = warship(s.figure);
            n += guessed_cannons(s, *t, war ? war_mounted : merchant_mounted, war ? war_most : merchant_most);
        }
    }
    return n;
}

const assets::FigureType* Engine::armable(const Ship& s) const {
    const assets::FigureType* t = s.cart() ? nullptr : ship_type(s);
    return t != nullptr && t->ship.max_cannons != 0 ? t : nullptr;
}

void Engine::own_cannons(PlayerId player, bool warships, std::uint32_t& mounted, std::uint32_t& most) const {
    mounted = 0;
    most = 0;
    for (const Ship& s : m_world.ships()) {
        if (s.owner != player || s.sinking || warship(s.figure) != warships) {
            continue;
        }
        if (const assets::FigureType* t = armable(s)) {
            mounted += std::min<std::uint32_t>(s.cannons, t->ship.max_cannons);
            most += t->ship.max_cannons;
        }
    }
}

std::uint32_t Engine::guessed_cannons(const Ship& s, const assets::FigureType& t, std::uint32_t mounted,
                                      std::uint32_t most) const {
    const std::uint32_t max = t.ship.max_cannons;
    // As its own ships of the class carry: a player who arms his ships half expects the same of
    // others. Without ships of the class of its own it knows no better than the type's maximum.
    // Rounded to the nearest cannon: cut down, every small ship lost up to one at every look.
    std::uint32_t guess = most == 0 ? max : (max * mounted + most / 2) / most;
    const auto seen = std::ranges::find(m_state.sightings, s.id, &Sighting::ship);
    if (seen != m_state.sightings.end()) {
        if (seen->unarmed) {
            return 0;
        }
        guess = std::max<std::uint32_t>(guess, seen->armed);
    }
    return std::min(guess, max);
}

void Engine::watch_fights() {
    // Open1602 (G5/B): what a fight at sea shows of a ship's cannons, a warship's or a merchant
    // ship's. A ship without cannons never fires, and a hit's damage tells the step of cannon_factor
    // it fired with. A ship shot at within its own range that stands, has no order and no white flag
    // fires back if it has cannons: if it has not after a reload (its type's Worktime), it has none.
    std::vector<Sighting>& seen = m_state.sightings;
    const auto record = [&](std::uint32_t id) -> Sighting& {
        const auto it = std::ranges::find(seen, id, &Sighting::ship);
        if (it != seen.end()) {
            return *it;
        }
        seen.push_back(Sighting{id});
        return seen.back();
    };
    const std::uint64_t now = now_ms();
    for (const PendingHit& h : m_world.pending_hits()) {
        const Ship* from = h.shooter.kind == Target::Kind::Ship ? m_world.ship(h.shooter.ship) : nullptr;
        if (const assets::FigureType* t = from != nullptr ? armable(*from) : nullptr;
            t != nullptr && t->combat.hit != 0) {
            const std::uint32_t step = h.damage / t->combat.hit;
            std::uint8_t fewest = 1;
            while (fewest < t->ship.max_cannons && Combat::cannon_factor(fewest) < step) {
                ++fewest;
            }
            Sighting& r = record(from->id);
            r.armed = std::max(r.armed, fewest);
            r.unarmed = false;
            r.challenged_ms = 0;
        }
        const Ship* at = h.target.kind == Target::Kind::Ship ? m_world.ship(h.target.ship) : nullptr;
        const assets::FigureType* t = at != nullptr ? armable(*at) : nullptr;
        if (t == nullptr || at->sinking || at->held || ship_moving(*at) || !at->target.empty() ||
            h.attacker == at->owner || !m_world.diplomacy().hostile(at->owner, h.attacker)) {
            continue;
        }
        // The shooter within its own reach (Shotradius in 1/8 tile).
        const std::int64_t reach = std::int64_t{t->combat.range8} * kEighth;
        if (std::max(std::abs(std::int64_t{h.from_x} - at->pos_x), std::abs(std::int64_t{h.from_y} - at->pos_y)) >
            reach) {
            continue;
        }
        Sighting& r = record(at->id);
        if (r.armed == 0 && !r.unarmed && r.challenged_ms == 0) {
            r.challenged_ms = now;
        }
    }
    for (Sighting& r : seen) {
        const Ship* s = m_world.ship(r.ship);
        if (s == nullptr || r.challenged_ms == 0) {
            continue;
        }
        const assets::FigureType* t = ship_type(*s);
        if (t == nullptr || s->held || ship_moving(*s) || !s->target.empty()) {
            r.challenged_ms = 0;  // it was kept from firing: nothing learnt
        } else if (t->work_time_ms != 0 && now - r.challenged_ms >= t->work_time_ms) {
            r.unarmed = true;
            r.challenged_ms = 0;
        }
    }
    // A ship gone, or at a Kontor, its owner's or another's, where it can load cannons: seen anew.
    // Cannons already in its holds it can mount anywhere, unseen by a player as well, until it fires.
    std::erase_if(seen, [&](const Sighting& r) {
        const Ship* s = m_world.ship(r.ship);
        if (s == nullptr || s->sinking) {
            return true;
        }
        return !ship_moving(*s) &&
               std::ranges::any_of(m_world.cities(), [&](const City& c) { return ship_at_city(*s, c); });
    });
}

std::uint32_t Engine::soldiers_on_island(PlayerId owner, std::uint16_t island) const {
    if (const Census* c = census(); c != nullptr && owner < c->players && island < c->islands) {
        return c->on_island[std::size_t{owner} * c->islands + island];
    }
    std::uint32_t n = 0;
    for (const Soldier& s : m_world.soldiers()) {
        n += s.owner == owner && s.island == island && !s.away() ? 1U : 0U;
    }
    return n;
}

std::vector<Soldier*> Engine::soldiers_near(PlayerId owner, const City& c, std::int32_t range, std::size_t max) {
    std::vector<Soldier*> out;
    if (!c.warehouse_x || !c.warehouse_y) {
        return out;
    }
    const WorldIsland& isl = island_of(c.island);
    const std::int32_t kx = static_cast<std::int32_t>(isl.x) + *c.warehouse_x;
    const std::int32_t ky = static_cast<std::int32_t>(isl.y) + *c.warehouse_y;
    for (Soldier& s : m_world.soldiers()) {
        if (out.size() >= max) {
            break;
        }
        if (s.owner != owner || s.island != c.island || s.away()) {
            continue;
        }
        if (sea_distance(s.pos_x / kPosUnit - kx, s.pos_y / kPosUnit - ky) <= range) {
            out.push_back(&s);
        }
    }
    return out;
}

std::uint32_t Engine::soldiers_aboard(const Ship& s) const {
    std::uint32_t n = 0;
    for (const CargoSlot& c : s.cargo) {
        n += is_soldier_ware(c.ware) ? 1U : 0U;
    }
    return n;
}

std::uint32_t Engine::free_holds(const Ship& s) const {
    std::uint32_t n = 0;
    for (const CargoSlot& c : std::span<const CargoSlot>(s.cargo.data(), usable_holds(s, *m_sim.m_figures))) {
        n += c.ware == 0 ? 1U : 0U;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------
// Recruiting and guards (every 6 s)

bool Engine::queue_unit(AiPlayer& p, const CastleEntry& castle, std::uint32_t family) {
    // Swordsmen and cavalry one at a time per castle, the others may stack.
    const CostBuilding* b = nullptr;
    const std::uint16_t island = p.castle_island.value_or(0);
    for (const CostBuilding& c : m_world.cost_buildings()) {
        if (c.kind == CostBuilding::Kind::Castle && c.island == island && c.x == castle.x && c.y == castle.y) {
            b = &c;
        }
    }
    if (b == nullptr) {
        return false;
    }
    // Open1602: a soldier of a package of the same kind is trained as often as the original's
    // (docs/modding/own-figures.md#soldiers); the original's computers know only theirs.
    if (!classic() && m_sim.m_figures != nullptr) {
        const std::uint32_t kind = family_index(m_sim.m_figures, family);
        std::vector<std::uint32_t> choice;
        for (const std::uint32_t f : m_sim.m_figures->unit_families()) {
            if (family_index(m_sim.m_figures, f) == kind) {
                choice.push_back(f);
            }
        }
        if (choice.size() > 1) {
            family = choice[rng().next() % choice.size()];
        }
    }
    const std::uint32_t t = family_index(m_sim.m_figures, family);
    for (const CastleSlot& slot : b->slots) {
        if (slot.figure != 0 && family_index(m_sim.m_figures, slot.figure) == t && t < 2) {
            return false;
        }
    }
    CastleOrder order;
    order.player = p.player;
    order.island = island;
    order.x = castle.x;
    order.y = castle.y;
    order.op = CastleOrder::Op::Recruit;
    order.figure = static_cast<std::uint16_t>(family);
    return execute(order).accepted;
}

bool Engine::guard_post(AiPlayer& p, City& c, CityPlan& r, std::int32_t& x, std::int32_t& y, bool& at_yard) {
    // The markets of the city in turn, then the own shipyard.
    const auto market_at = [&](std::int32_t mx, std::int32_t my) {
        const Tile t = tile(c.island, mx, my);
        return t.valid() && t.city == c.index && t.prod == prod::kMarket && !t.type->destroyed;
    };
    // The original fills the list once per plan (until the hourly re-init). Open1602 fills it again
    // when the survey counts more markets than it holds or the next post is no market any more
    // (GB-4), so new markets get guards and soldiers stop going to a burnt one. The original's
    // computers keep their list.
    const std::size_t markets = std::min<std::size_t>(r.slots[kSlotMarket].count, kMaxPosts);
    const bool stale =
        !classic() && (r.posts.size() < markets || (r.post_cursor < r.posts.size() &&
                                                    !market_at(r.posts[r.post_cursor].x, r.posts[r.post_cursor].y)));
    if (r.posts.empty() || stale) {
        r.posts.clear();
        const IslandMap& map = island_of(c.island).map;
        for (const Production& pr : m_world.productions()) {
            if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
                continue;
            }
            if (market_at(pr.x, pr.y) && r.posts.size() < kMaxPosts) {
                r.posts.push_back(GuardPost{pr.x, pr.y});
            }
        }
        r.post_cursor = 0;
    }
    at_yard = false;
    if (r.post_cursor < r.posts.size()) {
        x = r.posts[r.post_cursor].x;
        y = r.posts[r.post_cursor].y;
        ++r.post_cursor;
        return true;
    }
    r.post_cursor = 0;
    if (p.yard_valid && p.yard_island == c.island && r.slots[kSlotShipyard].count != 0) {
        x = p.yard_x;
        y = p.yard_y;
        at_yard = true;
        return true;
    }
    if (!r.posts.empty()) {
        x = r.posts[0].x;
        y = r.posts[0].y;
        r.post_cursor = 1;
        return true;
    }
    return false;
}

bool Engine::guard_takes(const Soldier& s, const Target& post) const {
    // Not one in a building or already on the way to the post. Open1602: nor, for an improved
    // computer, one on its way aboard a ship (board()): the guard sent it back to the post every
    // few seconds, so attack ships sailed without soldiers.
    return s.target.kind != Target::Kind::Building && !(s.target == post) &&
           !(rivals() && s.target.kind == Target::Kind::Ship);
}

void Engine::guard(AiPlayer& p, City& c, std::int32_t x, std::int32_t y, std::uint32_t n, bool castle_island) {
    // Soldiers beyond the reserve at the Kontor go to the post.
    std::vector<Soldier*> near = soldiers_near(p.player, c, 8, 16);
    const std::size_t reserve = castle_island ? 8 : 0;
    if (near.size() <= reserve) {
        return;
    }
    const WorldIsland& isl = island_of(c.island);
    const Target post =
        Target::spot(2 * (isl.x + static_cast<std::uint32_t>(x)) + 1, 2 * (isl.y + static_cast<std::uint32_t>(y)) + 1);
    std::uint32_t here = 0;
    for (const Soldier& s : m_world.soldiers()) {
        here += s.owner == p.player && s.target == post ? 1U : 0U;
    }
    const auto spare = static_cast<std::uint32_t>(near.size() - reserve);
    const std::uint32_t wanted = n < 2 ? spare + here : soldiers_on_island(p.player, c.island) / n + 1;
    if (here >= wanted) {
        return;
    }
    SoldierOrder order;
    order.player = p.player;
    order.target = post;
    for (std::size_t i = reserve; i < near.size(); ++i) {
        if (guard_takes(*near[i], post)) {
            order.soldiers.push_back(near[i]->id);
        }
    }
    if (!order.soldiers.empty()) {
        execute(order);
    }
}

bool Engine::market_tower(AiPlayer& p, City& c, std::int32_t mx, std::int32_t my) {
    // One stone tower beside each market, on a wall or the beach if there is one.
    const Tile m = tile(c.island, mx, my);
    const assets::BuildingType* tower = by_id(id::kStoneTower);
    if (!m.valid() || tower == nullptr) {
        return false;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    rotated_size(*m.type,
                 island_of(c.island).map.top(static_cast<std::uint32_t>(mx), static_cast<std::uint32_t>(my)).rotation,
                 w, h);
    std::int32_t best = 0;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    bool towers = false;
    rings(p, RingFilter::Own, mx, my, mx + static_cast<std::int32_t>(w) - 1, my + static_cast<std::int32_t>(h) - 1, 2,
          [&](const Tile& t, std::int32_t x, std::int32_t y, std::int32_t d) {
              if (t.prod == prod::kTower) {
                  towers = true;
              }
              std::int32_t score = 0;
              if (t.kind == kind::kWall || t.kind == kind::kWallBeach || t.kind == kind::kBeach ||
                  t.kind == kind::kBeachOuter || t.kind == kind::kBeachInner || t.kind == kind::kBeachVariant ||
                  t.kind == kind::kBeachRuin) {
                  score = 500;
              } else if (t.kind == kind::kRuin || t.ware == ware::kGrass || t.ware == ware::kTree) {
                  score = (2 - d) * 50;
              }
              if (t.prod == prod::kShipyard && d == 0) {
                  best = 0;
              }
              if (score > best && can_place(p, *tower, x, y, 1, 1)) {
                  best = score;
                  bx = x;
                  by = y;
              }
          });
    if (towers || best <= 0) {
        return false;
    }
    return build(p, {BuildItem{tower->id, bx, by, 0, 0}}, false) != 0;
}

void Engine::recruit(AiPlayer& p) {
    // One own city per call.
    const auto& cities = m_world.cities();
    if (cities.empty()) {
        return;
    }
    // Open1602: an improved computer deep in debt with its gold still falling disbands a soldier
    // every few seconds, as a player cuts the army's upkeep, down to half its inhabitants' army;
    // not while a war or a threat is worth the debt. An army beyond its budget otherwise only
    // stops growing: disbanding costs gold, and the budget moves with the goal.
    const std::int32_t upkeep = rivals() ? military_cost(p) : 0;
    const std::int32_t budget = upkeep != 0 ? army_budget(p) : 0;
    if (rivals() && net_income(p) < 0 && player(p).money < -1000 && !war_economy(p)) {
        const auto counts = army_counts(p.player);
        const std::uint32_t units = counts[0] + counts[1] + counts[2] + counts[3];
        const Balance& bal = m_world.balance();
        const std::uint32_t keep =
            bal.ai_inhabitants_per_unit != 0 ? total_population(p.player) / bal.ai_inhabitants_per_unit / 2 : 0;
        // Disbanding costs gold (the third value of the Soldat: line): only a soldier whose upkeep
        // pays that back within half an hour goes; one that costs nothing to keep stays.
        const assets::FigureTable* f = m_sim.figures();
        const auto pays_back = [&](const Soldier& s) {
            const assets::FigureType* type = f != nullptr ? f->get(s.figure) : nullptr;
            if (type == nullptr) {
                return false;
            }
            const std::uint32_t keep_cost = f->unit_upkeep(type->kind);
            return keep_cost != 0 && keep_cost * kDisbandPaybackMinutes >= f->unit_cost(type->kind);
        };
        if (units > keep) {
            for (const Soldier& s : m_world.soldiers()) {
                if (s.owner == p.player && !s.away() && !s.dying() && pays_back(s)) {
                    SoldierCommand cmd;
                    cmd.player = p.player;
                    cmd.soldier = s.id;
                    cmd.op = SoldierCommand::Op::Disband;
                    execute(cmd);
                    log(p, "disbands a soldier in debt (%u left, gold %d, upkeep %d of %d)", units - 1, player(p).money,
                        upkeep, budget);
                    break;
                }
            }
        }
        return;
    }
    City* c = nullptr;
    for (std::size_t n = 0; n < cities.size(); ++n) {
        p.recruit_cursor = static_cast<std::uint32_t>((p.recruit_cursor + 1) % cities.size());
        if (cities[p.recruit_cursor].owner == p.player) {
            c = city(cities[p.recruit_cursor].island, cities[p.recruit_cursor].index);
            break;
        }
    }
    if (c == nullptr) {
        return;
    }
    CityPlan& r = plan_of(p, c->island, c->index);
    std::int32_t px = 0;
    std::int32_t py = 0;
    bool at_yard = false;
    if (!guard_post(p, *c, r, px, py, at_yard)) {
        return;
    }
    const std::uint32_t army = m_sim.military().army_size(p.player);
    const std::uint32_t pop = total_population(p.player);
    const std::uint32_t cap = m_sim.rules().max_army;
    // Castles that still stand.
    std::erase_if(p.castles, [&](const CastleEntry& e) {
        const Tile t = tile(p.castle_island.value_or(0), e.x, e.y);
        return !t.valid() || t.prod != prod::kMilitary || t.owner != p.player;
    });
    if (p.castles.empty()) {
        p.castle_island.reset();
    }
    if ((cap != 0 && army >= cap) || army >= pop / 16) {
        // Room for better units: a swordsman goes when muskets are there (op 5 value 7).
        if (r.slots[kSlotMuskets].count != 0 && c->stock(ware::kMuskets) != 0) {
            for (const Soldier& s : m_world.soldiers()) {
                if (s.owner == p.player && s.island == c->island && family_index(m_sim.m_figures, s.figure) == 0 &&
                    !s.away()) {
                    SoldierCommand cmd;
                    cmd.player = p.player;
                    cmd.soldier = s.id;
                    cmd.op = SoldierCommand::Op::Disband;
                    execute(cmd);
                    break;
                }
            }
        }
    } else {
        // Trained units leave the castles for the Kontor.
        for (const CastleEntry& e : p.castles) {
            const std::uint16_t island = p.castle_island.value_or(0);
            for (const CostBuilding& b : m_world.cost_buildings()) {
                if (b.kind != CostBuilding::Kind::Castle || b.island != island || b.x != e.x || b.y != e.y) {
                    continue;
                }
                for (std::size_t i = 0; i < b.slots.size(); ++i) {
                    if (b.slots[i].figure != 0 && !b.slots[i].training) {
                        if (rivals() && !way_out(p, island, e)) {
                            break;  // the units stay in the castle
                        }
                        const WorldIsland& isl = island_of(island);
                        CastleOrder order;
                        order.player = p.player;
                        order.island = island;
                        order.x = e.x;
                        order.y = e.y;
                        order.op = CastleOrder::Op::Release;
                        order.slot = static_cast<std::uint8_t>(i);
                        order.spot = Target::spot(2 * (isl.x + e.rally_x) + 1, 2 * (isl.y + e.rally_y) + 1);
                        execute(order);
                        break;
                    }
                }
                break;
            }
        }
    }
    // Open1602: an improved computer adds a unit or tower only while its war economy pays for it.
    // Half the army its inhabitants call for is always kept up, to defend itself.
    const Balance& mb = m_world.balance();
    const std::uint32_t core =
        mb.ai_inhabitants_per_unit != 0
            ? static_cast<std::uint32_t>(std::uint64_t{unit_weights()[0]} * total_population(p.player) /
                                         mb.ai_inhabitants_per_unit / 2)
            : 0;
    // The core only with gold in hand and cities that pay for it; beyond it the budget with a quarter
    // to spare, as the income it is counted from moves. One unit in training at a time, so that a
    // goal or war does not order a dozen within minutes before the income shows their cost.
    const bool pays = rivals() && pays_upkeep(m_world, player(p));
    bool training = false;
    if (pays) {
        for (const CostBuilding& b : m_world.cost_buildings()) {
            if (b.kind != CostBuilding::Kind::Castle || b.island >= m_world.islands().size() ||
                m_world.islands()[b.island].map.top(b.x, b.y).owner != p.player) {
                continue;
            }
            for (const CastleSlot& slot : b.slots) {
                training = training || (slot.figure != 0 && slot.training);
            }
        }
    }
    const std::int32_t next = upkeep + unit_upkeep_estimate(p);
    const bool can_add =
        !pays || (!training && ((weighted_army(p.player) < core && player(p).money > 0 && next <= civil_income(p)) ||
                                std::int64_t{next} * 4 <= std::int64_t{army_budget(p)} * 3));
    // A tower beside the market (cannons from the city: 2 t in the home city, 1 t elsewhere).
    const bool home = p.home_valid && p.home_island == c->island && p.home_city == c->index;
    const std::uint32_t cannons_needed = home ? 0x41U : 0x20U;
    const assets::BuildingType* tower = by_id(id::kStoneTower);
    if (can_add && !at_yard && tower != nullptr && c->stock(m_buildings.materials().cannons) >= cannons_needed &&
        unlocked(p, *tower) && affordable(p, *c, *tower)) {
        const bool own_city = p.has_city && p.island == c->island && p.city == c->index;
        const bool saved = p.has_city;
        const std::uint16_t si = p.island;
        const std::uint8_t sc = p.city;
        p.has_city = true;
        p.island = c->island;
        p.city = c->index;
        const bool built = market_tower(p, *c, px, py);
        if (!own_city) {
            p.has_city = saved;
            p.island = si;
            p.city = sc;
        }
        if (built) {
            ++r.slots[kSlotStoneTower].count;
            return;
        }
    }
    const std::uint32_t target = army_target(p);
    if (target == 0) {
        return;
    }
    p.castle_cursor = static_cast<std::uint8_t>((p.castle_cursor + 1) % 3);
    if (p.castle_island && *p.castle_island == c->island) {
        if (can_add && p.castle_cursor < p.castles.size() && weighted_army(p.player) <= target) {
            const CastleEntry& e = p.castles[p.castle_cursor];
            // Open1602: a computer that pays upkeep orders one unit a look, so that the budget is
            // checked for each; the original's order up to four at once.
            const bool one = rivals() && pays_upkeep(m_world, player(p));
            bool queued = false;
            const auto order = [&](std::uint32_t family) {
                if (!(one && queued)) {
                    queued = queue_unit(p, e, family) || queued;
                    if (queued && one) {
                        log(p,
                            "recruits unit family %u (upkeep %d + %d, budget %d, civil %d, trade %d, gold %d, "
                            "goal %u, war %u, army %u core %u)",
                            family, upkeep, unit_upkeep_estimate(p), army_budget(p), civil_income(p),
                            trade_balance(player(p)), player(p).money, unsigned{p.strategy.goal},
                            unsigned{p.strategy.war_target}, weighted_army(p.player), core);
                    }
                }
            };
            if (r.slots[kSlotMuskets].count != 0 && c->stock(ware::kMuskets) > 31) {
                order(figure::kUnitFamilies[2]);
            }
            if (r.slots[kSlotCannons].count != 0 && c->stock(ware::kCannons) > 127) {
                order(figure::kUnitFamilies[3]);
            }
            if (r.slots[kSlotSwords].count != 0) {
                order(figure::kUnitFamilies[1]);
                if (r.slots[kSlotMuskets].count == 0 && r.slots[kSlotCannons].count == 0) {
                    order(figure::kUnitFamilies[0]);
                    order(figure::kUnitFamilies[1]);
                }
            }
        }
        if (!at_yard) {
            guard(p, *c, px, py, r.slots[kSlotMarket].count, true);
        }
        return;
    }
    // A colony: guards, and troops from home by ship.
    const std::uint32_t k = soldiers_on_island(p.player, c->island);
    if (k != 0 && !at_yard) {
        guard(p, *c, px, py, static_cast<std::uint32_t>(r.posts.size()), false);
    }
    City* home_c = home_city(p);
    if (home_c != nullptr && home_c != c && !classic()) {
        // Open1602: a colony with enemy soldiers on its island and fewer of its own than they have
        // gets troops from home at once; the original sends them only by the size of its army.
        std::uint32_t threat = 0;
        const std::vector<PlayerId> enemies = island_enemies(p, c->island);
        for (const Soldier& o : m_world.soldiers()) {
            threat += o.island == c->island && !o.away() &&
                              std::find(enemies.begin(), enemies.end(), o.owner) != enemies.end()
                          ? 1U
                          : 0U;
        }
        if (threat != 0 && k < threat + threat / 4 + 1 && soldiers_near(p.player, *home_c, 8, 16).size() >= 4) {
            if (Ship* ship = best_armed_ship(p, Target::city_of(home_c->island, home_c->index), 1)) {
                ship_send(p, *ship, Target::city_of(home_c->island, home_c->index), 1, city_param(*c));
                return;
            }
        }
    }
    if (home_c != nullptr && army >= 8) {
        // k < (army - 8) * 0.006 * (40 or 15), in integers (ADR 0004: no floating point in the game's
        // decisions). Equal to the floating point form for every army below 433 units.
        const std::uint64_t f = std::uint64_t{army - 8} * 6U * (has_population(*c, 0, 100) ? 40U : 15U);
        if (std::uint64_t{k} * 1000U < f && soldiers_near(p.player, *home_c, 8, 16).size() >= 8) {
            if (Ship* ship = best_armed_ship(p, Target::city_of(home_c->island, home_c->index), 1)) {
                ship_send(p, *ship, Target::city_of(home_c->island, home_c->index), 1, city_param(*c));
            }
        }
    }
}

void Engine::buy_pirate_peace(AiPlayer& p) {
    // With more than 10,000 gold and a price above 1000.
    PlayerId pirates = kNoPlayer;
    for (std::size_t i = 0; i < m_world.players().size(); ++i) {
        if (m_world.behaviour(m_world.players()[i]) == FactionBehaviour::Pirates) {
            pirates = static_cast<PlayerId>(i);
        }
    }
    if (pirates == kNoPlayer) {
        return;
    }
    const std::uint32_t price = pirate_peace_price(m_world, pirates, p.player);
    Player& pl = player(p);
    if (price > 1000 && static_cast<std::int64_t>(price) < std::int64_t{pl.money} - 9000) {
        m_world.diplomacy().add_event(pirates, p.player, DiplomacyEventType::PeaceBought, now_ms());
        pl.money = money_plus(pl.money, -std::int64_t{price});
    }
}

// ---------------------------------------------------------------------------------------------
// Ships on missions (with the logistics every 10 s)

bool Engine::attack_move_for(const Ship& s, std::uint8_t mission) const {
    // Raids, attacks and landings: improved computers' ships fight on the way only by such an
    // order, as a player's do; the original's engage on their own. A ship with soldiers aboard
    // sails on to land them by a plain move, as a player steers a troop ship: it neither chases
    // nor fires on the way (only an ordered target is fought), so it does not stop at every
    // hostile ship near its way.
    return rivals() && (mission == 3 || mission == 5 || mission == 7) && soldiers_aboard(s) == 0;
}

void Engine::ship_send(AiPlayer& p, Ship& s, const Target& t, std::uint8_t mission, std::uint32_t param) {
    ShipOrder order;
    order.player = p.player;
    order.ship = s.id;
    order.target = t;
    order.attack_move = attack_move_for(s, mission);
    execute(order);
    ShipNote& n = note(p, s.id);
    n.mission = mission;
    n.param = param;
    n.counter = 10;
    n.patrol = false;
    n.supply = false;
}

City* Engine::param_city(std::uint32_t param) {
    // The city a mission's parameter names (city_param).
    return city(static_cast<std::uint16_t>(param >> 8), static_cast<std::uint8_t>(param & 0xFF));
}

void Engine::mission_end(AiPlayer& p, Ship& s, ShipNote& n) {
    // Home with the troops, or stop.
    if (n.mission == 3 || n.mission == 4) {
        ShipOrder stop;
        stop.player = p.player;
        stop.ship = s.id;
        execute(stop);
        n.mission = 0;
        n.param = 0;
        n.counter = 0;
        return;
    }
    if (n.mission >= 5 && n.mission <= 9) {
        log(p, "ship %u calls its attack off (mission %u, %u soldiers aboard)", s.id, n.mission, soldiers_aboard(s));
    }
    if (City* home = home_city(p)) {
        ship_send(p, s, Target::city_of(home->island, home->index), 2, 0);
        return;
    }
    n.mission = 0;
    n.param = 0;
    n.counter = 0;
}

std::vector<std::uint32_t> Engine::boarders(const AiPlayer& p, const City& at) {
    // The soldiers near the city's Kontor that are not headed into a building.
    std::vector<std::uint32_t> out;
    for (const Soldier* soldier : soldiers_near(p.player, at, 8, 16)) {
        if (soldier->target.kind != Target::Kind::Building) {
            out.push_back(soldier->id);
        }
    }
    return out;
}

void Engine::board(AiPlayer& p, Ship& s, const City& at, std::uint32_t max) {
    SoldierOrder order;
    order.player = p.player;
    order.target = Target::other_ship(s.id);
    for (const std::uint32_t id : boarders(p, at)) {
        if (order.soldiers.size() >= max) {
            break;
        }
        // Open1602: not the soldiers on their way aboard another ship of the computer's that
        // takes troops on board (missions 1 and 5), as a convoy's ships board at once: each would call
        // them over to itself in turn, and none got on. A ship gone, sinking or on another
        // mission frees them.
        if (const Soldier* u = m_world.soldier(id);
            rivals() && u != nullptr && u->target.kind == Target::Kind::Ship && u->target.ship != s.id) {
            const Ship* other = m_world.ship(u->target.ship);
            const auto on = std::ranges::find(p.ships, u->target.ship, &ShipNote::id);
            if (other != nullptr && !other->sinking && other->owner == p.player && on != p.ships.end() &&
                (on->mission == 1 || on->mission == 5)) {
                continue;
            }
        }
        order.soldiers.push_back(id);
    }
    if (!order.soldiers.empty()) {
        execute(order);
    }
}

bool Engine::move_to_board(AiPlayer& p, Ship& s, ShipNote& n, const City& home) {
    // Open1602 (BD): to water from which the soldiers at the Kontor can go on board. The mission
    // and its counter go on, so a ship that finds no boarding there still ends its mission.
    const std::vector<std::uint32_t> soldiers = boarders(p, home);
    if (soldiers.empty()) {
        return false;
    }
    const auto water = boarding_water(p, s, home, soldiers);
    if (!water || (water->x == s.x && water->y == s.y)) {
        return false;
    }
    log(p, "ship %u moves to %u,%u: no cell walkable to the Kontor lies within boarding range", s.id, water->x,
        water->y);
    const std::uint8_t counter = n.counter;
    ship_send(p, s, *water, n.mission, n.param);
    n.counter = static_cast<std::uint8_t>(counter - 1);
    return true;
}

void Engine::missions(AiPlayer& p) {
    for (ShipNote& n : p.ships) {
        if (n.mission == 0) {
            continue;
        }
        Ship* s = m_world.ship(n.id);
        if (s == nullptr || s->held || s->repairing) {
            continue;
        }
        if (ship_moving(*s)) {
            continue;
        }
        const Target target = s->order;
        bool arrived = !target.empty();
        if (target.kind == Target::Kind::City) {
            const City* c = city(target.island, target.city);
            arrived = c != nullptr && ship_at_city(*s, *c);
        }
        if (!arrived) {
            if (n.counter == 0 || target.empty()) {
                mission_end(p, *s, n);
                continue;
            }
            --n.counter;
            ShipOrder again;
            again.player = p.player;
            again.ship = s->id;
            again.target = target;
            execute(again);
            continue;
        }
        switch (n.mission) {
            case 1: {
                // Troops at home for a colony.
                const City* home = home_city(p);
                if (home != nullptr && soldiers_aboard(*s) < 2 && n.counter != 0 && free_holds(*s) != 0) {
                    if (rivals() && move_to_board(p, *s, n, *home)) {
                        break;
                    }
                    board(p, *s, *home, free_holds(*s));
                    --n.counter;
                    break;
                }
                const City* colony = param_city(n.param);
                if (soldiers_aboard(*s) != 0 && colony != nullptr && colony->owner == p.player) {
                    ship_send(p, *s, Target::city_of(colony->island, colony->index), 2, 0);
                } else {
                    n.mission = 0;
                }
                break;
            }
            case 2: {
                // Open1602 (F5): at a city whose landing cell is cut off, first to a beach of the city,
                // with soldiers aboard to land there.
                const City* at = target.kind == Target::Kind::City ? city(target.island, target.city) : nullptr;
                if (rivals() && at != nullptr && at->owner == p.player && soldiers_aboard(*s) != 0) {
                    if (const auto shore = landing_shore(p, *s, *at)) {
                        log(p, "ship %u lands off %u,%u: the landing cell is cut off from the city", s->id, shore->x,
                            shore->y);
                        ship_send(p, *s, *shore, 2, 0);
                        break;
                    }
                }
                UnloadOrder order;
                order.player = p.player;
                order.ship = s->id;
                execute(order);
                n.mission = 0;
                n.param = 0;
                n.counter = 0;
                break;
            }
            case 5: {
                // Troops for an attack.
                const City* home = home_city(p);
                const std::uint32_t aboard = soldiers_aboard(*s);
                if (home != nullptr && n.counter != 0 && free_holds(*s) != 0) {
                    if (rivals() && move_to_board(p, *s, n, *home)) {
                        break;
                    }
                    const std::size_t before = soldiers_near(p.player, *home, 8, 16).size();
                    board(p, *s, *home, free_holds(*s));
                    --n.counter;
                    if (before != 0) {
                        break;
                    }
                }
                const City* t = param_city(n.param);
                // Open1602: a ship of a convoy (start_attack) is judged with the others: their
                // soldiers together (shell_towers).
                const bool convoy = rivals() && std::ranges::any_of(p.ships, [&](const ShipNote& o) {
                                        return o.id != n.id && o.param == n.param &&
                                               (o.mission == 5 || o.mission == 6) && m_world.ship(o.id) != nullptr;
                                    });
                if ((aboard > kFewSoldiers || (convoy && aboard != 0)) && t != nullptr) {
                    n.mission = 6;
                    plan_landing(p, *s, n);
                } else if (aboard != 0 && home != nullptr) {
                    log(p, "ship %u calls its attack off (too few soldiers: %u aboard)", s->id, aboard);
                    ship_send(p, *s, Target::city_of(home->island, home->index), 2, 0);  // S-101: home, not a stray ref
                } else {
                    mission_end(p, *s, n);
                }
                break;
            }
            case 6:
                plan_landing(p, *s, n);
                break;
            case 7:
            case 8:
            case 9: {
                const PlayerId owner = static_cast<PlayerId>(n.param & 0xFF);
                // Open1602: a convoy's ships (shell_towers) unload together, once none of them is
                // still under way to the beach, after their tries (every 10 s) only when the
                // soldiers aboard storm the towers alone; else the whole convoy calls it off, no
                // ships one by one.
                if (rivals() && n.mission == kMissionLanding && std::ranges::any_of(p.ships, [&](const ShipNote& o) {
                        const Ship* os = m_world.ship(o.id);
                        return o.id != n.id && o.mission == kMissionLanding && o.param == n.param && os != nullptr &&
                               !os->sinking && soldiers_aboard(*os) != 0 && ship_moving(*os);
                    })) {
                    if (n.counter != 0) {
                        --n.counter;
                        break;
                    }
                    if (!storms_alone(p, *s, owner)) {
                        log(p,
                            "ship %u calls the convoy's landing off: the others still under way, its %u soldiers "
                            "too few for the towers",
                            s->id, soldiers_aboard(*s));
                        const std::uint32_t param = n.param;
                        for (ShipNote& o : p.ships) {
                            if (o.mission == kMissionLanding && o.param == param) {
                                if (Ship* os = m_world.ship(o.id)) {
                                    mission_end(p, *os, o);
                                }
                            }
                        }
                        break;
                    }
                }
                if (m_world.diplomacy().relation(p.player, owner).peace != Treaty::Signed) {
                    log(p, "ship %u lands %u soldiers against player %u", s->id, soldiers_aboard(*s), owner);
                    if (p.strategy.war_target == owner) {
                        p.strategy.war_fails = 0;  // a landing is progress: the fight takes its time
                    }
                    UnloadOrder order;
                    order.player = p.player;
                    order.ship = s->id;
                    std::uint32_t first_id = 0;
                    if (logging() && rivals()) {
                        for (const Soldier& u : m_world.soldiers()) {
                            first_id = std::max(first_id, u.id + 1);
                        }
                    }
                    execute(order);
                    if (logging() && rivals()) {
                        log_landing(p, *s, first_id, owner);
                    }
                    n.mission = 0;
                    n.param = 0;
                } else {
                    mission_end(p, *s, n);
                }
                break;
            }
            case kMissionBreach: {
                // Open1602: shooting a gap into a walled coast for a landing (breach).
                const City* t = param_city(n.param);
                if (t == nullptr || t->owner == p.player ||
                    m_world.diplomacy().relation(p.player, t->owner).peace == Treaty::Signed) {
                    log(p, "ship %u ends its breach: no war with the city any more", s->id);
                    n.mission = 0;
                    n.param = 0;
                    n.counter = 0;
                    break;
                }
                const auto gap = landing_beach(p, *t);
                if (gap && !(rivals() && !covering_towers(p, *t, *gap).empty())) {
                    log(p, "ship %u: the wall of city %u/%u is open at %d,%d", s->id, t->island, t->index, gap->first,
                        gap->second);
                    if (soldiers_aboard(*s) > kFewSoldiers) {
                        ship_send(p, *s, water_near(t->island, gap->first, gap->second), kMissionLanding, t->owner);
                    } else {
                        n.mission = 0;
                        n.param = 0;
                        n.counter = 0;
                    }
                    break;
                }
                const auto wall = breach_target(p, *t, s);
                if (!wall || n.counter == 0 || !healthy(*s)) {
                    log(p, "ship %u ends its breach of city %u/%u (%s)", s->id, t->island, t->index,
                        !wall            ? "no wall left to shoot"
                        : n.counter == 0 ? "no tries left"
                                         : "damaged");
                    if (soldiers_aboard(*s) != 0) {
                        mission_end(p, *s, n);
                    } else {
                        n.mission = 0;
                        n.param = 0;
                        n.counter = 0;
                    }
                    break;
                }
                // Open1602 (3b): a ship still sailing up to a tower, not yet within its Shotradius of
                // it, spends no try: the tries count the shots, not the voyage.
                const Target at = Target::building(t->island, static_cast<std::uint32_t>(wall->first),
                                                   static_cast<std::uint32_t>(wall->second));
                bool closing = false;
                if (rivals() && std::ranges::any_of(m_world.towers(), [&](const Tower& tw) {
                        return tw.island == t->island && tw.x == wall->first && tw.y == wall->second;
                    })) {
                    const auto f = m_sim.combat().fighter(at);
                    const assets::FigureType* ft = ship_type(*s);
                    closing = f && ft != nullptr &&
                              f->cells.distance(2 * static_cast<std::int32_t>(s->x) + 1,
                                                2 * static_cast<std::int32_t>(s->y) + 1) >
                                  static_cast<std::int32_t>(ft->combat.range8 / 4) &&
                              (s->motion.phase == ShipMotion::Phase::Run || s->motion.phase == ShipMotion::Phase::Turn);
                }
                const std::uint8_t tries = n.counter;
                ship_send(p, *s, at, kMissionBreach, n.param);
                n.counter = closing ? tries : static_cast<std::uint8_t>(tries - 1);
                break;
            }
            case 10: {
                // An improved computer's scout at the island: it explores, then is free again.
                ExploreOrder order;
                order.player = p.player;
                order.ship = s->id;
                execute(order);
                n.mission = 0;
                n.param = 0;
                n.counter = 0;
                break;
            }
            default:
                break;
        }
    }
}

void Engine::plan_landing(AiPlayer& p, Ship& s, ShipNote& n) {
    // A beach near the target's markets, away from its towers and soldiers.
    const City* t = param_city(n.param);
    if (t == nullptr || t->owner == p.player || building_count(*t) <= 3 ||
        m_world.diplomacy().relation(p.player, t->owner).peace == Treaty::Signed ||
        soldiers_on_island(p.player, t->island) >= 8) {
        mission_end(p, s, n);
        return;
    }
    const auto beach = landing_beach(p, *t);
    if (!beach) {
        if (rivals()) {
            breach(p, s, n, *t);
        } else {
            mission_end(p, s, n);
        }
        return;
    }
    // Open1602 (3b): the towers that cover the landing or the way from it to the city are shelled
    // first by enough warships, as a player does, while the troops wait aboard at home; where none
    // can, the landing is called off (shell_towers).
    if (rivals()) {
        if (const auto towers = covering_towers(p, *t, *beach); !towers.empty()) {
            shell_towers(p, s, n, *t, towers, *beach);
            return;
        }
    }
    ship_send(p, s, water_near(t->island, beach->first, beach->second), kMissionLanding, t->owner);
}

std::vector<std::pair<std::int32_t, std::int32_t>> Engine::way_to(const City& c, std::int32_t cx,
                                                                  std::int32_t cy) const {
    // The cells on the straight line from cell (cx, cy) to the city's market, or its Kontor, which
    // a party walks or cuts its way along (way_in).
    std::vector<std::pair<std::int32_t, std::int32_t>> way;
    std::optional<std::pair<std::int32_t, std::int32_t>> goal;
    for (const Production& pr : m_world.productions()) {
        if (!goal && pr.used && pr.island == c.island) {
            const Tile tt = tile(c.island, pr.x, pr.y);
            if (tt.valid() && tt.city == c.index && tt.prod == prod::kMarket) {
                goal = std::pair{static_cast<std::int32_t>(pr.x), static_cast<std::int32_t>(pr.y)};
            }
        }
    }
    if (!goal && c.warehouse_x && c.warehouse_y) {
        goal = std::pair{static_cast<std::int32_t>(*c.warehouse_x), static_cast<std::int32_t>(*c.warehouse_y)};
    }
    if (goal) {
        const WorldIsland& isl = island_of(c.island);
        const std::int32_t gx = 2 * (static_cast<std::int32_t>(isl.x) + goal->first);
        const std::int32_t gy = 2 * (static_cast<std::int32_t>(isl.y) + goal->second);
        const std::int32_t n = std::max({std::abs(gx - cx), std::abs(gy - cy), 1});
        for (std::int32_t k = 0; k <= n; ++k) {
            way.emplace_back(cx + (gx - cx) * k / n, cy + (gy - cy) * k / n);
        }
    }
    return way;
}

std::vector<Engine::TowerCover> Engine::covering_towers(const AiPlayer& p, const City& c,
                                                        std::pair<std::int32_t, std::int32_t> beach) const {
    // The hostile towers of the island whose own reach (their fighter's cells within range8 / 4,
    // as they shoot and as the landing log counts them) covers the landing at `beach` or a cell of
    // the way from it to the city; the nearest to the landing first.
    const WorldIsland& isl = island_of(c.island);
    return towers_over_way(p, c, 2 * (static_cast<std::int32_t>(isl.x) + beach.first),
                           2 * (static_cast<std::int32_t>(isl.y) + beach.second));
}

std::size_t Engine::towers_in_the_way(const AiPlayer& p, const City& c, std::int32_t cx, std::int32_t cy) const {
    return towers_over_way(p, c, cx, cy).size();
}

std::vector<Engine::TowerCover> Engine::towers_over_way(const AiPlayer& p, const City& c, std::int32_t cx,
                                                        std::int32_t cy) const {
    // covering_towers from cell (cx, cy).
    const Combat& combat = m_sim.combat();
    std::vector<std::pair<std::int32_t, std::int32_t>> way = way_to(c, cx, cy);
    if (way.empty()) {
        way.emplace_back(cx, cy);
    }
    // Open1602 (F15): a city with no market and no Kontor standing is taken by its towers
    // (tower_goals), so every tower of it lies in the way.
    bool towers_only = false;
    if (rivals()) {
        bool standing = std::ranges::any_of(m_world.productions(), [&](const Production& pr) {
            if (!pr.used || pr.island != c.island) {
                return false;
            }
            const Tile tt = tile(c.island, pr.x, pr.y);
            return tt.valid() && tt.city == c.index && tt.prod == prod::kMarket && !tt.type->destroyed;
        });
        if (!standing && c.warehouse_x && c.warehouse_y) {
            const Tile k = tile(c.island, *c.warehouse_x, *c.warehouse_y);
            standing = k.valid() && !k.type->destroyed && k.type->kind == kind::kHq;
        }
        towers_only = !standing;
    }
    std::vector<std::pair<std::int32_t, TowerCover>> found;  // distance, tower
    for (const Tower& tw : m_world.towers()) {
        if (tw.island != c.island || !m_world.diplomacy().hostile(p.player, tw.owner)) {
            continue;
        }
        const auto f = combat.fighter(Target::building(tw.island, tw.x, tw.y));
        const Tile b = tile(tw.island, tw.x, tw.y);
        if (!f || !b.valid() || b.type->destroyed) {
            continue;
        }
        if (!(towers_only && b.city == c.index) && !std::ranges::any_of(way, [&](const auto& w) {
                return f->cells.distance(w.first, w.second) <= f->range8 / 4;
            })) {
            continue;
        }
        TowerCover t;
        t.x = tw.x;
        t.y = tw.y;
        const std::uint32_t damage = m_sim.military().building_damage(tw.island, tw.x, tw.y);
        t.health = b.type->max_health > damage ? b.type->max_health - damage : 1U;
        t.threat = f->threat;
        t.value = build_value(b.type->build_cost);
        found.emplace_back(f->cells.distance(cx, cy), t);
    }
    std::ranges::stable_sort(found, {}, &std::pair<std::int32_t, TowerCover>::first);
    std::vector<TowerCover> out;
    for (const auto& [d, t] : found) {
        out.push_back(t);
    }
    return out;
}

Engine::Shelling Engine::plan_shelling(const AiPlayer& p, const City& t, const std::vector<TowerCover>& towers,
                                       const std::vector<Ship*>& fleet, const Ship* troops) const {
    // Merlin's rule (plan "Landungstruppen und Türme", 2026-11-19): a fleet big enough to wreck
    // the towers, so that the landing loses no soldiers; ships may be lost while the damage is
    // worth more than the own losses. In gold from the game's data: the towers' build value and the
    // soldiers aboard `troops`, whom the towers would shoot (their cost by the Soldat: line),
    // against the price of the ships the towers sink while they are shelled. `fleet` the strongest
    // first.
    Shelling out;
    if (fleet.empty()) {
        out.refusal = "no warship free";
        return out;
    }
    // The towers the strongest ship can shell from the water; a tower inland, out of the ships'
    // range, no ship wrecks. The ships shoot from the water nearest to each (water_near), where
    // every hostile tower that reaches that water fires on them, the coast's too.
    std::vector<TowerCover>& targets = out.targets;
    for (const TowerCover& c : towers) {
        if (shells_from_water(*fleet.front(), t.island, c.x, c.y)) {
            targets.push_back(c);
        }
    }
    if (targets.empty()) {
        out.refusal = "none within the ships' reach from the water";
        return out;
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> spots;  // half tiles
    for (const TowerCover& c : targets) {
        const Target w = water_near(t.island, c.x, c.y);
        spots.emplace_back(2 * static_cast<std::int32_t>(w.x) + 1, 2 * static_cast<std::int32_t>(w.y) + 1);
    }
    const Combat& combat = m_sim.combat();
    const assets::FigureTable* figures = m_sim.figures();
    std::uint64_t health = 0;  // the targets' buildings, 1/32
    std::uint64_t threat = 0;  // the fire on the ships, 1/32 per second
    std::uint64_t gain = 0;    // gold
    for (const TowerCover& c : targets) {
        health += c.health;
        gain += c.value;
    }
    for (const Tower& tw : m_world.towers()) {
        if (tw.island != t.island || !m_world.diplomacy().hostile(p.player, tw.owner)) {
            continue;
        }
        const auto f = combat.fighter(Target::building(tw.island, tw.x, tw.y));
        const Tile b = tile(tw.island, tw.x, tw.y);
        if (f && b.valid() && !b.type->destroyed && std::ranges::any_of(spots, [&](const auto& w) {
                return f->cells.distance(w.first, w.second) <= f->range8 / 4;
            })) {
            threat += f->threat;
        }
    }
    // The soldiers aboard count only when no covering tower is left for them to land under.
    if (troops != nullptr && targets.size() == towers.size()) {
        for (const CargoSlot& c : troops->cargo) {
            if (is_soldier_ware(c.ware) && figures != nullptr) {
                gain += figures->disband_cost(hold_unit(c));
            }
        }
    }
    // Ships one by one, the strongest first: each adds its fire (Hitpoint * cannons per Worktime,
    // the naval reload), shortens the shelling and so the towers' fire on the fleet. Each ship
    // turns for the yard once it is no longer healthy (hull_to_spare), so the fleet finishes only
    // when what it can spare covers that fire; what it takes sinks the weakest hulls first. Of the
    // fleets that finish, the fewest ships that lose none; else the cheapest loss.
    std::uint64_t fire = 0;  // 1/32 per second
    std::size_t best = 0;
    std::uint64_t best_loss = 0;
    for (std::size_t k = 0; k < fleet.size(); ++k) {
        const assets::FigureType* ft = ship_type(*fleet[k]);
        if (ft == nullptr || ft->work_time_ms == 0) {
            continue;
        }
        fire += std::uint64_t{ft->combat.hit} * fleet[k]->cannons * 1000U / ft->work_time_ms;
        if (fire == 0) {
            continue;
        }
        const std::uint64_t taken = threat * health / fire;  // 1/32 HP the fleet takes
        std::uint64_t spare = 0;
        for (std::size_t i = 0; i <= k; ++i) {
            spare += hull_to_spare(*fleet[i]);
        }
        if (spare < taken) {
            continue;
        }
        std::vector<std::pair<std::uint64_t, std::uint64_t>> hulls;  // health, price
        for (std::size_t i = 0; i <= k; ++i) {
            const assets::FigureType* it = ship_type(*fleet[i]);
            const std::uint64_t max = it != nullptr && it->ship.max_health != 0 ? it->ship.max_health : 1U;
            const std::uint64_t h = fleet[i]->health != 0 ? std::min<std::uint64_t>(fleet[i]->health, max) : max;
            std::uint64_t price = it != nullptr ? it->ship.price : 0U;
            if (fleet[i] == troops && figures != nullptr) {
                for (const CargoSlot& c : troops->cargo) {
                    if (is_soldier_ware(c.ware)) {
                        price += figures->disband_cost(hold_unit(c));
                    }
                }
            }
            hulls.emplace_back(h, price);
        }
        std::ranges::sort(hulls);
        std::uint64_t left = taken;
        std::uint64_t loss = 0;
        for (const auto& [h, price] : hulls) {
            if (left < h) {
                break;
            }
            left -= h;
            loss += price;
        }
        if (best == 0 || loss < best_loss) {
            best = k + 1;
            best_loss = loss;
        }
        if (loss == 0) {
            break;
        }
    }
    out.ships = best;
    out.loss = best_loss;
    out.gain = gain;
    if (best == 0) {
        out.refusal = "the warships would turn away first";
    } else if (best_loss >= gain) {
        out.refusal = "shelling does not pay";
    }
    return out;
}

void Engine::shell_towers(AiPlayer& p, Ship& s, ShipNote& n, const City& t, const std::vector<TowerCover>& towers,
                          std::pair<std::int32_t, std::int32_t> beach) {
    // The towers over the landing are shelled first (plan_shelling), while the troop ship waits at
    // home, out of their reach, or by the troop ship itself. Open1602:
    // where no fleet can wreck them the soldiers aboard storm them on land when they are enough for
    // it (storm_needs); else it calls the landing off rather than land under them, where every
    // party too small for them died without taking a city (gate f17: 27 of 27).
    const std::uint32_t param = city_param(t);
    const auto call_off = [&](const char* why, std::uint64_t loss, std::uint64_t gain) {
        log(p, "ship %u calls the landing under %zu towers of city %u/%u off: %s (loss %llu, gain %llu)", s.id,
            towers.size(), t.island, t.index, why, static_cast<unsigned long long>(loss),
            static_cast<unsigned long long>(gain));
        mission_end(p, s, n);
    };
    if (n.counter == 0) {
        call_off("no tries left", 0, 0);
        return;
    }
    --n.counter;
    if (std::ranges::any_of(p.ships,
                            [&](const ShipNote& o) { return o.mission == kMissionBreach && o.param == param; })) {
        return;
    }
    // With no warship free the troop ship shells them itself, as a breach does, its soldiers aboard
    // counted with its price.
    std::vector<Ship*> fleet = free_warships(p);
    if (fleet.empty() && s.cannons != 0 && healthy(s)) {
        fleet.push_back(&s);
    }
    const Shelling plan = plan_shelling(p, t, towers, fleet, &s);
    if (plan.refusal != nullptr) {
        // The soldiers aboard the convoy, the ships of this attack (start_attack) waiting at home;
        // while one of them still boards, the others wait for it.
        std::vector<Ship*> convoy;
        bool boarding = false;
        for (const ShipNote& o : p.ships) {
            Ship* os = m_world.ship(o.id);
            if (os == nullptr || o.param != param || (o.mission != 5 && o.mission != 6)) {
                continue;
            }
            if (o.mission == 5) {
                boarding = true;
            } else if (os == &s || !ship_moving(*os)) {
                convoy.push_back(os);
            }
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> party;
        for (const Ship* o : convoy) {
            for (const CargoSlot& c : o->cargo) {
                if (is_soldier_ware(c.ware)) {
                    party.emplace_back(hold_unit(c), 0U);
                }
            }
        }
        const WorldIsland& bi = island_of(t.island);
        std::size_t plain = 0;  // logged: what it needs without the walk
        const std::size_t needs = storm_needs(p, t, towers, party,
                                              {2 * (static_cast<std::int32_t>(bi.x) + beach.first) + 1,
                                               2 * (static_cast<std::int32_t>(bi.y) + beach.second) + 1},
                                              &plain);
        if (needs <= party.size()) {
            for (Ship* o : convoy) {
                log(p,
                    "ship %u storms the %zu towers of city %u/%u with %zu soldiers in %zu ships (needs %zu, %zu "
                    "without the walk): %s",
                    o->id, towers.size(), t.island, t.index, party.size(), convoy.size(), needs, plain, plan.refusal);
                ship_send(p, *o, water_near(t.island, beach.first, beach.second), kMissionLanding, t.owner);
            }
            return;
        }
        if (boarding && n.counter != 0) {
            return;
        }
        if (plain <= party.size()) {
            log(p,
                "ship %u: the walk under the towers' fire keeps %zu soldiers from storming (needs %zu, %zu without it)",
                s.id, party.size(), needs, plain);
        }
        call_off(plan.refusal, plan.loss, plan.gain);
        return;
    }
    const Target at = Target::building(t.island, plan.targets.front().x, plan.targets.front().y);
    for (std::size_t k = 0; k < plan.ships; ++k) {
        log(p, "ship %u shells %zu of %zu towers of city %u/%u for ship %u (%zu ships, loss %llu, gain %llu)",
            fleet[k]->id, plan.targets.size(), towers.size(), t.island, t.index, s.id, plan.ships,
            static_cast<unsigned long long>(plan.loss), static_cast<unsigned long long>(plan.gain));
        ship_send(p, *fleet[k], at, kMissionBreach, param);
    }
}

std::size_t Engine::storm_needs(const AiPlayer& p, const City& c, const std::vector<TowerCover>& towers,
                                const std::vector<std::pair<std::uint32_t, std::uint32_t>>& units,
                                std::pair<std::int32_t, std::int32_t> from, std::size_t* plain) const {
    // In unit-seconds (ms): the work of wrecking the towers (their health by the party's mean fire
    // on them, Military::building_hit) and of beating the island's hostile soldiers, against what
    // k units give under the towers' and soldiers' fire E, which kills one of mean health h every
    // D = h / E. The party first walks the towers' reach down to its own under that fire
    // on the straight line from `from` (Military::cell_us by the land's Wegspeed, the walk T):
    // unit i fires from T on until it falls at i D, so k give the sum of i D - T over the i whose
    // i D is past T.
    if (plain != nullptr) {
        *plain = SIZE_MAX;
    }
    const assets::FigureTable* figures = m_sim.figures();
    if (figures == nullptr) {
        return SIZE_MAX;
    }
    struct Unit {
        std::uint32_t damage = 0;  // 1/32 HP per shot
        std::uint32_t work_time_ms = 1;
        bool cannon = false;
        std::int32_t range8 = 0;  // Shotradius, 1/8 tile
        std::uint32_t speed = 0;
        std::uint8_t speed_type = 0;
    };
    std::vector<Unit> party;
    std::int32_t reach8 = std::numeric_limits<std::int32_t>::max();  // the shortest Shotradius
    std::uint64_t health = 0;                                        // 1/32 HP
    std::uint64_t fire = 0;                                          // on soldiers, 1/32 HP per second
    for (const auto& [figure, h] : units) {
        const assets::FigureType* ft = figures->get(figure);
        if (ft == nullptr || ft->work_time_ms == 0) {
            continue;
        }
        const std::uint32_t max = std::max<std::uint32_t>(1, ft->combat.max_health);
        const std::uint32_t now = h != 0 ? std::min(h, max) : max;
        Unit u;
        u.range8 = static_cast<std::int32_t>(ft->combat.range8);
        reach8 = std::min(reach8, u.range8);
        u.speed = ft->speed;
        u.speed_type = ft->speed_type;
        u.damage = Combat::scaled_damage(ft->combat.hit, now, max);
        u.work_time_ms = ft->work_time_ms;
        u.cannon = ft->kind == figtype::kGunner;
        party.push_back(u);
        health += now;
        fire += std::uint64_t{u.damage} * 1000U / u.work_time_ms;
    }
    if (party.empty() || fire == 0) {
        return SIZE_MAX;
    }
    const std::uint64_t n = party.size();
    std::uint64_t work = 0;    // unit-ms
    std::uint64_t threat = 0;  // 1/32 HP per second
    std::uint64_t walk = 0;    // T, ms: the longest walk through a tower's reach
    for (const TowerCover& tc : towers) {
        const Tile b = tile(c.island, tc.x, tc.y);
        if (!b.valid()) {
            continue;
        }
        if (const auto f = m_sim.combat().fighter(Target::building(c.island, tc.x, tc.y))) {
            // The way from `from` up to the tower (walk_path: the walk around what stands in the
            // way; the straight line where none leads there), its cells within the tower's reach and
            // not yet within the unit's own.
            std::vector<std::pair<std::int32_t, std::int32_t>> way = walk_path(p, c.island, from, *f, reach8);
            const bool tiles = !way.empty();
            if (!tiles) {
                const std::int32_t tx = f->x8 / 4;
                const std::int32_t ty = f->y8 / 4;
                const std::int32_t steps = std::max(std::abs(tx - from.first), std::abs(ty - from.second));
                for (std::int32_t k = 1; k <= steps; ++k) {
                    way.emplace_back(from.first + (tx - from.first) * k / steps,
                                     from.second + (ty - from.second) * k / steps);
                }
            }
            std::uint64_t us = 0;  // the party's walks through its reach, summed
            std::int32_t px = from.first;
            std::int32_t py = from.second;
            for (const auto& [x, y] : way) {
                const bool diagonal = x != px && y != py;
                px = x;
                py = y;
                const std::int32_t d = f->cells.distance(x, y);
                if (d > f->range8 / 4) {
                    continue;
                }
                for (std::size_t i = 0; i < party.size(); ++i) {
                    const Unit& u = party[i];
                    if (d > u.range8 / 4) {
                        std::uint8_t ways = 0;
                        (void)m_sim.combat().land().cell(x, y, u.speed_type, &ways);
                        // A party walks together: each but the first beside another (run_us).
                        const std::uint32_t w = i == 0 ? ways : Military::crowded_ways(ways);
                        // a step of the tile way is two half tiles
                        us += Military::cell_us(u.speed, w, diagonal) * (tiles ? 2U : 1U);
                    }
                }
            }
            walk = std::max<std::uint64_t>(walk, us / party.size() / 1000U);
        }
        std::uint64_t on = 0;
        for (const Unit& u : party) {
            on += std::uint64_t{Military::building_hit(u.damage, u.cannon, b.type->kind)} * 1000U / u.work_time_ms;
        }
        if (on == 0) {
            return SIZE_MAX;
        }
        work += std::uint64_t{tc.health} * n * 1000U / on;
        threat += tc.threat;
    }
    for (const Soldier& o : m_world.soldiers()) {
        if (o.island != c.island || o.away() || !m_world.diplomacy().hostile(p.player, o.owner)) {
            continue;
        }
        const assets::FigureType* ft = figures->get(o.figure);
        if (ft == nullptr || ft->work_time_ms == 0) {
            continue;
        }
        const std::uint32_t max = std::max<std::uint32_t>(1, ft->combat.max_health);
        const std::uint32_t now = o.health != 0 ? std::min(o.health, max) : max;
        work += std::uint64_t{now} * n * 1000U / fire;
        threat += std::uint64_t{Combat::scaled_damage(ft->combat.hit, now, max)} * 1000U / ft->work_time_ms;
    }
    if (threat == 0) {
        if (plain != nullptr) {
            *plain = 1;
        }
        return 1;
    }
    // Times n E: f(k) = A (k (k + 1) / 2 - m (m + 1) / 2) - B (k - m) >= C, with A = health 1000
    // (n h 1000), B = n T E, C = n work E, and m = B / A the units that fall on the walk; the least
    // k from the root of the quadratic, set exact in whole numbers.
    const auto a = static_cast<std::int64_t>(health * 1000U);
    const auto cc = static_cast<std::int64_t>(work * n * threat);
    const auto least = [&](std::int64_t b) {
        const std::int64_t m = b / a;
        const auto f = [&](std::int64_t k) { return a * (k * (k + 1) / 2 - m * (m + 1) / 2) - b * (k - m); };
        const double qa = static_cast<double>(a) / 2.0;
        const double qb = static_cast<double>(a) / 2.0 - static_cast<double>(b);
        const double qc = static_cast<double>(b) * static_cast<double>(m) -
                          static_cast<double>(a) * static_cast<double>(m) * static_cast<double>(m + 1) / 2.0 -
                          static_cast<double>(cc);
        auto k = std::max<std::int64_t>(
            m + 1, static_cast<std::int64_t>((-qb + std::sqrt(std::max(0.0, qb * qb - 4.0 * qa * qc))) / (2.0 * qa)));
        while (f(k) < cc) {
            ++k;
        }
        while (k > m + 1 && f(k - 1) >= cc) {
            --k;
        }
        return static_cast<std::size_t>(std::max<std::int64_t>(k, 1));
    };
    if (plain != nullptr) {
        *plain = least(0);
    }
    return least(static_cast<std::int64_t>(n * walk * threat));
}

std::vector<std::pair<std::int32_t, std::int32_t>> Engine::walk_path(const AiPlayer& p, std::uint16_t island,
                                                                     std::pair<std::int32_t, std::int32_t> from,
                                                                     const Fighter& f, std::int32_t reach8) const {
    // Breadth first over the tiles the soldiers walk on (walk_flood's), from the tile of `from` to
    // the nearest within `reach8` of the fighter's cells.
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const auto ox = static_cast<std::int32_t>(isl.x);
    const auto oy = static_cast<std::int32_t>(isl.y);
    const std::int32_t sx = from.first / 2 - ox;
    const std::int32_t sy = from.second / 2 - oy;
    std::vector<std::pair<std::int32_t, std::int32_t>> out;
    if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
        return out;
    }
    const LandMap& land = m_sim.m_combat.land();
    const auto at = [w](std::int32_t x, std::int32_t y) { return static_cast<std::size_t>(y * w + x); };
    const auto goal = [&](std::int32_t x, std::int32_t y) {
        return f.cells.distance(2 * (ox + x) + 1, 2 * (oy + y) + 1) <= reach8 / 4;
    };
    std::vector<std::int32_t> parent(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), -1);
    std::vector<std::pair<std::int32_t, std::int32_t>> queue{{sx, sy}};
    parent[at(sx, sy)] = static_cast<std::int32_t>(at(sx, sy));
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto [x, y] = queue[head];
        if (goal(x, y)) {
            for (std::size_t i = at(x, y); static_cast<std::int32_t>(i) != parent[i];
                 i = static_cast<std::size_t>(parent[i])) {
                const auto ix = static_cast<std::int32_t>(i) % w;
                const auto iy = static_cast<std::int32_t>(i) / w;
                out.emplace_back(2 * (ox + ix) + 1, 2 * (oy + iy) + 1);
            }
            std::ranges::reverse(out);
            return out;
        }
        for (const auto& [dx, dy] : kSteps) {
            const std::int32_t nx = x + dx;
            const std::int32_t ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || parent[at(nx, ny)] != -1 ||
                land.tile(ox + nx, oy + ny, 0, nullptr, p.player) != LandMap::Cell::Walk) {
                continue;
            }
            parent[at(nx, ny)] = static_cast<std::int32_t>(at(x, y));
            queue.emplace_back(nx, ny);
        }
    }
    return out;
}

bool Engine::shells_from_water(const Ship& s, std::uint16_t island, std::uint32_t x, std::uint32_t y) const {
    // As the ship's cannons reach (Fleet::bearing_to): the building's nearest cell within its
    // Shotradius of the open water nearest to it.
    const auto f = m_sim.combat().fighter(Target::building(island, x, y));
    const assets::FigureType* ft = ship_type(s);
    if (!f || ft == nullptr) {
        return false;
    }
    const Target w = water_near(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    // water_near gives the tile itself when no water lies near.
    return m_sim.fleet().sea().sailable(static_cast<std::int32_t>(w.x), static_cast<std::int32_t>(w.y)) &&
           f->cells.distance(2 * static_cast<std::int32_t>(w.x) + 1, 2 * static_cast<std::int32_t>(w.y) + 1) <=
               static_cast<std::int32_t>(ft->combat.range8 / 4);
}

std::vector<Ship*> Engine::free_warships(AiPlayer& p) {
    // The healthy ships with cannons and nothing else to do, no troops aboard: the strongest first,
    // equals in the order of the ships.
    std::vector<std::pair<std::uint32_t, Ship*>> found;
    // Only ships the computer has a note of: called inside the missions' loop over the notes, a new
    // note (note()) would move the list under that loop.
    for (Ship* o : own_ships(p)) {
        const auto on_it = std::ranges::find(p.ships, o->id, &ShipNote::id);
        if (on_it == p.ships.end()) {
            continue;
        }
        const ShipNote& on = *on_it;
        if (o->id == p.settle.ship || o->held || on.mission != 0 || on.supply || o->cannons == 0 ||
            soldiers_aboard(*o) != 0 || !healthy(*o)) {
            continue;
        }
        found.emplace_back(ship_strength(health128(*o), o->cannons), o);
    }
    std::ranges::stable_sort(found, std::greater<>{}, &std::pair<std::uint32_t, Ship*>::first);
    std::vector<Ship*> out;
    for (const auto& [score, o] : found) {
        out.push_back(o);
    }
    return out;
}

Ship* Engine::free_warship(AiPlayer& p) {
    // The strongest of them (breach); of equals the first, as before.
    const std::vector<Ship*> all = free_warships(p);
    return all.empty() ? nullptr : all.front();
}

bool Engine::beach_wall(std::uint16_t island, std::uint32_t x, std::uint32_t y) const {
    // A wall standing on the beach: the beach wall, or a wall whose ground is beach.
    const Tile b = tile(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    if (!b.valid() || !(b.kind == kind::kWall || b.kind == kind::kWallBeach)) {
        return false;
    }
    const MapCell& g = island_of(island).map.ground(x, y);
    const assets::BuildingType* gt = g.empty() ? nullptr : m_buildings.by_sprite(g.sprite);
    return b.kind == kind::kWallBeach ||
           (gt != nullptr && (gt->kind == kind::kBeach || gt->kind == kind::kBeachVariant));
}

bool Engine::beach_gap(std::uint16_t island, std::uint32_t x, std::uint32_t y) const {
    // The ruin a wall on the beach leaves when it is shot down: a gap to land through.
    const Tile b = tile(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    if (!b.valid() || !(b.kind == kind::kBeachRuin || b.kind == kind::kRuin)) {
        return false;
    }
    const MapCell& g = island_of(island).map.ground(x, y);
    const assets::BuildingType* gt = g.empty() ? nullptr : m_buildings.by_sprite(g.sprite);
    return b.kind == kind::kBeachRuin ||
           (gt != nullptr && (gt->kind == kind::kBeach || gt->kind == kind::kBeachVariant));
}

void Engine::breach(AiPlayer& p, Ship& s, ShipNote& n, const City& t) {
    // Open1602: the target's coast is walled all along, so no beach is free to land on. As a
    // player would, a warship shoots a gap into the wall on the beach the landing would take, and
    // the troops wait aboard until it is open. With no warship free the troop ship shoots it itself.
    const auto wall = breach_target(p, t);
    if (!wall) {
        // Open1602: a free beach whose landing cell nothing in reach opens: the landing as before.
        if (const auto beach = landing_beach(p, t, false, true)) {
            log(p, "ship %u lands at beach %d,%d of city %u/%u: nothing to shoot clears a landing cell", s.id,
                beach->first, beach->second, t.island, t.index);
            ship_send(p, s, water_near(t.island, beach->first, beach->second), kMissionLanding, t.owner);
            return;
        }
        log(p, "ship %u finds no beach and no wall on the beach of city %u/%u", s.id, t.island, t.index);
        mission_end(p, s, n);
        return;
    }
    const std::uint32_t param = city_param(t);
    if (std::ranges::any_of(p.ships,
                            [&](const ShipNote& o) { return o.mission == kMissionBreach && o.param == param; })) {
        if (n.counter == 0) {
            mission_end(p, s, n);
        } else {
            --n.counter;  // the troops wait for the gap
        }
        return;
    }
    Ship* best = free_warship(p);
    const Target at =
        Target::building(t.island, static_cast<std::uint32_t>(wall->first), static_cast<std::uint32_t>(wall->second));
    const char* what = tile(t.island, wall->first, wall->second).kind == kind::kForest ? "trees" : "wall";
    if (best != nullptr) {
        log(p, "ship %u breaches the %s at %d,%d of city %u/%u for ship %u", best->id, what, wall->first, wall->second,
            t.island, t.index, s.id);
        ship_send(p, *best, at, kMissionBreach, param);
        return;
    }
    if (s.cannons == 0) {
        log(p, "ship %u finds the coast of city %u/%u walled and no ship with cannons to breach it", s.id, t.island,
            t.index);
        mission_end(p, s, n);
        return;
    }
    log(p, "ship %u breaches the %s at %d,%d of city %u/%u itself, %u soldiers aboard (no warship free)", s.id, what,
        wall->first, wall->second, t.island, t.index, soldiers_aboard(s));
    ship_send(p, s, at, kMissionBreach, param);
}

std::optional<std::pair<std::int32_t, std::int32_t>> Engine::breach_target(const AiPlayer& p, const City& c,
                                                                           const Ship* s) const {
    // What a breach shoots at: a wall on the beach, else what seals the landing cells of the beach.
    // Open1602 (3b): with a beach free, the covering tower nearest to it (covering_towers) that the
    // ship can shell from the water.
    if (rivals()) {
        if (const auto beach = landing_beach(p, c)) {
            for (const TowerCover& tc : covering_towers(p, c, *beach)) {
                if (s == nullptr || shells_from_water(*s, c.island, tc.x, tc.y)) {
                    return std::pair{static_cast<std::int32_t>(tc.x), static_cast<std::int32_t>(tc.y)};
                }
            }
            return std::nullopt;
        }
    }
    if (const auto wall = landing_beach(p, c, true)) {
        return wall;
    }
    return landing_blocker(p, c);
}

std::optional<std::pair<std::int32_t, std::int32_t>> Engine::landing_blocker(const AiPlayer& p, const City& c) const {
    // Open1602: the beach a landing would take is free, but nothing within the unload range of it
    // is a cell the soldiers can stand on: trees or a hostile wall behind the sand. The one of those
    // nearest to the beach, as a player shoots a forest ring open (a tree not its own, which a ship
    // fells as Combat::fellable allows, or a wall or gate of the enemy). The soldiers ashore cut
    // their own way on (way_in).
    const auto beach = landing_beach(p, c, false, true);
    if (!beach) {
        return std::nullopt;
    }
    const auto range = static_cast<std::int32_t>(m_world.balance().castle_unload_range);
    const IslandMap& map = island_of(c.island).map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    std::optional<std::pair<std::int32_t, std::int32_t>> best;
    std::int32_t best_d = 0;
    for (std::int32_t y = std::max(0, beach->second - range); y <= std::min(h - 1, beach->second + range); ++y) {
        for (std::int32_t x = std::max(0, beach->first - range); x <= std::min(w - 1, beach->first + range); ++x) {
            const Tile t = tile(c.island, x, y);
            if (!t.valid() || t.type->destroyed) {
                continue;
            }
            const bool tree = t.kind == kind::kForest && t.owner != p.player;
            const bool wall = (t.kind == kind::kWall || t.kind == kind::kWallBeach || t.kind == kind::kGate) &&
                              t.owner != kNoPlayer && m_world.diplomacy().hostile(p.player, t.owner);
            if (!tree && !wall) {
                continue;
            }
            const std::int32_t d = sea_distance(x - beach->first, y - beach->second);
            if (d <= range && (!best || d < best_d)) {
                best = std::pair{x, y};
                best_d = d;
            }
        }
    }
    return best;
}

std::optional<std::pair<std::int32_t, std::int32_t>> Engine::landing_beach(const AiPlayer& p, const City& c, bool wall,
                                                                           bool any) const {
    const City* t = &c;
    const std::uint16_t island = t->island;
    const WorldIsland& isl = island_of(island);
    // Targets: the markets, or the Kontor of a small city.
    std::vector<std::pair<std::int32_t, std::int32_t>> targets;
    const IslandMap& map = isl.map;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const Tile tt = tile(island, pr.x, pr.y);
        if (tt.valid() && tt.city == t->index && tt.prod == prod::kMarket) {
            targets.emplace_back(pr.x, pr.y);
        }
    }
    if (targets.empty() && t->warehouse_x && t->warehouse_y) {
        targets.emplace_back(*t->warehouse_x, *t->warehouse_y);
    }
    if (targets.empty()) {
        return std::nullopt;
    }
    // The danger, gathered once (TB-10): hostile soldiers on the island and hostile towers on it
    // (tile positions), hostile ships near it (world positions); a ship farther than 16 tiles
    // from the island's rectangle reaches no beach tile.
    std::vector<std::pair<std::int32_t, std::int32_t>> soldiers;
    for (const Soldier& o : m_world.soldiers()) {
        if (o.island == island && m_world.diplomacy().hostile(p.player, o.owner)) {
            soldiers.emplace_back(o.pos_x / kPosUnit, o.pos_y / kPosUnit);
        }
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> towers;
    for (const Tower& tw : m_world.towers()) {
        if (tw.island == island && m_world.diplomacy().hostile(p.player, tw.owner)) {
            towers.emplace_back(tw.x, tw.y);
        }
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> ships;
    for (const Ship& o : m_world.ships()) {
        if (!o.cart() && near_island(isl, o) && m_world.diplomacy().hostile(p.player, o.owner)) {
            ships.emplace_back(o.x, o.y);
        }
    }
    // Beach tiles within 24 tiles of a target, scored by the danger around them.
    std::uint32_t sealed = 0;
    std::int32_t best = 10000;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const Tile b = tile(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            if (!b.valid()) {
                continue;
            }
            if (wall ? !(beach_wall(island, x, y) && m_world.diplomacy().hostile(p.player, b.owner))
                     : !(b.kind == kind::kBeach || b.kind == kind::kBeachVariant ||
                         (rivals() && beach_gap(island, x, y)))) {
                continue;
            }
            std::int32_t near = 1000;
            for (const auto& [tx, ty] : targets) {
                near =
                    std::min(near, sea_distance(static_cast<std::int32_t>(x) - tx, static_cast<std::int32_t>(y) - ty));
            }
            if (near > 24) {
                continue;
            }
            std::int32_t score = 1000 + near * 4;
            const std::int32_t wx = static_cast<std::int32_t>(isl.x + x);
            const std::int32_t wy = static_cast<std::int32_t>(isl.y + y);
            for (const auto& [ox, oy] : soldiers) {
                score += sea_distance(ox - wx, oy - wy) <= 16 ? 10 : 0;
            }
            for (const auto& [tx, ty] : towers) {
                score +=
                    sea_distance(tx - static_cast<std::int32_t>(x), ty - static_cast<std::int32_t>(y)) <= 10 ? 100 : 0;
            }
            for (const auto& [ox, oy] : ships) {
                score += sea_distance(ox - wx, oy - wy) <= 16 ? 30 : 0;
            }
            if (score >= best) {
                continue;
            }
            // Open1602: a beach only with a cell within the unload range the soldiers can stand on
            // (Military::landing_cell); trees or walls right behind the sand seal it.
            if (!wall && !any && rivals() && !m_sim.military().landing_cell_at(wx, wy, p.player)) {
                ++sealed;
                continue;
            }
            best = score;
            bx = static_cast<std::int32_t>(x);
            by = static_cast<std::int32_t>(y);
        }
    }
    if (best >= 10000) {
        if (sealed != 0) {
            log_sealed(p, c, sealed);
        }
        return std::nullopt;
    }
    return std::pair{bx, by};
}

void Engine::log_sealed(const AiPlayer& p, const City& c, std::uint32_t beaches) const {
    // Once each time the beaches of a city start to have no landing cell (only the log).
    const std::pair<PlayerId, std::uint32_t> key{p.player, city_param(c)};
    if (std::ranges::find(m_sealed_logged, key) != m_sealed_logged.end()) {
        return;
    }
    m_sealed_logged.push_back(key);
    log(p, "beaches of city %u/%u have no landing cell (%u rejected): trees or walls behind them", c.island, c.index,
        beaches);
}

bool Engine::takes_landing(const AiPlayer& p, const Ship& s, const ShipNote& n) const {
    // Open1602: an improved computer's only when healthy. yard_service calls a badly damaged ship
    // to the yard even on a mission, and start_attack took it straight back for the next attack:
    // every 10 s, so that it never got repaired nor sailed (x3 o4-404-p0, 453 attack orders).
    return s.id != p.settle.ship && !s.held && n.mission == 0 && !n.supply && s.cannons != 0 &&
           free_holds(s) + soldiers_aboard(s) >= kLandingHolds && (classic() || healthy(s));
}

bool Engine::storms_alone(const AiPlayer& p, const Ship& s, PlayerId owner) {
    // The towers over the way from the ship to a city of `owner` on the island it lies off
    // (towers_over_way) against the soldiers aboard (storm_needs); none over the way: it lands.
    for (const City& c : m_world.cities()) {
        if (c.owner != owner || c.island != s.near_island) {
            continue;
        }
        const auto towers =
            towers_over_way(p, c, 2 * static_cast<std::int32_t>(s.x) + 1, 2 * static_cast<std::int32_t>(s.y) + 1);
        if (towers.empty()) {
            continue;
        }
        std::vector<std::pair<std::uint32_t, std::uint32_t>> party;
        for (const CargoSlot& h : s.cargo) {
            if (is_soldier_ware(h.ware)) {
                party.emplace_back(hold_unit(h), 0U);
            }
        }
        return storm_needs(p, c, towers, party,
                           {2 * static_cast<std::int32_t>(s.x) + 1, 2 * static_cast<std::int32_t>(s.y) + 1}) <=
               party.size();
    }
    return true;
}

std::vector<Ship*> Engine::landing_ships(AiPlayer& p) {
    // As best_armed_ship: strength above 8, equals in the order of the ships. Only notes the
    // computer has: a ship without one is idle.
    static const ShipNote idle{};
    std::vector<std::pair<std::uint32_t, Ship*>> found;
    for (Ship* s : own_ships(p)) {
        const auto it = std::ranges::find(p.ships, s->id, &ShipNote::id);
        if (!takes_landing(p, *s, it != p.ships.end() ? *it : idle)) {
            continue;
        }
        const std::uint32_t score = ship_strength(health128(*s), s->cannons);
        if (score > 8) {
            found.emplace_back(score, s);
        }
    }
    std::ranges::stable_sort(found, std::greater<>{}, &std::pair<std::uint32_t, Ship*>::first);
    std::vector<Ship*> out;
    for (const auto& [score, s] : found) {
        out.push_back(s);
    }
    return out;
}

Engine::Convoy Engine::plan_convoy(const AiPlayer& p, const City& t, const City& home,
                                   const std::vector<TowerCover>& towers, const std::vector<Ship*>& ships,
                                   std::pair<std::int32_t, std::int32_t> from) {
    // Ship by ship: their holds take the soldiers at home (as they board, soldiers_near), until
    // these storm the towers, or no more soldiers are at home for another ship.
    Convoy out;
    std::uint32_t holds = 0;
    for (std::size_t k = 0; k < ships.size(); ++k) {
        holds += free_holds(*ships[k]);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> party;
        for (const Soldier* u : soldiers_near(p.player, home, 8, holds)) {
            party.emplace_back(u->figure, u->health);
        }
        out.needs = storm_needs(p, t, towers, party, from, &out.plain);
        out.has = party.size();
        if (out.needs <= out.has) {
            out.ships = k + 1;
            return out;
        }
        if (party.size() < holds) {
            break;
        }
    }
    out.ships = 0;
    return out;
}

Ship* Engine::best_armed_ship(AiPlayer& p, const Target& ref, std::uint32_t max_same) {
    // An armed ship with room, well armed (and healthy, for an improved computer: takes_landing).
    (void)max_same;
    for (const ShipNote& n : p.ships) {
        const Ship* s = m_world.ship(n.id);
        if (s != nullptr && (n.mission == 1 || n.mission == 2 || n.mission == 5) && s->order == ref) {
            return nullptr;
        }
    }
    Ship* best = nullptr;
    std::uint32_t best_score = 8;
    for (Ship* s : own_ships(p)) {
        if (!takes_landing(p, *s, note(p, s->id))) {
            continue;
        }
        const std::uint32_t score = ship_strength(health128(*s), s->cannons);
        if (score > best_score) {
            best = s;
            best_score = score;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Looking for a target (after every step)

bool Engine::defenceless(std::uint16_t island, std::uint8_t city) {
    // The same as city_defenceless, whose scan of the island answers for all its cities
    // as long as its tiles stay the same.
    if (island >= m_world.islands().size()) {
        return false;
    }
    if (m_defences.size() < m_world.islands().size()) {
        m_defences.resize(m_world.islands().size());
    }
    Defences& d = m_defences[island];
    const std::uint64_t revision = island_revision(island);
    if (!d.valid || d.revision != revision) {
        d.valid = true;
        d.revision = revision;
        d.defended.fill(false);
        const IslandMap& map = island_of(island).map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const MapCell& c = map.top(x, y);
                if (c.empty() || d.defended[c.city]) {
                    continue;
                }
                const assets::BuildingType* b = m_buildings.by_sprite(c.sprite);
                if (b != nullptr &&
                    (((b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor) && !b->destroyed) ||
                     b->prod_kind == prod::kTower)) {
                    d.defended[c.city] = true;
                }
            }
        }
    }
    return !d.defended[city];
}

bool Engine::attack_ok(AiPlayer& p, const City& t, std::size_t& convoy) {
    convoy = 1;
    // The original's conditions, the cheap ones first: none has a side effect, so the
    // answer is the same, but most targets fail before the island or the world is walked (TB-9).
    for (const ShipNote& n : p.ships) {
        if (n.mission >= 5 && n.mission <= 9) {
            const City* c = param_city(n.param);
            if ((n.mission <= 6 && c != nullptr && c->owner == t.owner) ||
                (n.mission >= 7 && (n.param & 0xFF) == t.owner)) {
                return false;
            }
        }
    }
    const City* home = home_city(p);
    if (home == nullptr || soldiers_on_island(p.player, t.island) >= 10) {
        return false;
    }
    if (building_count(t) <= 4 || defenceless(t.island, t.index)) {
        return false;
    }
    if (soldiers_near(p.player, *home, 8, 4).size() < 4) {
        return false;
    }
    // Open1602: no attack on a city whose towers over the landing no fleet of the computer's armed,
    // healthy ships could wreck (plan_shelling, its gain aside: the troops are not aboard yet); the
    // landing would be called off.
    if (rivals()) {
        if (const auto beach = landing_beach(p, t)) {
            if (const auto towers = covering_towers(p, t, *beach); !towers.empty()) {
                std::vector<std::pair<std::uint32_t, Ship*>> armed;
                for (Ship* o : own_ships(p)) {
                    if (o->cannons != 0 && healthy(*o)) {
                        armed.emplace_back(ship_strength(health128(*o), o->cannons), o);
                    }
                }
                std::ranges::stable_sort(armed, std::greater<>{}, &std::pair<std::uint32_t, Ship*>::first);
                std::vector<Ship*> fleet;
                for (const auto& [score, o] : armed) {
                    fleet.push_back(o);
                }
                const Shelling plan = plan_shelling(p, t, towers, fleet, nullptr);
                // Or the party that as many ships as it takes bring from home storms them on land
                // (plan_convoy). Logged when it begins and ends.
                const Convoy cv =
                    plan.ships == 0
                        ? plan_convoy(p, t, *home, towers, landing_ships(p),
                                      {2 * (static_cast<std::int32_t>(island_of(t.island).x) + beach->first) + 1,
                                       2 * (static_cast<std::int32_t>(island_of(t.island).y) + beach->second) + 1})
                        : Convoy{1, 0, 0, 0};
                const std::size_t needs = cv.needs;
                const std::size_t has = cv.has;
                const std::pair<PlayerId, std::uint32_t> key{p.player, city_param(t)};
                const auto logged = std::ranges::find(m_skips_logged, key);
                if (cv.ships == 0) {
                    if (logged == m_skips_logged.end()) {
                        m_skips_logged.push_back(key);
                        if (needs == SIZE_MAX) {
                            log(p,
                                "skips city %u/%u: its %zu towers over the landing out of the fleet's reach (%s), "
                                "no party at home that harms them (%zu soldiers)",
                                t.island, t.index, towers.size(), plan.refusal != nullptr ? plan.refusal : "", has);
                        } else {
                            log(p,
                                "skips city %u/%u: its %zu towers over the landing out of the fleet's reach (%s), "
                                "a party would need %zu soldiers (%zu without the walk), has %zu",
                                t.island, t.index, towers.size(), plan.refusal != nullptr ? plan.refusal : "", needs,
                                cv.plain, has);
                        }
                    }
                    return false;
                }
                if (logged != m_skips_logged.end()) {
                    m_skips_logged.erase(logged);
                    log(p, "skips city %u/%u no more: %s (%zu ships)", t.island, t.index,
                        plan.ships != 0 ? "its towers within the fleet's reach" : "a party big enough to storm them",
                        cv.ships);
                }
                convoy = cv.ships;
            }
        }
    }
    return true;
}

void Engine::start_attack(AiPlayer& p, const City& t, std::size_t convoy) {
    if (m_world.diplomacy().relation(p.player, t.owner).peace == Treaty::Signed) {
        return;
    }
    City* home = home_city(p);
    if (home == nullptr) {
        return;
    }
    // Open1602: an improved computer sends one attack at a time, and gathers its soldiers at the
    // Kontor for the ship to take them on board.
    if (rivals() && std::any_of(p.ships.begin(), p.ships.end(),
                                [](const ShipNote& n) { return n.mission >= 5 && n.mission <= 9; })) {
        return;
    }
    const Target ref = Target::city_of(home->island, home->index);
    Ship* ship = best_armed_ship(p, ref, 1);
    if (ship == nullptr) {
        return;
    }
    log(p, "attack on city %u/%u of player %u with ship %u", t.island, t.index, t.owner, ship->id);
    // Open1602: a convoy, as many ships as it takes to bring a party big enough to storm towers no
    // fleet wrecks (attack_ok): each boards at home, and they land together (shell_towers).
    std::vector<Ship*> ships{ship};
    if (rivals() && convoy > 1) {
        for (Ship* o : landing_ships(p)) {
            if (ships.size() < convoy && o != ship) {
                ships.push_back(o);
                log(p, "ship %u joins the attack on city %u/%u (convoy of %zu)", o->id, t.island, t.index, convoy);
            }
        }
    }
    std::uint32_t holds = 0;
    for (Ship* o : ships) {
        ship_send(p, *o, ref, 5, city_param(t));
        note(p, o->id).counter = 80;
        holds += free_holds(*o);
    }
    if (rivals() && home->warehouse_x && home->warehouse_y) {
        const WorldIsland& isl = island_of(home->island);
        SoldierOrder gather;
        gather.player = p.player;
        gather.target = Target::spot(2 * (isl.x + *home->warehouse_x) + 1, 2 * (isl.y + *home->warehouse_y) + 1);
        for (const Soldier& s : m_world.soldiers()) {
            if (gather.soldiers.size() >= std::max<std::size_t>(8, holds)) {
                break;
            }
            if (s.owner == p.player && s.island == home->island && !s.away() &&
                (s.target.empty() || s.target.kind == Target::Kind::Position)) {
                gather.soldiers.push_back(s.id);
            }
        }
        if (!gather.soldiers.empty()) {
            execute(gather);
        }
    }
}

void Engine::raid_outpost(AiPlayer& p, const City& t) {
    // A warship shells the Kontor of a tiny outpost.
    if (m_world.diplomacy().relation(p.player, t.owner).peace == Treaty::Signed || !t.warehouse_x || !t.warehouse_y) {
        return;
    }
    for (const ShipNote& n : p.ships) {
        if (n.mission == 3 && n.param == city_param(t)) {
            return;
        }
    }
    Ship* best = nullptr;
    std::uint32_t best_score = 8;
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        if (s->id == p.settle.ship || s->held || n.mission != 0 || s->cannons == 0) {
            continue;
        }
        const std::uint32_t score = ship_strength(health128(*s), s->cannons);
        if (score > best_score) {
            best = s;
            best_score = score;
        }
    }
    if (best != nullptr) {
        ship_send(p, *best, Target::building(t.island, *t.warehouse_x, *t.warehouse_y), 3, city_param(t));
    }
}

void Engine::target_scan(AiPlayer& p) {
    const auto& cities = m_world.cities();
    if (cities.empty()) {
        return;
    }
    p.target_cursor = static_cast<std::uint32_t>((p.target_cursor + 1) % cities.size());
    const City& t = cities[p.target_cursor];
    const Player* o = m_world.player(t.owner);
    if (o == nullptr || t.owner == p.player || !knows_island(p, t.island)) {
        return;  // improved computers attack only cities on islands they discovered
    }
    const bool at_peace = m_world.diplomacy().relation(p.player, t.owner).peace == Treaty::Signed;
    bool ok = false;
    std::size_t convoy = 1;
    if (o->role == PlayerRole::Human) {
        const City* own = nullptr;
        for (const City& c : cities) {
            if (c.owner == p.player && c.island == t.island) {
                own = &c;
            }
        }
        if (own != nullptr && island_size(t.island) < 4) {
            if (p.cities > 2 && own->inhabitants.size() > 0 && own->inhabitants[0] < 4) {
                return;
            }
            if (market_count(t) < 2 && building_count(t) < 3 && soldiers_on_island(t.owner, t.island) == 0) {
                raid_outpost(p, t);
                return;
            }
        } else if (at_peace) {
            return;
        }
        ok = attack_ok(p, t, convoy);
    } else if (m_world.behaviour(*o) == FactionBehaviour::Natives && !at_peace) {
        ok = attack_ok(p, t, convoy);
    } else if (rivals() && o->role == PlayerRole::Computer && m_world.behaviour(*o) == FactionBehaviour::Settlers &&
               !at_peace && p.strategy.war_target == t.owner) {
        // Open1602: an improved computer attacks another computer it wages its war on, as a human;
        // after the war, while the peace is not signed yet, it leaves it alone.
        ok = attack_ok(p, t, convoy);
    }
    if (ok) {
        start_attack(p, t, convoy);
    }
}

// ---------------------------------------------------------------------------------------------
// The soldiers on one island per tick

std::vector<PlayerId> Engine::island_enemies(const AiPlayer& a, std::uint16_t island) const {
    // Hostile humans or natives here: soldiers of theirs or a city.
    std::vector<PlayerId> enemies;
    for (std::size_t b = 0; b < m_world.players().size(); ++b) {
        const Player& pl = m_world.players()[b];
        const auto pb = static_cast<PlayerId>(b);
        // Open1602: for improved computers, another computer at war with them too.
        const bool rival_computer = rivals() && pl.role == PlayerRole::Computer && pb != a.player &&
                                    m_world.behaviour(pl) == FactionBehaviour::Settlers;
        if ((pl.role != PlayerRole::Human && m_world.behaviour(pl) != FactionBehaviour::Natives && !rival_computer) ||
            m_world.diplomacy().relation(a.player, pb).peace == Treaty::Signed) {
            continue;
        }
        bool present = soldiers_on_island(pb, island) != 0;
        for (const City& c : m_world.cities()) {
            present = present || (c.owner == pb && c.island == island);
        }
        if (present) {
            enemies.push_back(pb);
        }
    }
    return enemies;
}

void Engine::peacetime_orders(AiPlayer& a, std::uint16_t island) {
    // Peacetime: soldiers without a city here are disbanded, the others gather at the
    // Kontor or go to the doctor.
    const City* own = nullptr;
    for (const City& c : m_world.cities()) {
        if (c.owner == a.player && c.island == island) {
            own = &c;
        }
    }
    if (own == nullptr) {
        for (const Soldier& s : m_world.soldiers()) {
            if (s.owner == a.player && s.island == island && !s.away()) {
                SoldierCommand cmd;
                cmd.player = a.player;
                cmd.soldier = s.id;
                cmd.op = SoldierCommand::Op::Disband;
                execute(cmd);
                break;
            }
        }
        return;
    }
    if (!own->warehouse_x || !own->warehouse_y) {
        return;
    }
    const CityPlan& r = plan_of(a, own->island, own->index);
    const WorldIsland& isl = island_of(island);
    const Target kontor = Target::spot(2 * (isl.x + *own->warehouse_x) + 1, 2 * (isl.y + *own->warehouse_y) + 1);
    SoldierOrder to_kontor;
    to_kontor.player = a.player;
    to_kontor.target = kontor;
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner != a.player || s.island != island || s.away() || s.motion.phase == SoldierMotion::Phase::Run) {
            continue;
        }
        const assets::FigureType* ft = m_sim.m_figures->get(s.figure);
        const std::uint32_t max = ft != nullptr && ft->combat.max_health != 0 ? ft->combat.max_health : 1;
        if (r.doctor && s.health != 0 && s.health * 128U / max < 96U) {
            SoldierOrder heal;
            heal.player = a.player;
            heal.soldiers.push_back(s.id);
            heal.target = Target::building(island, r.doctor->first, r.doctor->second);
            execute(heal);
            continue;
        }
        if (s.target.empty() || s.target.kind == Target::Kind::Position) {
            to_kontor.soldiers.push_back(s.id);
        }
    }
    if (!to_kontor.soldiers.empty()) {
        execute(to_kontor);
    }
}

bool Engine::attack_orders(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies) {
    bool ordered = false;
    // A hostile city: the group with the most soldiers goes for its markets, its Kontor,
    // its houses. Without one: the soldiers meet the largest enemy group with a quarter more.
    std::vector<std::uint32_t> mine;
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner == a.player && s.island == island && !s.away() &&
            (s.target.empty() || s.target.kind == Target::Kind::Position || s.target.kind == Target::Kind::Spot)) {
            mine.push_back(s.id);
        }
    }
    if (mine.empty()) {
        return false;
    }
    const City* hostile = nullptr;
    std::vector<std::pair<std::int32_t, std::int32_t>> goals;
    for (const City& c : m_world.cities()) {
        if (c.island != island || std::find(enemies.begin(), enemies.end(), c.owner) == enemies.end()) {
            continue;
        }
        goals.clear();
        const IslandMap& map = island_of(island).map;
        for (const Production& pr : m_world.productions()) {
            if (!pr.used || pr.island != island || !map.contains(pr.x, pr.y)) {
                continue;
            }
            const Tile t = tile(island, pr.x, pr.y);
            if (t.valid() && t.city == c.index && (t.prod == prod::kMarket || t.prod == prod::kKontor) &&
                !t.type->destroyed) {
                goals.emplace_back(pr.x, pr.y);
            }
        }
        if (rivals() && !goals.empty()) {
            // Open1602: a tower whose reach covers the way from the soldiers to the market shoots
            // them on the way, one by one, while they cannot hit back: those towers come first, as
            // a player takes them, the nearest to the soldiers first (towers_over_way).
            std::int64_t sx = 0;
            std::int64_t sy = 0;
            std::int64_t n = 0;
            for (const std::uint32_t id : mine) {
                if (const Soldier* u = m_world.soldier(id)) {
                    sx += u->cell_x();
                    sy += u->cell_y();
                    ++n;
                }
            }
            std::vector<std::pair<std::int32_t, std::int32_t>> over;
            for (const TowerCover& tc :
                 n == 0 ? std::vector<TowerCover>{}
                        : towers_over_way(a, c, static_cast<std::int32_t>(sx / n), static_cast<std::int32_t>(sy / n))) {
                over.emplace_back(tc.x, tc.y);
            }
            if (!over.empty() && reachable_goals(a, island, c, mine, over)) {
                goals = std::move(over);
                hostile = &c;
                break;
            }
        }
        if (rivals() && goals.empty()) {
            // Open1602 (F15): with its markets and Kontor gone, a city with a tower is still
            // defended (city_defenceless) and cannot be taken. Its towers come first, the nearest
            // to the soldiers first; the original goes for its houses alone, and once these are
            // gone its soldiers stand in front of the tower for good.
            tower_goals(island, c, mine, goals);
            // Only the towers, so that a house within reach does not keep the soldiers from
            // cutting their way to a tower behind a forest; the houses when no way leads to one.
            if (!goals.empty() && reachable_goals(a, island, c, mine, goals)) {
                hostile = &c;
                break;
            }
            goals.clear();
        }
        for (const House& h : m_world.houses()) {
            if (h.island == island && h.city == c.index && goals.size() < 16) {
                goals.emplace_back(h.x, h.y);
            }
        }
        if (classic()) {
            // The original takes the first hostile city on the island, even one with nothing left
            // to attack (a destroyed Kontor), and its soldiers stand there for good.
            hostile = &c;
            break;
        }
        // The original sends them to the markets even behind a wall, where they give up and get
        // the same order again: they stand in front of the wall for good.
        reachable_goals(a, island, c, mine, goals);
        if (!goals.empty()) {
            hostile = &c;  // Open1602: the first hostile city with something to attack
            break;
        }
    }
    if (hostile != nullptr) {
        // Open1602 (F15): a tower shoots back, and four soldiers fall one by one where all of them
        // take it: all go for a tower.
        const bool tower =
            rivals() && !goals.empty() && is_tower(tile(island, goals.front().first, goals.front().second));
        std::size_t next = 0;
        for (std::size_t g = 0; g < goals.size() && next < mine.size(); ++g) {
            const std::size_t count = tower ? mine.size() : g == 0 ? 4 : 2;
            SoldierOrder order;
            order.player = a.player;
            order.attack = true;
            order.attack_move = rivals();  // a player's units chase only by such an order
            order.target = Target::building(island, static_cast<std::uint32_t>(goals[g].first),
                                            static_cast<std::uint32_t>(goals[g].second));
            for (std::size_t k = 0; k < count && next < mine.size(); ++k) {
                order.soldiers.push_back(mine[next++]);
            }
            if (execute(order).accepted) {
                ordered = true;
            }
        }
    } else {
        // The enemy soldier nearest to the middle of ours.
        const Soldier* enemy = nullptr;
        std::int64_t cx = 0;
        std::int64_t cy = 0;
        for (const std::uint32_t id : mine) {
            const Soldier* s = m_world.soldier(id);
            cx += s->pos_x;
            cy += s->pos_y;
        }
        cx /= static_cast<std::int64_t>(mine.size());
        cy /= static_cast<std::int64_t>(mine.size());
        std::int64_t best = -1;
        std::uint32_t enemy_count = 0;
        for (const Soldier& o : m_world.soldiers()) {
            if (o.island != island || o.away() || std::find(enemies.begin(), enemies.end(), o.owner) == enemies.end()) {
                continue;
            }
            ++enemy_count;
            const std::int64_t d = std::abs(o.pos_x - cx) + std::abs(o.pos_y - cy);
            if (best < 0 || d < best) {
                best = d;
                enemy = &o;
            }
        }
        if (enemy != nullptr) {
            SoldierOrder order;
            order.player = a.player;
            order.attack = true;
            order.attack_move = rivals();  // a player's units chase only by such an order
            order.target = Target::soldier(enemy->id);
            const std::uint32_t need = enemy_count + enemy_count / 4;
            for (std::size_t k = 0; k < mine.size() && k < need; ++k) {
                order.soldiers.push_back(mine[k]);
            }
            if (execute(order).accepted) {
                ordered = true;
            }
        }
    }
    return ordered;
}

std::vector<std::uint8_t> Engine::land_reach(PlayerId owner, std::uint16_t island,
                                             const std::vector<std::uint32_t>& soldiers) const {
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    std::vector<std::uint8_t> reach(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    std::vector<std::pair<std::int32_t, std::int32_t>> open;
    for (const std::uint32_t id : soldiers) {
        const Soldier* s = m_world.soldier(id);
        if (s == nullptr) {
            continue;
        }
        const std::int32_t x = s->cell_x() / 2 - static_cast<std::int32_t>(isl.x);
        const std::int32_t y = s->cell_y() / 2 - static_cast<std::int32_t>(isl.y);
        if (x >= 0 && y >= 0 && x < w && y < h && reach[at(x, y)] == 0) {
            reach[at(x, y)] = 1;  // where a soldier stands, whatever lies there
            open.emplace_back(x, y);
        }
    }
    walk_flood(owner, island, reach, std::move(open));
    return reach;
}

void Engine::walk_flood(PlayerId owner, std::uint16_t island, std::vector<std::uint8_t>& reach,
                        std::vector<std::pair<std::int32_t, std::int32_t>> open) const {
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const LandMap& land = m_sim.m_combat.land();
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    while (!open.empty()) {
        const auto [x, y] = open.back();
        open.pop_back();
        for (const auto& [dx, dy] : kSteps) {
            const std::int32_t nx = x + dx;
            const std::int32_t ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || reach[at(nx, ny)] != 0 ||
                land.tile(static_cast<std::int32_t>(isl.x) + nx, static_cast<std::int32_t>(isl.y) + ny, 0, nullptr,
                          owner) != LandMap::Cell::Walk) {
                continue;
            }
            reach[at(nx, ny)] = 1;
            open.emplace_back(nx, ny);
        }
    }
}

std::vector<std::uint8_t> Engine::walk_reach_to(const AiPlayer& p, const City& c, bool kontor) const {
    // The tiles of `c`'s island from which `p`'s soldiers can walk up to the city's buildings (with
    // `kontor`, to its Kontor), one flag per tile; none at all when nothing of the city is in reach.
    const WorldIsland& isl = island_of(c.island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const LandMap& land = m_sim.m_combat.land();
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    const auto walk = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h &&
               land.tile(static_cast<std::int32_t>(isl.x) + x, static_cast<std::int32_t>(isl.y) + y, 0, nullptr,
                         p.player) == LandMap::Cell::Walk;
    };
    const std::int32_t goal = kontor ? kind::kHq : kind::kBuilding;
    std::vector<std::uint8_t> reach(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    std::vector<std::pair<std::int32_t, std::int32_t>> open;
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const Tile t = tile(c.island, x, y);
            if (!t.valid() || t.city != c.index || t.owner != c.owner || t.kind != goal) {
                continue;
            }
            for (const auto& [dx, dy] : kSteps) {
                if (walk(x + dx, y + dy) && reach[at(x + dx, y + dy)] == 0) {
                    reach[at(x + dx, y + dy)] = 1;
                    open.emplace_back(x + dx, y + dy);
                }
            }
        }
    }
    walk_flood(p.player, c.island, reach, std::move(open));
    return reach;
}

bool Engine::way_out(const AiPlayer& p, std::uint16_t island, const CastleEntry& e) const {
    // Open1602 (F4): units released from a castle step out on the cell next to it the engine
    // picks and walk to the rally point, the Kontor. Where trees, walls or buildings close that
    // cell off from the Kontor, they would stand there for good: they stay in the castle instead.
    const auto cell = m_sim.military().exit_cell(island, e.x, e.y);
    const City* c = cell ? city(island, tile(island, e.x, e.y).city) : nullptr;
    if (c == nullptr || c->owner != p.player) {
        return true;  // the engine releases nobody, or the castle is not in a city of its own
    }
    const std::vector<std::uint8_t> reach = walk_reach_to(p, *c, true);
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const std::int32_t x = cell->first / 2 - static_cast<std::int32_t>(isl.x);
    const std::int32_t y = cell->second / 2 - static_cast<std::int32_t>(isl.y);
    const auto key = std::tuple{p.player, island, e.x, e.y};
    const auto kept = std::ranges::find(m_castles_kept, key);
    if (x < 0 || y < 0 || x >= w || y >= h ||
        reach[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] != 0) {
        if (kept != m_castles_kept.end()) {
            m_castles_kept.erase(kept);
        }
        return true;
    }
    if (kept == m_castles_kept.end()) {
        m_castles_kept.push_back(key);
        log(p, "castle at %u,%u keeps its units: its way out is cut off from the Kontor", e.x, e.y);
    }
    return false;
}

std::optional<Target> Engine::landing_shore(const AiPlayer& p, const Ship& s, const City& c) const {
    // Open1602 (F5): soldiers land on the nearest cell they can stand on, which at a Kontor behind
    // the city's own coast wall is the pocket by the pier, cut off from the city. The tiles the
    // city's buildings can be walked to from; when the landing cell is not among them, the water
    // off the shore tile among them nearest to the ship from which they would land among them.
    const auto landing = m_sim.military().landing_cell(s);
    if (!landing) {
        return std::nullopt;
    }
    const WorldIsland& isl = island_of(c.island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    std::vector<std::uint8_t> reach = walk_reach_to(p, c, false);
    if (std::ranges::none_of(reach, [](std::uint8_t r) { return r != 0; })) {
        return std::nullopt;  // nothing of the city to walk to
    }
    const std::int32_t lx = landing->first / 2 - static_cast<std::int32_t>(isl.x);
    const std::int32_t ly = landing->second / 2 - static_cast<std::int32_t>(isl.y);
    if (lx < 0 || ly < 0 || lx >= w || ly >= h || reach[at(lx, ly)] != 0) {
        return std::nullopt;
    }
    const auto shore = [&](std::int32_t x, std::int32_t y) {
        return std::ranges::any_of(kSteps, [&](const std::pair<std::int32_t, std::int32_t>& d) {
            const Tile n = tile(c.island, x + d.first, y + d.second);
            return n.valid() && (n.kind == kind::kBeach || n.kind == kind::kBeachOuter || n.kind == kind::kBeachInner ||
                                 n.kind == kind::kBeachVariant || n.kind == kind::kBeachMouth);
        });
    };
    std::vector<std::pair<std::int32_t, std::int32_t>> shores;  // distance to the ship, tile
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            if (reach[at(x, y)] != 0 && shore(x, y)) {
                const std::int32_t d =
                    sea_distance(static_cast<std::int32_t>(isl.x) + x - static_cast<std::int32_t>(s.x),
                                 static_cast<std::int32_t>(isl.y) + y - static_cast<std::int32_t>(s.y));
                shores.emplace_back(d, static_cast<std::int32_t>(at(x, y)));
            }
        }
    }
    std::ranges::sort(shores);
    Ship there = s;
    for (const auto& [d, i] : shores) {
        const Target t = water_near(c.island, i % w, i / w);
        there.x = static_cast<std::uint16_t>(t.x);
        there.y = static_cast<std::uint16_t>(t.y);
        const auto cell = m_sim.military().landing_cell(there);
        if (!cell) {
            continue;
        }
        const std::int32_t x = cell->first / 2 - static_cast<std::int32_t>(isl.x);
        const std::int32_t y = cell->second / 2 - static_cast<std::int32_t>(isl.y);
        if (x >= 0 && y >= 0 && x < w && y < h && reach[at(x, y)] != 0) {
            return t;
        }
    }
    return std::nullopt;
}

std::optional<Target> Engine::boarding_water(const AiPlayer& p, const Ship& s, const City& c,
                                             const std::vector<std::uint32_t>& soldiers) const {
    // Open1602 (BD): soldiers go on board only from a cell within the engine's boarding range of the
    // ship. Off a quay strip that the Kontor itself, a tower, a wall or trees cut off from the land
    // behind, the soldiers gathered there end their ways out of range and stand: a player moves the
    // ship to where they can reach it. The tiles they can walk to are flooded from where they stand.
    const WorldIsland& isl = island_of(c.island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    const std::vector<std::uint8_t> reach = land_reach(p.player, c.island, soldiers);
    const auto boards = [&](const Ship& ship, std::int32_t x, std::int32_t y) {
        const std::int32_t cx = 2 * (static_cast<std::int32_t>(isl.x) + x);
        const std::int32_t cy = 2 * (static_cast<std::int32_t>(isl.y) + y);
        return Military::boards_from(ship, cx, cy) || Military::boards_from(ship, cx + 1, cy) ||
               Military::boards_from(ship, cx, cy + 1) || Military::boards_from(ship, cx + 1, cy + 1);
    };
    std::vector<std::pair<std::int32_t, std::int32_t>> shores;  // distance to the ship, tile
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    const auto shore = [&](std::int32_t x, std::int32_t y) {
        return std::ranges::any_of(kSteps, [&](const std::pair<std::int32_t, std::int32_t>& d) {
            const Tile n = tile(c.island, x + d.first, y + d.second);
            return n.valid() && (n.kind == kind::kBeach || n.kind == kind::kBeachOuter || n.kind == kind::kBeachInner ||
                                 n.kind == kind::kBeachVariant || n.kind == kind::kBeachMouth);
        });
    };
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            if (reach[at(x, y)] == 0) {
                continue;
            }
            if (boards(s, x, y)) {
                return std::nullopt;  // the soldiers can walk into boarding range
            }
            if (shore(x, y)) {
                const std::int32_t d =
                    sea_distance(static_cast<std::int32_t>(isl.x) + x - static_cast<std::int32_t>(s.x),
                                 static_cast<std::int32_t>(isl.y) + y - static_cast<std::int32_t>(s.y));
                shores.emplace_back(d, static_cast<std::int32_t>(at(x, y)));
            }
        }
    }
    std::ranges::sort(shores);
    Ship there = s;
    for (const auto& [d, i] : shores) {
        const Target t = water_near(c.island, i % w, i / w);
        there.x = static_cast<std::uint16_t>(t.x);
        there.y = static_cast<std::uint16_t>(t.y);
        if (boards(there, i % w, i / w) && m_sim.fleet().finds_way(s, t)) {
            return t;  // water the ship can sail to (a patch of water by the coast may be cut off)
        }
    }
    return std::nullopt;
}

void Engine::tower_goals(std::uint16_t island, const City& hostile, const std::vector<std::uint32_t>& soldiers,
                         std::vector<std::pair<std::int32_t, std::int32_t>>& goals) const {
    std::int64_t cx = 0;
    std::int64_t cy = 0;
    std::int64_t n = 0;
    for (const std::uint32_t id : soldiers) {
        const Soldier* s = m_world.soldier(id);
        if (s == nullptr) {
            continue;
        }
        cx += s->cell_x() / 2;
        cy += s->cell_y() / 2;
        ++n;
    }
    if (n == 0) {
        return;
    }
    const WorldIsland& isl = island_of(island);
    cx = cx / n - isl.x;
    cy = cy / n - isl.y;
    std::vector<std::tuple<std::int64_t, std::int32_t, std::int32_t>> towers;  // distance, origin
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const Tile t = tile(island, x, y);
            if (t.valid() && t.origin && t.city == hostile.index && t.owner == hostile.owner && is_tower(t) &&
                !t.type->destroyed) {
                towers.emplace_back(std::abs(x - cx) + std::abs(y - cy), x, y);
            }
        }
    }
    std::ranges::sort(towers);
    for (const auto& [d, x, y] : towers) {
        goals.emplace_back(x, y);
    }
}

bool Engine::reachable_goals(const AiPlayer& a, std::uint16_t island, const City& hostile,
                             const std::vector<std::uint32_t>& soldiers,
                             std::vector<std::pair<std::int32_t, std::int32_t>>& goals) const {
    if (goals.empty()) {
        return false;
    }
    const std::vector<std::uint8_t> reach = land_reach(a.player, island, soldiers);
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    const auto reached = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h && reach[at(x, y)] != 0;
    };
    // The tiles of the goals, by the goal's index; a goal is kept when one of its tiles lies next
    // to one the soldiers reach.
    constexpr std::int16_t kNone = -1;
    std::vector<std::int16_t> goal_at(reach.size(), kNone);
    std::vector<std::uint8_t> keep(goals.size(), 0);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const Tile t = tile(island, x, y);
            if (!t.valid() || t.city != hostile.index || t.owner != hostile.owner) {
                continue;
            }
            for (std::size_t g = 0; g < goals.size(); ++g) {
                if (goals[g].first != t.ox || goals[g].second != t.oy) {
                    continue;
                }
                goal_at[at(x, y)] = static_cast<std::int16_t>(g);
                if (reached(x - 1, y) || reached(x + 1, y) || reached(x, y - 1) || reached(x, y + 1)) {
                    keep[g] = 1;
                }
                break;
            }
        }
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> kept;
    for (std::size_t g = 0; g < goals.size(); ++g) {
        if (keep[g] != 0) {
            kept.push_back(goals[g]);
        }
    }
    if (kept.empty()) {
        if (const auto in = way_in(a, island, hostile, reach, goal_at)) {
            kept.push_back(*in);
        }
    }
    if (kept.empty()) {
        return false;
    }
    goals = std::move(kept);
    return true;
}

std::optional<std::pair<std::int32_t, std::int32_t>> Engine::way_in(const AiPlayer& a, std::uint16_t island,
                                                                    const City& hostile,
                                                                    const std::vector<std::uint8_t>& reach,
                                                                    const std::vector<std::int16_t>& goal_at) const {
    // The cheapest way from where the soldiers stand to a goal, over open land and through what
    // they can knock down: trees and fields of nobody or of the enemy, and the enemy's buildings,
    // walls, gates and towers. Each of those costs by its hit points (the sturdy kinds take only
    // 51/128 of a hit without cannons), so a forest is cut through before a wall is broken and a
    // short way round beats both. The answer is the first such obstacle on the way, by its origin.
    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const LandMap& land = m_sim.m_combat.land();
    const auto at = [w](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    constexpr std::uint32_t kBlocked = 0;
    // What entering tile (x, y) costs: 1 for open land, more for an obstacle, kBlocked for none.
    const auto cost = [&](std::int32_t x, std::int32_t y) -> std::uint32_t {
        if (land.tile(static_cast<std::int32_t>(isl.x) + x, static_cast<std::int32_t>(isl.y) + y, 0, nullptr,
                      a.player) == LandMap::Cell::Walk) {
            return 1;
        }
        const Tile t = tile(island, x, y);
        if (!t.valid() || t.type->destroyed) {
            return kBlocked;
        }
        const bool tree = t.kind == kind::kForest && (t.owner == kNoPlayer || t.owner == hostile.owner);
        const bool building = t.owner == hostile.owner && destroyable(*t.type, false);
        if (!tree && !building) {
            return kBlocked;
        }
        const std::uint32_t hp = std::max<std::uint32_t>(1, t.type->max_health / 32);
        return 1 + 2 * (building ? hp * 5 / 2 : hp);
    };
    constexpr std::uint32_t kFar = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> dist(reach.size(), kFar);
    std::vector<std::int32_t> from(reach.size(), -1);
    using Entry = std::pair<std::uint32_t, std::uint32_t>;  // cost, tile; ties go to the lower tile
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    for (std::size_t i = 0; i < reach.size(); ++i) {
        if (reach[i] != 0) {
            dist[i] = 0;
            open.emplace(0, static_cast<std::uint32_t>(i));
        }
    }
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    while (!open.empty()) {
        const auto [d, i] = open.top();
        open.pop();
        if (d != dist[i]) {
            continue;
        }
        if (goal_at[i] >= 0) {
            // Back along the way: the obstacle nearest to the soldiers.
            auto k = static_cast<std::int32_t>(i);
            auto first = k;
            while (from[static_cast<std::size_t>(k)] >= 0) {
                first = k;
                k = from[static_cast<std::size_t>(k)];
            }
            const Tile t = tile(island, first % w, first / w);
            return std::pair{t.ox, t.oy};
        }
        const auto x = static_cast<std::int32_t>(i % static_cast<std::uint32_t>(w));
        const auto y = static_cast<std::int32_t>(i / static_cast<std::uint32_t>(w));
        for (const auto& [dx, dy] : kSteps) {
            const std::int32_t nx = x + dx;
            const std::int32_t ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                continue;
            }
            const std::size_t n = at(nx, ny);
            const std::uint32_t c = goal_at[n] >= 0 ? 1 : cost(nx, ny);
            if (c == kBlocked || d + c >= dist[n]) {
                continue;
            }
            dist[n] = d + c;
            from[n] = static_cast<std::int32_t>(i);
            open.emplace(d + c, static_cast<std::uint32_t>(n));
        }
    }
    return std::nullopt;
}

bool Engine::defend(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies) {
    // Open1602: enemy soldiers inside one of its cities come before everything else. The original
    // meets only the enemy soldier nearest to the middle of its free soldiers, with a quarter more
    // than the enemy has, and goes for a hostile city on the island first: its soldiers stand by or
    // march off while the enemy burns its own city.
    const WorldIsland& isl = island_of(island);
    const auto mine_at = [&](const Soldier& s) {
        const std::int32_t x = s.cell_x() / 2 - static_cast<std::int32_t>(isl.x);
        const std::int32_t y = s.cell_y() / 2 - static_cast<std::int32_t>(isl.y);
        if (!isl.map.contains(x, y)) {
            return false;
        }
        const std::uint8_t c = isl.map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).city;
        const City* here = c != MapCell::kNoCity ? m_world.city(island, c) : nullptr;
        return here != nullptr && here->owner == a.player;
    };
    std::vector<const Soldier*> intruders;
    for (const Soldier& o : m_world.soldiers()) {
        if (o.island == island && !o.away() && std::find(enemies.begin(), enemies.end(), o.owner) != enemies.end() &&
            mine_at(o)) {
            intruders.push_back(&o);
        }
    }
    if (intruders.empty()) {
        return false;
    }
    // Every soldier that is not fighting a unit already or on its way to the doctor: free ones,
    // guards and those attacking enemy buildings come back. Each goes for the intruder nearest to it.
    std::vector<SoldierOrder> orders;
    for (const Soldier& s : m_world.soldiers()) {
        if (s.owner != a.player || s.island != island || s.away() || s.target.kind == Target::Kind::Soldier) {
            continue;
        }
        if (s.target.kind == Target::Kind::Building) {
            const Tile t = tile(island, static_cast<std::int32_t>(s.target.x), static_cast<std::int32_t>(s.target.y));
            if (t.valid() && t.owner == a.player) {
                continue;  // healing
            }
        }
        const Soldier* best = nullptr;
        std::int64_t best_d = 0;
        for (const Soldier* o : intruders) {
            const std::int64_t d = std::abs(o->pos_x - s.pos_x) + std::abs(o->pos_y - s.pos_y);
            if (best == nullptr || d < best_d) {
                best = o;
                best_d = d;
            }
        }
        if (best == nullptr) {
            break;  // not reached: there is an intruder
        }
        const Target target = Target::soldier(best->id);
        auto it = std::find_if(orders.begin(), orders.end(), [&](const SoldierOrder& o) { return o.target == target; });
        if (it == orders.end()) {
            SoldierOrder order;
            order.player = a.player;
            order.attack = true;
            order.attack_move = rivals();  // a player's units chase only by such an order
            order.target = target;
            orders.push_back(order);
            it = orders.end() - 1;
        }
        it->soldiers.push_back(s.id);
    }
    bool ordered = false;
    for (const SoldierOrder& order : orders) {
        ordered = execute(order).accepted || ordered;
    }
    return ordered;
}

std::optional<std::uint16_t> Engine::next_watched_island() {
    const auto n = static_cast<std::uint32_t>(m_world.islands().size());
    if (classic()) {
        // The original looks at one island per step, whether anything stands there or not, so on a
        // world with many islands its soldiers wait long for their orders.
        m_state.island_cursor = (m_state.island_cursor + 1) % n;
        return static_cast<std::uint16_t>(m_state.island_cursor);
    }
    // Open1602: the next island where a computer has soldiers and whose clock has run down.
    std::vector<std::uint8_t> manned(n, 0);
    for (const Soldier& s : m_world.soldiers()) {
        const Player* pl = m_world.player(s.owner);
        if (s.island < n && !s.away() && pl != nullptr && pl->role == PlayerRole::Computer) {
            manned[s.island] = 1;
        }
    }
    for (std::uint32_t k = 0; k < n; ++k) {
        m_state.island_cursor = (m_state.island_cursor + 1) % n;
        if (manned[m_state.island_cursor] != 0 && now_ms() >= m_state.islands[m_state.island_cursor].next_ms) {
            return static_cast<std::uint16_t>(m_state.island_cursor);
        }
    }
    return std::nullopt;
}

void Engine::island_controller() {
    const auto n = static_cast<std::uint32_t>(m_world.islands().size());
    if (n == 0 || m_state.islands.size() != n) {
        return;
    }
    const std::optional<std::uint16_t> next = next_watched_island();
    if (!next) {
        return;
    }
    const std::uint16_t island = *next;
    IslandWatch& w = m_state.islands[island];
    if (now_ms() < w.next_ms) {
        return;
    }
    bool ordered = false;
    bool conflict = false;
    for (AiPlayer& a : m_state.players) {
        if (a.pirates || soldiers_on_island(a.player, island) == 0) {
            continue;
        }
        const Player* pa = m_world.player(a.player);
        if (pa == nullptr || pa->role != PlayerRole::Computer) {
            continue;
        }
        const std::vector<PlayerId> enemies = island_enemies(a, island);
        if (enemies.empty()) {
            peacetime_orders(a, island);
            continue;
        }
        conflict = true;
        if (!classic() && defend(a, island, enemies)) {
            ordered = true;
            continue;
        }
        ordered = attack_orders(a, island, enemies) || ordered;
    }
    // After orders the original looks again at the island's next turn, one round over all islands
    // later; Open1602 visits only manned islands, so it waits a second instead.
    const std::uint64_t again = classic() ? 0 : 1'000;
    w.next_ms = now_ms() + (conflict && ordered ? again : conflict ? 3'000 : 8'000);
}

// ---------------------------------------------------------------------------------------------
// Diplomacy (every 30 s or after a change)

void Engine::diplomacy(AiPlayer& p) {
    if (p.cities < 1) {
        return;
    }
    Diplomacy& d = m_world.diplomacy();
    if (p.previous_mood.size() < m_world.players().size()) {
        p.previous_mood.resize(m_world.players().size(), 0);
    }
    for (std::size_t bi = 0; bi < m_world.players().size(); ++bi) {
        const auto b = static_cast<PlayerId>(bi);
        if (b == p.player) {
            continue;
        }
        const PlayerRole role = m_world.players()[bi].role;
        const Relation& rel = d.relation(p.player, b);
        const auto order = [&](DiplomacyOrder::Kind kind, Treaty value) {
            DiplomacyOrder o;
            o.player = p.player;
            o.partner = b;
            o.kind = kind;
            o.value = value;
            execute(o);
        };
        if (role == PlayerRole::Computer) {
            // Computers always trade with each other. The original's never touch their peace;
            // Open1602's improved computers offer peace to a computer they do not mind (mood not
            // below 0 and not falling) and sign one unless they are angry (war among computers
            // comes with their war plans).
            // Improved computers at war trade only once the war is over.
            const bool at_war = rivals() && rel.peace == Treaty::None;
            if (rel.trade == Treaty::None && !at_war) {
                order(DiplomacyOrder::Kind::Trade, Treaty::Offered);
            } else if (rel.trade == Treaty::Received && !at_war) {
                order(DiplomacyOrder::Kind::Trade, Treaty::Signed);
            }
            if (rivals() && m_world.player(b) != nullptr && m_world.player(b)->status == PlayerStatus::Playing) {
                const std::int32_t m = d.mood(m_world, p.player, b, 0, now_ms());
                const std::int32_t prev = p.previous_mood[bi];
                const std::uint8_t st = stance_of(p, b);
                const bool waging = p.strategy.war_target == b;  // its own war plan: no peace
                if (!waging && rel.peace == Treaty::None && m >= prev && m >= 0 && st != stance::kPressure &&
                    st != stance::kHostile) {
                    order(DiplomacyOrder::Kind::Peace, Treaty::Offered);
                    log(p, "offers peace to computer %u (mood %d)", b, m);
                } else if (!waging && rel.peace == Treaty::Received && m > -20) {
                    order(DiplomacyOrder::Kind::Peace, Treaty::Signed);
                    log(p, "signs peace with computer %u (mood %d)", b, m);
                }
                p.previous_mood[bi] = m;
            }
            continue;
        }
        if (role != PlayerRole::Human) {
            continue;
        }
        const std::int32_t m = d.mood(m_world, p.player, b, 0, now_ms());
        const std::int32_t prev = p.previous_mood[bi];
        const bool waging = rivals() && p.strategy.war_target == b;  // its own war plan: no peace
        switch (rel.peace) {
            case Treaty::None:
                if (m >= prev && m > 20 && !waging) {
                    order(DiplomacyOrder::Kind::Peace, Treaty::Offered);
                }
                break;
            case Treaty::Offered:
            case Treaty::Signed:
                if (m < -90 || prev - m > 60) {
                    if (stronger(p.player, b)) {
                        log(p, "declares war on player %u (mood %d)", b, m);
                        order(DiplomacyOrder::Kind::Peace, Treaty::None);
                        subsidy(p, 3);
                    } else {
                        PlayerId pirates = kNoPlayer;
                        for (std::size_t i = 0; i < m_world.players().size(); ++i) {
                            if (m_world.behaviour(m_world.players()[i]) == FactionBehaviour::Pirates) {
                                pirates = static_cast<PlayerId>(i);
                            }
                        }
                        Player& pl = player(p);
                        if (pirates != kNoPlayer) {
                            const std::uint32_t price = pirate_bribe_price(m_world, pirates, p.player, b);
                            if (static_cast<std::int64_t>(price) < std::int64_t{pl.money} - 9000) {
                                d.add_event(pirates, p.player, DiplomacyEventType::BribedBy, now_ms());
                                d.add_event(pirates, b, DiplomacyEventType::BribedAgainst, now_ms());
                                pl.money = money_plus(pl.money, -std::int64_t{price});
                            }
                        }
                    }
                }
                break;
            case Treaty::Received:
                if (m > -20 && !waging) {
                    order(DiplomacyOrder::Kind::Peace, Treaty::Signed);
                    // Ships on missions against b come home.
                    for (ShipNote& n : p.ships) {
                        if (n.mission == 0) {
                            continue;
                        }
                        const City* c = param_city(n.param);
                        const bool against = (n.mission >= 7 && (n.param & 0xFF) == b) ||
                                             (n.mission != 2 && n.mission < 7 && c != nullptr && c->owner == b);
                        if (Ship* s = m_world.ship(n.id); against && s != nullptr) {
                            mission_end(p, *s, n);
                        }
                    }
                }
                break;
        }
        if (d.relation(p.player, b).peace == Treaty::Signed) {
            const Treaty trade = d.relation(p.player, b).trade;
            if (trade == Treaty::None) {
                std::uint32_t largest = 0;
                for (const City& c : m_world.cities()) {
                    if (c.owner == b) {
                        largest = std::max(largest, population(c));
                    }
                }
                const std::uint32_t roll = (rng().next() & 63U) + 40U;
                if (m >= prev && m > -10 && largest > roll) {
                    order(DiplomacyOrder::Kind::Trade, Treaty::Offered);
                }
            } else if (trade == Treaty::Offered || trade == Treaty::Signed) {
                if (m <= -56) {
                    order(DiplomacyOrder::Kind::Trade, Treaty::None);
                    continue;  // the mood is not remembered after this
                }
            } else if (trade == Treaty::Received && m > -21) {
                order(DiplomacyOrder::Kind::Trade, Treaty::Signed);
                continue;
            }
        }
        p.previous_mood[bi] = m;
    }
}

// ---------------------------------------------------------------------------------------------
// The pirates' nest (every 10 s; task 5 =)

void Engine::pirate_step(AiPlayer& p) {
    if (!pirates_enabled(m_world, m_sim.rules())) {
        return;  // no pirates
    }
    // Only once some player has grown.
    if (pirate_max_ships(m_world) <= 1) {
        return;
    }
    const std::vector<Ship*> ships = own_ships(p);
    if (ships.empty()) {
        return;
    }
    // A nest, and with Rules::pirate_nests one for every four players (the original founds one).
    if (own_city_count(p.player) < pirate_nests(m_world, m_sim.rules())) {
        if (!p.settle.active && p.stack.empty() && now_ms() - p.settle.last_ms > 36'000) {
            free_colony_cargo(p, *ships.front(), false);
            p.settle.ship = ships.front()->id;
            p.settle.need = 150;
            p.settle.floor = 0;
            p.settle.active = true;
            p.settle.pass = 1;
            pick_island(p);
        }
    }
    run_task(p);
}

void Engine::pirate_develop(AiPlayer& p) {
    // Pirate task 5: every 42 s the nest offers its most valuable goods, and only then
    // the pirates build: houses, a hunter, a fisher and four towers.
    pop(p);
    if (now_ms() - p.refresh_ms <= kNestOfferMs) {
        return;
    }
    City* c = current_city(p);
    if (c == nullptr) {
        return;
    }
    pirate_nest_offer(m_world, m_buildings, *c);
    p.refresh_ms = now_ms();
    const CityPlan* r = current_plan(p);
    if (r == nullptr || r->slots[kSlotPirateTower].count >= 4) {
        return;
    }
    const auto house = [&](std::uint32_t building, std::uint32_t n) {
        if (r->slots[kSlotPirateHouses].count >= n || r->slots[kSlotPirateHouses].attempts >= 2) {
            return false;
        }
        const assets::BuildingType* b = by_id(building);
        if (b == nullptr) {
            return false;
        }
        if (push(p, kPirateHouses, false)) {
            start_search(p, *b, kSlotPirateHouses, false);
        }
        return true;
    };
    const std::uint32_t first = (rng().next() & 1U) != 0 ? id::kPirateHouse2 : id::kPirateHouse1;
    if (house(first, 5) || house(id::kPirateHouse3, 6) || need(p, kSlotHunter, 1) || need(p, kSlotFisher, 1)) {
        return;
    }
    need_tower(p, 2, 4);
}

void Engine::log_landing(const AiPlayer& p, const Ship& s, std::uint32_t first_id, PlayerId owner) {
    // What a landing meets, for the landing troops' plan (only the log): the party by unit type,
    // the hostile towers that reach the landing cell and those on the island, the hostile
    // soldiers on the island, and the target city as it stands.
    const Combat& combat = m_sim.combat();
    std::uint32_t last_id = 0;
    std::uint16_t island = 0;
    std::int32_t cx = 0;
    std::int32_t cy = 0;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> types;  // figure, count
    std::uint32_t hp = 0;
    for (const Soldier& u : m_world.soldiers()) {
        if (u.owner != p.player || u.id < first_id) {
            continue;
        }
        last_id = std::max(last_id, u.id);
        island = u.island;
        cx = u.cell_x();
        cy = u.cell_y();
        hp += u.health;
        tally(types, u.figure);
    }
    if (last_id == 0) {
        log(p, "landing of ship %u: no soldier got off", s.id);
        return;
    }
    // The target: the city of the defender on the island nearest to the landing.
    const auto& cities = m_world.cities();
    std::uint32_t city = static_cast<std::uint32_t>(cities.size());
    for (std::uint32_t i = 0; i < cities.size(); ++i) {
        if (cities[i].island == island && cities[i].owner == owner) {
            city = i;
            break;
        }
    }
    // The way to the city (way_to).
    const std::vector<std::pair<std::int32_t, std::int32_t>> way =
        city < cities.size() ? way_to(cities[city], cx, cy) : std::vector<std::pair<std::int32_t, std::int32_t>>{};
    std::uint32_t covering = 0;
    std::uint32_t reach = 0;
    std::uint32_t towers = 0;
    for (const Tower& tw : m_world.towers()) {
        if (tw.island != island || !m_world.diplomacy().hostile(p.player, tw.owner)) {
            continue;
        }
        ++towers;
        const auto f = combat.fighter(Target::building(tw.island, tw.x, tw.y));
        if (f && f->cells.distance(cx, cy) <= f->range8 / 4) {
            ++reach;
        }
        if (f && std::ranges::any_of(
                     way, [&](const auto& c) { return f->cells.distance(c.first, c.second) <= f->range8 / 4; })) {
            ++covering;
        }
    }
    // Hostile soldiers: those in the field the attacker sees (on a discovered island, as seen_army
    // counts them), and all of them with the units in the castles, which it cannot see.
    const bool known = island_of(island).discovered_by(p.player);
    std::vector<std::uint32_t> foes;
    std::uint32_t field = 0;
    std::uint32_t field_hp = 0;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> field_types;
    for (const Soldier& u : m_world.soldiers()) {
        if (u.island == island && !u.away() && m_world.diplomacy().hostile(p.player, u.owner)) {
            foes.push_back(u.id);
            ++field;
            field_hp += u.health;
            tally(field_types, u.figure);
        }
    }
    std::uint32_t hidden = 0;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> castle_types;
    for (const CostBuilding& b : m_world.cost_buildings()) {
        if (b.kind != CostBuilding::Kind::Castle || b.island != island ||
            !m_world.diplomacy().hostile(p.player, m_world.islands()[b.island].map.top(b.x, b.y).owner)) {
            continue;
        }
        for (const CastleSlot& slot : b.slots) {
            if (slot.figure != 0) {
                ++hidden;
                tally(castle_types, slot.figure);
            }
        }
    }
    log(p,
        "landing of ship %u: towers covering the way to the city %u (%u cells); defenders in the field %sHP %u/32; in "
        "castles %s",
        s.id, covering, static_cast<unsigned>(way.size()), tallied(field_types).c_str(), field_hp,
        tallied(castle_types).c_str());
    const std::string party = tallied(types);
    std::uint32_t people = 0;
    if (city < cities.size()) {
        for (const std::uint32_t n : cities[city].inhabitants) {
            people += n;
        }
    }
    log(p,
        "landing of ship %u on island %u against player %u: party %sHP %u/32; towers reaching the landing %u of %u on "
        "the island; hostile soldiers seen %u, all %u (field %u, in castles %u); city %u inhabitants %u",
        s.id, unsigned{island}, unsigned{owner}, party.c_str(), hp, reach, towers, known ? field : 0U, field + hidden,
        field, hidden, city, people);
    LandingWatch w;
    w.player = p.player;
    w.owner = owner;
    w.island = island;
    w.city = city;
    w.ship = s.id;
    w.landed_ms = now_ms();
    w.next_ms = now_ms() + kLandingLogMs;
    w.first_id = first_id;
    w.last_id = last_id;
    w.towers = towers;
    w.foes = std::move(foes);
    w.ships = ships_off(island, p.player);
    for (const std::uint32_t id : w.ships) {
        if (const Ship* o = m_world.ship(id)) {
            log(p, "landing of ship %u: ship %u off the island, figure %u, HP %u/32, cannons %u, soldiers aboard %u",
                s.id, o->id, unsigned{o->figure}, o->health, unsigned{o->cannons}, soldiers_aboard(*o));
        }
    }
    w.buildings = buildings_of(island, owner);
    std::uint32_t standing = 0;
    std::uint64_t worth = 0;
    for (const BuildingCount& b : w.buildings) {
        standing += b.count;
        worth += b.value * b.count;
    }
    log(p, "landing of ship %u: player %u has %u buildings on the island (value %llu)", s.id, unsigned{owner}, standing,
        static_cast<unsigned long long>(worth));
    m_landings.push_back(w);
}

std::vector<Engine::BuildingCount> Engine::buildings_of(std::uint16_t island, PlayerId owner) const {
    std::vector<BuildingCount> out;
    const IslandMap& map = island_of(island).map;
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(map.height()); ++y) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(map.width()); ++x) {
            const Tile t = tile(island, x, y);
            // Buildings only: not the trees, fields and ground a city's land carries (woodcutters
            // and farms take those all the time), nor ruins.
            if (!t.valid() || !t.origin || t.owner != owner || t.kind == kind::kForest || t.kind == kind::kGround ||
                t.kind == kind::kRuin || t.kind == kind::kBeachRuin) {
                continue;
            }
            const std::uint64_t value = build_value(t.type->build_cost);
            auto it = std::ranges::find(out, t.type->id, &BuildingCount::id);
            if (it == out.end()) {
                out.push_back(BuildingCount{t.type->id, 1, value});
            } else {
                ++it->count;
            }
        }
    }
    return out;
}

std::vector<std::uint32_t> Engine::ships_off(std::uint16_t island, PlayerId player) const {
    const WorldIsland& isl = island_of(island);
    std::vector<std::uint32_t> out;
    for (const Ship& o : m_world.ships()) {
        if (o.owner == player && !o.cart() && !o.sinking && near_island(isl, o)) {
            out.push_back(o.id);
        }
    }
    return out;
}

void Engine::watch_landings() {
    // Each minute after a landing: the party alive and its HP, the hostile towers that reach one of
    // them, the towers left on the island, and the target city's owner and inhabitants. When the
    // party is gone, its end (only the log).
    const Combat& combat = m_sim.combat();
    const auto& cities = m_world.cities();
    for (std::size_t i = 0; i < m_landings.size();) {
        LandingWatch& w = m_landings[i];
        if (now_ms() < w.next_ms) {
            ++i;
            continue;
        }
        w.next_ms += kLandingLogMs;
        const AiPlayer* p = nullptr;
        for (const AiPlayer& q : m_state.players) {
            p = q.player == w.player ? &q : p;
        }
        std::uint32_t alive = 0;
        std::uint32_t hp = 0;
        std::uint32_t ashore = 0;
        std::vector<std::pair<std::int32_t, std::int32_t>> cells;
        for (const Soldier& u : m_world.soldiers()) {
            if (u.owner != w.player || u.id < w.first_id || u.id > w.last_id || u.dying()) {
                continue;
            }
            ++alive;
            hp += u.health;
            if (u.island == w.island) {
                ++ashore;
                cells.emplace_back(u.cell_x(), u.cell_y());
            }
        }
        std::vector<std::pair<std::uint16_t, std::uint32_t>> foes;
        std::uint32_t foes_hp = 0;
        std::uint32_t there = 0;
        std::uint32_t fresh = 0;
        std::uint32_t came = 0;
        for (const Soldier& u : m_world.soldiers()) {
            if (u.island == w.island && !u.away() && m_world.diplomacy().hostile(w.player, u.owner)) {
                tally(foes, u.figure);
                foes_hp += u.health;
                // Where it comes from: on the island at the landing, a unit new since (recruited
                // or out of a castle), or one that came from another island.
                if (std::ranges::find(w.foes, u.id) != w.foes.end()) {
                    ++there;
                } else if (u.id > w.last_id) {
                    ++fresh;
                } else {
                    ++came;
                }
            }
        }
        std::uint32_t reach = 0;
        std::uint32_t towers = 0;
        for (const Tower& tw : m_world.towers()) {
            if (tw.island != w.island || !m_world.diplomacy().hostile(w.player, tw.owner)) {
                continue;
            }
            ++towers;
            const auto f = combat.fighter(Target::building(tw.island, tw.x, tw.y));
            if (f && std::ranges::any_of(
                         cells, [&](const auto& c) { return f->cells.distance(c.first, c.second) <= f->range8 / 4; })) {
                ++reach;
            }
        }
        PlayerId owner = kNoPlayer;
        std::uint32_t people = 0;
        if (w.city < cities.size()) {
            owner = cities[w.city].owner;
            for (const std::uint32_t n : cities[w.city].inhabitants) {
                people += n;
            }
        }
        std::uint32_t afloat = 0;
        std::uint32_t ship_hp = 0;
        std::uint32_t aboard = 0;  // a party that boarded again leaves the soldiers' list
        for (const std::uint32_t id : w.ships) {
            if (const Ship* o = m_world.ship(id); o != nullptr && !o->sinking) {
                ++afloat;
                ship_hp += o->health;
                aboard += soldiers_aboard(*o);
            }
        }
        // The defender's buildings destroyed since the landing, by building id, and their value.
        std::string lost;
        std::uint64_t value = 0;
        const auto now_b = buildings_of(w.island, w.owner);
        for (const BuildingCount& b : w.buildings) {
            const auto it = std::ranges::find(now_b, b.id, &BuildingCount::id);
            const std::uint32_t left = it != now_b.end() ? it->count : 0;
            if (left < b.count) {
                value += b.value * (b.count - left);
                lost += std::to_string(b.id) + "x" + std::to_string(b.count - left) + " ";
            }
        }
        const auto minute = static_cast<unsigned>((now_ms() - w.landed_ms) / kLandingLogMs);
        if (p != nullptr) {
            log(*p,
                "landing of ship %u, minute %u: %u alive (%u ashore), HP %u/32; towers reaching them %u, on the "
                "island %u of %u; defenders in the field %sHP %u/32 (%u there at the landing, %u new, %u from other "
                "islands); city %u owner %u inhabitants %u; ships afloat "
                "%u of %u, HP %u/32, soldiers aboard %u; buildings destroyed %s(value %llu)%s",
                w.ship, minute, alive, ashore, hp, reach, towers, w.towers, tallied(foes).c_str(), foes_hp, there,
                fresh, came, w.city, unsigned{owner}, people, afloat, static_cast<unsigned>(w.ships.size()), ship_hp,
                aboard, lost.c_str(), static_cast<unsigned long long>(value),
                alive == 0 ? "; the party is gone (fallen or aboard)" : "");
        }
        if (alive == 0) {
            m_landings.erase(m_landings.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

}  // namespace o1602::sim::ai
