// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/sea_paths.hpp"

#include <algorithm>
#include <optional>

#include "o1602/sim/pathfinding.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kBlocked = 1;
constexpr std::uint8_t kVisited = 4;
constexpr std::uint32_t kStraight = kStepStraight;  // open sea (0x0046F8A0 class 0)
constexpr std::uint32_t kDiagonal = kStepDiagonal;
constexpr std::uint32_t kSoftStraight = 500;  // around a ship lying still (cost class 0x7E)
constexpr std::uint32_t kSoftDiagonal = 711;
constexpr std::uint32_t kDangerStraight = 504;  // near hostile towers (cost class 0x7F)
constexpr std::uint32_t kDangerDiagonal = 716;
constexpr std::uint8_t kClassSoft = 1;
constexpr std::uint8_t kClassDanger = 2;
constexpr std::uint32_t kInfinite = 0xFFFFFFFFU;

// Neighbour order of the original's wavefront: the diagonals NW, NE, SW, SE first, then N, S, W, E.
constexpr std::uint8_t kOrder[8] = {7, 1, 5, 3, 0, 4, 6, 2};

}  // namespace

bool sailable_building(const assets::BuildingType& building) noexcept {
    return building.kind == building_kind::kSea;
}

void SeaMap::rebuild(const World& world, const assets::BuildingTable& buildings) {
    m_width = world.width();
    m_height = world.height();
    m_cells.assign(std::size_t{m_width} * m_height, 0);
    for (std::uint32_t y = 0; y < m_height; ++y) {
        for (std::uint32_t x = 0; x < m_width; ++x) {
            m_cells[std::size_t{y} * m_width + x] = world.cell(x, y).island == World::kSea ? 1 : 0;
        }
    }
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        update_island(world, buildings, static_cast<std::uint16_t>(i));
    }
}

void SeaMap::update_island(const World& world, const assets::BuildingTable& buildings, std::uint16_t index) {
    if (index >= world.islands().size()) {
        return;
    }
    const WorldIsland& island = world.islands()[index];
    const IslandMap& map = island.map;
    for (std::uint32_t ly = 0; ly < map.height(); ++ly) {
        for (std::uint32_t lx = 0; lx < map.width(); ++lx) {
            const std::uint32_t wx = island.x + lx;
            const std::uint32_t wy = island.y + ly;
            if (wx >= m_width || wy >= m_height) {
                continue;
            }
            const MapCell& c = map.top(lx, ly);
            const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
            m_cells[std::size_t{wy} * m_width + wx] = b != nullptr && sailable_building(*b) ? 1 : 0;
        }
    }
}

