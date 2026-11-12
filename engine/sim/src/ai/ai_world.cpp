// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the computer players read from the world: tiles, cities, land, money.
#include <algorithm>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

#include "o1602/sim/growth.hpp"

namespace o1602::sim::ai {

bool Engine::inside(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    return island < m_world.islands().size() && m_world.islands()[island].map.contains(x, y);
}

Tile Engine::tile(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    Tile t;
    if (!inside(island, x, y)) {
        return t;
    }
    const IslandMap& map = m_world.islands()[island].map;
    return tile_of(map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)), x, y);
}

Tile Engine::tile_of(const MapCell& c, std::int32_t x, std::int32_t y) const {
    Tile t;
    t.city = c.city;
    t.owner = c.owner;
    if (c.empty()) {
        return t;
    }
    const assets::BuildingType* b = m_buildings.by_sprite(c.sprite);
    if (b == nullptr) {
        return t;
    }
    const auto [ox, oy] = IslandMap::origin_of(c, *b, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    t.ox = ox;
    t.oy = oy;
    t.origin = ox == x && oy == y;
    // Growing and withered crops count as the ripe crop (the original adds or takes one record).
    const assets::BuildingType* n = b;
    if (b->prod_kind == prod::kGrowing) {
        const assets::BuildingType* r = b->withered ? ripe_of_withered(m_buildings, *b) : ripe_record(m_buildings, *b);
        if (r != nullptr) {
            n = r;
        }
    }
    t.type = n;
    t.kind = n->kind;
    t.prod = n->prod_kind;
    t.ware = n->ware;
    t.raw = n->raw_ware;
    t.growth = static_cast<std::uint8_t>(std::uint32_t{n->edge_growth} * 32U / 128U);
    return t;
}

City* Engine::current_city(AiPlayer& p) {
    return p.has_city ? city(p.island, p.city) : nullptr;
}

const City* Engine::current_city(const AiPlayer& p) const {
    return p.has_city ? city(p.island, p.city) : nullptr;
}

City* Engine::home_city(AiPlayer& p) {
    if (!p.home_valid) {
        return nullptr;
    }
    City* c = city(p.home_island, p.home_city);
    return c != nullptr && c->owner == p.player ? c : nullptr;
}

CityPlan& Engine::plan_of(AiPlayer& p, std::uint16_t island, std::uint8_t c) {
    for (CityPlan& r : p.plans) {
        if (r.island == island && r.city == c) {
            return r;
        }
    }
    CityPlan r;
    r.island = island;
    r.city = c;
    r.stage_ms = now_ms();
    p.plans.push_back(r);
    return p.plans.back();
}

CityPlan* Engine::current_plan(AiPlayer& p) {
    if (!p.has_city) {
        return nullptr;
    }
    return &plan_of(p, p.island, p.city);
}

std::uint32_t Engine::own_city_count(PlayerId owner) const {
    std::uint32_t n = 0;
    for (const City& c : m_world.cities()) {
        n += c.owner == owner ? 1U : 0U;
    }
    return n;
}

std::uint32_t Engine::population(const City& c, std::size_t from) const {
    std::uint32_t n = 0;
    for (std::size_t k = from; k < c.inhabitants.size(); ++k) {
        n += c.inhabitants[k];
    }
    return n;
}

std::uint32_t Engine::demand(const City& c, std::uint8_t w) const {
    // Food by all inhabitants, the other consumer goods by the classes that want them.
    if (w == m_buildings.food_ware()) {
        return static_cast<std::uint32_t>((std::uint64_t{population(c)} * m_buildings.food_need() * 6) >> 8);
    }
    std::uint64_t sum = 0;
    const auto& classes = m_buildings.resident_classes();
    for (std::size_t k = 0; k < classes.size() && k < c.inhabitants.size(); ++k) {
        for (const assets::ResidentClass::Need& n : classes[k].needs) {
            if (n.ware == w) {
                sum += std::uint64_t{c.inhabitants[k]} * n.amount;
            }
        }
    }
    return static_cast<std::uint32_t>((sum * 6) >> 8);
}

std::uint32_t Engine::supply(const City& c, std::uint8_t ware) const {
    const IslandMap& map = island_of(c.island).map;
    std::uint64_t sum = 0;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty()) {
            continue;
        }
        const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->ware != ware || b->interval == 0 || b->prod_kind == prod::kMarket ||
            b->prod_kind == prod::kKontor) {
            continue;
        }
        if (pr.elapsed >= 60) {
            sum += std::uint64_t{pr.produced} * 60 / pr.elapsed;
        } else {
            sum += std::uint64_t{b->produce_amount} * 30 / b->interval;
        }
    }
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(sum, 0xFFFFFFFFU));
}

bool Engine::food_short(const City& c) const {
    const std::uint8_t food = m_buildings.food_ware();
    return short_of_food(c.stock(food), supply(c, food), demand(c, food));
}

bool Engine::food_behind(const City& c) const {
    const std::uint8_t food = m_buildings.food_ware();
    const std::uint32_t made = supply(c, food);
    const std::uint32_t eaten = demand(c, food);
    return made < eaten && std::uint64_t{c.stock(food)} < std::uint64_t{eaten - made} * kFoodAheadMinutes;
}

