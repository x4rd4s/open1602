// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/start_places.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "o1602/sim/random.hpp"
#include "o1602/sim/sea_paths.hpp"

namespace o1602::sim {

namespace {

/// The farthest a ship looks for free water about the tile it should start on.
constexpr std::int64_t kSearchRadius = 16;

/// Whether a ship can start on world tile (x, y): water ships sail on, and no ship of `taken`
/// within a tile.
bool free_water(const World& world, const assets::BuildingTable& buildings, const std::vector<StartPlace>& taken,
                std::int64_t x, std::int64_t y) {
    if (x < 0 || y < 0 || x >= std::int64_t{world.width()} || y >= std::int64_t{world.height()}) {
        return false;
    }
    const World::Cell& cell = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (cell.island != World::kSea) {
        const MapCell& c = world.islands()[cell.island].map.top(cell.local_x, cell.local_y);
        const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
        if (b == nullptr || !sailable_building(*b)) {
            return false;
        }
    }
    return std::none_of(taken.begin(), taken.end(), [&](const StartPlace& t) {
        return std::abs(std::int64_t{t.x} - x) <= 1 && std::abs(std::int64_t{t.y} - y) <= 1;
    });
}

/// The free tile nearest to (x, y), in rings about it; nothing within kSearchRadius.
std::optional<StartPlace> nearest_free(const World& world, const assets::BuildingTable& buildings,
                                       const std::vector<StartPlace>& taken, std::int64_t x, std::int64_t y) {
    for (std::int64_t r = 0; r <= kSearchRadius; ++r) {
        for (std::int64_t dy = -r; dy <= r; ++dy) {
            for (std::int64_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) == r && free_water(world, buildings, taken, x + dx, y + dy)) {
                    return StartPlace{static_cast<std::uint32_t>(x + dx), static_cast<std::uint32_t>(y + dy)};
                }
            }
        }
    }
    return std::nullopt;
}

/// The index of the first ship (not a trade cart) of `player` in World::ships(), nothing if none.
std::optional<std::size_t> first_ship(const World& world, PlayerId player) {
    const auto& ships = world.ships();
    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (ships[i].owner == player && !ships[i].cart() && !ships[i].sinking) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace

bool ship_start_marker(const ScenarioMarker& marker) noexcept {
    return marker.shape == ScenarioMarker::Shape::Point && marker.name.starts_with(kShipStartMarker);
}

std::vector<PlayerId> players_starting_at_sea(const World& world) {
    std::vector<PlayerId> out;
    for (std::size_t slot = 0; slot < world.players().size(); ++slot) {
        const Player& p = world.players()[slot];
        const auto id = static_cast<PlayerId>(slot);
        if (!p.active() || (p.role != PlayerRole::Human && p.role != PlayerRole::Computer) ||
            world.behaviour(p) != FactionBehaviour::Settlers) {
            continue;
        }
        const bool city =
            std::any_of(world.cities().begin(), world.cities().end(), [&](const City& c) { return c.owner == id; });
        if (!city && first_ship(world, id)) {
            out.push_back(id);
        }
    }
    return out;
}

std::vector<StartPlace> start_places(const World& world) {
    std::vector<StartPlace> out;
    const auto add = [&](StartPlace p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) {
            out.push_back(p);
        }
    };
    for (const PlayerId player : players_starting_at_sea(world)) {
        const Ship& s = world.ships()[*first_ship(world, player)];
        add(StartPlace{s.x, s.y});
    }
    for (const ScenarioMarker& m : world.markers()) {
        if (ship_start_marker(m)) {
            add(StartPlace{m.x, m.y});
        }
    }
    return out;
}

void shuffle_start_places(World& world, const assets::BuildingTable& buildings, std::uint64_t seed) {
    const std::vector<PlayerId> players = players_starting_at_sea(world);
    std::vector<StartPlace> places = start_places(world);
    if (players.empty() || places.size() < 2) {
        return;
    }
    // Fisher-Yates from the game's seed: the same game gives the same places on every computer.
    Random random(seed, 0x5747'4152'5453ULL);
    for (std::size_t i = places.size() - 1; i > 0; --i) {
        std::swap(places[i], places[random.below(static_cast<std::uint32_t>(i + 1))]);
    }
    // Where every ship goes: those that do not move hold their tiles first.
    std::vector<Ship>& ships = world.ships();
    std::vector<bool> moving(ships.size(), false);
    for (std::size_t i = 0; i < ships.size(); ++i) {
        moving[i] = !ships[i].cart() && std::find(players.begin(), players.end(), ships[i].owner) != players.end();
    }
    std::vector<StartPlace> taken;
    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (!moving[i] && !ships[i].cart()) {
            taken.push_back(StartPlace{ships[i].x, ships[i].y});
        }
    }
    std::vector<std::optional<StartPlace>> to(ships.size());
    for (std::size_t n = 0; n < players.size(); ++n) {
        const Ship& anchor = ships[*first_ship(world, players[n])];
        const std::int64_t dx = std::int64_t{places[n].x} - anchor.x;
        const std::int64_t dy = std::int64_t{places[n].y} - anchor.y;
        for (std::size_t i = 0; i < ships.size(); ++i) {
            if (moving[i] && ships[i].owner == players[n]) {
                to[i] = nearest_free(world, buildings, taken, ships[i].x + dx, ships[i].y + dy);
                if (!to[i]) {
                    to[i] = StartPlace{ships[i].x, ships[i].y};
                }
                taken.push_back(*to[i]);
            }
        }
    }
    for (std::size_t i = 0; i < ships.size(); ++i) {
        if (to[i] && (to[i]->x != ships[i].x || to[i]->y != ships[i].y)) {
            ships[i].place(to[i]->x, to[i]->y);
            ships[i].near_island = World::kSea;
        }
    }
}

}  // namespace o1602::sim
