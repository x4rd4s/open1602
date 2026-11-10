// SPDX-License-Identifier: GPL-3.0-or-later
//
// Building sites of the computer players: every search walks along
// the straight road runs of the current city (task 7), places the footprint of the building
// beside the road, checks it with the build rules and scores the surroundings ring by ring. One
// candidate that fits is scored per AI step, so a search spans many steps; the best one is built
// once the road list is exhausted.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>
#include <vector>

#include "ai_engine.hpp"

#include "o1602/sim/houses.hpp"

namespace o1602::sim::ai {

namespace {

/// Rings of the original searches. A search whose callback only looks at d below some limit
/// passes that limit instead (TB-11): the rings past it cost `tile()` calls and change nothing.
constexpr std::int32_t kRings = 16;
/// Road positions one search step may try before it yields (TB-12). The cursor stays in
/// `p.search`, so the next step goes on where this one stopped. Without the cap a built-up city
/// with no free site walked its whole road list with up to four `plan_build` calls per position.
constexpr std::int32_t kSearchPositions = 32;
/// Open1602: the trees next to a woodcutter enough where no place has the usual 10 (search_farm).
constexpr std::int32_t kSparseWoodTrees = 4;
/// Open1602, town_zone(): houses beyond those there are that the zone has room for, and its
/// smallest and largest radius.
constexpr std::int32_t kZoneSpareHouses = 12;
constexpr std::int32_t kZoneMinRadius = 8;
constexpr std::int32_t kZoneMaxRadius = 30;
/// Open1602: the houses a city has before its town zone is planned.
constexpr std::int32_t kZoneFirstHouses = 12;
/// Open1602: added to the score of a site on the right side of the town zone, so that any such site
/// wins over any on the wrong side, which is taken only where none is left.
constexpr std::int32_t kZoneTier = 1 << 16;
/// Open1602: tiles inside the town zone's edge a workshop may stand without taking house land.
constexpr std::int32_t kZoneWorkshopDepth = 4;
/// Open1602: rings between a market in the town zone and the city's other markets and its Kontor.
constexpr std::int32_t kTownMarketSpacing = 12;
/// Open1602: unclaimed tiles about a market site from which it counts as claiming land.
constexpr std::int32_t kMarketClaim = 150;
/// Island rows the wide quarry and mine search scans per step (TB-15).
constexpr std::int32_t kMineRows = 8;

/// The coast for the scoring (Kind 5, 23, 25, 26, 27, 29).
bool coast(std::int32_t k) {
    return k == kind::kWallBeach || k == kind::kBeach || k == kind::kBeachOuter || k == kind::kBeachInner ||
           k == kind::kBeachVariant || k == kind::kBeachRuin;
}

/// Farms, plantations and woodcutters: their buildings carry these wares.
bool raw_producer(std::uint8_t w) {
    return w == ware::kWool || w == ware::kSugar || w == ware::kTobacco || w == ware::kMeat || w == ware::kSpices ||
           w == ware::kCocoa || w == ware::kAlcohol || w == ware::kWood;
}

bool plantable(const Tile& t) {
    return (t.ware == 0 && t.kind == kind::kRuin) || t.ware == ware::kGrass || t.ware == ware::kTree;
}

/// Kontor or market (ALLWARE).
bool market_or_kontor(const Tile& t) {
    return t.ware == ware::kAll && (t.prod == prod::kMarket || t.prod == prod::kKontor);
}

/// Open1602: how much a farm or field site would take from the town. Houses and public buildings
/// near it and the middle of a market's reach are where the town grows; the original's computers
/// put their fields there as readily as at the edge, so that grain grew between the houses and the
/// town had to spread over new markets. Fields are kept for the edge of the market's reach.
std::int32_t town_cost(const Tile& t, std::int32_t d) {
    if (!t.origin) {
        return 0;
    }
    if (t.prod == prod::kHouse && d < 5) {
        return (5 - d) * 24;
    }
    if (t.prod >= prod::kPublicFirst && t.prod <= prod::kPublicLast && d < 5) {
        return (5 - d) * 16;
    }
    if (t.ware == ware::kAll && t.prod == prod::kMarket && t.kind == kind::kBuilding && d < 7) {
        return (7 - d) * 24;
    }
    return 0;
}

/// A site's score less its town_cost(), but a site that scored at all still does: where no other
/// is left the farm goes into the town rather than nowhere.
std::int32_t away_from_town(std::int32_t score, std::int32_t cost) {
    return score > 0 ? std::max(score - cost, 1) : score;
}

/// A candidate site: a rectangle of island tiles.
struct Site {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t parity = 0;
    std::int32_t side = 0;
    std::int32_t stub_x = -1;
    std::int32_t stub_y = -1;
    [[nodiscard]] std::int32_t w() const { return x1 - x0 + 1; }
    [[nodiscard]] std::int32_t h() const { return y1 - y0 + 1; }
};

/// Tile of the segment frame: `along` steps in the run's direction, `perp` to its left.
void frame(const Segment& s, std::int32_t cx, std::int32_t cy, std::int32_t along, std::int32_t perp, std::int32_t& x,
           std::int32_t& y) {
    x = cx + along * s.dx - perp * s.dy;
    y = cy + along * s.dy + perp * s.dx;
}

Site frame_rect(const Segment& s, std::int32_t cx, std::int32_t cy, std::int32_t a0, std::int32_t a1, std::int32_t p0,
                std::int32_t p1) {
    std::int32_t ax = 0;
    std::int32_t ay = 0;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    frame(s, cx, cy, a0, p0, ax, ay);
    frame(s, cx, cy, a1, p1, bx, by);
    Site r;
    r.x0 = std::min(ax, bx);
    r.x1 = std::max(ax, bx);
    r.y0 = std::min(ay, by);
    r.y1 = std::max(ay, by);
    return r;
}

/// A 2x2 building in one of two rows on each side of the road.
Site gen_double_row(const Segment& s, std::int32_t cx, std::int32_t cy, std::int32_t side) {
    const std::int32_t off = side == 0 ? 1 : side == 1 ? 3 : side == -1 ? -2 : -4;
    Site r = frame_rect(s, cx, cy, -1, 0, off, off + 1);
    r.side = side;
    return r;
}

/// A 2x2 farm beside the road or one row back with a road stub.
Site gen_farm(const Segment& s, std::int32_t cx, std::int32_t cy, std::int32_t side, bool grain) {
    const std::int32_t off = side == 0 ? 1 : side == 1 ? 2 : side == -1 ? -2 : -3;
    const std::int32_t a0 = grain ? -2 : -3;
    Site r = frame_rect(s, cx, cy, a0, a0 + 1, off, off + 1);
    r.side = side;
    if (side == 1 || side == -2) {
        frame(s, cx, cy, grain ? -1 : -2, side == 1 ? 1 : -1, r.stub_x, r.stub_y);
    }
    return r;
}

/// One tile beside the road.
Site gen_single(const Segment& s, std::int32_t cx, std::int32_t cy, std::int32_t side) {
    const std::int32_t off = side == 0 ? 1 : side == 1 ? 2 : side == -1 ? -1 : -2;
    Site r = frame_rect(s, cx, cy, 0, 0, off, off);
    r.side = side;
    return r;
}

/// A w x h building ending at the cursor, turned for sides -2 and 1. Empty (x0 > x1)
/// if it would reach past the start of the run.
Site gen_sized(const Segment& s, std::int32_t cx, std::int32_t cy, std::int32_t side, std::int32_t w, std::int32_t h,
               std::int32_t remaining) {
    const bool turned = side == -2 || side == 1;
    const std::int32_t along = turned ? h : w;
    const std::int32_t perp = turned ? w : h;
    Site r;
    if (along - 1 > remaining) {
        r.x0 = 1;
        r.x1 = 0;
        return r;
    }
    if (side >= 0) {
        r = frame_rect(s, cx, cy, -(along - 1), 0, 1, perp);
    } else {
        r = frame_rect(s, cx, cy, -(along - 1), 0, -perp, -1);
    }
    r.side = side;
    return r;
}

}  // namespace

namespace {

/// Open1602: a farm's or a workshop's score, raised above every site deeper in the town zone when
/// its middle lies more than `margin` tiles beyond the zone's edge (a negative margin: less than so
/// many inside it).
std::int32_t outside_zone(const TownZone* zone, std::int32_t margin, const Site& c, std::int32_t score) {
    if (zone == nullptr || score <= 0) {
        return score;
    }
    return zone_excess(*zone, c.x0, c.y0, c.x1, c.y1) > margin ? score + kZoneTier : score;
}

/// Open1602: an improved computer also takes a site that scores nothing, after every site that
/// does. The scores reward company (houses, a market, the raw material) and punish the coast and
/// the fields, so in a full town or on a small island every site left may score 0 or less; the
/// original then finds no site for a house or a workshop at all, however much room there is.
constexpr std::int32_t kAnySiteBand = 1 << 20;

std::int32_t any_site(bool classic, std::int32_t score) {
    if (classic) {
        return score;
    }
    return score > 0 ? score + kAnySiteBand : std::max(1, kAnySiteBand + score);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Road runs (task 7)

Engine::CityScans& Engine::city_scans(std::uint16_t island, std::uint8_t city) {
    const auto it = std::find_if(m_city_scans.begin(), m_city_scans.end(),
                                 [&](const CityScans& s) { return s.island == island && s.city == city; });
    if (it != m_city_scans.end()) {
        return *it;
    }
    CityScans& s = m_city_scans.emplace_back();
    s.island = island;
    s.city = city;
    return s;
}

void Engine::build_segments(AiPlayer& p) {
    p.segments.clear();
    if (!p.has_city) {
        return;
    }
    // The segments follow from the island's tiles alone: while none changed, the last layout's
    // (before any task closed one) are the answer (TB-11).
    CityScans& known = city_scans(p.island, p.city);
    const std::uint64_t revision = island_revision(p.island);
    if (known.layout_revision == revision) {
        p.segments = known.segments;
        return;
    }
    known.layout_revision = revision;
    const std::uint16_t island = p.island;
    const IslandMap& map = island_of(island).map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    // Road piece of the current city: 0 straight, 1 curve, 2 T, 3 cross; -1 no road.
    const auto piece = [&](std::int32_t x, std::int32_t y) -> std::int32_t {
        const Tile t = tile(island, x, y);
        if (!t.valid() || t.city != p.city || (t.kind != kind::kRoad && t.kind != kind::kBridge)) {
            return -1;
        }
        if (t.kind == kind::kBridge || t.type->kreuz_base <= 0) {
            return 0;
        }
        const auto v = static_cast<std::int32_t>(t.type->record) - t.type->kreuz_base;
        return v >= 0 && v <= 3 ? v : 0;
    };
    const auto add = [&](Segment s) {
        if (s.length <= 1) {
            return;
        }
        if (p.segments.size() < 256) {
            p.segments.push_back(s);
            return;
        }
        if (s.length > 3) {
            for (Segment& o : p.segments) {
                if (o.length <= 3) {
                    o = s;
                    return;
                }
            }
        }
    };
    // Rows (dir 0 / 2), then columns (dir 1 / 3).
    for (std::int32_t pass = 0; pass < 2; ++pass) {
        const bool rows = pass == 0;
        const std::int32_t outer = rows ? h : w;
        const std::int32_t inner = rows ? w : h;
        for (std::int32_t o = 0; o < outer; ++o) {
            std::int32_t start = -1;
            bool open = true;
            for (std::int32_t i = 0; i <= inner; ++i) {
                const std::int32_t x = rows ? i : o;
                const std::int32_t y = rows ? o : i;
                const std::int32_t v = i < inner ? piece(x, y) : -1;
                const auto make = [&](std::int32_t from, std::int32_t to, bool reversed, bool flag) {
                    Segment s;
                    if (rows) {
                        s.x0 = reversed ? to : from;
                        s.x1 = reversed ? from : to;
                        s.y0 = s.y1 = o;
                        s.dx = reversed ? -1 : 1;
                        s.dir = reversed ? 2 : 0;
                    } else {
                        s.y0 = reversed ? to : from;
                        s.y1 = reversed ? from : to;
                        s.x0 = s.x1 = o;
                        s.dy = reversed ? -1 : 1;
                        s.dir = reversed ? 3 : 1;
                    }
                    s.length = to - from;
                    s.open = flag;
                    add(s);
                };
                if (v < 0) {
                    if (start >= 0) {
                        make(start, i - 1, true, true);  // ends at a tile that is no road: a dead end
                    }
                    start = -1;
                    open = true;
                    continue;
                }
                if (start < 0) {
                    if (v == 0) {
                        start = i;
                    }
                    continue;
                }
                if (v == 1) {
                    make(start, i - 1, false, true);
                    start = -1;
                    open = true;
                } else if (v >= 2) {
                    // A junction ends the run when the crossing road goes on two tiles away.
                    const bool far = rows ? (piece(x, y - 2) >= 0 || piece(x, y + 2) >= 0)
                                          : (piece(x - 2, y) >= 0 || piece(x + 2, y) >= 0);
                    if (far) {
                        make(start, i - 1, false, open);
                        start = -1;
                        open = false;
                    }
                }
            }
        }
    }
    known.segments = p.segments;
}

// ---------------------------------------------------------------------------------------------
// The town zone (Open1602)

namespace {

std::int32_t isqrt32(std::int64_t v) {
    if (v <= 0) {
        return 0;
    }
    std::int64_t r = 0;
    std::int64_t bit = std::int64_t{1} << 40;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<std::int32_t>(r);
}

}  // namespace

TownStyle town_style(std::uint32_t seed, std::uint16_t island, std::uint8_t city) {
    // From the computer's number drawn for this game, the island and the city: two computers, and
    // two cities of one, plan their towns differently, the same computer differently in another
    // game, and the same city the same way after a game is loaded.
    std::uint64_t h = (static_cast<std::uint64_t>(seed) << 24) ^ (static_cast<std::uint64_t>(island) << 8) ^ city;
    h += 0x9E3779B97F4A7C15ULL;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h ^= h >> 31;
    TownStyle t;
    t.room = 6 + static_cast<std::int32_t>(h % 4);              // 6 (close) to 9 (spacious) tiles a house
    t.shift = static_cast<std::int32_t>((h >> 8) % 5);          // 0 to 4 tiles away from the Kontor
    t.block = 7 + static_cast<std::int32_t>((h >> 16) % 3);     // roads every 8 to 10 tiles
    t.field_margin = static_cast<std::int32_t>((h >> 24) % 4);  // fields 0 to 3 tiles further out
    return t;
}

TownStyle Engine::town_style_of(AiPlayer& p) {
    if (p.town_seed == 0) {
        p.town_seed = rng().below(0xFFFFFFFFU) + 1;
    }
    return town_style(p.town_seed, p.island, p.city);
}

std::int32_t zone_excess(const TownZone& z, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    const std::int64_t dx = static_cast<std::int64_t>(x0 + x1) - 2 * z.cx;
    const std::int64_t dy = static_cast<std::int64_t>(y0 + y1) - 2 * z.cy;
    return isqrt32(dx * dx + dy * dy) / 2 - z.radius;
}

const TownZone& Engine::town_zone(AiPlayer& p) {
    CityScans& known = city_scans(p.island, p.city);
    const std::uint64_t revision = island_revision(p.island);
    if (known.zone_revision == revision) {
        return known.zone;
    }
    known.zone_revision = revision;
    TownZone& z = known.zone;
    z = TownZone{};
    const City* c = current_city(p);
    if (c == nullptr || !c->warehouse_x || !c->warehouse_y) {
        return z;
    }
    // A new city is founded as in the original: its first houses go next to its market, which keeps
    // it on time for the settlers; the town is planned once it has outgrown them.
    std::int32_t houses = 0;
    for (const House& hs : m_world.houses()) {
        houses += hs.island == p.island && hs.city == p.city ? 1 : 0;
    }
    if (houses < kZoneFirstHouses) {
        return z;
    }
    const IslandMap& map = island_of(p.island).map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    const std::int32_t kx = *c->warehouse_x + 1;
    const std::int32_t ky = *c->warehouse_y + 1;
    // The middle: the market nearest the Kontor, else the Kontor.
    std::int32_t mx = kx;
    std::int32_t my = ky;
    std::int64_t nearest = -1;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            if (map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).city != p.city) {
                continue;
            }
            const Tile t = tile(p.island, x, y);
            if (!t.origin || t.prod != prod::kMarket || t.type->destroyed) {
                continue;
            }
            const std::int32_t cx = x + static_cast<std::int32_t>(t.type->size_x) / 2;
            const std::int32_t cy = y + static_cast<std::int32_t>(t.type->size_y) / 2;
            const std::int64_t d = std::int64_t{cx - kx} * (cx - kx) + std::int64_t{cy - ky} * (cy - ky);
            if (nearest < 0 || d < nearest) {
                nearest = d;
                mx = cx;
                my = cy;
            }
        }
    }
    const TownStyle style = town_style_of(p);
    // Some towns lie a little further inland from the Kontor than others.
    if (style.shift != 0 && (mx != kx || my != ky)) {
        const std::int32_t len =
            std::max(1, isqrt32(std::int64_t{mx - kx} * (mx - kx) + std::int64_t{my - ky} * (my - ky)));
        mx += (mx - kx) * style.shift / len;
        my += (my - ky) * style.shift / len;
    }
    // As large as the room of the houses there are and a dozen more.
    const std::int64_t wanted = static_cast<std::int64_t>(houses + kZoneSpareHouses) * style.room;
    std::int64_t room = 0;
    std::int32_t r = kZoneMinRadius;
    std::vector<std::int64_t> by_ring(static_cast<std::size_t>(kZoneMaxRadius) + 1, 0);
    for (std::int32_t y = std::max(0, my - kZoneMaxRadius); y <= std::min(h - 1, my + kZoneMaxRadius); ++y) {
        for (std::int32_t x = std::max(0, mx - kZoneMaxRadius); x <= std::min(w - 1, mx + kZoneMaxRadius); ++x) {
            const std::int32_t d = isqrt32(std::int64_t{x - mx} * (x - mx) + std::int64_t{y - my} * (y - my));
            if (d > kZoneMaxRadius) {
                continue;
            }
            const MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            if (cell.city != p.city && cell.city != MapCell::kNoCity) {
                continue;
            }
            const Tile t = tile(p.island, x, y);
            const bool land =
                t.kind == kind::kGround || t.kind == kind::kForest || t.kind == kind::kRuin || t.kind == kind::kRoad ||
                t.kind == kind::kSquare ||
                (t.kind == kind::kBuilding && (t.prod == prod::kHouse || t.prod == prod::kMarket ||
                                               (t.prod >= prod::kPublicFirst && t.prod <= prod::kPublicLast)));
            if (land) {
                ++by_ring[static_cast<std::size_t>(d)];
            }
        }
    }
    for (r = 0; r <= kZoneMaxRadius; ++r) {
        room += by_ring[static_cast<std::size_t>(r)];
        if (r >= kZoneMinRadius && room >= wanted) {
            break;
        }
    }
    z.valid = true;
    z.cx = mx;
    z.cy = my;
    z.radius = std::min(r, kZoneMaxRadius);
    return z;
}

