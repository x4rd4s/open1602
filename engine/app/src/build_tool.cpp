// SPDX-License-Identifier: GPL-3.0-or-later
#include "build_tool.hpp"

#include <algorithm>
#include <cstdlib>

namespace o1602::app {

namespace {

constexpr std::int32_t kKindRoad = 1;
constexpr std::int32_t kKindWall = 4;
constexpr std::int32_t kKindWallBeach = 5;
constexpr std::int32_t kKindForest = 10;
constexpr std::int32_t kKindGround = 11;
constexpr std::int32_t kKindSquare = 13;
constexpr std::int32_t kKindHq = 35;
constexpr std::int32_t kProdGrowing = 10;
constexpr std::size_t kMaxItems = 512;  // the original's drag list holds 0x200 entries

std::optional<sim::DragLine> line_kind(const assets::BuildingType& b) noexcept {
    if (b.kind == kKindRoad) {
        return sim::DragLine::Road;
    }
    if (b.kind == kKindWall || b.kind == kKindWallBeach) {
        return sim::DragLine::Wall;
    }
    return std::nullopt;
}
bool area_kind(const assets::BuildingType& b) noexcept {
    return b.prod_kind == kProdGrowing || b.kind == kKindGround || b.kind == kKindSquare;
}

/// A well mixed number for a tile (splitmix64 finaliser): the same tile and seed always give the
/// same number, so a tree keeps its look while the drag is shown.
std::uint32_t tile_hash(std::uint32_t seed, std::uint16_t island, std::int32_t x, std::int32_t y) noexcept {
    std::uint64_t z = (std::uint64_t{seed} << 32U) ^ (std::uint64_t{island} << 24U) ^
                      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 12U) ^
                      static_cast<std::uint32_t>(y);
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return static_cast<std::uint32_t>(z ^ (z >> 31U));
}

}  // namespace

BuildTool::BuildTool(const assets::BuildingTable& buildings) : m_buildings(&buildings) {
    for (const assets::BuildingType& b : buildings.types()) {
        const auto& c = b.build_cost;
        if (b.destroyed || (c.money == 0 && c.tools == 0 && c.wood == 0 && c.bricks == 0)) {
            continue;  // terrain, ruins and other things nobody builds
        }
        if (b.land_record != assets::BuildingType::kNoRecord || buildings.by_id(b.id) != &b) {
            continue;  // beach variants come from building the land record on the beach
        }
        if (b.build_gfx == 0) {
            continue;  // no icon in the build menu: ground and other things of the editor
        }
        m_list.push_back(&b);
    }
    std::stable_sort(m_list.begin(), m_list.end(), [](const assets::BuildingType* a, const assets::BuildingType* b) {
        return a->build_gfx < b->build_gfx;
    });
}

void BuildTool::set_mode(Mode mode) noexcept {
    m_mode = mode;
    if (mode != Mode::Build) {
        m_ship.reset();
    }
    m_drag_start.reset();
}

void BuildTool::cycle(int step) noexcept {
    if (m_list.empty()) {
        return;
    }
    const auto n = static_cast<std::int64_t>(m_list.size());
    m_selected = static_cast<std::size_t>(((static_cast<std::int64_t>(m_selected) + step) % n + n) % n);
}

void BuildTool::rotate(int step) noexcept {
    m_rotation = static_cast<std::uint8_t>((m_rotation + step) & 3);
}

bool BuildTool::select(std::uint32_t id) noexcept {
    for (std::size_t i = 0; i < m_list.size(); ++i) {
        if (m_list[i]->id == id) {
            m_selected = i;
            m_ship.reset();
            return true;
        }
    }
    return false;
}

bool BuildTool::select_kontor(std::uint32_t ship) noexcept {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < m_list.size(); ++i) {
        if (m_list[i]->kind == kKindHq && (!best || m_list[i]->id < m_list[*best]->id)) {
            best = i;
        }
    }
    if (!best) {
        return false;
    }
    m_selected = *best;
    m_ship = ship;
    return true;
}

void BuildTool::press(const Spot& spot) noexcept {
    m_drag_start = spot;
}

const assets::BuildingType* BuildTool::selected() const noexcept {
    return m_list.empty() ? nullptr : m_list[m_selected];
}

bool BuildTool::hatched_drag() const noexcept {
    const assets::BuildingType* b = selected();
    return m_mode == Mode::Build && m_drag_start && b != nullptr && b->prod_kind == kProdGrowing;
}

