// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/validate.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

namespace o1602::sim {

namespace {

core::Status bad(const std::string& what) {
    return core::make_error(core::ErrorCode::InvalidFormat, "game: " + what);
}

class Check {
public:
    explicit Check(const World& w) : m_w(w) {}

    [[nodiscard]] bool owner(PlayerId p) const noexcept { return p == kNoPlayer || p < m_w.players().size(); }
    [[nodiscard]] bool island(std::uint16_t i) const noexcept { return i < m_w.islands().size(); }
    [[nodiscard]] bool tile(std::uint16_t i, std::uint32_t x, std::uint32_t y) const noexcept {
        return island(i) && x < m_w.islands()[i].map.width() && y < m_w.islands()[i].map.height();
    }
    /// Every entry of a list stands on a tile of its island.
    template <typename List>
    [[nodiscard]] bool on_islands(const List& list) const {
        for (const auto& e : list) {
            if (!tile(e.island, e.x, e.y)) {
                return false;
            }
        }
        return true;
    }

private:
    const World& m_w;
};

/// Ids of a list are unique, not 0, and below the next id the world hands out.
template <typename List>
[[nodiscard]] bool ids_unique(const List& list, std::uint32_t next) {
    if (next == std::numeric_limits<std::uint32_t>::max()) {
        return false;  // the next new unit would share its id with the one after it
    }
    std::vector<std::uint32_t> ids;
    ids.reserve(list.size());
    for (const auto& e : list) {
        if (e.id == 0 || e.id >= next) {
            return false;
        }
        ids.push_back(e.id);
    }
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

}  // namespace

core::Status validate(const World& w) {
    const Check c(w);
    if (w.players().size() > kMaxPlayerCount) {
        return bad("more than " + std::to_string(kMaxPlayerCount) + " players");
    }
    // Islands are addressed by their 8-bit number (state sections, the original's format), and
    // their coordinates are 8 bit wide in places (craters, deer spots, deposits).
    constexpr std::size_t kMaxIslandSide = 255;
    std::array<bool, 256> numbers{};
    for (std::size_t i = 0; i < w.islands().size(); ++i) {
        const WorldIsland& island = w.islands()[i];
        const IslandMap& map = island.map;
        if (numbers[island.header.number]) {
            return bad("two islands with the number " + std::to_string(island.header.number));
        }
        numbers[island.header.number] = true;
        if (map.width() > kMaxIslandSide || map.height() > kMaxIslandSide) {
            return bad("island " + std::to_string(i) + " is wider or higher than 255 tiles");
        }
        if (std::uint64_t{island.x} + map.width() > w.width() || std::uint64_t{island.y} + map.height() > w.height()) {
            return bad("island " + std::to_string(i) + " lies outside the map");
        }
        for (const Crater& crater : island.craters) {
            if (crater.x >= map.width() || crater.y >= map.height()) {
                return bad("crater outside island " + std::to_string(i));
            }
        }
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                if (!c.owner(map.top(x, y).owner)) {
                    return bad("tile of island " + std::to_string(i) + " owned by an unknown player");
                }
            }
        }
    }
    for (const City& city : w.cities()) {
        if (!c.island(city.island) || !c.owner(city.owner)) {
            return bad("city " + city.name + " on an unknown island or of an unknown player");
        }
    }
    for (const Ship& ship : w.ships()) {
        if (!w.contains(ship.x, ship.y) || !c.owner(ship.owner)) {
            return bad("ship " + std::to_string(ship.id) + " outside the map or of an unknown player");
        }
    }
    for (const Soldier& s : w.soldiers()) {
        const bool inside =
            s.pos_x >= 0 && s.pos_y >= 0 &&
            w.contains(static_cast<std::uint32_t>(s.pos_x) >> 12U, static_cast<std::uint32_t>(s.pos_y) >> 12U);
        if (!inside || !c.owner(s.owner) || (s.island != 0xFFFF && !c.island(s.island))) {
            return bad("unit " + std::to_string(s.id) + " outside the map or of an unknown player");
        }
    }
    if (!ids_unique(w.ships(), w.next_ship_id()) || !ids_unique(w.soldiers(), w.next_soldier_id())) {
        return bad("ship or unit ids twice, 0 or not below the next id");
    }
    for (const Tower& t : w.towers()) {
        if (!c.tile(t.island, t.x, t.y) || !c.owner(t.owner)) {
            return bad("tower outside its island or of an unknown player");
        }
    }
    if (!c.on_islands(w.houses()) || !c.on_islands(w.growth().entries) || !c.on_islands(w.cost_buildings()) ||
        !c.on_islands(w.animals()) || !c.on_islands(w.disasters()) || !c.on_islands(w.building_damage())) {
        return bad("a building or record outside its island");
    }
    for (const Production& p : w.productions()) {
        if (p.used && !c.tile(p.island, p.x, p.y)) {
            return bad("production outside its island");
        }
    }
    const VolcanoState& v = w.volcanoes();
    for (const Eruption& e : v.eruptions) {
        if (!c.tile(e.crater.island, e.crater.x, e.crater.y)) {
            return bad("eruption of an unknown crater");
        }
    }
    for (const LavaLump& lump : v.lava) {
        if (!c.island(lump.island)) {
            return bad("lava of an unknown island");
        }
    }
    if (v.last && !c.island(v.last->island)) {
        return bad("last eruption on an unknown island");
    }
    return core::Ok{};
}

}  // namespace o1602::sim