// ---------------------------------------------------------------------------------------------
// Search state and walking along the runs

void Engine::start_search(AiPlayer& p, const assets::BuildingType& b, std::uint8_t slot, bool from_start) {
    Search s;
    s.building = b.id;
    s.slot = slot;
    s.radius = b.radius;
    if (!p.segments.empty()) {
        const Segment& g = p.segments.front();
        s.x = from_start ? g.x0 : g.x1;
        s.y = from_start ? g.y0 : g.y1;
        s.ref_x = s.x;
        s.ref_y = s.y;
        s.remaining = g.length;
    }
    p.search = s;
    if (logging()) {
        site_tally(p) = SiteTally{};
    }
}

bool Engine::advance_cursor(AiPlayer& p, std::int32_t limit) {
    Search& s = p.search;
    if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
        return false;
    }
    const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
    if (s.remaining <= 0 || s.remaining < limit) {
        ++s.segment;
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return false;
        }
        const Segment& n = p.segments[static_cast<std::size_t>(s.segment)];
        s.x = n.x1;
        s.y = n.y1;
        s.ref_x = n.x1;
        s.ref_y = n.y1;
        s.remaining = n.length;
        return true;
    }
    --s.remaining;
    s.x -= g.dx;
    s.y -= g.dy;
    return true;
}

