// SPDX-License-Identifier: GPL-3.0-or-later
// Pirates at sea (docs/re/pirates.md): new ships, arming, hunting, plunder, the nest's sales.
#include <algorithm>

#include "o1602/sim/combat.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kPirateStepMs = 10'000;  // the pirates' AI step (0x0042B4B0)
constexpr std::uint8_t kMaxPirateCannons = 10;   // Maxkanon of PIRAT
constexpr std::int32_t kWanderRange = 90;        // tiles: random places at sea (0x004455F0)
constexpr std::uint32_t kNestFoodMin = 25 * 32;  // the nest sells when it holds more food
constexpr std::uint32_t kNestFoodCost = 10 * 32;
constexpr std::uint16_t kValuable = 35;  // base price of Tabak: plunder is worth at least this
constexpr std::uint32_t kPlunderAmount = 50 * 32;

}  // namespace

PlayerId Fleet::pirates() const {
    for (std::size_t p = 0; p < m_world->players().size(); ++p) {
        if (m_world->players()[p].role == PlayerRole::Pirates) {
            return static_cast<PlayerId>(p);
        }
    }
    return kNoPlayer;
}

std::uint32_t pirate_max_ships(const World& world) noexcept {
    // Pirates_MaxShips 0x0040F310: by the highest level any player reached. Bit 18 (75 settlers)
    // would allow one ship, but the pirates need at least two to start (original).
    std::uint32_t best = 0;
    for (const Player& p : world.players()) {
        if (p.role != PlayerRole::Human && p.role != PlayerRole::Computer) {
            continue;
        }
        const std::uint32_t m = (p.infra_mask >> 30 & 1U) != 0   ? 4U
                                : (p.infra_mask >> 29 & 1U) != 0 ? 3U
                                : (p.infra_mask >> 24 & 1U) != 0 ? 2U
                                : (p.infra_mask >> 18 & 1U) != 0 ? 1U
                                                                 : 0U;
        best = std::max(best, m);
    }
    return best;
}

bool pirates_enabled(const World& world, const Rules& rules) noexcept {
    switch (rules.pirates) {
        case Pirates::Off:
            return false;
        case Pirates::On:
            return true;
        case Pirates::Game:
            break;
    }
    return (world.game_settings().flags & World::GameSettings::kNoPirates) == 0;
}

std::vector<std::size_t> Fleet::pirate_bases() const {
    std::vector<std::size_t> bases;
    const PlayerId p = pirates();
    for (std::size_t i = 0; i < m_world->cities().size(); ++i) {
        const City& c = m_world->cities()[i];
        if (c.owner == p && p != kNoPlayer && c.warehouse_x && c.warehouse_y) {
            bases.push_back(i);
        }
    }
    return bases;
}

void Fleet::pirates_tick(std::uint32_t dt_ms) {
    m_pirate_spawn_ms = m_pirate_spawn_ms > dt_ms ? m_pirate_spawn_ms - dt_ms : 0;
    m_pirate_ms += dt_ms;
    if (m_pirate_ms < kPirateStepMs) {
        return;
    }
    m_pirate_ms -= kPirateStepMs;
    pirates_step();
}