bool Engine::stock_full(const City& c, std::uint8_t ware) const {
    return ware != 0 && full_stock(c.stock(ware), c.capacity);
}

std::int32_t Engine::kontor_size(const City& c) const {
    // The rings of paving and towers go round the Kontor's record (3 x 3 tiles in the original).
    constexpr std::int32_t kOriginalSize = 3;
    if (!c.warehouse_x || !c.warehouse_y) {
        return kOriginalSize;
    }
    const Tile t = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    return t.valid() && t.type->size_x != 0 ? static_cast<std::int32_t>(t.type->size_x) : kOriginalSize;
}

std::uint32_t Engine::kontor_level(const City& c) const {
    if (!c.warehouse_x || !c.warehouse_y) {
        return 0;
    }
    const Tile t = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    if (!t.valid() || t.prod != prod::kKontor || t.type->destroyed) {
        return 0;
    }
    const std::uint32_t level = id::kontor_level(t.type->id);
    return level != 0 ? level : 1;
}

std::uint32_t Engine::market_count(const City& c) const {
    std::uint32_t n = 0;
    const IslandMap& map = m_world.islands()[c.island].map;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty()) {
            continue;
        }
        const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite);
        if (b != nullptr && (b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor) && !b->destroyed) {
            ++n;
        }
    }
    return n;
}

std::uint32_t Engine::building_count(const City& c) const {
    std::uint32_t n = 0;
    const IslandMap& map = m_world.islands()[c.island].map;
    for (const Production& pr : m_world.productions()) {
        if (pr.used && pr.island == c.island && map.contains(pr.x, pr.y) && map.top(pr.x, pr.y).city == c.index) {
            ++n;
        }
    }
    for (const House& h : m_world.houses()) {
        if (h.island == c.island && h.city == c.index) {
            ++n;
        }
    }
    return n;
}

std::uint32_t Engine::free_land(std::uint16_t island, std::uint8_t own, std::uint32_t* percent) const {
    const IslandMap& map = m_world.islands()[island].map;
    const std::uint32_t total = map.width() * map.height();
    std::int64_t n = total;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            if (c.city != MapCell::kNoCity && c.city != own) {
                --n;
                continue;
            }
            const assets::BuildingType* b = c.empty() ? nullptr : m_buildings.by_sprite(c.sprite);
            if (b == nullptr) {
                continue;
            }
            switch (b->kind) {
                case kind::kWallBeach:
                case kind::kTowerBeach:
                case kind::kRock:
                case kind::kSea:
                case kind::kSurf:
                case kind::kSurfCorner:
                case kind::kRiverMouth:
                case kind::kBeach:
                case kind::kBeachVariant:
                case kind::kBeachRuin:
                case kind::kPier:
                case kind::kSlope:
                case kind::kSlopeCorner:
                    n -= 1;
                    break;
                case kind::kRiver:
                case kind::kRiverCorner:
                case kind::kBridge:
                    n -= 4;
                    break;
                case kind::kBeachOuter:
                case kind::kBeachInner:
                    n -= 2;
                    break;
                default:
                    break;
            }
        }
    }
    const auto result = static_cast<std::uint32_t>(std::max<std::int64_t>(n, 0));
    if (percent != nullptr) {
        *percent = total >= 100 ? result / (total / 100) : 0;
    }
    return result;
}

std::uint32_t Engine::civ_level(const AiPlayer& p) const {
    return player(p).ship_step();
}

bool Engine::unlocked(const AiPlayer& p, const assets::BuildingType& b) const {
    return player(p).unlocked(b.infra);
}

bool Engine::affordable(const AiPlayer& p, const City& c, const assets::BuildingType& b) const {
    // The original compares the gold unsigned, so a debt passes (S-096); Open1602 does not.
    const auto& m = m_buildings.materials();
    const auto& cost = b.build_cost;
    const bool goods = c.stock(m.wood) >= cost.wood && c.stock(m.bricks) >= cost.bricks &&
                       c.stock(m.tools) >= cost.tools && c.stock(m.cannons) >= cost.cannons;
    // Open1602: an improved rival builds from its budget (gold_budget): the gold less what a
    // founding under way keeps stays above its cushion now and, with the building's running costs,
    // over the horizon. Not a producer, which may be the one to make the goods it buys, a house,
    // which brings the taxes, or a market or Kontor, which the houses and the goods need: held
    // back, the markets alone stood still for 2000 computer minutes in a test of ten worlds. Their
    // gold leaves all the same, and the budget sees it gone at the next plan.
    const bool exempt =
        b.ware != 0 || b.prod_kind == prod::kHouse || b.prod_kind == prod::kMarket || b.prod_kind == prod::kKontor;
    if (rivals() && !p.pirates && !exempt) {
        // Interim, removed with G4 (the value of a ship): its first yard, the small one, needs only
        // its price in hand beside what a founding keeps, not the cushion or the horizon. The budget
        // sees only the yard's upkeep, not what its ships bring, and small towns that lost a few coins
        // a minute never built one.
        if (b.id == id::kShipyardSmall && !p.yard_valid) {
            // What is kept for it (yard_savings) is its own gold.
            return goods && std::int64_t{player(p).money} - (reserved(p) - yard_savings(p)) >= cost.money;
        }
        Commitment plan;
        plan.cost = cost.money;
        plan.upkeep = b.running_cost;
        return goods && can_commit(p, plan, Spend::Building);
    }
    return goods && std::int64_t{player(p).money} >= cost.money;
}