SeaCourse SeaPlanner::plan(const SeaMap& sea, const SeaPlan& r) {
    SeaCourse course;
    const std::int32_t size = 2 * r.radius + 1;
    if (r.radius <= 0) {
        return course;
    }
    const std::int32_t x0 = r.start_x - r.radius + r.shift_x;
    const std::int32_t y0 = r.start_y - r.radius + r.shift_y;
    const auto cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    m_cost.assign(cells, kInfinite);
    m_state.assign(cells, 0);
    m_class.assign(cells, 0);
    const auto inside = [&](std::int32_t x, std::int32_t y) {
        return x >= x0 && y >= y0 && x < x0 + size && y < y0 + size;
    };
    const auto at = [&](std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x - x0);
    };
    if (!inside(r.start_x, r.start_y)) {
        return course;
    }
    for (std::int32_t y = y0; y < y0 + size; ++y) {
        for (std::int32_t x = x0; x < x0 + size; ++x) {
            if (!sea.sailable(x, y)) {
                m_state[at(x, y)] = kBlocked;
            }
        }
    }
    // Water near hostile towers first; the ships close by are marked over it (0x00455A20).
    for (const SeaPlan::Danger& d : r.danger) {
        for (std::int32_t y = std::max(d.y0, y0); y <= std::min(d.y1, y0 + size - 1); ++y) {
            for (std::int32_t x = std::max(d.x0, x0); x <= std::min(d.x1, x0 + size - 1); ++x) {
                m_class[at(x, y)] = kClassDanger;
            }
        }
    }
    // Other ships (0x004560A0): 3 x 3 squares, softened after a failure for ships lying still.
    for (const SeaPlan::Obstacle& o : r.ships) {
        if (o.x == r.start_x && o.y == r.start_y) {
            continue;
        }
        const bool soft = r.soft_ships && !o.moving;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const std::int32_t x = o.x + dx;
                const std::int32_t y = o.y + dy;
                if (!inside(x, y)) {
                    continue;
                }
                if (!soft || (dx == 0 && dy == 0)) {
                    m_state[at(x, y)] = kBlocked;
                } else {
                    m_class[at(x, y)] = kClassSoft;
                }
            }
        }
    }
    const std::size_t start = at(r.start_x, r.start_y);
    m_state[start] = 0;  // the ship's own tile is always open
    if (r.goal && r.goal(r.start_x, r.start_y)) {
        course.reached_goal = true;
        course.end_x = r.start_x;
        course.end_y = r.start_y;
        return course;
    }

    // Dijkstra with a binary heap; ties in insertion order.
    m_heap.clear();
    std::uint32_t seq = 0;
    const auto push = [&](std::uint32_t cost, std::size_t cell) {
        m_heap.push_back(Node{cost, seq++, static_cast<std::uint32_t>(cell)});
        std::push_heap(m_heap.begin(), m_heap.end(),
                       [](const Node& a, const Node& b) { return a.cost != b.cost ? a.cost > b.cost : a.seq > b.seq; });
    };
    const auto pop = [&]() {
        std::pop_heap(m_heap.begin(), m_heap.end(),
                      [](const Node& a, const Node& b) { return a.cost != b.cost ? a.cost > b.cost : a.seq > b.seq; });
        const Node n = m_heap.back();
        m_heap.pop_back();
        return n;
    };
    m_cost[start] = 0;
    push(0, start);
    std::optional<std::size_t> goal;
    std::optional<std::size_t> edge;  // best visited cell on the window edge towards the aim point
    std::int32_t edge_distance = sea_distance(r.aim_x - r.start_x, r.aim_y - r.start_y);
    const bool aim_outside = !inside(r.aim_x, r.aim_y);
    while (!m_heap.empty()) {
        const Node n = pop();
        if ((m_state[n.cell] & kVisited) != 0) {
            continue;
        }
        m_state[n.cell] |= kVisited;
        const auto cx = static_cast<std::int32_t>(n.cell % static_cast<std::size_t>(size)) + x0;
        const auto cy = static_cast<std::int32_t>(n.cell / static_cast<std::size_t>(size)) + y0;
        if (n.cell != start && r.goal && r.goal(cx, cy)) {
            goal = n.cell;
            break;
        }
        if (aim_outside && (cx == x0 || cy == y0 || cx == x0 + size - 1 || cy == y0 + size - 1)) {
            const std::int32_t d = sea_distance(r.aim_x - cx, r.aim_y - cy);
            if (d < edge_distance) {
                edge_distance = d;
                edge = n.cell;
            }
        }
        for (const std::uint8_t dir : kOrder) {
            const std::int32_t nx = cx + step_x(dir);
            const std::int32_t ny = cy + step_y(dir);
            if (!inside(nx, ny)) {
                continue;
            }
            const std::size_t nc = at(nx, ny);
            if ((m_state[nc] & (kBlocked | kVisited)) != 0) {
                continue;
            }
            const bool diagonal = (dir & 1U) != 0;
            if (diagonal) {
                // No cut corners: both straight neighbours must be open.
                if ((m_state[at(cx + step_x(dir), cy)] & kBlocked) != 0 ||
                    (m_state[at(cx, cy + step_y(dir))] & kBlocked) != 0) {
                    continue;
                }
            }
            // The cost is that of the tile left (as the land search).
            std::uint32_t step = diagonal ? kDiagonal : kStraight;
            if (m_class[n.cell] == kClassSoft) {
                step = diagonal ? kSoftDiagonal : kSoftStraight;
            } else if (m_class[n.cell] == kClassDanger) {
                step = diagonal ? kDangerDiagonal : kDangerStraight;
            }
            const std::uint32_t cost = n.cost + step;
            if (cost < m_cost[nc]) {
                m_cost[nc] = cost;
                m_state[nc] = static_cast<std::uint8_t>((m_state[nc] & 0x0FU) | ((dir + 1U) << 4U));
                push(cost, nc);
            }
        }
    }
    std::optional<std::size_t> end = goal ? goal : edge;
    if (!end) {
        return course;
    }
    course.reached_goal = goal.has_value();
    // Walk back from the end to the start, then emit runs from the start side.
    std::vector<std::uint8_t> dirs;
    std::size_t cell = *end;
    while (cell != start) {
        const std::uint8_t from = static_cast<std::uint8_t>(m_state[cell] >> 4U);
        if (from == 0) {
            return SeaCourse{};  // cannot happen: every visited cell but the start has a parent
        }
        const auto dir = static_cast<std::uint8_t>(from - 1U);
        dirs.push_back(dir);
        const auto cx = static_cast<std::int32_t>(cell % static_cast<std::size_t>(size)) + x0;
        const auto cy = static_cast<std::int32_t>(cell / static_cast<std::size_t>(size)) + y0;
        cell = at(cx - step_x(dir), cy - step_y(dir));
    }
    std::reverse(dirs.begin(), dirs.end());
    std::int32_t x = r.start_x;
    std::int32_t y = r.start_y;
    std::size_t i = 0;
    while (i < dirs.size() && course.path.size() < kMaxRuns) {
        const std::uint8_t dir = dirs[i];
        const std::uint8_t cls = m_class[at(x, y)];
        std::uint8_t n = 0;
        // A run: same direction and cost class, at most two tiles (0x0046CF70).
        while (i < dirs.size() && dirs[i] == dir && n < kMaxRunTiles && m_class[at(x, y)] == cls) {
            x += step_x(dir);
            y += step_y(dir);
            ++n;
            ++i;
        }
        course.path.push_back(path_code(dir, n));
    }
    if (i < dirs.size()) {
        course.reached_goal = false;  // truncated: the ship plans again at the end
    }
    course.end_x = x;
    course.end_y = y;
    return course;
}

}  // namespace o1602::sim