bool Engine::can_place(const AiPlayer& p, const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::int32_t w,
                       std::int32_t h) const {
    if (!inside(p.island, x, y) || !inside(p.island, x + w - 1, y + h - 1)) {
        return false;
    }
    // Kept between calls: the site searches test thousands of sites a minute (TB-9).
    BuildOrder& order = m_place_order;
    BuildPlan& plan = m_place_plan;
    order.player = p.player;
    order.island = p.island;
    order.orient_to_road = false;
    const std::uint8_t rotation = (static_cast<std::uint32_t>(w) != b.size_x) ? 1 : 0;
    order.items.assign(1, BuildItem{b.id, x, y, rotation, 0});
    plan_build(m_world, m_buildings, order, plan);
    if (plan.city != p.city) {
        return false;
    }
    switch (plan.items.front().refusal) {
        case BuildRefusal::None:
        case BuildRefusal::Money:
        case BuildRefusal::Tools:
        case BuildRefusal::Wood:
        case BuildRefusal::Bricks:
        case BuildRefusal::Cannons:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Site families

namespace {

/// The parity penalty: two-wide buildings line up on every second road tile.
std::int32_t parity_penalty(std::int32_t score, std::int32_t parity, std::int32_t divisor) {
    return (parity & 1) != 0 ? score - score / divisor : score;
}

}  // namespace

bool Engine::search_houses(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    const TownZone* zone = classic() ? nullptr : &town_zone(p);
    if (zone != nullptr && !zone->valid) {
        zone = nullptr;
    }
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_double_row(g, s.x, s.y, side);
            if (!can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            Acc acc;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 14,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (coast(t.kind) && d < 8) {
                          acc.score += d - 8;
                      }
                      if (t.kind == kind::kRock && d < 2) {
                          acc.score -= 16 << (2 - d);
                      }
                      if (t.prod == prod::kShipyard && d <= 1) {
                          acc.score -= 1000;
                      }
                      if (t.origin) {
                          switch (t.prod) {
                              case prod::kHouse:
                                  if (d < 5) {
                                      acc.score += (4 - d) * 8;
                                  }
                                  break;
                              case prod::kWirt:
                                  if (d < 9) {
                                      acc.score += (8 - d) * 4;
                                  }
                                  break;
                              case prod::kKapelle:
                              case prod::kKirche:
                                  if (d < 8) {
                                      acc.score += (7 - d) * 8;
                                  }
                                  break;
                              case prod::kBadehaus:
                              case prod::kTheater:
                              case prod::kHochschule:
                                  if (d < 13) {
                                      acc.score += (12 - d) * 8;
                                  }
                                  break;
                              case prod::kKlinik:
                              case prod::kSchule:
                                  if (d < 13) {
                                      acc.score += (12 - d) * 4;
                                  }
                                  break;
                              case prod::kGalgen:
                                  if (d < 14) {
                                      acc.score += (7 - d) * 8;
                                  }
                                  break;
                              case prod::kBrunnen:
                                  if (d < 12) {
                                      acc.score += (7 - d) * 8;
                                  }
                                  break;
                              default:
                                  break;
                          }
                          if (t.ware == ware::kAll && t.kind == kind::kBuilding && d < 13) {
                              acc.market = true;  // a market place, the Kontor does not count
                              acc.score += (12 - d) * 8;
                          }
                      }
                      if (raw_producer(t.ware) && d < 2) {
                          acc.score -= 8 << (2 - d);
                      }
                      if (t.ware == ware::kGrain && d == 0) {
                          acc.score -= 64;
                      }
                  });
            const std::int32_t parity = std::abs(s.x - s.ref_x) + std::abs(s.y - s.ref_y);
            std::int32_t score = parity_penalty(acc.score, parity, 8);
            if (zone != nullptr && score > 0) {
                // Open1602: houses in the town zone first, the nearer its middle the better.
                const std::int32_t out = zone_excess(*zone, c.x0, c.y0, c.x1, c.y1);
                score = out <= 0 ? score + kZoneTier - 4 * (out + zone->radius) : std::max(1, score / (1 + out));
            }
            score = any_site(classic(), score);
            tally_site(p, true, acc.market, score);
            if (acc.market && score > s.best) {
                s.best = score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
            }
        }
        if (!advance_cursor(p, 2)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_workshop(AiPlayer& p, bool cannons) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    const std::uint8_t raw = b->raw_ware;
    const TownZone* zone = !classic() && town_zone(p).valid ? &town_zone(p) : nullptr;
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, -1}) {
            Site c = cannons ? gen_sized(g, s.x, s.y, side, static_cast<std::int32_t>(b->size_x),
                                         static_cast<std::int32_t>(b->size_y), s.remaining)
                             : gen_double_row(g, s.x, s.y, side);
            if (c.x0 > c.x1 || !can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            Acc acc;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 9,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (d == 0 && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                          acc.road = true;
                      }
                      if (coast(t.kind) && d < 8) {
                          acc.score -= 2 << (8 - d);
                      }
                      if (t.origin && raw != 0 && t.ware == raw && t.prod != prod::kRipe && d < 9) {
                          acc.score += (12 - d) * 32;
                      }
                      if (t.prod == prod::kShipyard && d < 2) {
                          acc.score -= 1000;
                      }
                      if (t.origin && market_or_kontor(t) && d < 9) {
                          acc.market = true;
                          acc.score += (8 - d) * 32;
                          if (d == 0) {
                              acc.score -= 64;
                          }
                      }
                      if (t.ware == ware::kGrain && d == 0) {
                          acc.score -= 64;
                      }
                      if (raw_producer(t.ware) && d < 2) {
                          acc.score -= 32 << (2 - d);
                      }
                  });
            const std::int32_t parity = std::abs(s.x - s.ref_x) + std::abs(s.y - s.ref_y);
            std::int32_t score = cannons ? acc.score : parity_penalty(acc.score, parity, 4);
            // Open1602: workshops at the edge of the town zone or outside, not on its house land.
            score = any_site(classic(), outside_zone(zone, -kZoneWorkshopDepth, c, score));
            tally_site(p, acc.road, acc.market, score);
            if (acc.market && acc.road && score > s.best) {
                s.best = score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
            }
        }
        if (!advance_cursor(p, cannons ? 0 : 2)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_stonemason(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, -1}) {
            Site c = gen_double_row(g, s.x, s.y, side);
            if (!can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            Acc acc;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 13,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (d == 0 && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                          acc.road = true;
                      }
                      if (t.origin && market_or_kontor(t) && d < 13) {
                          acc.market = true;
                      }
                      if (t.ware == ware::kStone && d < 5) {
                          acc.score += 8 << (5 - d);
                      }
                  });
            const std::int32_t parity = std::abs(s.x - s.ref_x) + std::abs(s.y - s.ref_y);
            const std::int32_t score = parity_penalty(acc.score, parity, 4);
            if (acc.market && acc.road && score > s.best) {
                s.best = score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
            }
        }
        if (!advance_cursor(p, 2)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_farm(AiPlayer& p, bool woodcutter) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    const std::uint8_t own = b->ware;
    // Open1602: a woodcutter wants 10 trees about it. Where no place along the roads has them (an
    // island with little forest), a second round takes a place with a few, whose woodcutter the
    // trees planted about it (task_farm) feed; the original's computer built none and ran out of wood.
    const bool sparse = woodcutter && s.phase == 1;
    const bool improved = !woodcutter && !classic();
    const TownZone* zone = improved && town_zone(p).valid ? &town_zone(p) : nullptr;
    const std::int32_t margin = improved ? town_style_of(p).field_margin : 0;
    const auto finished = [&] {
        if (woodcutter && s.best == 0 && s.phase == 0 && !classic()) {
            start_search(p, *b, s.slot, false);
            p.search.phase = 1;
            return false;
        }
        return true;
    };
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return finished();
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        if (woodcutter || g.length > 4) {
            for (const std::int32_t side : {0, 1, -1, -2}) {
                Site c = gen_farm(g, s.x, s.y, side, false);
                if (!can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                    continue;
                }
                if (c.stub_x >= 0) {
                    const Tile st = tile(p.island, c.stub_x, c.stub_y);
                    if (!st.valid() || st.city != p.city ||
                        !(st.kind == kind::kForest || st.kind == kind::kGround || st.kind == kind::kRuin ||
                          st.kind == kind::kRoad)) {
                        continue;
                    }
                }
                any = true;
                Acc acc;
                if (!woodcutter) {
                    acc.score = 2 * s.remaining;
                }
                rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 13,
                      [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                          if (d <= 1 && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                              acc.road = true;
                          }
                          if (woodcutter) {
                              if (t.prod == prod::kShipyard && d <= 3) {
                                  acc.score -= 500;
                              }
                              if (market_or_kontor(t) && d < 13) {
                                  acc.market = true;
                              }
                              if (raw_producer(t.ware) && d < 4) {
                                  acc.score -= 64 << (4 - d);
                              }
                              // The trees next to the hut (the original's); Open1602 counts all
                              // in the woodcutter's reach, so that it does not stand in a clearing
                              // with a few trees beside a wood.
                              if (t.ware == ware::kTree && d < (classic() ? 2 : 4)) {
                                  ++acc.count;
                                  acc.score += t.growth;
                              }
                              return;
                          }
                          if (t.origin && t.prod == prod::kHouse && d < 2) {
                              acc.score -= 32 << (2 - d);
                          }
                          if (improved) {
                              acc.extra += town_cost(t, d);
                          }
                          if (t.prod == prod::kShipyard && d <= 3) {
                              acc.score -= 1000;
                          }
                          if (t.ware == own && t.prod != prod::kRipe && d > 2 && d < 10) {
                              acc.score += (10 - d) * 8;
                          }
                          if (plantable(t) && d < 2) {
                              ++acc.count;
                              acc.score += t.growth;
                          }
                          if (t.origin && market_or_kontor(t) && d < 13) {
                              acc.market = true;
                              if (t.kind == kind::kBuilding) {
                                  acc.score += 12 - d;
                              } else if (d < 5) {
                                  acc.score -= 128;
                              }
                          }
                          if (t.ware == ware::kGrain && d == 0) {
                              acc.score -= 64;
                          }
                          if (raw_producer(t.ware) && d < 5) {
                              acc.score -= 32 << (5 - d);
                          }
                      });
                if (side == 1 || side == -2) {
                    acc.score += 32;
                }
                const std::int32_t parity = std::abs(s.x - s.ref_x) + std::abs(s.y - s.ref_y);
                const std::int32_t score =
                    outside_zone(zone, margin, c, away_from_town(parity_penalty(acc.score, parity, 4), acc.extra));
                const bool enough = woodcutter ? acc.count >= (sparse ? kSparseWoodTrees : 10) : acc.count >= 19;
                if (enough && acc.road && acc.market && score > s.best && farm_room(p, *b, c.x0, c.y0, c.x1, c.y1)) {
                    s.best = score;
                    s.best_x0 = c.x0;
                    s.best_y0 = c.y0;
                    s.best_x1 = c.x1;
                    s.best_y1 = c.y1;
                    s.stub_x = c.stub_x;
                    s.stub_y = c.stub_y;
                }
            }
        }
        if (!advance_cursor(p, 6)) {
            return finished();
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_grain(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    const bool improved = !classic();
    const TownZone* zone = improved && town_zone(p).valid ? &town_zone(p) : nullptr;
    const std::int32_t margin = improved ? town_style_of(p).field_margin : 0;
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_farm(g, s.x, s.y, side, true);
            if (!can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            if (c.stub_x >= 0) {
                const Tile st = tile(p.island, c.stub_x, c.stub_y);
                if (!st.valid() || st.city != p.city ||
                    !(st.kind == kind::kForest || st.kind == kind::kGround || st.kind == kind::kRuin ||
                      st.kind == kind::kRoad)) {
                    continue;
                }
            }
            any = true;
            Acc acc;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 13,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (d <= 1 && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                          acc.road = true;
                      }
                      if (t.prod == prod::kShipyard && d <= 3) {
                          acc.score -= 500;
                      }
                      if (d == 0 && plantable(t)) {
                          ++acc.count;
                          acc.score += t.growth;
                      }
                      if (t.origin && market_or_kontor(t) && d < 13) {
                          acc.market = true;
                      }
                      if (raw_producer(t.ware) && d < 3) {
                          acc.score -= 32 << (3 - d);
                      }
                      if (t.origin && t.ware == ware::kGrain && t.prod != prod::kRipe && d > 1 && d < 9) {
                          acc.score += (8 - d) * 4;
                      }
                      if (improved) {
                          acc.extra += town_cost(t, d);
                      }
                  });
            const std::int32_t parity = std::abs(s.x - s.ref_x) + std::abs(s.y - s.ref_y);
            const std::int32_t score = outside_zone(
                zone, margin, c, away_from_town((parity & 1) != 0 ? acc.score - 32 : acc.score, acc.extra));
            if (acc.count >= 9 && acc.road && acc.market && score > s.best &&
                farm_room(p, *b, c.x0, c.y0, c.x1, c.y1)) {
                s.best = score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
                s.stub_x = c.stub_x;
                s.stub_y = c.stub_y;
            }
        }
        if (!advance_cursor(p, 4)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_hunter(AiPlayer& p) {
    // One road position per call.
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr || s.segment >= static_cast<std::int32_t>(p.segments.size())) {
        return true;
    }
    static const std::vector<std::uint8_t> kCircle8 = circle_half_widths(8);
    const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
    const auto& spots = island_of(p.island).deer_spots;
    for (const std::int32_t side : {0, -1}) {
        Site c = gen_single(g, s.x, s.y, side);
        if (!can_place(p, *b, c.x0, c.y0, 1, 1)) {
            continue;
        }
        Acc acc;
        std::int32_t min_dx = 8;
        rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, kRings,
              [&](const Tile& t, std::int32_t x, std::int32_t y, std::int32_t d) {
                  for (const DeerSpot& spot : spots) {
                      if (!spot.used) {
                          continue;
                      }
                      const std::int32_t dx = std::abs(x - spot.x);
                      const std::int32_t dy = std::abs(y - spot.y);
                      if (dy < 8 && dx <= kCircle8[static_cast<std::size_t>(dy)]) {
                          min_dx = std::min(min_dx, dx);
                      }
                  }
                  if (d == 0 && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                      acc.road = true;
                  }
                  if (market_or_kontor(t) && d < 12) {
                      acc.market = true;
                  }
                  if ((t.ware == ware::kGrain || t.ware == ware::kFood) && d == 0) {
                      acc.score -= 10;
                  }
                  if (t.ware == ware::kWood && d < 3) {
                      acc.score -= 16 << (3 - d);
                  }
                  if (t.ware == ware::kGrass && d < 4) {
                      acc.score += (4 - d) * 8;
                  }
              });
        const std::int32_t score = acc.score + 8 * (8 - min_dx);
        if (min_dx < 8 && acc.market && acc.road && score >= s.best) {
            s.best = std::max(score, 1);
            s.best_x0 = s.best_x1 = c.x0;
            s.best_y0 = s.best_y1 = c.y0;
        }
    }
    return !advance_cursor(p, 6);
}