void Fleet::pirates_step() {
    // Pirates_Step 0x00429FD0.
    const PlayerId p = pirates();
    const World::GameSettings& settings = m_world->game_settings();
    const std::uint32_t max = pirate_max_ships(*m_world);
    if (p == kNoPlayer || !pirates_enabled(*m_world, *m_rules) || max <= 1) {
        return;
    }
    std::uint32_t ships = 0;
    for (const Ship& s : m_world->ships()) {
        ships += s.owner == p && !s.sinking ? 1U : 0U;
    }
    const std::vector<std::size_t> bases = pirate_bases();
    Random& random = m_world->random(RandomStream::Pirates);
    // A new ship at a random edge of the map, unarmed for one step.
    if (ships < max && m_pirate_armed && m_pirate_spawn_ms == 0 &&
        ((settings.flags & World::GameSettings::kPirateGoal) == 0 || !bases.empty())) {
        for (int tries = 0; tries < 16; ++tries) {
            const std::uint32_t r = random.next();
            std::int32_t x = 0;
            std::int32_t y = 0;
            const auto along =
                static_cast<std::int32_t>((r >> 2) % ((r & 2U) != 0 ? m_world->height() : m_world->width()));
            switch (r & 3U) {
                case 0:  // top
                    x = along;
                    break;
                case 1:  // bottom
                    x = along;
                    y = static_cast<std::int32_t>(m_world->height()) - 1;
                    break;
                case 2:  // left
                    y = along;
                    break;
                default:  // right
                    x = static_cast<std::int32_t>(m_world->width()) - 1;
                    y = along;
                    break;
            }
            if (!m_sea.sailable(x, y)) {
                continue;
            }
            Ship pirate = make_ship(next_ship_name(), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                    figure::kPirate, p, static_cast<std::uint8_t>(random.below(8)));
            if (const assets::FigureType* t = m_figures->get(figure::kPirate)) {
                pirate.cargo.resize(t->ship.cargo_slots);
                pirate.health = t->ship.max_health;  // a loaded game would fill 0 to full (Fleet), the running one not
            }
            m_world->add_ship(std::move(pirate));
            m_events.push_back(ShipEvent{ShipEvent::Kind::PirateAppeared, m_world->ships().back().id, p});
            break;
        }
        m_pirate_armed = false;
        m_pirate_spawn_ms = settings.pirate_interval_s * 1000U;
        return;
    }
    // Arming: by the difficulty of the game.
    if (!m_pirate_armed) {
        m_pirate_armed = true;
        std::uint32_t cannons = 1;
        switch (settings.difficulty) {
            case 0:
                cannons = 1;
                break;
            case 1:
                cannons = (max + 1) / 2;
                break;
            case 2:
                cannons = max + 2;
                break;
            default:
                cannons = 2 * max + 4;
                break;
        }
        const assets::FigureType* t = m_figures->get(figure::kPirate);
        const std::uint32_t cap = t != nullptr && t->ship.max_cannons != 0 ? t->ship.max_cannons : kMaxPirateCannons;
        for (Ship& s : m_world->ships()) {
            if (s.owner == p && s.cannons == 0 && !s.sinking) {
                s.cannons = static_cast<std::uint8_t>(std::min(cannons, cap));
                m_pirate_spawn_ms = settings.pirate_interval_s * 1000U;
            }
        }
    }
    // The nest: with enough food it sells its most valuable goods (0x0041DD80).
    if (ships != 0 && !bases.empty()) {
        const City& found = m_world->cities()[bases.front()];
        City* nest = m_world->city(found.island, found.index);
        if (nest != nullptr && nest->stock(ware::kFood) > kNestFoodMin) {
            nest->goods[ware::kFood] -= kNestFoodCost;
            pirate_nest_sales(*nest);
        }
    }
}

void Fleet::pirate_nest_sales(City& nest) {
    // The N most valuable wares (N = Maxware of the nest's Kontor) at 40 % of the base price.
    std::size_t count = 6;
    if (nest.warehouse_x && nest.warehouse_y && nest.island < m_world->islands().size()) {
        const MapCell& c = m_world->islands()[nest.island].map.top(*nest.warehouse_x, *nest.warehouse_y);
        if (const assets::BuildingType* b = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite)) {
            count = b->max_wares;
        }
    }
    std::vector<std::pair<std::uint64_t, std::uint8_t>> values;
    for (std::uint8_t w = ware::kFirstTrade; w <= ware::kLastTrade; ++w) {
        values.emplace_back(std::uint64_t{base_price(w)} * nest.stock(w), w);
    }
    std::stable_sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (nest.trade.size() < ware::kLastTrade + 1U) {
        nest.trade.resize(ware::kLastTrade + 1U);
    }
    for (City::Trade& t : nest.trade) {
        t.sell = false;
        t.buy = false;
    }
    for (std::size_t i = 0; i < values.size() && i < count; ++i) {
        City::Trade& t = nest.trade[values[i].second];
        t.sell = true;
        t.keep = 0;
        t.sell_price = static_cast<std::uint16_t>(base_price(values[i].second) * 51 / 128);
    }
}