const assets::BuildingType* Engine::producer(std::uint8_t w, std::uint8_t raw) const {
    if (w >= m_producers.size()) {
        return nullptr;
    }
    for (const assets::BuildingType* b : m_producers[w]) {
        if (b->raw_ware == raw) {
            return b;
        }
    }
    return nullptr;
}

std::uint32_t Engine::fertility(std::uint16_t island, std::uint8_t w) const {
    return island < m_world.islands().size() ? island_fertility(m_world, m_world.islands()[island], w) : 0;
}

std::uint32_t Engine::full_crops(std::uint16_t island) const {
    std::uint32_t n = 0;
    for (const std::uint8_t w : {ware::kSpiceField, ware::kTobaccoField, ware::kWineField, ware::kCottonField,
                                 ware::kCocoaField, ware::kSugarField}) {
        n += fertility(island, w) == 128 ? 1U : 0U;
    }
    return n;
}

bool Engine::is_settler(PlayerId owner) const {
    const Player* pl = m_world.player(owner);
    return pl != nullptr && (pl->role == PlayerRole::Human || pl->role == PlayerRole::Computer);
}

std::uint32_t Engine::island_size(std::uint16_t island) const {
    const assets::IslandHeader& h = m_world.islands()[island].header;
    if (h.template_number != assets::IslandHeader::kNoTemplate) {
        return h.template_size;
    }
    const std::uint32_t area = std::uint32_t{h.width} * h.height;
    return area <= 900 ? 0U : area <= 1600 ? 1U : area <= 2600 ? 2U : area <= 4200 ? 3U : 4U;
}

std::uint32_t Engine::island_region(std::uint16_t island, std::uint32_t nests) const {
    const WorldIsland& i = m_world.islands()[island];
    return pirate_region(m_world, i.x + i.map.width() / 2, i.y + i.map.height() / 2, nests);
}

void Engine::add_goods(City& c, std::uint8_t w, std::uint32_t amount) {
    // The computer players' gifts to themselves are neither production nor trade: not booked in
    // the goods statistics (nobody else may see them anyway).
    c.ensure_ware(w);
    const std::uint32_t room = c.capacity > c.goods[w] ? c.capacity - c.goods[w] : 0;
    c.goods[w] += std::min(amount, room);
}

std::uint32_t Engine::remove_goods(City& c, std::uint8_t w, std::uint32_t amount) {
    c.ensure_ware(w);
    const std::uint32_t taken = std::min(amount, c.goods[w]);
    c.goods[w] -= taken;
    c.book(w, GoodsFlow::Construction, taken);
    return taken;
}

std::uint32_t Engine::army_strength(PlayerId owner) const {
    return m_sim.military().army_size(owner);
}

std::uint32_t Engine::total_population(PlayerId owner) const {
    std::uint32_t n = 0;
    for (const City& c : m_world.cities()) {
        if (c.owner == owner) {
            n += population(c);
        }
    }
    return n;
}

bool Engine::behind(const AiPlayer& p) const {
    // Behind the leading human in inhabitants (scaled by the difficulty) or in units.
    std::uint32_t best_pop = 0;
    std::uint32_t best_army = 0;
    for (std::size_t h = 0; h < m_world.players().size(); ++h) {
        if (m_world.players()[h].role != PlayerRole::Human) {
            continue;
        }
        best_pop = std::max(best_pop, total_population(static_cast<PlayerId>(h)));
        best_army = std::max(best_army, army_strength(static_cast<PlayerId>(h)));
    }
    std::uint32_t threshold = best_pop;
    switch (difficulty()) {
        case 0:
            threshold = best_pop - best_pop / 4;
            break;
        case 1:
            break;
        case 2:
            threshold = best_pop + best_pop / 4;
            break;
        default:
            threshold = best_pop + best_pop / 2;
            break;
    }
    return total_population(p.player) < threshold || army_strength(p.player) < best_army;
}

bool Engine::threatened(const AiPlayer& p) const {
    // The current city was attacked within 30 s, or a human not at peace has soldiers
    // on its island (the original looks only at the first such human, S-102).
    const City* c = current_city(p);
    if (c == nullptr) {
        return false;
    }
    if (c->last_attacked_ms != 0 && now_ms() - c->last_attacked_ms < 30'000) {
        return true;
    }
    for (const Soldier& s : m_world.soldiers()) {
        if (s.island != c->island || s.owner == p.player || s.dying()) {
            continue;
        }
        const Player* o = m_world.player(s.owner);
        if (o != nullptr && o->role == PlayerRole::Human && m_world.diplomacy().hostile(p.player, s.owner)) {
            return true;
        }
    }
    return false;
}

}  // namespace o1602::sim::ai