std::optional<sim::BuildOrder> BuildTool::build_order(const sim::World& world, sim::PlayerId player,
                                                      const Spot& spot) const {
    const assets::BuildingType* b = selected();
    if (m_mode != Mode::Build || b == nullptr) {
        return std::nullopt;
    }
    sim::BuildOrder order;
    order.orient_to_road = m_orient_to_road;
    order.player = player;
    order.island = spot.island;
    const Spot start = m_drag_start && m_drag_start->island == spot.island ? *m_drag_start : spot;
    // Trees come in RandAnz variants RandAdd records apart; the original's menu draws one per tile
    // with rand() (docs/re/construction.md: the variant is part of the order).
    const auto variant = [&](std::int32_t x, std::int32_t y) {
        if (b->kind != kKindForest || b->rand_count <= 1) {
            return b->id;
        }
        const std::uint32_t pick = tile_hash(m_variant_seed, spot.island, x, y) % b->rand_count;
        const assets::BuildingType* v = m_buildings->by_record(b->record + pick * b->rand_add);
        return v != nullptr ? v->id : b->id;
    };
    const auto add = [&](std::int32_t x, std::int32_t y) {
        if (order.items.size() < kMaxItems) {
            order.items.push_back(sim::BuildItem{variant(x, y), x, y, m_rotation, 0});
        }
    };
    if (const auto line = line_kind(*b); m_drag_start && line) {
        // The original's path search from the drag start to the cursor (0x0040AB20).
        for (const sim::DragStep& step : sim::drag_path(world, *m_buildings, spot.island, *line, {start.x, start.y},
                                                        {spot.x, spot.y}, m_rotation)) {
            if (order.items.size() < kMaxItems) {
                order.items.push_back(sim::BuildItem{b->id, step.x, step.y, step.rotation, 0});
            }
        }
    } else if (m_drag_start && area_kind(*b)) {
        for (std::int32_t y = std::min(start.y, spot.y); y <= std::max(start.y, spot.y); ++y) {
            for (std::int32_t x = std::min(start.x, spot.x); x <= std::max(start.x, spot.x); ++x) {
                add(x, y);
            }
        }
    } else {
        add(spot.x, spot.y);
    }
    // A Kontor outside every city is built from the player's nearest ship.
    if (b->kind == kKindHq) {
        const sim::WorldIsland& island = world.islands()[spot.island];
        const std::int64_t wx = static_cast<std::int64_t>(island.x) + spot.x;
        const std::int64_t wy = static_cast<std::int64_t>(island.y) + spot.y;
        std::int64_t best = -1;
        const sim::Ship* chosen = m_ship ? world.ship(*m_ship) : nullptr;
        if (chosen != nullptr && chosen->owner != player) {
            chosen = nullptr;  // sunk and reused, or taken: the nearest one pays
        }
        if (chosen != nullptr) {
            order.ship = chosen->id;
            best = 0;
        }
        for (std::size_t i = 0; i < world.ships().size() && chosen == nullptr; ++i) {
            const sim::Ship& s = world.ships()[i];
            if (s.owner != player) {
                continue;
            }
            const std::int64_t d = std::abs(s.x - wx) + std::abs(s.y - wy);
            if (best < 0 || d < best) {
                best = d;
                order.ship = s.id;
            }
        }
        const sim::MapCell& cell =
            island.map.contains(spot.x, spot.y)
                ? island.map.top(static_cast<std::uint32_t>(spot.x), static_cast<std::uint32_t>(spot.y))
                : sim::MapCell{};
        if (cell.city != sim::MapCell::kNoCity) {
            order.ship.reset();  // an upgrade in the city pays from the warehouse
        }
    }
    return order;
}

std::optional<sim::DemolishOrder> BuildTool::demolish_order(sim::PlayerId player, const Spot& spot) const {
    if (m_mode != Mode::Demolish) {
        return std::nullopt;
    }
    const Spot start = m_drag_start && m_drag_start->island == spot.island ? *m_drag_start : spot;
    return sim::DemolishOrder{player, spot.island, {start.x, start.y}, {spot.x, spot.y}};
}

std::string BuildTool::describe() const {
    if (m_mode == Mode::Demolish) {
        return "demolish (drag: roads, fields, trees; click: buildings)";
    }
    const assets::BuildingType* b = selected();
    if (m_mode != Mode::Build || b == nullptr) {
        return {};
    }
    const auto& c = b->build_cost;
    std::string s = "build " + std::to_string(b->id) + " (" + std::to_string(m_selected + 1) + "/" +
                    std::to_string(m_list.size()) + ", rotation " + std::to_string(m_rotation) + ") " +
                    std::to_string(c.money) + " gold";
    const auto goods = [&](std::uint32_t amount, const char* name) {
        if (amount != 0) {
            s += ", " + std::to_string(amount / 32) + " " + name;
        }
    };
    goods(c.tools, "tools");
    goods(c.wood, "wood");
    goods(c.bricks, "bricks");
    goods(c.cannons, "cannons");
    return s;
}

}  // namespace o1602::app
