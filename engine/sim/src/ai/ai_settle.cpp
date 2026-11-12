// SPDX-License-Identifier: GPL-3.0-or-later
//
// Founding cities: the island search, the free land
// test (task 2), the Kontor site and the free cargo (task 3), the landing (task 4), later colonies
// and giving a city up (task 0x2E).
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint64_t kColonyCooldownMs = 60'000;      // 60 s between settlement events
constexpr std::uint64_t kColonySpacingMs = 20 * 60'000;  // improved computers: between two foundings
constexpr std::int32_t kNeedStep = 50;
constexpr std::uint64_t kLeftIslandMs = 2'700'000;  // improved computers: 45 min before they settle a left island again
constexpr std::uint32_t kPirateRegionPasses = 6;    // passes a further nest keeps to a part without one
constexpr std::uint8_t kLandRetries = 4;
constexpr std::uint32_t kOreLowTons = 80;  // improved computers look for new ore below this much in their veins
constexpr std::uint32_t kReach = 5;        // a ship within 5 tiles of a Kontor lies at it
constexpr std::uint32_t kLandRange = 12;   // founding: the ship within 12 tiles of the site

bool in_circle(std::int32_t dx, std::int32_t dy, std::uint32_t r) {
    static const std::vector<std::uint8_t> kNear = circle_half_widths(kReach);
    static const std::vector<std::uint8_t> kLand = circle_half_widths(kLandRange);
    const std::vector<std::uint8_t>& circle = r == kLandRange ? kLand : kNear;
    const std::int32_t ax = dx < 0 ? -dx : dx;
    const std::int32_t ay = dy < 0 ? -dy : dy;
    return static_cast<std::uint32_t>(ay) <= r && ax <= circle[static_cast<std::size_t>(ay)];
}

/// Land a settlement may claim (task 2): forest, ground, ruins, slopes.
bool settle_ground(std::int32_t k) {
    return k == kind::kForest || k == kind::kGround || k == kind::kRuin || k == kind::kSlope || k == kind::kSlopeCorner;
}

bool beach_kind(std::int32_t k) {
    return k == kind::kBeach || k == kind::kBeachInner || k == kind::kBeachVariant || k == kind::kBeachRuin ||
           k == kind::kPier;
}

}  // namespace

