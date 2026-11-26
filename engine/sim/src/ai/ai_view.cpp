// SPDX-License-Identifier: GPL-3.0-or-later
//
// ai::View (ai_view.hpp): each query delegates to the engine's own helper, so that the computers'
// decisions can move to it without a change of what they decide.

#include "ai_view.hpp"

#include <algorithm>

#include "o1602/sim/growth.hpp"

namespace o1602::sim::ai {

View Engine::view(const AiPlayer& p) const {
    return View(*this, p);
}

std::vector<const City*> View::own_cities() const {
    std::vector<const City*> out;
    for (const City& c : m_engine.m_world.cities()) {
        if (c.owner == m_viewer.player) {
            out.push_back(&c);
        }
    }
    return out;
}

std::uint32_t View::own_city_count() const {
    return m_engine.own_city_count(m_viewer.player);
}

std::uint32_t View::own_population() const {
    return m_engine.total_population(m_viewer.player);
}

std::uint32_t View::own_supply(const City& city, std::uint8_t ware) const {
    return city.owner == m_viewer.player ? m_engine.supply(city, ware) : 0U;
}

const Overview& View::overview() const {
    return m_engine.overview(m_viewer);
}

WareView View::ware(std::uint8_t ware, std::uint32_t minutes, std::optional<std::uint16_t> island) const {
    return m_engine.ware_view(m_viewer, ware, minutes, island);
}

bool View::knows_island(std::uint16_t island) const {
    return m_engine.knows_island(m_viewer, island);
}

bool View::knows_crops(std::uint16_t island) const {
    return m_engine.knows_crops(m_viewer, island);
}

std::optional<std::uint32_t> View::fertility(std::uint16_t island, std::uint8_t ware) const {
    if (island >= m_engine.m_world.islands().size() || !knows_crops(island)) {
        return std::nullopt;
    }
    return island_fertility(m_engine.m_world, m_engine.m_world.islands()[island], ware);
}

bool View::grows(std::uint16_t island, std::uint8_t crop) const {
    const std::uint32_t f = fertility(island, crop).value_or(0);
    return crop == ware::kOre ? f >= 64 : f == 128;
}

std::uint32_t View::full_crops(std::uint16_t island) const {
    // Open1602 TODO(PM): a fixed list of the six luxury field wares, not the goods table.
    std::uint32_t n = 0;
    for (const std::uint8_t w : {ware::kSpiceField, ware::kTobaccoField, ware::kWineField, ware::kCottonField,
                                 ware::kCocoaField, ware::kSugarField}) {
        n += fertility(island, w).value_or(0) == 128 ? 1U : 0U;
    }
    return n;
}

bool View::free_deposit(std::uint16_t island, std::uint8_t ware) const {
    if (island >= m_engine.m_world.islands().size() || !knows_crops(island)) {
        return false;
    }
    for (const auto& d : m_engine.island_of(island).header.deposits) {
        if (d.ware == ware && d.amount > 0 && m_engine.tile(island, d.x, d.y).city == MapCell::kNoCity) {
            return true;
        }
    }
    return false;
}

std::uint32_t View::island_size(std::uint16_t island) const {
    return m_engine.island_size(island);
}

std::vector<const City*> View::seen_cities(PlayerId owner) const {
    std::vector<const City*> out;
    for (const City& c : m_engine.m_world.cities()) {
        if (c.owner == owner && (owner == m_viewer.player || m_engine.city_known(m_viewer, c))) {
            out.push_back(&c);
        }
    }
    return out;
}

std::uint32_t View::seen_city_count(PlayerId owner) const {
    return static_cast<std::uint32_t>(seen_cities(owner).size());
}

std::uint32_t View::seen_houses(PlayerId owner) const {
    std::uint32_t n = 0;
    const std::vector<const City*> cities = seen_cities(owner);
    for (const House& h : m_engine.m_world.houses()) {
        n += std::ranges::any_of(cities, [&](const City* c) { return h.island == c->island && h.city == c->index; })
                 ? 1U
                 : 0U;
    }
    return n;
}

std::uint32_t View::seen_army(PlayerId owner) const {
    return m_engine.seen_army(m_viewer.player, owner);
}

std::uint32_t View::seen_cannons(PlayerId owner) const {
    return m_engine.seen_cannons(m_viewer.player, owner);
}

std::optional<KontorOffer> View::kontor_offer(const City& partner, std::uint8_t ware) const {
    if (partner.owner == m_viewer.player || !m_engine.kontor_known(m_viewer, partner)) {
        return std::nullopt;
    }
    const City::Trade* t = partner.trade_of(ware);
    if (t == nullptr || !t->sell) {
        return std::nullopt;
    }
    return KontorOffer{t->sell_price, m_engine.kontor_offer(partner, ware)};
}

bool View::omniscient_level() const {
    return m_engine.sees_all();
}

}  // namespace o1602::sim::ai
