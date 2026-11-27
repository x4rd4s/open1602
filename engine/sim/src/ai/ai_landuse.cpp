// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602 (PB.3): the land use of a computer's city, derived from the state of the game: its town
// zone, the work circles of its producers and what stands on each tile. No decision reads it yet.

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

namespace o1602::sim::ai {

namespace {

bool grown_record(const assets::BuildingType& b) {
    return b.prod_kind == prod::kRipe || b.prod_kind == prod::kGrowing;
}

}  // namespace

bool in_zone(const TownZone& z, std::int32_t x, std::int32_t y) noexcept {
    if (!z.valid) {
        return false;
    }
    const std::int64_t dx = x - z.cx;
    const std::int64_t dy = y - z.cy;
    const std::int64_t r = std::int64_t{z.radius} + 1;
    return dx * dx + dy * dy < r * r;  // isqrt(dx * dx + dy * dy) <= radius
}

std::int32_t work_land_under(const LandUse& u, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    std::int32_t n = 0;
    for (std::int32_t y = y0; y <= y1; ++y) {
        for (std::int32_t x = x0; x <= x1; ++x) {
            const std::uint8_t b = u.at(x, y);
            n += (b & LandUse::bit(LandUse::Use::Free)) != 0 && (b & LandUse::bit(LandUse::Use::Work)) != 0 ? 1 : 0;
        }
    }
    return n;
}

ZoneOverlap zone_overlap(const LandUse& u) {
    ZoneOverlap o;
    for (const std::uint32_t type : u.workers) {
        if (std::ranges::none_of(o.by_type, [&](const auto& e) { return e.first == type; })) {
            o.by_type.emplace_back(type, 0);
        }
    }
    for (std::int32_t y = u.y0; y < u.y0 + u.h; ++y) {
        for (std::int32_t x = u.x0; x < u.x0 + u.w; ++x) {
            const std::uint8_t b = u.at(x, y);
            if ((b & LandUse::bit(LandUse::Use::Zone)) == 0 || (b & LandUse::bit(LandUse::Use::Free)) == 0) {
                continue;
            }
            ++o.zone_free;
            o.zone_work += (b & LandUse::bit(LandUse::Use::Work)) != 0 ? 1 : 0;
            for (auto& [type, tiles] : o.by_type) {
                for (std::size_t i = 0; i < u.circles.size(); ++i) {
                    if (u.workers[i] == type && in_circle(u.circles[i], x, y)) {
                        ++tiles;
                        break;
                    }
                }
            }
        }
    }
    return o;
}

std::vector<Catchment> Engine::own_circles(const AiPlayer& p,
                                           const std::function<bool(const assets::BuildingType&)>& keep,
                                           std::vector<std::uint32_t>* types) const {
    std::vector<Catchment> circles;
    const IslandMap& map = m_world.islands()[p.island].map;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != p.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        const assets::BuildingType* b = cell.empty() ? nullptr : m_buildings.by_sprite(cell.sprite);
        if (cell.city != p.city || b == nullptr || b->destroyed || !keep(*b)) {
            continue;
        }
        circles.push_back(catchment_of(*b, cell.rotation, TilePos{pr.x, pr.y}));
        if (types != nullptr) {
            types->push_back(b->id);
        }
    }
    return circles;
}

std::vector<Catchment> Engine::work_circles(const AiPlayer& p, std::vector<std::uint32_t>* types) const {
    return own_circles(
        p,
        [&](const assets::BuildingType& b) {
            return b.interval != 0 && !grown_record(b) && b.radius != 0 && b.raw_ware != 0 &&
                   !catchment_covers_water(b) &&
                   std::ranges::any_of(m_buildings.types(), [&](const assets::BuildingType& t) {
                       return grown_record(t) && t.ware == b.raw_ware;
                   });
        },
        types);
}

const LandUse& Engine::land_use(AiPlayer& p) {
    CityScans& known = city_scans(p.island, p.city);
    const std::uint64_t revision = island_revision(p.island);
    const bool seeded = p.town_seed != 0;
    if (known.land_revision == revision && known.land_seeded == seeded) {
        return known.land;
    }
    known.land_revision = revision;
    known.land_seeded = seeded;
    LandUse& u = known.land;
    u = LandUse{};
    // The zone only once the player drew its town style: taking it here draws nothing.
    if (!classic() && seeded) {
        u.zone = town_zone(p);
    }
    u.circles = work_circles(p, &u.workers);
    const IslandMap& map = m_world.islands()[p.island].map;  // ai-view: eigen: the own island's terrain
    const auto mw = static_cast<std::int32_t>(map.width());
    const auto mh = static_cast<std::int32_t>(map.height());
    std::int32_t x0 = mw;
    std::int32_t y0 = mh;
    std::int32_t x1 = -1;
    std::int32_t y1 = -1;
    for (std::int32_t y = 0; y < mh; ++y) {
        for (std::int32_t x = 0; x < mw; ++x) {
            if (map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).city == p.city) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    }
    if (u.zone.valid) {
        x0 = std::max(0, std::min(x0, u.zone.cx - u.zone.radius));
        y0 = std::max(0, std::min(y0, u.zone.cy - u.zone.radius));
        x1 = std::min(mw - 1, std::max(x1, u.zone.cx + u.zone.radius));
        y1 = std::min(mh - 1, std::max(y1, u.zone.cy + u.zone.radius));
    }
    if (x1 < x0 || y1 < y0) {
        return u;
    }
    u.x0 = x0;
    u.y0 = y0;
    u.w = x1 - x0 + 1;
    u.h = y1 - y0 + 1;
    u.bits.assign(static_cast<std::size_t>(u.w) * static_cast<std::size_t>(u.h), 0);
    for (std::int32_t y = y0; y <= y1; ++y) {
        for (std::int32_t x = x0; x <= x1; ++x) {
            const MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            std::uint8_t b = 0;
            if (in_zone(u.zone, x, y)) {
                b |= LandUse::bit(LandUse::Use::Zone);
            }
            if (std::ranges::any_of(u.circles, [&](const Catchment& c) { return in_circle(c, x, y); })) {
                b |= LandUse::bit(LandUse::Use::Work);
            }
            // A cell without a record is free land; a record the table does not know is nothing.
            const Tile t = cell.empty() ? Tile{} : tile_of(cell, x, y);
            if (cell.empty() || (t.valid() && (t.kind == kind::kGround || t.kind == kind::kForest ||
                                               t.prod == prod::kRipe || t.prod == prod::kGrowing))) {
                b |= LandUse::bit(LandUse::Use::Free);
            } else if (t.valid() && cell.city == p.city) {
                if (t.kind == kind::kRoad || t.kind == kind::kBridge) {
                    b |= LandUse::bit(LandUse::Use::Road);
                } else if (t.prod == prod::kHouse) {
                    b |= LandUse::bit(LandUse::Use::House);
                } else {
                    b |= LandUse::bit(LandUse::Use::Built);
                }
            }
            u.bits[static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(u.w) +
                   static_cast<std::size_t>(x - x0)] = b;
        }
    }
    return u;
}

}  // namespace o1602::sim::ai