Ship* Engine::settle_ship(AiPlayer& p) {
    if (p.settle.ship == 0) {
        return nullptr;
    }
    Ship* s = m_world.ship(p.settle.ship);
    if (s == nullptr || s->owner != p.player || s->sinking) {
        p.settle.ship = 0;
        return nullptr;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// The island search

bool Engine::island_allowed(const AiPlayer& p, std::uint16_t island) const {
    // No own city there, and no city of a human or computer with at most two cities.
    for (const City& c : m_world.cities()) {
        if (c.island != island) {
            continue;
        }
        if (c.owner == p.player) {
            return false;
        }
        if (is_settler(c.owner) && own_city_count(c.owner) <= 2) {
            return false;
        }
    }
    return true;
}

bool Engine::free_deposit(std::uint16_t island, std::uint8_t w) const {
    // A vein of ore or gold left on the island that no city's land covers yet.
    for (const auto& d : island_of(island).header.deposits) {
        if (d.ware == w && d.amount > 0 && tile(island, d.x, d.y).city == MapCell::kNoCity) {
            return true;
        }
    }
    return false;
}

bool Engine::island_contested(const AiPlayer& p, std::uint16_t island) const {
    // Open1602, improved computers: an island with a city of a small rival (two cities at most),
    // that the original's computers never settle, when it has a free vein of the ore or gold its
    // goal looks for. Not next to a partner or an ally, not next to a rival stronger than itself,
    // and never on an island with a city of its own.
    const std::uint8_t crop = p.settle.crop;
    if (crop == 0 || crop != goal_crop(p) || !knows_crops(p, island) || !free_deposit(island, crop)) {
        return false;
    }
    const std::uint32_t own = strength(p.player);
    bool contested = false;
    for (const City& c : m_world.cities()) {
        if (c.island != island) {
            continue;
        }
        if (c.owner == p.player) {
            return false;
        }
        if (!is_settler(c.owner) || own_city_count(c.owner) > 2) {
            continue;
        }
        if (stance_of(p, c.owner) == stance::kPartner || m_world.diplomacy().allied(p.player, c.owner) ||
            threat_of(c.owner) > own) {
            return false;
        }
        contested = true;
    }
    return contested;
}

void Engine::pick_island(AiPlayer& p) {
    const auto n = static_cast<std::uint16_t>(m_world.islands().size());
    if (n == 0) {
        return;
    }
    Settlement& s = p.settle;
    if (settle_ship(p) == nullptr && own_ships(p).empty()) {
        return;  // nothing to sail with: no island scans until there is a ship again (TB-13)
    }
    if (p.pirates) {
        // Pirates: the first island whose size class is at most the pass number (smallest first).
        // A further nest (Rules::pirate_nests) goes to a part of the map without one, as long as
        // the passes find such an island, and never to an island with a nest.
        const std::uint32_t nests = pirate_nests(m_world, m_sim.rules());
        std::vector<bool> region_has_nest(nests, false);
        std::vector<bool> island_has_nest(n, false);
        for (const City& c : m_world.cities()) {
            if (c.owner == p.player && c.island < n) {
                island_has_nest[c.island] = true;
                region_has_nest[island_region(c.island, nests)] = true;
            }
        }
        for (std::uint32_t k = 0; k < 2U * n; ++k) {
            s.cursor = static_cast<std::uint16_t>(s.cursor + 1);
            if (s.cursor >= n) {
                s.cursor = 0;
                ++s.pass;
                s.last_ms = now_ms();
            }
            if (island_has_nest[s.cursor] ||
                (s.pass <= kPirateRegionPasses && region_has_nest[island_region(s.cursor, nests)])) {
                continue;
            }
            if (island_size(s.cursor) <= std::max<std::uint32_t>(s.pass, 1)) {
                s.island = s.cursor;
                push(p, kSettleArea, true);
                return;
            }
        }
        return;
    }
    // Improved computers see what a player sees: only the islands their ships have discovered, the
    // crops only of those they explored. While a ship explores, the search waits for it.
    const bool sighted = rivals();
    if (sighted && exploring(p)) {
        return;
    }
    // Open1602: an improved computer with a home city looks at the islands nearest to it first, so
    // that its colonies lie within reach of its ships; the original goes by the islands' numbers.
    std::vector<std::uint16_t> order;
    if (rivals()) {
        if (const City* home = home_city(p)) {
            const auto at = city_point(*home);
            std::vector<std::pair<std::int64_t, std::uint16_t>> by_way;
            by_way.reserve(n);
            for (std::uint16_t i = 0; i < n; ++i) {
                const WorldIsland& isl = island_of(i);
                const std::int64_t dx = std::int64_t{isl.x} + isl.map.width() / 2 - at.first;
                const std::int64_t dy = std::int64_t{isl.y} + isl.map.height() / 2 - at.second;
                by_way.emplace_back(dx * dx + dy * dy, i);
            }
            std::sort(by_way.begin(), by_way.end());
            for (const auto& [d, i] : by_way) {
                order.push_back(i);
            }
        }
    }
    const bool for_goal = s.crop != 0 && s.crop == goal_crop(p);
    for (std::uint32_t k = 0; k <= n; ++k) {
        s.cursor = static_cast<std::uint16_t>(s.cursor + 1);
        if (s.cursor >= n) {
            // Before island 0, so that the next search tests it first (it was passed over).
            s.cursor = std::numeric_limits<std::uint16_t>::max();
            // Nothing among the known islands: a ship sails to the next unknown one first; every
            // trip counts as a pass, so the search still ends.
            if (sighted && explore_next(p)) {
                log(p, "exploring for an island (need %d, crop %u)", s.need, s.crop);
            }
            if (s.need <= s.floor) {
                if (s.crop != 0) {
                    // Drop the crop and search again from 1800 tiles.
                    s.previous_crop = s.crop;
                    s.crop = 0;
                    s.need = 1800;
                    s.floor = 300;
                    return;
                }
                s.failed = true;
                p.colony_failed = true;
                log(p, "no island found for a colony");
                s.previous_crop = 0;
                s.last_ms = now_ms();
                return;
            }
            s.need -= kNeedStep;
            return;
        }
        const std::uint16_t i = order.empty() ? s.cursor : order[s.cursor];
        if (!(island_allowed(p, i) || island_contested(p, i)) || !knows_island(p, i) || left_lately(p, i)) {
            continue;
        }
        // A colony for the goal's ore takes a small vein too (as crop_available counts it), if it is
        // free; the rest grows fully.
        if (s.crop != 0 && (!knows_crops(p, i) || (for_goal && s.crop == ware::kOre ? fertility(i, s.crop) < 64
                                                                                    : fertility(i, s.crop) != 128))) {
            continue;
        }
        if (for_goal && !free_deposit(i, s.crop)) {
            continue;  // every vein lies in someone's land already
        }
        if (island_size(i) < (p.cities <= 1 ? 2U : 1U)) {
            continue;
        }
        // The free land task 2 counts never exceeds the island's rectangle (TB-13).
        const IslandMap& map = island_of(i).map;
        if (std::int64_t{map.width()} * std::int64_t{map.height()} < std::int64_t{s.need}) {
            continue;
        }
        s.island = i;
        push(p, kSettleArea, true);
        return;
    }
}

bool Engine::left_lately(AiPlayer& p, std::uint16_t island) {
    // Open1602: an island an improved computer gave a city up on is passed over for 45 minutes;
    // before, it founded there again at once and gave the city up again.
    // A computer without a city founds where it can.
    std::erase_if(p.left_islands, [&](const auto& e) { return now_ms() - e.second >= kLeftIslandMs; });
    return home_city(p) != nullptr &&
           std::any_of(p.left_islands.begin(), p.left_islands.end(), [&](const auto& e) { return e.first == island; });
}

bool Engine::knows_island(const AiPlayer& p, std::uint16_t island) const {
    // The original's computers know every island; an improved one those its ships discovered or
    // explored (the setting that shows every island's resources explores them all from the start).
    if (!rivals() || island_of(island).discovered_by(p.player) || island_of(island).explored_by(p.player)) {
        return true;
    }
    return std::any_of(m_world.cities().begin(), m_world.cities().end(),
                       [&](const City& c) { return c.island == island && c.owner == p.player; });
}

bool Engine::knows_crops(const AiPlayer& p, std::uint16_t island) const {
    // An improved computer knows the crops of an island it explored or has a city on.
    if (!rivals() || island_of(island).explored_by(p.player)) {
        return true;
    }
    return std::any_of(m_world.cities().begin(), m_world.cities().end(),
                       [&](const City& c) { return c.island == island && c.owner == p.player; });
}

Ship* Engine::explorer(AiPlayer& p) {
    // The ship of the settlement, or the first of the computer's, which becomes it.
    if (Ship* s = settle_ship(p)) {
        return s;
    }
    const std::vector<Ship*> ships = own_ships(p);
    if (ships.empty()) {
        return nullptr;
    }
    p.settle.ship = ships.front()->id;
    return ships.front();
}

bool Engine::exploring(AiPlayer& p) {
    // The search waits while the settlement's ship sails or explores. A ship that stopped in the
    // rectangle of an island it may settle and has not explored explores it.
    Ship* ship = settle_ship(p);
    if (ship == nullptr) {
        return false;
    }
    if (ship_moving(*ship) || ship->explore_steps != 0) {
        return true;
    }
    for (std::size_t i = 0; i < m_world.islands().size(); ++i) {
        const WorldIsland& isl = m_world.islands()[i];
        if (ship->x >= isl.x && ship->x < isl.x + isl.map.width() && ship->y >= isl.y &&
            ship->y < isl.y + isl.map.height()) {
            if (isl.explored_by(p.player) || !island_allowed(p, static_cast<std::uint16_t>(i))) {
                return false;
            }
            ExploreOrder order;
            order.player = p.player;
            order.ship = ship->id;
            return execute(order).accepted;
        }
    }
    return false;
}

bool Engine::explore_next(AiPlayer& p) {
    // The nearest island the computer does not know (looking for a crop: also one it knows but has
    // not explored) that could take a city: the ship sails into its rectangle, which discovers it.
    Ship* ship = explorer(p);
    if (ship == nullptr) {
        return false;
    }
    const Settlement& s = p.settle;
    std::int64_t best = -1;
    Target goal;
    for (std::size_t i = 0; i < m_world.islands().size(); ++i) {
        const auto island = static_cast<std::uint16_t>(i);
        const bool unknown = !knows_island(p, island) || (s.crop != 0 && !knows_crops(p, island));
        // Its shape is on the map; who lives there only once it is discovered.
        if (!unknown || island_size(island) < (p.cities <= 1 ? 2U : 1U) ||
            (knows_island(p, island) && !island_allowed(p, island))) {
            continue;
        }
        rim_goal(*ship, island, best, goal);
    }
    if (best < 0) {
        return false;
    }
    ShipOrder order;
    order.player = p.player;
    order.ship = ship->id;
    order.target = goal;
    return execute(order).accepted;
}

bool Engine::rim_goal(const Ship& ship, std::uint16_t island, std::int64_t& best, Target& goal) const {
    const SeaMap& sea = m_sim.fleet().sea();
    const WorldIsland& isl = island_of(island);
    const auto x0 = static_cast<std::int32_t>(isl.x);
    const auto y0 = static_cast<std::int32_t>(isl.y);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    bool found = false;
    for (std::int32_t y = y0; y < y0 + h; ++y) {
        for (std::int32_t x = x0; x < x0 + w; ++x) {
            if ((x != x0 && x != x0 + w - 1 && y != y0 && y != y0 + h - 1) || !sea.sailable(x, y)) {
                continue;  // the rim of the rectangle, where the sea comes in
            }
            const std::int64_t dx = x - static_cast<std::int64_t>(ship.x);
            const std::int64_t dy = y - static_cast<std::int64_t>(ship.y);
            const std::int64_t d = dx * dx + dy * dy;
            if (best < 0 || d < best) {
                best = d;
                goal = Target::position(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                found = true;
            }
        }
    }
    return found;
}

// ---------------------------------------------------------------------------------------------
// Task 2: free land

void Engine::task_settle_area(AiPlayer& p) {
    pop(p);
    Settlement& s = p.settle;
    s.regions.clear();
    if (s.island < 0 || static_cast<std::size_t>(s.island) >= m_world.islands().size()) {
        return;
    }
    const AreaScan& scan = area_scan(static_cast<std::uint16_t>(s.island));
    s.regions = scan.regions;
    if (scan.total >= s.need) {
        push(p, kSettleSite, true);
    }
}

const Engine::AreaScan& Engine::area_scan(std::uint16_t island) {
    // The scan depends on the island's tiles only: kept while none of them changed (TB-13).
    if (m_area_scans.size() < m_world.islands().size()) {
        m_area_scans.resize(m_world.islands().size());
    }
    AreaScan& scan = m_area_scans[island];
    if (const std::uint64_t revision = island_revision(island); scan.revision != revision) {
        scan_area(island, scan);
        scan.revision = revision;
    }
    return scan;
}

void Engine::scan_area(std::uint16_t island, AreaScan& out) const {
    out.regions.clear();
    out.beach.clear();
    const IslandMap& map = island_of(island).map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    std::vector<std::uint8_t> ok(std::size_t(w) * std::size_t(h), 0);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const Tile t = tile(island, x, y);
            ok[std::size_t(y) * std::size_t(w) + std::size_t(x)] =
                t.valid() && t.city == MapCell::kNoCity && settle_ground(t.kind) ? 1 : 0;
            if (t.valid() && beach_kind(t.kind) && t.city == MapCell::kNoCity) {
                out.beach.emplace_back(x, y);
            }
        }
    }
    // Vertical runs of at least 5 free tiles, then horizontal runs of 5 among them.
    constexpr std::int32_t kRun = 5;
    std::vector<std::uint8_t> v(ok.size(), 0);
    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t start = 0;
        for (std::int32_t y = 0; y <= h; ++y) {
            const bool free = y < h && ok[std::size_t(y) * std::size_t(w) + std::size_t(x)] != 0;
            if (!free) {
                if (y - start >= kRun) {
                    for (std::int32_t k = start; k < y; ++k) {
                        v[std::size_t(k) * std::size_t(w) + std::size_t(x)] = 1;
                    }
                }
                start = y + 1;
            }
        }
    }
    std::vector<std::uint8_t> m(ok.size(), 0);
    for (std::int32_t y = 0; y < h; ++y) {
        std::int32_t start = 0;
        for (std::int32_t x = 0; x <= w; ++x) {
            const bool free = x < w && v[std::size_t(y) * std::size_t(w) + std::size_t(x)] != 0;
            if (!free) {
                if (x - start >= kRun) {
                    for (std::int32_t k = start; k < x; ++k) {
                        m[std::size_t(y) * std::size_t(w) + std::size_t(k)] = 1;
                    }
                }
                start = x + 1;
            }
        }
    }
    // Connected regions.
    std::int32_t& total = out.total;
    total = 0;
    std::vector<std::int32_t> todo;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            if (m[std::size_t(y) * std::size_t(w) + std::size_t(x)] != 1) {
                continue;
            }
            Region r{x, y, x, y, 0};
            todo.assign(1, y * w + x);
            m[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 2;
            while (!todo.empty()) {
                const std::int32_t i = todo.back();
                todo.pop_back();
                const std::int32_t cx = i % w;
                const std::int32_t cy = i / w;
                ++r.area;
                r.x0 = std::min(r.x0, cx);
                r.y0 = std::min(r.y0, cy);
                r.x1 = std::max(r.x1, cx);
                r.y1 = std::max(r.y1, cy);
                constexpr std::array<std::array<std::int32_t, 2>, 4> kN{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (const auto& d : kN) {
                    const std::int32_t nx = cx + d[0];
                    const std::int32_t ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    std::uint8_t& c = m[std::size_t(ny) * std::size_t(w) + std::size_t(nx)];
                    if (c == 1) {
                        c = 2;
                        todo.push_back(ny * w + nx);
                    }
                }
            }
            total += r.area;
            out.regions.push_back(r);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Task 3: the Kontor site

std::uint64_t Engine::island_revision(std::uint16_t island) const {
    const IslandMap& map = island_of(island).map;
    std::uint64_t revision = 0;
    for (std::size_t b = 0; b < std::size_t{map.blocks_x()} * map.blocks_y(); ++b) {
        revision = std::max(revision, map.block_revision(b));
    }
    return revision;
}

const Engine::SiteCounts& Engine::site_counts(std::uint16_t island) {
    // The island's tiles have not changed while no block has a newer revision: the counts of the
    // last test still hold for the next region and the next computer (TB-13).
    const IslandMap& map = island_of(island).map;
    const std::uint64_t revision = island_revision(island);
    if (m_site_counts.size() < m_world.islands().size()) {
        m_site_counts.resize(m_world.islands().size());
    }
    std::optional<SiteCounts>& cached = m_site_counts[island];
    if (cached && cached->revision == revision) {
        return *cached;
    }
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    cached.reset();
    cached.emplace(SiteCounts{revision,
                              AreaCount(w, h,
                                        [&](std::int32_t x, std::int32_t y) {
                                            const Tile t = tile(island, x, y);
                                            return t.valid() && t.city == MapCell::kNoCity &&
                                                   (t.ware == ware::kTree || t.ware == ware::kGrass ||
                                                    t.kind == kind::kRuin);
                                        }),
                              AreaCount(w, h,
                                        [&](std::int32_t x, std::int32_t y) {
                                            const Tile t = tile(island, x, y);
                                            return t.valid() && t.ware == ware::kTree;
                                        }),
                              AreaCount(w, h,
                                        [&](std::int32_t x, std::int32_t y) {
                                            const Tile t = tile(island, x, y);
                                            return t.valid() && t.ware == ware::kFish;
                                        }),
                              AreaCount(w, h, [&](std::int32_t x, std::int32_t y) {
                                  const Tile t = tile(island, x, y);
                                  return t.valid() && t.prod == prod::kFisher;
                              })});
    return *cached;
}

bool Engine::fisher_spot(const SiteCounts& counts, std::int32_t x, std::int32_t y) {
    // As search_fisher: more than 13 shoals within 6 tiles, no other fishing hut within 8.
    constexpr std::int32_t kFisherShoals = 13;
    return counts.fish.count(x - 6, y - 6, x + 6, y + 6) > kFisherShoals &&
           counts.fishers.count(x - 8, y - 8, x + 8, y + 8) == 0;
}

bool Engine::fisher_room(std::uint16_t island, const SiteCounts& counts, std::int32_t cx, std::int32_t cy) {
    // The hut wants the Kontor within 13 tiles and a road's end within 7; close to the Kontor,
    // where the first roads are, the city finds the site from the start.
    constexpr std::int32_t kNear = 6;
    for (const auto& [x, y] : area_scan(island).beach) {
        if (std::abs(x - cx) <= kNear && std::abs(y - cy) <= kNear && fisher_spot(counts, x, y)) {
            return true;
        }
    }
    return false;
}

void Engine::task_settle_site(AiPlayer& p) {
    pop(p);
    Settlement& s = p.settle;
    if (s.island < 0 || static_cast<std::size_t>(s.island) >= m_world.islands().size() || s.regions.empty()) {
        return;
    }
    const auto island = static_cast<std::uint16_t>(s.island);
    bool experienced = p.cities > 1;
    for (const City& c : m_world.cities()) {
        experienced = experienced || (c.owner == p.player && population(c) >= 1200);
    }
    // The largest region left; too small ends the test of this island.
    auto best_region = std::max_element(s.regions.begin(), s.regions.end(),
                                        [](const Region& a, const Region& b) { return a.area < b.area; });
    if (best_region->area <= s.need / 4) {
        return;
    }
    const Region region = *best_region;
    best_region->area = 1;

    const WorldIsland& isl = island_of(island);
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    const SiteCounts& counts = site_counts(island);
    const AreaCount& green = counts.green;
    const AreaCount& trees = counts.trees;
    // Open1602: on an island with fish, a city's Kontor stands where at least one fishing hut finds
    // its fish, so that the new town feeds itself from the start. Where no beach of the island has
    // fish, any site will do, as in the original.
    bool fish_first = false;
    if (!classic() && !p.pirates) {
        // Every beach tile of the island is near itself.
        for (const auto& [x, y] : area_scan(island).beach) {
            if (fisher_spot(counts, x, y)) {
                fish_first = true;
                break;
            }
        }
    }
    const assets::BuildingType* kontor = by_id(p.pirates ? id::kPirateKontor : id::kKontor1);
    if (kontor == nullptr) {
        return;
    }
    const bool vein = s.crop != 0 && s.crop == goal_crop(p);
    constexpr std::int32_t kVeinReach = 24;
    constexpr std::int32_t kVeinBonus = 1 << 16;
    const auto near_vein = [&](std::int32_t cx, std::int32_t cy) {
        for (const auto& d : isl.header.deposits) {
            if (d.ware == s.crop && d.amount > 0 && tile(island, d.x, d.y).city == MapCell::kNoCity &&
                std::max(std::abs(std::int32_t{d.x} - cx), std::abs(std::int32_t{d.y} - cy)) <= kVeinReach) {
                return true;
            }
        }
        return false;
    };
    // Candidates: Kontor sites on the beach around the region (the original walks straight beach
    // runs of at least four tiles; Open1602 lets the build rules decide what stands at the coast).
    struct Best {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint8_t rotation = 0;
        std::int32_t green = -1;
    } best;
    std::vector<std::uint8_t> tried(std::size_t(w) * std::size_t(h), 0);
    const std::int32_t min_depth = experienced ? 12 : 21;
    // The free beach tiles around the region, row by row as the island's scan keeps them.
    const std::int32_t bx0 = std::max(region.x0 - 1, 0);
    const std::int32_t by0 = std::max(region.y0 - 1, 0);
    const std::int32_t bx1 = std::min(region.x1 + 1, w - 1);
    const std::int32_t by1 = std::min(region.y1 + 1, h - 1);
    // Open1602: the window used to hold only Kontors whose top left corner lies up to two tiles left
    // of and above the beach tile; improved computers also try those beside it on every side, so
    // that their Kontors may face any side of the island.
    const std::int32_t before = classic() ? 2 : static_cast<std::int32_t>(std::max(kontor->size_x, kontor->size_y));
    const std::int32_t after = classic() ? 0 : 1;
    for (const auto& [x, y] : area_scan(island).beach) {
        if (x < bx0 || x > bx1 || y < by0 || y > by1) {
            continue;
        }
        for (std::int32_t oy = y - before; oy <= y + after; ++oy) {
            for (std::int32_t ox = x - before; ox <= x + after; ++ox) {
                if (!isl.map.contains(ox, oy) || tried[std::size_t(oy) * std::size_t(w) + std::size_t(ox)] != 0) {
                    continue;
                }
                tried[std::size_t(oy) * std::size_t(w) + std::size_t(ox)] = 1;
                BuildOrder order;
                order.player = p.player;
                order.island = island;
                order.items.push_back(BuildItem{kontor->id, ox, oy, 0, 0});
                const BuildPlan plan = plan_build(m_world, m_buildings, order);
                const PlannedBuild& item = plan.items.front();
                switch (item.refusal) {
                    case BuildRefusal::None:
                    case BuildRefusal::Money:
                    case BuildRefusal::Tools:
                    case BuildRefusal::Wood:
                    case BuildRefusal::Bricks:
                        break;
                    default:
                        continue;
                }
                std::uint32_t fw = 0;
                std::uint32_t fh = 0;
                rotated_size(*kontor, item.rotation, fw, fh);
                if (!open_sea_ahead(island, item.x, item.y, fw, fh, item.rotation)) {
                    continue;  // e.g. the shore of a lake inside the island
                }
                // Land behind the Kontor, away from the water side.
                const std::uint8_t inland = static_cast<std::uint8_t>((item.rotation + 2U) & 3U);
                const std::int32_t cx = item.x + static_cast<std::int32_t>(fw) / 2;
                const std::int32_t cy = item.y + static_cast<std::int32_t>(fh) / 2;
                std::int32_t sx = cx;
                std::int32_t sy = cy;
                switch (inland) {
                    case 0:
                        sx = item.x + static_cast<std::int32_t>(fw);
                        break;
                    case 1:
                        sy = item.y + static_cast<std::int32_t>(fh);
                        break;
                    case 2:
                        sx = item.x - 1;
                        break;
                    default:
                        sy = item.y - 1;
                        break;
                }
                const std::int32_t depth = free_run(island, MapCell::kNoCity, sx, sy, inland);
                if (depth <= min_depth) {
                    continue;
                }
                if (!experienced && trees.count(cx - 12, cy - 12, cx + 12, cy + 12) <= 60) {
                    continue;
                }
                if (fish_first && !fisher_room(island, counts, cx, cy)) {
                    continue;
                }
                std::int32_t g = green.count(cx - 32, cy - 32, cx + 32, cy + 32);
                // Open1602: a colony for ore or gold stands by a free vein, so that its market can
                // take the mountain in.
                if (vein && near_vein(cx, cy)) {
                    g += kVeinBonus;
                }
                if (g > best.green) {
                    best = {item.x, item.y, item.rotation, g};
                }
            }
        }
    }
    if (best.green < 0) {
        push(p, kSettleSite, true);  // the next region
        return;
    }
    s.site_x = best.x;
    s.site_y = best.y;
    s.site_rotation = best.rotation;
    Ship* ship = settle_ship(p);
    if (ship == nullptr) {
        std::vector<Ship*> ships = own_ships(p);
        if (ships.empty()) {
            return;
        }
        ship = ships.front();
        s.ship = ship->id;
    }
    free_colony_cargo(p, *ship, !p.pirates);
    ShipOrder order;
    order.player = p.player;
    order.ship = ship->id;
    order.target = water_near(island, best.x, best.y);
    execute(order);
    s.retries = kLandRetries;
    s.sailing = true;
}

void Engine::free_colony_cargo(AiPlayer& p, Ship& ship, bool computer) {
    // The AI's founding goods come from nowhere (the original sends them as raw cargo
    // messages). Wood and tools up to 50 t; computers food up to 15 t and bricks 20 t if neither
    // food nor bricks are aboard, pirates food 15 t and bricks up to 20 t.
    (void)p;
    const auto& m = m_buildings.materials();
    const std::uint8_t food = m_buildings.food_ware();
    std::size_t usable = 0;
    for (const CargoSlot& c : ship.cargo) {
        if (c.ware == 0 || c.ware == m.wood || c.ware == m.tools || c.ware == food || c.ware == m.bricks) {
            ++usable;
        }
    }
    if (usable < 2) {
        return;
    }
    const auto top_up = [&](std::uint8_t w, std::uint32_t target) {
        std::uint32_t have = ship.load(w);
        for (CargoSlot& c : ship.cargo) {
            if (have >= target) {
                return;
            }
            if (c.ware == w || c.ware == 0) {
                const std::uint32_t room = kHoldCapacity - std::min(c.amount, kHoldCapacity);
                const std::uint32_t add = std::min(room, target - have);
                if (add != 0) {
                    c.ware = w;
                    c.amount += add;
                    have += add;
                }
            }
        }
    };
    const bool food_or_bricks = ship.load(food) != 0 || ship.load(m.bricks) != 0;
    top_up(m.wood, 50 * kTon);
    top_up(m.tools, 50 * kTon);
    if (computer) {
        top_up(food, 15 * kTon);
        if (!food_or_bricks) {
            top_up(m.bricks, 20 * kTon);
        }
    } else {
        top_up(food, 15 * kTon);
        top_up(m.bricks, 20 * kTon);
    }
}

bool Engine::open_sea_ahead(std::uint16_t island, std::int32_t x, std::int32_t y, std::uint32_t width,
                            std::uint32_t height, std::uint8_t rotation) const {
    // From the beach of a candidate, every tile towards the water out to the edge of
    // the island's rectangle must be sea, so that a ship can reach the site from outside. Without
    // it the computer can pick the shore of a lake inside the island, which its ship never
    // reaches. Open1602 starts at the Kontor's water side and allows a few tiles of beach or surf
    // before the water (the original skips two or three tiles).
    constexpr std::int32_t kShore = 3;
    constexpr std::array<std::array<std::int32_t, 2>, 4> kSeaward{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    const WorldIsland& isl = island_of(island);
    const SeaMap& sea = m_sim.fleet().sea();
    const auto& d = kSeaward[rotation & 3U];
    const auto w = static_cast<std::int32_t>(isl.map.width());
    const auto h = static_cast<std::int32_t>(isl.map.height());
    // The middle of the Kontor's water side, one tile out.
    std::int32_t tx = d[0] > 0   ? x + static_cast<std::int32_t>(width)
                      : d[0] < 0 ? x - 1
                                 : x + static_cast<std::int32_t>(width) / 2;
    std::int32_t ty = d[1] > 0   ? y + static_cast<std::int32_t>(height)
                      : d[1] < 0 ? y - 1
                                 : y + static_cast<std::int32_t>(height) / 2;
    bool water = false;
    std::int32_t shore = 0;
    for (; tx >= 0 && ty >= 0 && tx < w && ty < h; tx += d[0], ty += d[1]) {
        if (sea.sailable(static_cast<std::int32_t>(isl.x) + tx, static_cast<std::int32_t>(isl.y) + ty)) {
            water = true;
        } else if (water || ++shore > kShore) {
            return false;
        }
    }
    return water;
}

Target Engine::water_near(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    // The original sails to the beach tile; Open1602 picks the open water nearest to it, so the
    // course can be planned.
    const WorldIsland& isl = island_of(island);
    const std::int32_t wx = static_cast<std::int32_t>(isl.x) + x;
    const std::int32_t wy = static_cast<std::int32_t>(isl.y) + y;
    const SeaMap& sea = m_sim.fleet().sea();
    for (std::int32_t r = 0; r <= 10; ++r) {
        std::int32_t best = -1;
        std::int32_t bx = 0;
        std::int32_t by = 0;
        for (std::int32_t dy = -r; dy <= r; ++dy) {
            for (std::int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r || !sea.sailable(wx + dx, wy + dy)) {
                    continue;
                }
                const std::int32_t d = dx * dx + dy * dy;
                if (best < 0 || d < best) {
                    best = d;
                    bx = wx + dx;
                    by = wy + dy;
                }
            }
        }
        if (best >= 0) {
            return Target::position(static_cast<std::uint32_t>(bx), static_cast<std::uint32_t>(by));
        }
    }
    return Target::position(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy));
}

// ---------------------------------------------------------------------------------------------
// Task 4: landing and founding

void Engine::task_settle_land(AiPlayer& p) {
    Settlement& s = p.settle;
    Ship* ship = settle_ship(p);
    if (ship == nullptr || s.island < 0 || static_cast<std::size_t>(s.island) >= m_world.islands().size()) {
        pop(p);
        end_settlement(p);
        return;
    }
    pop(p);
    if (ship_moving(*ship)) {
        s.sailing = true;  // the root task comes back when the ship has stopped
        return;
    }
    s.sailing = false;
    const auto island = static_cast<std::uint16_t>(s.island);
    const WorldIsland& isl = island_of(island);
    const std::uint32_t free = free_land(island, MapCell::kNoCity, nullptr);
    if (!p.pirates && p.cities == 0 && free <= 600) {
        return;  // the search goes on with the next island
    }
    // Other cities on the island must leave room.
    for (const City& c : m_world.cities()) {
        if (c.island != island || c.owner == p.player) {
            continue;
        }
        const Player* o = m_world.player(c.owner);
        if (o != nullptr && m_world.behaviour(*o) == FactionBehaviour::Natives && island_size(island) >= 4) {
            continue;
        }
        // The original's computers keep off the largest islands where another computer has a
        // city, though not where a human has one; improved, a computer's city counts as a human's.
        const bool human = o != nullptr && o->role == PlayerRole::Human;
        if ((classic() && !human && island_size(island) > 3) || own_city_count(c.owner) < 2 || free < 300 ||
            has_population(c, 0, 50)) {
            return;
        }
    }
    const std::int32_t tx = static_cast<std::int32_t>(isl.x) + s.site_x;
    const std::int32_t ty = static_cast<std::int32_t>(isl.y) + s.site_y;
    if (!in_circle(static_cast<std::int32_t>(ship->x) - tx, static_cast<std::int32_t>(ship->y) - ty, kLandRange)) {
        if (s.retries > 0) {
            --s.retries;
            ShipOrder order;
            order.player = p.player;
            order.ship = ship->id;
            order.target = water_near(island, s.site_x, s.site_y);
            execute(order);
            s.sailing = true;
            return;
        }
        site_unreachable(p);
        return;
    }
    Player& pl = player(p);
    if (gifts() && pl.money < cheats().founding_below) {
        pl.money =
            money_plus(pl.money, std::int64_t{cheats().founding_money});  // the original's founding money (cheat)
    }
    BuildOrder order;
    order.player = p.player;
    order.island = island;
    order.ship = ship->id;
    order.items.push_back(
        BuildItem{p.pirates ? id::kPirateKontor : id::kKontor1, s.site_x, s.site_y, s.site_rotation, 0});
    const bool contested = !island_allowed(p, island);
    const CommandResult result = execute(order);
    log(p, "found a city on island %u at %d,%d%s: %s", island, s.site_x, s.site_y, contested ? " (contested)" : "",
        result.build && result.build->placed != 0
            ? "built"
            : (result.build ? refusal_name(result.build->items.front().refusal) : "?"));
    if (!result.build || result.build->placed == 0) {
        push(p, kSettleSite, true);
        return;
    }
    const PlannedBuild& built = result.build->items.front();
    const Tile t = tile(island, built.x, built.y);
    City* c = t.city != MapCell::kNoCity ? city(island, t.city) : nullptr;
    if (c == nullptr || c->owner != p.player) {
        end_settlement(p);
        return;
    }
    p.has_city = true;
    p.island = island;
    p.city = c->index;
    p.has_plan = true;
    ++p.cities;
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        if (&m_world.cities()[i] == c) {
            p.city_cursor = static_cast<std::uint32_t>(i);
        }
    }
    CityPlan& r = plan_of(p, island, c->index);
    r = CityPlan{};
    r.island = island;
    r.city = c->index;
    r.stage_ms = now_ms();
    r.land = free_land(island, c->index, &r.land_percent);
    unload_at(*c, *ship, false, false);
    if (gifts()) {
        add_goods(*c, m_buildings.materials().bricks,
                  cheats().founding_bricks * kStockUnitsPerTon);  // free bricks after founding
    }
    s.failed = false;
    p.budget = 14;
    if (!p.home_valid) {
        p.home_valid = true;
        p.home_island = island;
        p.home_city = c->index;
    }
    push(p, kSurvey, true);
    push(p, kLayout, true);
    end_settlement(p);
}

void Engine::site_unreachable(AiPlayer& p) {
    // The original ends the settlement here and starts the search afresh, which picks the same
    // site again (the search has no randomness): a computer whose site cannot be reached tries it
    // forever. Open1602 goes on with the next island of the same search instead; the ship stays.
    if (classic()) {
        end_settlement(p);
        return;
    }
    Settlement& s = p.settle;
    log(p, "the ship cannot reach the site on island %d, the search goes on", s.island);
    s.retries = 0;
    s.sailing = false;
    s.regions.clear();
    s.island = -1;
}

void Engine::end_settlement(AiPlayer& p) {
    Settlement& s = p.settle;
    s.last_ms = now_ms();
    s.previous_crop = s.crop;
    s.crop = 0;
    s.retries = 0;
    s.ship = 0;
    s.active = false;
    s.sailing = false;
    s.regions.clear();
    s.island = -1;
}

void Engine::unload_at(City& c, Ship& ship, bool keep_basics, bool destroy) {
    // Every hold into the warehouse as far as there is room; with `destroy` the rest
    // is thrown away (S-097: the original does this when it brings goods home).
    const auto& m = m_buildings.materials();
    for (CargoSlot& slot : ship.cargo) {
        if (!assets::is_good_number(slot.ware)) {
            continue;
        }
        if (keep_basics && (slot.ware == m.wood || slot.ware == m.tools || slot.ware == m.bricks ||
                            slot.ware == m_buildings.food_ware())) {
            continue;
        }
        c.ensure_ware(slot.ware);
        const std::uint32_t room = c.capacity > c.goods[slot.ware] ? c.capacity - c.goods[slot.ware] : 0;
        const std::uint32_t moved = std::min(room, slot.amount);
        c.goods[slot.ware] += moved;
        c.book(slot.ware, GoodsFlow::ShippedIn, moved);
        slot.amount -= moved;
        if (destroy) {
            slot.amount = 0;
        }
        if (slot.amount == 0) {
            slot = CargoSlot{};
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Later colonies

bool Engine::crop_available(const AiPlayer& p, std::uint8_t crop) const {
    // Some city of any computer player lies on an island that grows the crop fully
    // (ore: a big or small vein; gold: a big one). Shared by all the original's computers; an
    // improved computer knows only its own cities.
    for (const City& c : m_world.cities()) {
        const Player* o = m_world.player(c.owner);
        if (o == nullptr || o->role != PlayerRole::Computer || (rivals() && c.owner != p.player)) {
            continue;
        }
        const std::uint32_t f = fertility(c.island, crop);
        if (crop == ware::kOre ? f >= 64 : f == 128) {
            return true;
        }
    }
    return false;
}

std::vector<std::uint8_t> Engine::colony_candidates(AiPlayer& p) {
    // The original's order of motives; choose_colony_crop takes the first. G4b weighs them all.
    std::vector<std::uint8_t> out;
    const auto add = [&](std::uint8_t crop) {
        if (std::find(out.begin(), out.end(), crop) == out.end()) {
            out.push_back(crop);
        }
    };
    const std::uint8_t skip = p.settle.previous_crop;
    // Open1602: an improved computer looks for what its goal needs first: gold for its
    // aristocrats, ore to arm.
    if (const std::uint8_t g = goal_crop(p); g != 0 && g != skip) {
        add(g);
    }
    // Open1602: and then a crop for what its home city's people use, make too little of and run
    // out of soonest, alcohol from wine or sugar: settlers without it fell back and the city into
    // debt, while the original went for spices and tobacco first.
    if (const City* home = rivals() ? home_city(p) : nullptr) {
        struct Need {
            std::uint8_t good;
            std::array<std::uint8_t, 2> crops;
        };
        std::array<Need, 4> needs{{{ware::kAlcohol, {ware::kWineField, ware::kSugarField}},
                                   {ware::kTobaccoGoods, {ware::kTobaccoField, 0}},
                                   {ware::kSpices, {ware::kSpiceField, 0}},
                                   {ware::kCocoa, {ware::kCocoaField, 0}}}};
        // What the houses will use once they rise counts too (demand_ahead): a colony founded for
        // the next class's tobacco grew sugar instead, and the city's tavern never opened.
        const auto lasts = [&](std::uint8_t w) -> std::uint64_t {
            const std::uint32_t used = demand_ahead(*home, w);
            if (used == 0 || supply(*home, w) >= used) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return std::uint64_t{home->stock(w)} * 256 / used;
        };
        std::ranges::stable_sort(needs, {}, [&](const Need& n) { return lasts(n.good); });
        for (const Need& n : needs) {
            if (lasts(n.good) == std::numeric_limits<std::uint64_t>::max() ||
                std::ranges::any_of(n.crops, [&](std::uint8_t crop) { return crop != 0 && crop_available(p, crop); })) {
                continue;
            }
            for (const std::uint8_t crop : n.crops) {
                if (crop != 0 && crop != skip) {
                    add(crop);
                }
            }
        }
    }
    for (const std::uint8_t crop : {ware::kSpiceField, ware::kTobaccoField, ware::kWineField, ware::kCocoaField,
                                    ware::kCottonField, ware::kSugarField}) {
        if (crop != skip && !crop_available(p, crop)) {
            add(crop);
        }
    }
    struct Pair {
        std::uint8_t slot;
        std::uint8_t crop;
        bool needs_score;
    };
    constexpr std::array<Pair, 6> kPairs{{{kSlotSpices, ware::kSpiceField, false},
                                          {kSlotTobaccoGoods, ware::kTobaccoField, false},
                                          {kSlotWine, ware::kWineField, false},
                                          {kSlotCocoa, ware::kCocoaField, false},
                                          {kSlotCotton, ware::kCottonField, true},
                                          {kSlotSugar, ware::kSugarField, true}}};
    for (const Pair& pr : kPairs) {
        if (pr.needs_score && p.score <= 2) {
            continue;
        }
        if (pr.crop != skip && slot_present(p, pr.slot) == 0) {
            add(pr.crop);
        }
    }
    return out;
}

std::uint8_t Engine::choose_colony_crop(AiPlayer& p) {
    const std::vector<std::uint8_t> crops = colony_candidates(p);
    return crops.empty() ? 0 : crops.front();
}

Engine::Chain Engine::colony_chain(const AiPlayer& p, std::uint8_t crop) const {
    // The fields its colony order plants by the highest stage reached (colony_luxury), and the
    // workshop that works their harvest where it takes one; a mine for ore or gold.
    std::uint32_t fields = 8;
    switch (p.max_stage) {
        case stage::kSettlers:
        case stage::kMain:
        case stage::kMain2:
        case stage::kCitizens2:
            fields = 4;
            break;
        case stage::kCitizens:
        case stage::kMerchants:
        case stage::kMerchants2:
            fields = 6;
            break;
        case stage::kAristocrats:
        case stage::kAristocrats2:
            fields = 10;
            break;
        default:
            break;
    }
    const auto mine = [&](std::uint8_t slot) {
        const auto d = need_def(slots(), slot, m_world.balance().computer_producers);
        return d ? by_id(d->fixed) : nullptr;
    };
    switch (crop) {
        case ware::kSpiceField:
            return {{{producer(ware::kSpices, crop), fields}}};
        case ware::kWineField:
            return {{{producer(ware::kAlcohol, crop), fields}}};
        case ware::kCocoaField:
            return {{{producer(ware::kCocoa, crop), fields}}};
        case ware::kTobaccoField:
            return {{{producer(ware::kTobacco, crop), fields}, {producer(ware::kTobaccoGoods, ware::kTobacco), 1}}};
        case ware::kSugarField:
            return {{{producer(ware::kSugar, crop), fields}, {producer(ware::kAlcohol, ware::kSugar), 1}}};
        case ware::kCottonField:
            return {{{producer(ware::kWool, crop), fields}, {producer(ware::kCloth, ware::kWool), 1}}};
        case ware::kOre:
            return {{{mine(kSlotIronMine), 1}}};
        case ware::kGold:
            return {{{mine(kSlotGoldMine), 1}}};
        default:
            return {};
    }
}

std::int64_t Engine::chain_upkeep(const AiPlayer& p, std::uint8_t crop) const {
    std::int64_t sum = 0;
    for (const auto& [b, n] : colony_chain(p, crop).parts) {
        sum += b != nullptr ? std::int64_t{n} * b->running_cost : 0;
    }
    return sum;
}

std::int64_t Engine::colony_rank(const AiPlayer& p, std::uint8_t crop) {
    // Ranks the candidates of one computer at one time, so what all of them cost alike (the colony's
    // gold, its Kontor, the gold the home city goes without until the colony delivers) is left out.
    const City* home = p.home_valid ? city(p.home_island, p.home_city) : nullptr;
    if (home == nullptr || home->owner != p.player) {
        return 0;
    }
    const Balance& at = m_world.balance();
    const auto& classes = m_buildings.resident_classes();
    const std::uint8_t food = m_buildings.food_ware();
    std::uint8_t good = 0;  // the inhabitants' good the crop brings
    switch (crop) {
        case ware::kWineField:
        case ware::kSugarField:
            good = ware::kAlcohol;
            break;
        case ware::kTobaccoField:
            good = ware::kTobaccoGoods;
            break;
        case ware::kSpiceField:
            good = ware::kSpices;
            break;
        case ware::kCocoaField:
            good = ware::kCocoa;
            break;
        case ware::kCottonField:
            good = ware::kCloth;
            break;
        case ware::kGold:
            good = ware::kJewellery;
            break;
        default:
            break;
    }
    // The taxes a minute of a full house of class k at the home city's rate.
    const auto house_tax = [&](std::size_t k) -> std::int64_t {
        const std::int64_t rate = k < home->tax_rate.size() ? home->tax_rate[k] : kFullSupply;
        return static_cast<std::int64_t>(std::uint64_t{classes[k].max_per_house} * classes[k].tax * 6 / 32) * rate /
               kFullSupply;
    };
    // The city the colony becomes, on the nearest island it knows to grow the crop (any island it
    // knows for none): as many houses as its free land holds beside the fields, as densely as the
    // home city's houses stand on its land, full of the highest class whose goods it will have (the
    // crop's, food, and what the home city holds and can send). A city founded as a colony without
    // houses (becomes_colony) has none.
    std::int64_t city_value = 0;
    const CityPlan* own = nullptr;
    for (const CityPlan& r : p.plans) {
        own = r.island == home->island && r.city == home->index ? &r : own;
    }
    const auto [home_x, home_y] = city_point(*home);
    std::optional<std::uint16_t> island;
    std::uint32_t nearest = 0;
    for (std::size_t i = 0; i < m_world.islands().size(); ++i) {
        const auto at_island = static_cast<std::uint16_t>(i);
        if (crop != 0 && crop != kAnyCrop) {
            const std::uint32_t f = fertility(at_island, crop);
            if ((crop == ware::kOre || crop == ware::kGold) ? f < 64 : f != 128) {
                continue;
            }
        }
        if (!knows_crops(p, at_island) || !island_allowed(p, at_island) ||
            std::ranges::any_of(m_world.cities(),
                                [&](const City& c) { return c.island == at_island && c.owner == p.player; })) {
            continue;
        }
        const WorldIsland& w = m_world.islands()[i];
        const std::uint32_t sail = sail_seconds(
            p, {home_x, home_y},
            {static_cast<std::int32_t>(w.x + w.map.width() / 2), static_cast<std::int32_t>(w.y + w.map.height() / 2)});
        if (!island || sail < nearest) {
            island = at_island;
            nearest = sail;
        }
    }
    const Chain chain = colony_chain(p, crop);
    if (island && own != nullptr && own->land != 0) {
        std::uint32_t land = free_land(*island, MapCell::kNoCity, nullptr);
        const std::uint32_t fields = chain.parts.empty() ? 0 : chain.parts.front().second;
        land -= std::min(land, fields * at.ai_colony_luxury_tiles);
        if (!becomes_colony(p, nullptr, land, 0)) {
            const std::uint64_t sites = std::uint64_t{land} * own->slots[kSlotHouses].count / own->land;
            std::size_t top = 0;
            for (std::size_t k = 0; k < classes.size(); ++k) {
                const bool has_all = std::ranges::all_of(classes[k].needs, [&](const auto& n) {
                    return n.ware == food || n.ware == good || home->stock(n.ware) != 0;
                });
                if (!has_all) {
                    break;
                }
                top = k;
            }
            city_value = static_cast<std::int64_t>(sites) * house_tax(top);
        }
    }
    // The houses of the home city that rise with the good: those of the class below the lowest that
    // asks for it, as far as that class's other goods are in store, at the fuller houses of the class
    // above.
    std::int64_t rise = 0;
    for (std::size_t k = 1; good != 0 && k < classes.size() && k - 1 < home->inhabitants.size(); ++k) {
        if (std::ranges::none_of(classes[k].needs, [&](const auto& n) { return n.ware == good; })) {
            continue;
        }
        std::uint64_t others = 0;
        std::uint64_t held = 0;
        for (const assets::ResidentClass::Need& n : classes[k].needs) {
            if (n.ware != good && n.ware != food) {
                ++others;
                held += home->stock(n.ware) != 0 ? 1U : 0U;
            }
        }
        const std::uint32_t per_house = std::max<std::uint32_t>(classes[k - 1].max_per_house, 1);
        const std::uint64_t houses = home->inhabitants[k - 1] / per_house;
        const std::uint64_t rising = others != 0 ? houses * held / others : houses;
        rise = static_cast<std::int64_t>(rising) * (house_tax(k) - house_tax(k - 1));
        break;
    }
    return city_value - chain_upkeep(p, crop) + rise;
}

void Engine::colony_plan(AiPlayer& p, bool asked) {
    // Open1602 (G4b): one decision a step, whatever order or stage asks for the colony; asked in
    // several modes a step, the colony was wanted for one crop and not wanted by turns.
    if (m_colony_plan_ms.size() <= p.player) {
        m_colony_plan_ms.resize(std::size_t{p.player} + 1, 0);
    }
    Settlement& s = p.settle;
    if (!asked && s.plan_crop == 0) {
        return;
    }
    const std::uint64_t step = std::max<std::uint64_t>(now_ms(), 1);
    if (m_colony_plan_ms[p.player] == step) {
        return;
    }
    m_colony_plan_ms[p.player] = step;
    if (s.plan_ship != 0) {
        const Ship* kept = m_world.ship(s.plan_ship);
        if (kept == nullptr || kept->owner != p.player || kept->sinking) {
            s.plan_ship = 0;
        }
    }
    // Under way the plan stands: the ship is called home to load or sails.
    if (s.active || s.ship != 0) {
        return;
    }
    const auto clear = [&] {
        if (s.plan_crop != 0) {
            log(p, "colony plan dropped (crop %u)", unsigned{s.plan_crop});
        }
        s.plan_crop = 0;
        s.plan_ms = 0;
        s.plan_ship = 0;
    };
    // The candidates are the original's motives in its order, each as its budget weighs it
    // (colony_commitment, its own gold set aside by reserved() left out for the plan's).
    std::vector<std::uint8_t> crops = colony_candidates(p);
    if (crops.empty()) {
        crops.push_back(kAnyCrop);  // the original then founds on any island
    }
    Budget b = gold_budget(p);
    if (s.plan_crop != 0) {
        b.reserved -= colony_money(p);
    }
    std::optional<std::uint8_t> best;
    std::int64_t best_rank = 0;
    bool plan_holds = false;
    for (const std::uint8_t crop : crops) {
        const std::uint8_t as = crop == kAnyCrop ? std::uint8_t{0} : crop;
        if (b.refusal(colony_commitment(p, as)) != Refusal::None) {
            continue;
        }
        plan_holds = plan_holds || crop == s.plan_crop;
        const std::int64_t rank = colony_rank(p, crop);
        if (!best || rank > best_rank) {
            best = crop;
            best_rank = rank;
        }
    }
    if (!best || (!asked && !plan_holds)) {
        clear();
        return;
    }
    if (!asked) {
        return;  // the plan holds; a new one is made when an order asks
    }
    if (*best != s.plan_crop) {
        // Another colony takes the plan's place only while no ship is kept for it.
        if (plan_holds && s.plan_ship != 0) {
            return;
        }
        log(p, "colony plan: crop %u rank %lld (first %u rank %lld)", unsigned{*best},
            static_cast<long long>(best_rank), unsigned{crops.front()},
            static_cast<long long>(colony_rank(p, crops.front())));
        if (!plan_holds) {
            s.plan_ship = 0;
        }
        s.plan_crop = *best;
        s.plan_ms = step;
    }
    // A ship is kept for it at once, so that the supply runs do not take it.
    if (s.plan_ship == 0) {
        if (const std::uint32_t ship = colony_ship_candidate(p); ship != 0) {
            s.plan_ship = ship;
            log(p, "colony plan: ship %u kept", ship);
        }
    }
}

namespace {}  // namespace

std::int64_t Engine::colony_deficit(const AiPlayer& p) const {
    // What one of the computer's cities beyond the home city costs a minute beyond its taxes, on
    // average over those that cost more than they pay; 0 without such a city. Its uninhabited
    // colonies too: plantations and mines pay no taxes and cost 200 to 1400 gold a minute each,
    // and five of them founded on a margin of 500 a minute took a computer into a debt of 79,000.
    // A young colony (young_colony) is left out: with its Kontor alone it cost little, and each
    // founding made the next one look cheaper.
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    std::int64_t sum = 0;
    std::int64_t n = 0;
    for (std::size_t i = 0; i < m_world.cities().size(); ++i) {
        const City& c = m_world.cities()[i];
        if (c.owner != p.player || is_home(p, c) || (rivals() && young_colony(c))) {
            continue;
        }
        if (const std::int64_t d = std::int64_t{costs[i]} - tax_income(c, m_buildings); d > 0) {
            sum += d;
            ++n;
        }
    }
    return n != 0 ? sum / n : 0;
}

std::uint8_t Engine::goal_crop(const AiPlayer& p) const {
    // What the goal of an improved computer asks a colony for: gold or ore it has none of, once
    // its budget carries a colony that brings nothing within the horizon (a mine pays off late).
    if (!rivals() || p.pirates || !can_commit(p, colony_commitment(p, 0), Spend::GoalColony)) {
        return 0;
    }
    if (p.strategy.goal == goal::kGold && !crop_available(p, ware::kGold)) {
        return ware::kGold;
    }
    if ((p.strategy.goal == goal::kMilitary && !crop_available(p, ware::kOre)) || ore_running_out(p)) {
        return ware::kOre;
    }
    return 0;
}

bool Engine::ore_running_out(const AiPlayer& p) const {
    // Open1602: an improved computer that mines iron ore sees its veins run low before they are
    // gone, as a player does: the ore left in the veins of its own islands (a big vein that never
    // runs out counts as endless) below kOreLowTons, while it has an iron mine, or no vein at all
    // once its city is past the settlers. It then looks for a
    // colony with a vein of its own (goal_crop), and buys ore and iron in the meantime.
    if (!rivals() || p.pirates) {
        return false;
    }
    bool mines = false;
    for (const CityPlan& r : p.plans) {
        mines = mines || r.slots[kSlotIronMine].count != 0;
    }
    if (!mines) {
        // Without a mine of its own: a city past the settlers with no vein on its islands needs one
        // for its tools and iron.
        return p.max_stage >= stage::kCitizens && !crop_available(p, ware::kOre);
    }
    const bool game_endless = (m_world.game_settings().flags & World::GameSettings::kEndlessVeins) != 0;
    const bool endless = m_sim.rules().endless_veins(game_endless);
    std::vector<std::uint16_t> islands;
    std::uint64_t left = 0;
    for (const City& c : m_world.cities()) {
        if (c.owner != p.player || std::find(islands.begin(), islands.end(), c.island) != islands.end()) {
            continue;
        }
        islands.push_back(c.island);
        for (const auto& d : island_of(c.island).header.deposits) {
            if (d.ware != ware::kOre || d.amount == 0) {
                continue;
            }
            if (endless && d.kind != 1 && player(p).unlocked(infra::kLevel3F)) {
                return false;  // a big vein its deep mines dig for good
            }
            left += d.amount;
        }
    }
    return left < kOreLowTons * kTon;
}

std::uint32_t Engine::colony_ship_candidate(AiPlayer& p) {
    // An idle or patrolling ship without a mission, the fewest cannons.
    if (Ship* s = settle_ship(p)) {
        return s->id;
    }
    // G4b: the ship kept for the planned colony.
    if (p.settle.plan_ship != 0) {
        if (const Ship* kept = m_world.ship(p.settle.plan_ship);
            kept != nullptr && kept->owner == p.player && !kept->sinking && !kept->repairing) {
            return kept->id;
        }
    }
    // Open1602: an improved computer does not send its only ship to found a city while one of its
    // cities runs out of food (food_runs_out): the founding took the ship for twenty minutes, and a
    // colony of 450 people starved meanwhile.
    if (rivals() && !p.pirates && own_ships(p).size() < 2 && food_runs_out(p)) {
        return 0;
    }
    std::uint32_t best = 0;
    std::uint32_t best_cannons = 25;
    for (Ship* s : own_ships(p)) {
        const ShipNote& n = note(p, s->id);
        const bool idle = !ship_moving(*s) && !s->route.active;
        if (s->repairing || s->held || n.mission != 0 || !(idle || n.patrol) || s->cannons >= best_cannons) {
            continue;
        }
        best = s->id;
        best_cannons = s->cannons;
    }
    return best;
}

std::uint32_t Engine::city_limit() const {
    if (m_sim.rules().computer_cities == ComputerCities::Original) {
        return kOriginalCityLimit;
    }
    std::uint32_t islands = 0;
    for (std::size_t i = 0; i < m_world.islands().size(); ++i) {
        islands += island_size(static_cast<std::uint16_t>(i)) >= 1 ? 1U : 0U;
    }
    std::uint32_t players = 0;
    for (const Player& pl : m_world.players()) {
        players += (pl.role == PlayerRole::Human || pl.role == PlayerRole::Computer) && pl.active() ? 1U : 0U;
    }
    const std::uint32_t most = kCityLimitMost[std::min<std::size_t>(difficulty(), kCityLimitMost.size() - 1)];
    return std::clamp(islands / std::max(players, 1U), kOriginalCityLimit, most);
}

bool Engine::wants_room(const AiPlayer& p) const {
    // Not in the original: its computers found cities only for crops. Scaled, one whose towns
    // reached the citizens and earn well settles on, a city at a time with a pause between.
    if (m_sim.rules().computer_cities != ComputerCities::Scaled || p.pirates || tutorial()) {
        return false;
    }
    if (p.cities == 0 || p.max_stage < stage::kCitizens || p.settle.active) {
        return false;
    }
    if (now_ms() - p.settle.last_ms < kRoomPauseMs || p.cities >= city_limit()) {
        return false;
    }
    if (p.settle.ship != 0) {
        return true;  // the ship is being loaded: go on whatever the gold does meanwhile
    }
    // An improved rival settles for room once its budget carries a colony that opens no class.
    if (rivals()) {
        return can_commit(p, colony_commitment(p, 0), Spend::RoomColony);
    }
    const auto n = static_cast<std::int32_t>(p.cities);
    return player(p).money >= kRoomMoney && net_income(p) >= kRoomIncome * n;
}

bool Engine::want_colony(AiPlayer& p, std::uint8_t mode, bool force, bool food) {
    // A scenario can forbid further cities.
    if (player(p).scenario.has(scenario_flag::kNoColonies)) {
        return false;
    }
    if (now_ms() - p.settle.last_ms < kColonyCooldownMs) {
        return false;
    }
    // Open1602: an improved computer founds a colony only with the gold to build it up; founded
    // with its last gold, the colony stood empty and was given up. An improved rival founds one
    // from its budget (gold_budget): the colony's gold now, what one of its colonies costs beyond
    // its taxes (colony_deficit) from now on and what the crop's good opens in the home city
    // (gate_value) once the colony delivers. Weighed before the ship is called home only: from
    // then on the budget keeps the colony's gold (reserved), and a ship called for a founding
    // that was then refused lay idle at the Kontor. Three colonies in two hours took a computer
    // from 24,000 gold into a debt it never left; a city that waited for a good of its next class
    // sat on 45,000 gold for hours.
    // G4b: an improved rival weighs it in its one colony decision (colony_plan), below.
    const bool planned = rivals() && !p.pirates;
    if (planned && p.settle.ship == 0 && !p.settle.active) {
    } else if (!classic() && player(p).money < colony_money(p)) {
        return false;
    }
    const std::uint32_t n = p.cities;
    // The original's computers found a further colony only while their cities earn more than they
    // cost, or with 15,001 gold in hand.
    if (classic() && n >= 2 && net_income(p) <= static_cast<std::int32_t>(50 * n) && player(p).money < 15001) {
        return false;
    }
    if (p.settle.active && !p.settle.sailing && !classic() && !p.settle.failed) {
        // Open1602: the loaded ship looks for its island at every look of the city's order, not
        // only when the whole order is through (root); it lay loaded at the Kontor for minutes.
        const std::size_t before = p.stack.size();
        pick_island(p);
        return p.stack.size() > before;
    }
    if (p.settle.active || n >= city_limit()) {
        return false;
    }
    // Open1602: an improved computer founds one colony after the other: the next only once the
    // last one's costs show in its income (kColonySpacingMs). Three founded within forty minutes
    // spent the gold of a lone home city, whose income then could not carry them.
    if (rivals() && n >= 2 && now_ms() - p.settle.last_ms < kColonySpacingMs) {
        return false;
    }
    std::vector<std::uint8_t> wanted{ware::kWineField};
    if (mode >= 2) {
        wanted.push_back(ware::kTobaccoField);
        wanted.push_back(ware::kSpiceField);
    }
    if (mode >= 3) {
        wanted.push_back(ware::kCocoaField);
        wanted.push_back(ware::kSugarField);
    }
    if (mode >= 4) {
        wanted.push_back(ware::kGold);
    }
    if (const std::uint8_t g = goal_crop(p); g != 0) {
        wanted.push_back(g);
    }
    bool all = true;
    for (const std::uint8_t crop : wanted) {
        all = all && crop_available(p, crop);
    }
    if (all && !force && !wants_room(p)) {
        return false;
    }
    // G4b: the order asks; the plan decides which colony, if any.
    if (planned) {
        colony_plan(p, true);
        if (p.settle.plan_crop == 0 && p.settle.ship == 0) {
            return false;
        }
    }
    const std::uint32_t ship = colony_ship_candidate(p);
    if (ship == 0) {
        p.settle.ship = 0;
        p.settle.load_ms = 0;
        return false;
    }
    if (!classic() && p.settle.ship == ship && !colony_load_in_stock(p, p.settle.load_food) && !colony_loading(p)) {
        // Called from another city's order, which does not hold the load: the ship goes back to work.
        p.settle.ship = 0;
        p.settle.load_ms = 0;
        return false;
    }
    if (p.settle.ship != ship) {
        // Open1602: an improved computer calls the ship home only once the city holds the load; the
        // original called it at once, and a city short of tools or bricks kept it idle at the Kontor
        // for good, away from every other job.
        if (!classic() && !colony_load_in_stock(p, food)) {
            return false;
        }
        log(p, "colony (mode %u): ship %u goes home to load", mode, ship);
        // First call: the ship goes to the home city to load.
        p.settle.ship = ship;
        p.settle.load_ms = classic() ? 0 : std::max<std::uint64_t>(now_ms(), 1);
        p.settle.load_food = food;
        Ship* s = m_world.ship(ship);
        if (const City* home = home_city(p); home != nullptr && s != nullptr) {
            send_to_city(p, *s, *home);
        }
        return true;
    }
    const std::uint8_t plan = p.settle.plan_crop;
    return prepare_colony_ship(
        p, planned && plan != 0 ? (plan == kAnyCrop ? std::uint8_t{0} : plan) : choose_colony_crop(p), food);
}

bool Engine::colony_loading(AiPlayer& p) {
    // Whether the colony ship already holds part of its load.
    const Ship* ship = settle_ship(p);
    if (ship == nullptr) {
        return false;
    }
    const auto& m = m_buildings.materials();
    return ship->load(m.wood) != 0 || ship->load(m.tools) != 0 || ship->load(m.bricks) != 0;
}

std::uint32_t Engine::colony_load(std::uint32_t tons) const {
    // The original's 30 t of wood, tools and bricks (15 t of food) took a city hours to spare; two
    // thirds found a Kontor, a market and the first farms all the same.
    return classic() ? tons : tons * 2 / 3;
}

bool Engine::colony_load_in_stock(AiPlayer& p, bool food) {
    // The whole load in the home city: an improved computer loads all of its stock there
    // (prepare_colony_ship) and sets out as soon as the ship is home.
    const City* c = home_city(p);
    if (c == nullptr) {
        return false;
    }
    const auto& m = m_buildings.materials();
    const std::uint32_t full = colony_load(30) * kTon;
    return c->stock(m.wood) >= full && c->stock(m.tools) >= full &&
           (food ? c->stock(m_buildings.food_ware()) >= colony_load(15) * kTon : c->stock(m.bricks) >= full);
}

bool Engine::prepare_colony_ship(AiPlayer& p, std::uint8_t crop, bool food) {
    Settlement& s = p.settle;
    if (s.active) {
        pick_island(p);
        return true;
    }
    if (s.failed) {
        if (now_ms() - s.last_ms < kColonyCooldownMs) {
            return false;
        }
        s.failed = false;
    }
    Ship* ship = settle_ship(p);
    City* c = current_city(p);
    if (ship == nullptr || c == nullptr) {
        return false;
    }
    const auto& m = m_buildings.materials();
    const std::uint8_t food_ware = m_buildings.food_ware();
    if (gifts() && behind(p) && !threatened(p)) {
        // The colony subsidy (cheat): one ware per call into the current city.
        bool given = true;
        if (c->stock(m.wood) < cheats().wood_below * kStockUnitsPerTon) {
            add_goods(*c, m.wood, cheats().wood * kStockUnitsPerTon);
        } else if (c->stock(m.tools) < cheats().tools_below * kStockUnitsPerTon) {
            add_goods(*c, m.tools, cheats().tools * kStockUnitsPerTon);
        } else {
            std::uint32_t inhabited = 0;
            for (const City& o : m_world.cities()) {
                inhabited += o.owner == p.player && population(o) > 0 ? 1U : 0U;
            }
            if (inhabited < 2 && c->stock(food_ware) < cheats().food_below * kStockUnitsPerTon) {
                add_goods(*c, food_ware, cheats().food * kStockUnitsPerTon);
            } else if (inhabited >= 2 && c->stock(m.bricks) < cheats().bricks_below * kStockUnitsPerTon) {
                add_goods(*c, m.bricks, cheats().bricks * kStockUnitsPerTon);
            } else {
                given = false;
            }
        }
        if (given) {
            return true;
        }
    }
    // Loading from the current city. The original loads wherever the ship is (S-097); Open1602 sends
    // the ship to the city first, an improved computer to its home city, where it called the ship.
    City* home = classic() ? nullptr : home_city(p);
    City& here = home != nullptr ? *home : *c;
    if (ship_moving(*ship)) {
        return true;
    }
    if (!ship_at_city(*ship, here)) {
        send_to_city(p, *ship, here);
        return true;
    }
    load_colony_ship(p, here, *ship, food, crop);
    return true;
}

bool Engine::load_colony_ship(AiPlayer& p, City& c, Ship& ship, bool food, std::uint8_t crop) {
    Settlement& s = p.settle;
    const auto& m = m_buildings.materials();
    const std::uint8_t food_ware = m_buildings.food_ware();
    if (!classic()) {
        // Open1602: other goods aboard go into the warehouse first; a ship whose holds were full of
        // them took no wood or tools and lay at the Kontor.
        unload_at(c, ship, true, false);
    }
    const auto load = [&](std::uint8_t w, std::uint32_t enough, std::uint32_t min_city, std::uint32_t amount) {
        // Open1602: an improved computer sets out with nine tenths too; the last ton of food was
        // eaten in town as fast as it came in, and the ship waited for it.
        if (ship.load(w) >= (classic() ? enough : enough - enough / 10)) {
            return true;
        }
        // Open1602: an improved computer loads what the city has; the original leaves it min_city, so
        // a city short of tools kept its ship waiting at the Kontor.
        const std::uint32_t stock = c.stock(w);
        if (stock >= min_city || (!classic() && stock != 0)) {
            CargoTransfer t;
            t.player = p.player;
            t.ship = ship.id;
            t.ware = w;
            t.to_ship = true;
            t.amount = classic() ? amount : std::min(stock, enough - ship.load(w));  // all at once
            execute(t);
        }
        return false;
    };
    // Open1602: an improved computer sets out with two thirds of the original's load (colony_load).
    const std::uint32_t full = colony_load(30);
    bool ready = load(m.wood, full * kTon, 15 * kTon, 5 * kTon);
    ready = load(m.tools, full * kTon, 18 * kTon, 5 * kTon) && ready;
    if (food) {
        ready = load(food_ware, colony_load(15) * kTon, 9 * kTon, 3 * kTon) && ready;
    } else {
        ready = load(m.bricks, full * kTon, 15 * kTon, 5 * kTon) && ready;
    }
    if (ready) {
        log(p, "colony ship %u loaded: wood %u tools %u bricks %u food %u", ship.id, ship.load(m.wood) / 32,
            ship.load(m.tools) / 32, ship.load(m.bricks) / 32, ship.load(food_ware) / 32);
        s.need = 1800;
        s.floor = 300;
        s.crop = crop;
        s.active = true;
        s.load_ms = 0;
    }
    return ready;
}

void Engine::continue_colony_load(AiPlayer& p) {
    // Open1602: an improved computer loads its colony ship at home every few seconds; the original
    // loaded only when the city's order asked for the colony again, and an order that moved on left
    // the ship waiting.
    Settlement& s = p.settle;
    City* home = home_city(p);
    Ship* ship = settle_ship(p);
    if (classic() || s.active || s.load_ms == 0 || home == nullptr || ship == nullptr || ship_moving(*ship)) {
        return;
    }
    if (!ship_at_city(*ship, *home)) {
        log(p, "colony ship %u: not at home yet (at %u,%u, target %d, route %d)", ship->id, unsigned{ship->x},
            unsigned{ship->y}, ship->target.empty() ? 0 : 1, ship->route.active ? 1 : 0);
        if (ship->target.empty()) {
            send_to_city(p, *ship, *home);
        }
        return;
    }
    if (!load_colony_ship(p, *home, *ship, s.load_food, choose_colony_crop(p))) {
        const auto& m = m_buildings.materials();
        log(p, "colony ship %u: loading, aboard wood %u tools %u bricks %u food %u, holds %zu", ship->id,
            ship->load(m.wood) / kTon, ship->load(m.tools) / kTon, ship->load(m.bricks) / kTon,
            ship->load(m_buildings.food_ware()) / kTon, ship->cargo.size());
    }
}

// ---------------------------------------------------------------------------------------------
// Task 0x2E: giving a city up

void Engine::task_abandon(AiPlayer& p) {
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        pop(p);
        return;
    }
    const std::uint16_t island = c->island;
    const std::uint8_t index = c->index;
    const IslandMap& map = island_of(island).map;
    // At most 25 removals per step. A refused order is skipped, and a pass that removes nothing
    // gives up: the city stays (GB-3; the original repeats the pass every step until the re-init).
    std::uint32_t done = 0;
    bool others = false;
    for (std::uint32_t y = 0; y < map.height() && done < 25; ++y) {
        for (std::uint32_t x = 0; x < map.width() && done < 25; ++x) {
            const Tile t = tile(island, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            if (!t.valid() || t.city != index || !t.origin || t.owner != p.player) {
                continue;
            }
            if (!territory_building(m_buildings, *t.type) || t.ware == ware::kTree) {
                continue;
            }
            if (t.prod == prod::kKontor) {
                continue;  // last
            }
            others = true;
            if (demolish_tile(p, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)) != 0) {
                ++done;
            }
        }
    }
    if (others && done == 0) {
        log(p, "abandon: nothing could be demolished, the city stays");
        pop(p);
        r->flags &= ~city_flag::kAbandon;
        return;
    }
    if (others) {
        return;
    }
    // Only the Kontor is left: the stock goes on an idle ship lying there, then the Kontor goes.
    if (c->warehouse_x && c->warehouse_y) {
        demolish_tile(p, *c->warehouse_x, *c->warehouse_y);
    }
    pop(p);
    if (p.cities > 0) {
        --p.cities;
    }
    r->flags &= ~city_flag::kAbandon;
    p.has_city = false;
}

}  // namespace o1602::sim::ai