bool Engine::search_market(AiPlayer& p) {
    // As far from the other markets as their reach allows,
    // where most unclaimed land lies.
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    const CityPlan* r = current_plan(p);
    if (b == nullptr || r == nullptr) {
        return true;
    }
    const std::int32_t min_dist = r->slots[kSlotMarket].count == 0 ? 5 : 8;
    // Open1602: in the town zone, where a market serves houses rather than claims land, markets keep
    // 12 rings apart, three quarters of their reach; the original's 8 gave a town of a hundred houses
    // a dozen markets on its house land.
    const TownZone* zone = !classic() && town_zone(p).valid ? &town_zone(p) : nullptr;
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_sized(g, s.x, s.y, side, 4, 3, s.remaining);
            if (c.x0 > c.x1 || !can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            Acc acc;
            std::int32_t nearest = 32;
            std::int32_t free = 0;
            rings(p, RingFilter::Any, c.x0, c.y0, c.x1, c.y1, kRings,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (d == 0 && (t.kind == kind::kRoad || t.kind == kind::kBridge) && t.city == p.city) {
                          acc.road = true;
                      }
                      if (t.city == MapCell::kNoCity &&
                          (t.kind == kind::kForest || t.kind == kind::kGround || t.kind == kind::kRuin ||
                           t.kind == kind::kRock || t.kind == kind::kRiver || t.kind == kind::kSlope)) {
                          ++free;
                      }
                      if (t.origin && market_or_kontor(t)) {
                          acc.market = true;
                          nearest = std::min(nearest, d);
                      }
                      if (raw_producer(t.ware) && d < 3) {
                          acc.score -= 64 << (3 - d);
                      }
                      if (t.ware == ware::kGrain && d == 0) {
                          acc.score -= 64;
                      }
                  });
            std::int32_t score = acc.score + 32 * nearest + 4 * free;
            if (c.w() == static_cast<std::int32_t>(b->size_x)) {
                score += 16;
            }
            const std::int32_t spacing = min_dist == 8 && zone != nullptr && free < kMarketClaim &&
                                                 zone_excess(*zone, c.x0, c.y0, c.x1, c.y1) <= 0
                                             ? kTownMarketSpacing
                                             : min_dist;
            if (acc.market && acc.road && nearest > spacing && free > 40 && nearest >= s.extra && score >= s.best) {
                s.best = std::max(score, 1);
                s.extra = nearest;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
                s.rotation = c.w() == static_cast<std::int32_t>(b->size_x) ? 0 : 1;
            }
        }
        if (!advance_cursor(p, 0)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

namespace {

/// A distance in tiles without a square root: the longer leg and half the shorter.
std::int32_t tile_distance(std::int32_t dx, std::int32_t dy) {
    const std::int32_t ax = std::abs(dx);
    const std::int32_t ay = std::abs(dy);
    return std::max(ax, ay) + std::min(ax, ay) / 2;
}

/// Tiles a claim market must come nearer to its target than the city's nearest market or Kontor.
constexpr std::int32_t kClaimStep = 3;

}  // namespace

DepositAccess Engine::deposit_access(const AiPlayer& p, const assets::IslandHeader::Deposit& d, std::int32_t& free_x,
                                     std::int32_t& free_y) {
    // The mountain's rock tiles carry the deposit's origin; a mine stands on a slope next to them.
    const City* c = m_world.city(p.island, p.city);
    const std::int32_t kx = c != nullptr && c->warehouse_x ? *c->warehouse_x : d.x;
    const std::int32_t ky = c != nullptr && c->warehouse_y ? *c->warehouse_y : d.y;
    constexpr std::int32_t kMountain = 24;  // larger than any mountain
    bool free = false;
    std::int32_t best = 0;
    for (std::int32_t y = d.y; y < d.y + kMountain; ++y) {
        for (std::int32_t x = d.x; x < d.x + kMountain; ++x) {
            const Tile rock = tile(p.island, x, y);
            if (!rock.valid() || rock.kind != kind::kRock || rock.ox != d.x || rock.oy != d.y) {
                continue;
            }
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const Tile slope = tile(p.island, x + dx, y + dy);
                if (!slope.valid() || slope.kind != kind::kSlope) {
                    continue;
                }
                if (slope.city == p.city) {
                    return DepositAccess::Own;
                }
                if (slope.city != MapCell::kNoCity) {
                    continue;
                }
                const std::int32_t dist = tile_distance(x + dx - kx, y + dy - ky);
                if (!free || dist < best) {
                    free = true;
                    best = dist;
                    free_x = x + dx;
                    free_y = y + dy;
                }
            }
        }
    }
    return free ? DepositAccess::Free : DepositAccess::Closed;
}