void Fleet::pirate_choose(Ship& ship) {
    // An idle pirate (0x00452370): one time in four it goes after a player's ship, otherwise to a
    // random place at sea, and only if three of those fail to one of its bases.
    Random& random = m_world->random(RandomStream::Pirates);
    const PlayerId p = ship.owner;
    ship.held = false;
    ship.repairing = false;
    if ((random.next() & 3U) == 0) {
        // Pirate_FindPrey 0x004488D0: a player the pirates do not like, with a ship.
        PlayerId prey_owner = kNoPlayer;
        std::int64_t best = 0;
        for (std::size_t b = 0; b < m_world->players().size(); ++b) {
            const Player& player = m_world->players()[b];
            if (player.role != PlayerRole::Human && player.role != PlayerRole::Computer) {
                continue;
            }
            const auto pb = static_cast<PlayerId>(b);
            const std::int32_t mood = m_world->diplomacy().mood(*m_world, p, pb, 0, m_world->time_ms());
            const bool has_ship = std::any_of(m_world->ships().begin(), m_world->ships().end(),
                                              [&](const Ship& s) { return s.owner == pb && !s.sinking; });
            if (mood >= 1 || !has_ship) {
                continue;
            }
            const std::int64_t v = static_cast<std::int64_t>(random.next() & 15U) + mood;
            if (prey_owner == kNoPlayer || v < best) {
                best = v;
                prey_owner = pb;
            }
        }
        if (prey_owner != kNoPlayer) {
            std::vector<const Ship*> fleet;
            for (const Ship& s : m_world->ships()) {
                if (s.owner == prey_owner && !s.sinking) {
                    fleet.push_back(&s);
                }
            }
            const Ship* prey = nullptr;
            for (int tries = 0; tries < 4 && !fleet.empty(); ++tries) {
                prey = fleet[random.below(static_cast<std::uint32_t>(fleet.size()))];
                if (prey->near_island == 0xFFFF) {
                    break;  // preferably one out at sea
                }
            }
            if (prey != nullptr) {
                // Where the prey is now; the pirate does not follow it (original).
                ship.target = Target::position(prey->x, prey->y);
                ship.order = ship.target;
                return;
            }
        }
    }
    // Pirate_RandomSeaPos 0x004455F0 draws with the world twins of Island_RandomX (0x004432C0 and
    // 0x004432F0): base + r - 2 * (rand() % r), clamped to the map, so only offsets of r's parity.
    const auto random_coord = [&random](std::uint32_t base, std::uint32_t size) {
        const std::int32_t v =
            static_cast<std::int32_t>(base) + kWanderRange - 2 * static_cast<std::int32_t>(random.below(kWanderRange));
        return static_cast<std::uint32_t>(std::clamp(v, 0, static_cast<std::int32_t>(size) - 1));
    };
    for (int tries = 0; tries < 3; ++tries) {
        const std::uint32_t x = random_coord(ship.x, m_world->width());
        const std::uint32_t y = random_coord(ship.y, m_world->height());
        if (m_world->cell(x, y).island == World::kSea) {
            ship.target = Target::position(x, y);
            ship.order = ship.target;
            return;
        }
    }
    const std::vector<std::size_t> bases = pirate_bases();
    if (!bases.empty()) {
        const City& base = m_world->cities()[bases[random.below(static_cast<std::uint32_t>(bases.size()))]];
        ship.target = Target::city_of(base.island, base.index);
        ship.order = ship.target;
    }
}

bool Fleet::plunder(Ship& pirate, Ship& victim) {
    // Pirate_BuildPlunderRoute 0x00455840: the victim ship twice, then a pirate base twice; up to
    // six valuable holds, 50 t each, from the victim to the base. Peace with the victim's owner in
    // any case, also when nothing comes of it (original).
    const PlayerId p = pirate.owner;
    pirate.target = {};
    pirate.order = {};
    pirate.motion.path.clear();
    pirate.motion.path_index = 0;
    const std::vector<std::size_t> bases = pirate_bases();
    bool any = false;
    if (!bases.empty()) {
        const City& base =
            m_world->cities()
                [bases[m_world->random(RandomStream::Pirates).below(static_cast<std::uint32_t>(bases.size()))]];
        TradeRoute route;
        route.stops[0].target = Target::other_ship(victim.id);
        route.stops[1].target = Target::other_ship(victim.id);
        route.stops[2].target = Target::city_of(base.island, base.index);
        route.stops[3].target = Target::city_of(base.island, base.index);
        std::size_t n = 0;
        for (const CargoSlot& c : victim.cargo) {
            if (n >= 6 || c.ware == 0 || c.ware > ware::kLastTrade || base_price(c.ware) < kValuable) {
                continue;  // soldiers on board are not loot (the original reads past its table)
            }
            const std::size_t stop = n / 3;
            const std::size_t entry = n % 3;
            route.stops[stop].entries[entry] = RouteEntry{c.ware, false, kPlunderAmount};
            route.stops[2 + stop].entries[entry] = RouteEntry{c.ware, true, kPlunderAmount};
            ++n;
        }
        if (n != 0) {
            // Op 0x28 value 1: the route starts at its first stop.
            route.active = true;
            route.one_way = true;
            route.current = 0;
            pirate.route = route;
            pirate.target = route.stops[0].target;
            any = true;
        }
        // The surrender pleases the pirates whether or not there was loot (events 9 and 6 after
        // 0x00455840 returns, 0x00452BE0): without this they would declare war again at their
        // next look and hunt the same ship until it sinks.
        Diplomacy& d = m_world->diplomacy();
        for (std::size_t b = 0; b < m_world->players().size(); ++b) {
            const PlayerRole role = m_world->players()[b].role;
            if (role == PlayerRole::Human || role == PlayerRole::Computer) {
                d.add_event(p, static_cast<PlayerId>(b), DiplomacyEventType::AnySurrender, m_world->time_ms());
            }
        }
        d.add_event(p, victim.owner, DiplomacyEventType::Surrendered, m_world->time_ms());
    }
    if (!any) {
        const Player* owner = m_world->player(victim.owner);
        if (owner != nullptr && owner->role != PlayerRole::Human) {
            victim.held = false;  // a computer's ship sails on
        } else {
            victim.target = {};
            victim.motion.path.clear();
            victim.motion.path_index = 0;
        }
    }
    m_world->diplomacy().set_peace(*m_world, p, victim.owner, Treaty::Signed, m_world->time_ms());
    m_events.push_back(ShipEvent{ShipEvent::Kind::Plundered, victim.id, victim.owner, 0, 0, 0, any ? 1 : 0});
    return any;
}

}  // namespace o1602::sim