std::int32_t Engine::market_distance(const AiPlayer& p, std::int32_t x, std::int32_t y) {
    // From the middle of the city's nearest market or Kontor to (x, y).
    const IslandMap& map = m_world.islands()[p.island].map;
    std::int32_t best = 1 << 15;
    for (std::uint32_t ty = 0; ty < map.height(); ++ty) {
        for (std::uint32_t tx = 0; tx < map.width(); ++tx) {
            if (map.top(tx, ty).city != p.city) {
                continue;
            }
            const Tile t = tile(p.island, static_cast<std::int32_t>(tx), static_cast<std::int32_t>(ty));
            if (!t.origin || !market_or_kontor(t) || t.type->destroyed) {
                continue;
            }
            const std::int32_t mx = static_cast<std::int32_t>(tx) + static_cast<std::int32_t>(t.type->size_x) / 2;
            const std::int32_t my = static_cast<std::int32_t>(ty) + static_cast<std::int32_t>(t.type->size_y) / 2;
            best = std::min(best, tile_distance(mx - x, my - y));
        }
    }
    return best;
}

bool Engine::search_claim(AiPlayer& p) {
    // Open1602 (task kClaimLand): along the road runs like search_market, the market site whose
    // reach takes in the target tile; failing that, the one nearest to it that comes at least
    // kClaimStep tiles nearer than the city's markets so far (`extra`), a step on the way there.
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr || s.target_x < 0) {
        return true;
    }
    const std::vector<std::uint8_t>& circle = circle_table(b->radius);
    const auto radius = static_cast<std::int32_t>(b->radius);
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_sized(g, s.x, s.y, side, 4, 3, s.remaining);
            if (c.x0 > c.x1 || !can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            const std::int32_t dx = std::max({c.x0 - s.target_x, 0, s.target_x - c.x1});
            const std::int32_t dy = std::max({c.y0 - s.target_y, 0, s.target_y - c.y1});
            const bool reaches = dy <= radius && dx <= circle[static_cast<std::size_t>(dy)];
            const std::int32_t dist = tile_distance((c.x0 + c.x1) / 2 - s.target_x, (c.y0 + c.y1) / 2 - s.target_y);
            if (!reaches && dist + kClaimStep > s.extra) {
                continue;
            }
            bool road = false;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 1,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t) {
                      road = road || t.kind == kind::kRoad || t.kind == kind::kBridge;
                  });
            if (!road) {
                continue;
            }
            const std::int32_t score = (reaches ? 1 << 14 : 0) + std::max(1, 4096 - 16 * dist);
            if (score > s.best) {
                s.best = score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
                s.rotation = c.w() == static_cast<std::int32_t>(b->size_x) ? 0 : 1;
            }
        }
        if (!advance_cursor(p, 0)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::farm_room(AiPlayer& p, const assets::BuildingType& b, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                       std::int32_t y1) {
    // Room for the fields the maintenance sweep wants to see later (14, grain 7): the original
    // places farms where the sweep tears them down again, and builds them anew (see sweep()).
    // Trees in reach of the city's own woodcutters are no room for other crops (S-122).
    std::int32_t n = 0;
    const std::uint8_t raw = b.raw_ware;
    std::vector<WoodReach> woods;
    if (raw != ware::kTree) {
        woods = woodcutters_near(p, x0, y0, x1, y1, b.radius);
    }
    rings(p, RingFilter::Own, x0, y0, x1, y1, b.radius,
          [&](const Tile& t, std::int32_t x, std::int32_t y, std::int32_t) {
              if (t.ware == raw || (t.ware == 0 && t.kind == kind::kRuin) || t.ware == ware::kGrass ||
                  (t.ware == ware::kTree && !in_wood_reach(woods, x, y))) {
                  ++n;
              }
          });
    return n >= (b.ware == ware::kGrain ? 7 : 14);
}

assets::ServiceBits Engine::house_services(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    const House* h = m_world.house(island, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
    return h != nullptr ? h->services : 0;
}

bool Engine::search_public(AiPlayer& p) {
    // The site that reaches the most houses without the service. Open1602 measures
    // from the building's centre over its whole reach (the original measures from a corner and
    // only 16 rings, S-099).
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    const auto& houses = m_world.houses();
    const std::vector<std::uint8_t>& circle = circle_table(b->radius);
    const auto gap = [](std::int32_t origin, std::int32_t size, std::int32_t house) {
        const std::int32_t w = size - 1;
        const std::int32_t first = origin + w / 2;
        const std::int32_t last = first + (w & 1);
        const std::int32_t house_last = house + 1;
        if (house_last < first) {
            return first - house_last;
        }
        if (house > last) {
            return house - last;
        }
        return 0;
    };
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_sized(g, s.x, s.y, side, static_cast<std::int32_t>(b->size_x),
                               static_cast<std::int32_t>(b->size_y), s.remaining);
            if (c.x0 > c.x1 || !can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            std::int32_t count = 0;
            std::int32_t reached = 0;
            for (const House& h : houses) {
                if (h.island != p.island || h.city != p.city) {
                    continue;
                }
                const std::int32_t dx = gap(c.x0, c.w(), h.x);
                const std::int32_t dy = gap(c.y0, c.h(), h.y);
                if (dy <= b->radius && dx <= circle[static_cast<std::size_t>(dy)]) {
                    ++reached;
                    count += (h.services & s.service) == 0 ? 1 : 0;
                }
            }
            std::int32_t score = 32 * count;
            if (!classic() && count != 0) {
                // Open1602: of two sites that serve as many houses, the one amid more of them, where
                // the town grows on around it; and a little chance, so that not every town looks
                // alike.
                score += 4 * (reached - count) + static_cast<std::int32_t>(rng().below(24));
            }
            if (score > s.best) {
                s.best = score;
                s.extra = count;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
                s.rotation = c.w() == static_cast<std::int32_t>(b->size_x) ? 0 : 1;
            }
        }
        if (!advance_cursor(p, 0)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_military(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_sized(g, s.x, s.y, side, static_cast<std::int32_t>(b->size_x),
                               static_cast<std::int32_t>(b->size_y), s.remaining);
            if (c.x0 > c.x1 || !can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            Acc acc;
            acc.score = 1000;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 9,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (d == 0 && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                          acc.road = true;
                      }
                      if (coast(t.kind) && d < 4) {
                          acc.score -= 1 << (4 - d);
                      }
                      if (t.origin && market_or_kontor(t) && d < 9) {
                          acc.market = true;
                          acc.score += (9 - d) * 64;
                      }
                      if (t.ware == ware::kGrain && d == 0) {
                          acc.score -= 32;
                      }
                      if (raw_producer(t.ware) && d < 2) {
                          acc.score -= 2 << (2 - d);
                      }
                  });
            if (acc.road && acc.market && acc.score > s.best) {
                s.best = acc.score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
                s.rotation = c.w() == static_cast<std::int32_t>(b->size_x) ? 0 : 1;
            }
        }
        if (!advance_cursor(p, 0)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_denkmal(AiPlayer& p) {
    // Next to a road, where most house tiles lie within 8 rings.
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, -1}) {
            Site c = gen_single(g, s.x, s.y, side);
            if (!can_place(p, *b, c.x0, c.y0, static_cast<std::int32_t>(b->size_x),
                           static_cast<std::int32_t>(b->size_y))) {
                continue;
            }
            any = true;
            std::int32_t count = 0;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 8,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t) {
                      count += t.prod == prod::kHouse ? 1 : 0;
                  });
            if (count > s.best) {
                s.best = count;
                s.best_x0 = s.best_x1 = c.x0;
                s.best_y0 = s.best_y1 = c.y0;
            }
        }
        if (!advance_cursor(p, 2)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::search_pirate_houses(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        for (const std::int32_t side : {0, 1, -1, -2}) {
            Site c = gen_double_row(g, s.x, s.y, side);
            if (!can_place(p, *b, c.x0, c.y0, c.w(), c.h())) {
                continue;
            }
            any = true;
            std::int32_t score = 4000;
            rings(p, RingFilter::Own, c.x0, c.y0, c.x1, c.y1, 6,
                  [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
                      if (coast(t.kind) && d < 6) {
                          score -= 4 << (6 - d);
                      }
                  });
            const std::int32_t parity = std::abs(s.x - s.ref_x) + std::abs(s.y - s.ref_y);
            score = parity_penalty(score, parity, 8);
            if (score > s.best) {
                s.best = score;
                s.best_x0 = c.x0;
                s.best_y0 = c.y0;
                s.best_x1 = c.x1;
                s.best_y1 = c.y1;
            }
        }
        if (!advance_cursor(p, 2)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Coast and mountain walks

namespace {

constexpr std::array<std::array<std::int32_t, 2>, 4> kStep{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

}  // namespace

std::int32_t Engine::coast_walk(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::int32_t* cx,
                                std::int32_t* cy, bool water) {
    // Over own forest, ground and ruins (roads only at the start) to the first beach tile.
    std::int32_t count = 1;
    const auto& d = kStep[dir & 3U];
    for (std::int32_t i = 0; i < 16; ++i) {
        const Tile t = tile(p.island, x, y);
        if (!t.valid() || t.city != p.city) {
            return -1;
        }
        if (t.kind == kind::kBeach || t.kind == kind::kWallBeach || t.kind == kind::kBeachOuter ||
            t.kind == kind::kBeachVariant || t.kind == kind::kBeachRuin) {
            if (water) {
                const Tile w1 = tile(p.island, x + d[0], y + d[1]);
                const Tile w2 = tile(p.island, x + 2 * d[0], y + 2 * d[1]);
                const auto wet = [](const Tile& w) {
                    return w.valid() && (w.kind == kind::kSea || w.kind == kind::kSurf || w.kind == kind::kSurfCorner ||
                                         w.kind == kind::kRiverMouth);
                };
                if (!wet(w1) && !wet(w2)) {
                    return -1;
                }
            }
            *cx = x;
            *cy = y;
            return count;
        }
        if (t.kind == kind::kForest || t.kind == kind::kGround || t.kind == kind::kRuin) {
            ++count;
        } else if (!((t.kind == kind::kRoad || t.kind == kind::kBridge) && count <= 2)) {
            return -1;
        }
        x += d[0];
        y += d[1];
    }
    return -1;
}

std::int32_t Engine::mountain_walk(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::uint8_t w,
                                   std::int32_t* ox, std::int32_t* oy, std::int32_t max_steps, bool bind, bool commit) {
    const auto& d = kStep[dir & 3U];
    for (std::int32_t steps = 0; steps < 16; ++steps) {
        const Tile t = tile(p.island, x, y);
        if (!t.valid() || t.city != p.city) {
            return -1;
        }
        if (t.kind == kind::kRoad || t.kind == kind::kForest || t.kind == kind::kGround || t.kind == kind::kRuin ||
            t.kind == kind::kRiver) {
            x += d[0];
            y += d[1];
            continue;
        }
        if (t.kind != kind::kSlope || steps >= max_steps) {
            return -1;
        }
        const Tile rock = tile(p.island, x + d[0], y + d[1]);
        if (!rock.valid() || rock.kind != kind::kRock) {
            return -1;
        }
        bool next = false;
        for (const auto& n : kStep) {
            const Tile o = tile(p.island, x + n[0], y + n[1]);
            next = next || (o.valid() && o.kind == kind::kSlope);
        }
        if (!next) {
            return -1;
        }
        *ox = x;
        *oy = y;
        if (w == ware::kStone || !bind) {
            return steps;
        }
        // Ore and gold: only a deposit that lies in this mountain (the original moves the largest
        // unexplored deposit of the island here, S-093).
        WorldIsland& isl = m_world.island(p.island);
        for (auto& dep : isl.header.deposits) {
            if (dep.ware == w && dep.x == rock.ox && dep.y == rock.oy && dep.amount > 0) {
                if (!commit) {
                    return steps;
                }
                dep.mask.insert(p.player);
                if (w == ware::kOre) {
                    const assets::BuildingType* deep = by_id(id::kOreMineDeep);
                    p.search.building =
                        deep != nullptr && unlocked(p, *deep) && dep.kind == 0 ? id::kOreMineDeep : id::kOreMine;
                }
                return steps;
            }
        }
        return -1;
    }
    return -1;
}

std::int32_t Engine::free_run(std::uint16_t island, std::uint8_t own, std::int32_t x, std::int32_t y,
                              std::uint8_t dir) const {
    // Straight tiles of the city or of nobody that a road can take (single river
    // tiles may be bridged); 0 below two.
    const auto& d = kStep[dir & 3U];
    std::int32_t n = 0;
    bool river = false;
    for (std::int32_t i = 0; i < 64; ++i) {
        const Tile t = tile(island, x + d[0] * i, y + d[1] * i);
        if (!t.valid() || (t.city != own && t.city != MapCell::kNoCity)) {
            break;
        }
        if (t.kind == kind::kRiver) {
            if (river) {
                break;
            }
            river = true;
            ++n;
            continue;
        }
        river = false;
        if (t.kind == kind::kRoad || t.kind == kind::kWall || t.kind == kind::kForest || t.kind == kind::kGround ||
            t.kind == kind::kRuin || t.kind == kind::kBridge) {
            ++n;
            continue;
        }
        break;
    }
    return n < 2 ? 0 : n;
}

Acc Engine::fisher_site(AiPlayer& p, std::int32_t cx, std::int32_t cy) {
    // The fish a hut on (cx, cy) reaches, a market or Kontor near, and no other hut close by.
    Acc acc;
    rings(p, RingFilter::OwnOrSea, cx, cy, cx, cy, 13, [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t d) {
        if (t.origin && market_or_kontor(t) && d < 13) {
            acc.market = true;
        }
        if (t.prod == prod::kFisher && d < 8) {
            acc.count = -100000;
            acc.score -= 0x8000;
        }
        if (t.ware == ware::kFish && d < 6) {
            ++acc.count;
            acc.score += 2 * t.growth;
        }
    });
    return acc;
}

bool Engine::fisher_path(AiPlayer& p, const assets::BuildingType& hut, std::vector<BuildItem>& road) {
    constexpr std::int32_t kFisherPath = 12;
    const IslandMap& map = m_world.islands()[p.island].map;
    const auto width = static_cast<std::int32_t>(map.width());
    const auto height = static_cast<std::int32_t>(map.height());
    if (width <= 0 || height <= 0) {
        return false;
    }
    // From the city's roads and its Kontor (a road beside it joins it).
    std::vector<std::int32_t> prev(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -2);
    std::vector<std::int32_t> dist(prev.size(), 0);
    std::vector<std::int32_t> queue;
    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
            const Tile t = tile(p.island, x, y);
            if (t.city == p.city && (t.kind == kind::kRoad || t.kind == kind::kBridge || t.prod == prod::kKontor)) {
                prev[static_cast<std::size_t>(y * width + x)] = -1;
                queue.push_back(y * width + x);
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::int32_t i = queue[head];
        const std::int32_t x = i % width;
        const std::int32_t y = i / width;
        for (std::uint8_t dir = 0; dir < 4; ++dir) {
            const std::int32_t nx = x + kStep[dir][0];
            const std::int32_t ny = y + kStep[dir][1];
            const Tile t = tile(p.island, nx, ny);
            if (!t.valid() || t.city != p.city) {
                continue;
            }
            const auto n = static_cast<std::size_t>(ny * width + nx);
            if (prev[n] != -2) {
                continue;
            }
            if (t.kind == kind::kBeach || t.kind == kind::kBeachOuter || t.kind == kind::kBeachVariant ||
                t.kind == kind::kBeachRuin) {
                prev[n] = -3;  // tried
                if (!can_place(p, hut, nx, ny, 1, 1)) {
                    continue;
                }
                const Acc acc = fisher_site(p, nx, ny);
                if (acc.count <= 13 || !acc.market) {
                    continue;
                }
                for (std::int32_t k = i; prev[static_cast<std::size_t>(k)] >= 0;
                     k = prev[static_cast<std::size_t>(k)]) {
                    road.push_back(BuildItem{id::kRoadDirt, k % width, k / width, 0, 0});
                }
                std::reverse(road.begin(), road.end());
                Search& s = p.search;
                s.best = acc.score > 0 ? acc.score : 1;
                s.best_x0 = s.best_x1 = nx;
                s.best_y0 = s.best_y1 = ny;
                s.extra = -1;
                return true;
            }
            if (dist[static_cast<std::size_t>(i)] >= kFisherPath ||
                !(t.kind == kind::kGround || t.kind == kind::kForest || t.kind == kind::kRuin)) {
                continue;
            }
            prev[n] = i;
            dist[n] = dist[static_cast<std::size_t>(i)] + 1;
            queue.push_back(ny * width + nx);
        }
    }
    return false;
}

bool Engine::search_fisher(AiPlayer& p) {
    // From the dead ends of the road to the beach, where fish are and no other fisher.
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    while (s.segment < static_cast<std::int32_t>(p.segments.size())) {
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        const std::int32_t index = s.segment;
        ++s.segment;
        if (!g.open) {
            continue;
        }
        const std::uint8_t out = static_cast<std::uint8_t>((g.dir + 2U) & 3U);
        std::int32_t sx = g.x0;
        std::int32_t sy = g.y0;
        bool extend = false;
        {
            const Tile beyond = tile(p.island, sx + kStep[out][0], sy + kStep[out][1]);
            if (beyond.valid() && (beyond.kind == kind::kRoad || beyond.kind == kind::kForest ||
                                   beyond.kind == kind::kGround || beyond.kind == kind::kRuin)) {
                sx += kStep[out][0];
                sy += kStep[out][1];
                extend = true;
            }
        }
        // Outwards and both sides (the original never tries the third direction, S-099).
        const std::array<std::uint8_t, 3> dirs = {out, static_cast<std::uint8_t>((g.dir + 3U) & 3U),
                                                  static_cast<std::uint8_t>((g.dir + 1U) & 3U)};
        for (const std::uint8_t dir : std::span(dirs).first(classic() ? 2 : 3)) {
            std::int32_t cx = 0;
            std::int32_t cy = 0;
            const std::int32_t dist = coast_walk(p, sx, sy, dir, &cx, &cy, false);
            if (dist < 0 || dist >= 7) {
                continue;
            }
            if (!can_place(p, *b, cx, cy, 1, 1)) {
                continue;
            }
            const Acc acc = fisher_site(p, cx, cy);
            if ((acc.count > 13 || p.pirates) && acc.market && acc.score > s.best) {
                s.best = acc.score;
                s.best_x0 = s.best_x1 = cx;
                s.best_y0 = s.best_y1 = cy;
                s.extra = dist < 2 ? -1 : index;
                s.phase = extend ? 1 : 0;
            }
        }
        return false;  // one road end per step
    }
    return true;
}

bool Engine::search_shipyard(AiPlayer& p) {
    // The coast nearest to a road, with room for the yard; in Open1602 only at the open
    // sea.
    const assets::BuildingType* b = by_id(p.search.building);
    Search& s = p.search;
    if (b == nullptr) {
        return true;
    }
    for (std::int32_t visited = 1;; ++visited) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return true;
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        bool any = false;
        std::vector<std::uint8_t> dirs{static_cast<std::uint8_t>((g.dir + 1U) & 3U),
                                       static_cast<std::uint8_t>((g.dir + 3U) & 3U)};
        if (s.remaining == g.length) {
            dirs.push_back(static_cast<std::uint8_t>((g.dir + 2U) & 3U));
        }
        for (const std::uint8_t dir : dirs) {
            std::int32_t cx = 0;
            std::int32_t cy = 0;
            const std::int32_t dist = coast_walk(p, s.x + kStep[dir][0], s.y + kStep[dir][1], dir, &cx, &cy, true);
            if (dist < 0 || dist > 6) {
                continue;
            }
            // A yard whose water side lies on this beach tile.
            const auto w = static_cast<std::int32_t>(std::max(b->size_x, b->size_y));
            for (std::int32_t oy = cy - w + 1; oy <= cy; ++oy) {
                for (std::int32_t ox = cx - w + 1; ox <= cx; ++ox) {
                    if (!inside(p.island, ox, oy)) {
                        continue;
                    }
                    BuildOrder order;
                    order.player = p.player;
                    order.island = p.island;
                    order.items.push_back(BuildItem{b->id, ox, oy, 0, 0});
                    const BuildPlan plan = plan_build(m_world, m_buildings, order);
                    const PlannedBuild& item = plan.items.front();
                    const BuildRefusal rf = item.refusal;
                    if (plan.city != p.city ||
                        !(rf == BuildRefusal::None || rf == BuildRefusal::Money || rf == BuildRefusal::Tools ||
                          rf == BuildRefusal::Wood || rf == BuildRefusal::Bricks)) {
                        continue;
                    }
                    // Open1602: the open sea in front of the yard, as for the Kontor. The
                    // original takes any coast, also the shore of a lake inside the island, where the
                    // yard builds ships that never reach the sea.
                    if (!classic()) {
                        std::uint32_t fw = 0;
                        std::uint32_t fh = 0;
                        rotated_size(*b, item.rotation, fw, fh);
                        if (!open_sea_ahead(p.island, item.x, item.y, fw, fh, item.rotation)) {
                            continue;
                        }
                    }
                    any = true;
                    const std::int32_t score = 7 - dist;
                    if (score > s.best) {
                        s.best = score;
                        s.best_x0 = ox;
                        s.best_y0 = oy;
                        s.best_x1 = ox;
                        s.best_y1 = oy;
                        s.rotation = dir;
                        s.stub_x = s.x + kStep[dir][0];
                        s.stub_y = s.y + kStep[dir][1];
                        s.extra = dist;
                    }
                }
            }
        }
        if (!advance_cursor(p, 0)) {
            return true;
        }
        if (any || visited >= kSearchPositions) {
            return false;
        }
    }
}

bool Engine::mine_path(AiPlayer& p, std::uint8_t w, std::vector<BuildItem>& road) {
    // Open1602: when no straight road reaches a slope (search_mine, search_mine_wide), the
    // shortest way over the city's open ground from any of its roads to a slope with the deposit's
    // rock behind it, up to kMinePath tiles; the road along it goes to `road`, the slope and the
    // mine's building to the search as search_mine would leave them.
    constexpr std::int32_t kMinePath = 24;
    const IslandMap& map = m_world.islands()[p.island].map;
    const auto width = static_cast<std::int32_t>(map.width());
    const auto height = static_cast<std::int32_t>(map.height());
    if (width <= 0 || height <= 0) {
        return false;
    }
    std::vector<std::int32_t> prev(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -2);
    std::vector<std::int32_t> dist(prev.size(), 0);
    std::vector<std::int32_t> queue;
    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
            const Tile t = tile(p.island, x, y);
            if (t.city == p.city && (t.kind == kind::kRoad || t.kind == kind::kBridge)) {
                prev[static_cast<std::size_t>(y * width + x)] = -1;
                queue.push_back(y * width + x);
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::int32_t i = queue[head];
        if (dist[static_cast<std::size_t>(i)] >= kMinePath) {
            continue;
        }
        const std::int32_t x = i % width;
        const std::int32_t y = i / width;
        for (std::uint8_t dir = 0; dir < 4; ++dir) {
            const std::int32_t nx = x + kStep[dir][0];
            const std::int32_t ny = y + kStep[dir][1];
            const Tile t = tile(p.island, nx, ny);
            if (!t.valid() || t.city != p.city) {
                continue;
            }
            std::int32_t ox = 0;
            std::int32_t oy = 0;
            if (t.kind == kind::kSlope) {
                if (mountain_walk(p, nx, ny, dir, w, &ox, &oy, 1, true, false) != 0) {
                    continue;
                }
                // Found: bind the deposit as search_mine does, and lay the road back to the roads.
                mountain_walk(p, nx, ny, dir, w, &ox, &oy, 1, true, true);
                for (std::int32_t k = i; prev[static_cast<std::size_t>(k)] != -1;
                     k = prev[static_cast<std::size_t>(k)]) {
                    road.push_back(BuildItem{id::kRoadDirt, k % width, k / width, 0, 0});
                }
                std::reverse(road.begin(), road.end());
                Search& s = p.search;
                s.best = 1;
                s.best_x1 = ox;
                s.best_y1 = oy;
                s.rotation = dir;
                s.extra = 0;
                return true;
            }
            const auto n = static_cast<std::size_t>(ny * width + nx);
            if (prev[n] != -2 || !(t.kind == kind::kGround || t.kind == kind::kForest || t.kind == kind::kRuin)) {
                continue;
            }
            prev[n] = i;
            dist[n] = dist[static_cast<std::size_t>(i)] + 1;
            queue.push_back(ny * width + nx);
        }
    }
    return false;
}

bool Engine::join_path(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h,
                       std::vector<BuildItem>& road) {
    constexpr std::int32_t kJoinPath = 24;
    road.clear();
    const IslandMap& map = m_world.islands()[p.island].map;
    const auto width = static_cast<std::int32_t>(map.width());
    const auto height = static_cast<std::int32_t>(map.height());
    if (width <= 0 || height <= 0 || w <= 0 || h <= 0) {
        return false;
    }
    const auto own_road = [&](std::int32_t tx, std::int32_t ty) {
        const Tile t = tile(p.island, tx, ty);
        return t.valid() && t.city == p.city && (t.kind == kind::kRoad || t.kind == kind::kBridge);
    };
    // The tiles beside the building (not its corners), where a road reaches its door.
    const auto beside = [&](std::int32_t tx, std::int32_t ty) {
        const bool row = tx >= x && tx < x + w && (ty == y - 1 || ty == y + h);
        const bool column = ty >= y && ty < y + h && (tx == x - 1 || tx == x + w);
        return row || column;
    };
    for (std::int32_t i = 0; i < w; ++i) {
        if (own_road(x + i, y - 1) || own_road(x + i, y + h)) {
            return true;
        }
    }
    for (std::int32_t j = 0; j < h; ++j) {
        if (own_road(x - 1, y + j) || own_road(x + w, y + j)) {
            return true;
        }
    }
    std::vector<std::int32_t> prev(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -2);
    std::vector<std::int32_t> dist(prev.size(), 0);
    std::vector<std::int32_t> queue;
    for (std::int32_t ty = 0; ty < height; ++ty) {
        for (std::int32_t tx = 0; tx < width; ++tx) {
            if (own_road(tx, ty)) {
                prev[static_cast<std::size_t>(ty * width + tx)] = -1;
                queue.push_back(ty * width + tx);
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::int32_t i = queue[head];
        if (dist[static_cast<std::size_t>(i)] >= kJoinPath) {
            continue;
        }
        for (std::uint8_t dir = 0; dir < 4; ++dir) {
            const std::int32_t nx = i % width + kStep[dir][0];
            const std::int32_t ny = i / width + kStep[dir][1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                continue;
            }
            const auto n = static_cast<std::size_t>(ny * width + nx);
            const Tile t = tile(p.island, nx, ny);
            if (prev[n] != -2 || !t.valid() || t.city != p.city ||
                !(t.kind == kind::kGround || t.kind == kind::kForest || t.kind == kind::kRuin)) {
                continue;
            }
            prev[n] = i;
            dist[n] = dist[static_cast<std::size_t>(i)] + 1;
            if (beside(nx, ny)) {
                for (auto k = static_cast<std::int32_t>(n); prev[static_cast<std::size_t>(k)] != -1;
                     k = prev[static_cast<std::size_t>(k)]) {
                    road.push_back(BuildItem{id::kRoadDirt, k % width, k / width, 0, 0});
                }
                std::reverse(road.begin(), road.end());
                return true;
            }
            queue.push_back(ny * width + nx);
        }
    }
    return false;
}

bool Engine::join_roads(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
    std::vector<BuildItem> road;
    if (!join_path(p, x, y, w, h, road)) {
        log(p, "no road reaches the building at %d,%d", x, y);
        return false;
    }
    if (road.empty()) {
        return true;
    }
    const std::size_t want = road.size();
    const std::size_t placed = build(p, std::move(road), false);
    log(p, "road of %zu tiles to the building at %d,%d: %zu placed", want, x, y, placed);
    return placed == want;
}

bool Engine::search_mine(AiPlayer& p, std::uint8_t w) {
    // One road tile per call from the start of the runs; the first mountain reached
    // within seven tiles ends the search.
    Search& s = p.search;
    for (std::int32_t k = 0; k < 4; ++k) {
        if (s.segment >= static_cast<std::int32_t>(p.segments.size())) {
            return classic() || search_mine_wide(p, w);
        }
        const Segment& g = p.segments[static_cast<std::size_t>(s.segment)];
        for (const std::uint8_t dir :
             {static_cast<std::uint8_t>((g.dir + 2U) & 3U), static_cast<std::uint8_t>((g.dir + 1U) & 3U),
              static_cast<std::uint8_t>((g.dir + 3U) & 3U)}) {
            std::int32_t ox = 0;
            std::int32_t oy = 0;
            const std::int32_t steps = mountain_walk(p, s.x + kStep[dir][0], s.y + kStep[dir][1], dir, w, &ox, &oy);
            if (steps >= 0 && steps < 8) {
                s.best = (8 - steps) * 16;
                s.best_x0 = s.x;
                s.best_y0 = s.y;
                s.best_x1 = ox;
                s.best_y1 = oy;
                s.rotation = dir;
                s.extra = steps;
                return true;
            }
        }
        // Forwards along the run.
        if (s.remaining <= 0) {
            ++s.segment;
            if (s.segment < static_cast<std::int32_t>(p.segments.size())) {
                const Segment& n = p.segments[static_cast<std::size_t>(s.segment)];
                s.x = n.x0;
                s.y = n.y0;
                s.remaining = n.length;
            }
        } else {
            --s.remaining;
            s.x += g.dx;
            s.y += g.dy;
        }
    }
    return false;
}

bool Engine::search_mine_wide(AiPlayer& p, std::uint8_t w) {
    // Open1602 (S-099): the original only walks from the road runs and only up to seven tiles, so a
    // market in front of the hill or a river at its foot leaves many cities without a quarry (and
    // therefore without bricks) for good. When that walk found nothing, every road tile of the city
    // is tried in all four directions with up to twelve tiles of new road; the shortest road wins,
    // the first in row order among equals.
    //
    // A few rows per step (TB-15): `phase` is the next row and `extra` the steps of the best road
    // so far (`best` > 0 once there is one). The deposit is only bound once the scan is done.
    Search& s = p.search;
    const IslandMap& map = m_world.islands()[p.island].map;
    const std::int32_t height = static_cast<std::int32_t>(map.height());
    const std::int32_t end = std::min(height, s.phase + kMineRows);
    for (std::int32_t y = s.phase; y < end; ++y) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(map.width()); ++x) {
            const Tile t = tile(p.island, x, y);
            if (t.city != p.city || (t.kind != kind::kRoad && t.kind != kind::kBridge)) {
                continue;
            }
            for (std::uint8_t dir = 0; dir < 4; ++dir) {
                std::int32_t ox = 0;
                std::int32_t oy = 0;
                const std::int32_t steps =
                    mountain_walk(p, x + kStep[dir][0], y + kStep[dir][1], dir, w, &ox, &oy, 13, true, false);
                if (steps >= 0 && (s.best < 1 || steps < s.extra)) {
                    s.best = std::max<std::int32_t>(1, (14 - steps) * 8);
                    s.best_x0 = x;
                    s.best_y0 = y;
                    s.best_x1 = ox;
                    s.best_y1 = oy;
                    s.rotation = dir;
                    s.extra = steps;
                }
            }
        }
    }
    s.phase = end;
    if (end < height) {
        return false;
    }
    if (s.best > 0) {
        // The world may have changed since the row was scanned: walk once more and bind the deposit.
        const auto dir = static_cast<std::uint8_t>(s.rotation & 3);
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        const std::int32_t steps =
            mountain_walk(p, s.best_x0 + kStep[dir][0], s.best_y0 + kStep[dir][1], dir, w, &ox, &oy, 13, true);
        if (steps < 0) {
            s.best = 0;
        } else {
            s.best = std::max<std::int32_t>(1, (14 - steps) * 8);
            s.best_x1 = ox;
            s.best_y1 = oy;
            s.extra = steps;
        }
    }
    return true;
}

}  // namespace o1602::sim::ai
