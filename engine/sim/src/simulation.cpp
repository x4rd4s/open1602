// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/simulation.hpp"

#include <algorithm>
#include <array>

#include "o1602/sim/growth.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_extension.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kProductionPeriodMs = 1000;  // 0x0047DAF0: once per game second
constexpr std::uint32_t kUpkeepPeriodMs = 5000;      // 0x00476350: each player once per 5 s
constexpr std::uint32_t kDiplomacyTickMs = 5000;     // 0x00476350: the diplomacy counters fade
constexpr std::uint8_t kDefeatChecks = 40;           // 0x00476350: 40 checks of 5 s
constexpr std::size_t kArchClass = 2;                // 0x004805D0(K, 2): inhabitants of class 2 and up
constexpr std::uint64_t kArchInhabitants = 200;      // more than 199

}  // namespace

Simulation::Simulation(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                       Rules rules, Parts)
    : m_world(&world),
      m_buildings(&buildings),
      m_rules(rules),
      m_figures(&figures),
      m_logistics(world, buildings, figures),
      m_fleet(world, buildings, figures, m_rules),
      m_combat(world, buildings, figures),
      m_military(world, buildings, figures, m_rules, m_combat),
      m_ai(*this) {
    m_military.set_fleet(&m_fleet);
    m_fleet.set_combat(&m_combat);
    m_fleet.set_directed([this](std::uint32_t ship) { return m_ai.directs(ship); });
    m_time_ms = world.time_ms();
    m_island_ms = static_cast<std::uint32_t>(m_time_ms % Logistics::kIslandEpochMs);
    // The cities consume at whole multiples of 10 s of game time (game08 -> game09, the video of
    // game04); the extra tick the original runs right after loading is not taken over (S-091).
    m_city_ms = static_cast<std::uint32_t>(m_time_ms % kCityTickMs);
    if (world.saved_rules()) {
        m_rules = *world.saved_rules();  // the settings the game was started with
    }
}

Simulation::Simulation(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                       Rules rules)
    : Simulation(world, buildings, figures, rules, Parts{}) {
    recount_disasters(world);
    if (const std::vector<std::uint8_t> state = world.take_simulation_state(); !state.empty()) {
        // Each part loads whole or not at all, but a later part may fail after an earlier one
        // was taken over: then all go back to how they are now, fresh.
        core::ByteWriter fresh;
        save_state(fresh);
        if (restore_state(state)) {
            return;  // a game saved by Open1602 continues where it was
        }
        m_state_damaged = true;
        restore_state(fresh.take());
    }
    // As the original after every load: the computer players look at the world anew (0x0042B710).
    m_ai.reset();
    // As when the original loads a game: every deer spot gets its deer.
    m_logistics.spawn_deer_at_spots();
    // The original takes the saved coverage of the houses; Open1602 recomputes it once, which
    // also restores what the savegame does not keep (gallows, fountains).
    update_services(world, buildings);
}

bool Simulation::state_readable(World& world, const assets::BuildingTable& buildings,
                                const assets::FigureTable& figures) {
    if (world.simulation_state().empty()) {
        return true;
    }
    Simulation probe(world, buildings, figures, Rules::open1602(), Parts{});
    return probe.restore_state(world.simulation_state());
}

std::optional<std::uint64_t> Simulation::state_hash() const {
    const auto bytes = write_game(*m_world, *m_buildings, this, SaveOptions{false});
    if (!bytes.ok()) {
        return std::nullopt;
    }
    return savegame_fingerprint(bytes.value());
}

namespace {

/// Name lists of a game: at most this many names of at most this many bytes each.
constexpr std::uint32_t kMaxNames = 4096;
constexpr std::uint32_t kMaxNameBytes = 255;

void write_names(core::ByteWriter& out, const std::vector<std::string>& names) {
    const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(names.size(), kMaxNames));
    out.u32(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        out.string(std::string_view(names[i]).substr(0, kMaxNameBytes));
    }
}

bool read_names(StateIn& in, std::vector<std::string>& names) {
    const std::uint32_t n = in.count(4);
    if (n > kMaxNames) {
        return false;
    }
    names.clear();
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        std::string name = in.string();
        if (name.size() > kMaxNameBytes) {
            return false;
        }
        names.push_back(std::move(name));
    }
    return in.ok();
}

}  // namespace

void Simulation::offer_names(std::vector<std::string> cities, std::vector<std::string> ships) {
    if (m_city_names.empty() && m_fleet.ship_names().empty()) {
        m_city_names = std::move(cities);
        m_fleet.set_ship_names(std::move(ships));
    }
}

void Simulation::save_state(core::ByteWriter& out) const {
    constexpr std::uint16_t kVersion = 6;
    out.u16(kVersion);
    out.u32(m_production_ms);
    out.u32(m_city_ms);
    out.u32(m_disaster_ms);
    out.u32(m_upkeep_ms);
    out.u32(m_island_ms);
    m_logistics.save(out);
    m_fleet.save(out);
    m_military.save(out);
    m_ai.save(out);
    write_names(out, m_city_names);
    write_names(out, m_fleet.ship_names());
    out.u32(m_speed);
    out.u8(m_paused ? 1 : 0);
}

bool Simulation::restore_state(const std::vector<std::uint8_t>& state) {
    if (state.empty()) {
        return false;
    }
    core::ByteReader reader(state);
    StateIn in(reader);
    const std::uint16_t version = in.u16();
    if (version < 1 || version > 6) {
        return false;
    }
    const std::uint32_t production = in.u32();
    const std::uint32_t city = in.u32();
    const std::uint32_t disaster = in.u32();
    const std::uint32_t upkeep = in.u32();
    const std::uint32_t island = in.u32();
    // A clock at or past its period would fire on every tick until it has caught up.
    if (production >= kProductionPeriodMs || city >= kCityTickMs || disaster >= kDisasterEpochMs ||
        upkeep >= kUpkeepPeriodMs || island >= Logistics::kIslandEpochMs) {
        return false;
    }
    if (!in.ok() || !m_logistics.load(reader, version)) {
        return false;
    }
    if (version >= 2 && !m_fleet.load(reader)) {
        return false;
    }
    if (version >= 3 && !m_military.load(reader)) {
        return false;
    }
    if (version >= 4) {
        if (!m_ai.load(reader)) {
            return false;
        }
    } else {
        m_ai.reset();  // games of earlier versions had no computer players' state
    }
    std::vector<std::string> cities;
    std::vector<std::string> ships;
    if (version >= 5 && (!read_names(in, cities) || !read_names(in, ships))) {
        return false;
    }
    // Games of earlier versions did not keep the speed and the pause: they load running at speed 1.
    std::uint32_t speed = 1;
    bool paused = false;
    if (version >= 6) {
        speed = in.u32();
        const std::uint8_t pause = in.u8();
        if (!in.ok() || speed == 0 || pause > 1) {
            return false;
        }
        paused = pause != 0;
    }
    m_production_ms = production;
    m_city_ms = city;
    m_disaster_ms = disaster;
    m_upkeep_ms = upkeep;
    m_island_ms = island;
    if (version >= 5) {
        m_city_names = std::move(cities);
        m_fleet.set_ship_names(std::move(ships));
    }
    set_speed(speed);
    m_paused = paused;
    return true;
}

void Simulation::set_speed(std::uint32_t speed) noexcept {
    m_speed = std::min(speed, m_rules.max_game_speed);
}

void Simulation::advance(std::uint32_t real_ms, const BeforeTick& before_tick) {
    m_behind = false;
    if (m_speed == 0 || m_paused) {
        apply_commands();
        return;
    }
    // The limit is on real time, before the speed: at speed 8 a frame of 400 ms is no stall.
    const std::uint64_t game_ms = real_ms > kMaxCatchUpMs ? kTickMs : std::uint64_t{real_ms} * m_speed;
    constexpr std::uint64_t kMaxOwedMs = std::uint64_t{kTickMs} * kMaxTicksPerAdvance;
    m_pending_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_pending_ms + game_ms, 2 * kMaxOwedMs));
    std::uint32_t ran = 0;
    while (m_pending_ms >= kTickMs) {
        if (ran == kMaxTicksPerAdvance || (before_tick && !before_tick())) {
            m_behind = true;
            break;
        }
        m_pending_ms -= kTickMs;
        tick();
        ++ran;
        if (m_paused) {
            m_pending_ms = 0;  // a pause from the commands of this tick: the rest of the time is dropped
            break;
        }
    }
    // Owed is at most one more step; beyond that the game runs slower instead of ever catching up.
    m_pending_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_pending_ms, kMaxOwedMs));
}

namespace {

PlayerId sender(const Command& command) {
    return std::visit([](const auto& c) { return c.player; }, command);
}

}  // namespace

std::uint64_t Simulation::schedule(std::uint64_t tick, Command command) {
    const std::uint64_t sequence = m_next_command++;
    const PlayerId from = sender(command);
    // After every command of an earlier tick, or of the same tick and an earlier or the same player.
    const auto at = std::upper_bound(m_commands.begin(), m_commands.end(), std::pair{tick, from},
                                     [](const std::pair<std::uint64_t, PlayerId>& key, const Queued& q) {
                                         return key < std::pair{q.tick, sender(q.command)};
                                     });
    m_commands.insert(at, Queued{tick, sequence, std::move(command)});
    return sequence;
}

void Simulation::apply_commands() {
    const std::uint64_t now = next_tick();
    std::size_t due = 0;
    while (due < m_commands.size() && m_commands[due].tick <= now) {
        ++due;
    }
    // Moved out first: a command may not add to the queue, but the vector must not be touched
    // while it is walked.
    std::vector<Queued> commands(std::make_move_iterator(m_commands.begin()),
                                 std::make_move_iterator(m_commands.begin() + static_cast<std::ptrdiff_t>(due)));
    m_commands.erase(m_commands.begin(), m_commands.begin() + static_cast<std::ptrdiff_t>(due));
    for (Queued& q : commands) {
        CommandResult result;
        // A player's command acts for that player only: one without a valid sender would pass every
        // owner check (kNoPlayer) and is refused.
        const PlayerId from = sender(q.command);
        if (q.tick < now) {
            result.late = true;
        } else if (from != kNoPlayer && m_world->player(from) != nullptr) {
            result = execute(q.command);
        }
        result.sequence = q.sequence;
        m_command_results.push(std::move(result));
    }
    trim_events();  // also while the game is paused
}

CommandResult Simulation::execute(const Command& command) {
    CommandResult result;
    // A defeated player or a slot nobody plays gives no orders (the original's controller 9, 10).
    const PlayerId from = sender(command);
    if (const Player* p = m_world->player(from); p != nullptr && !p->active()) {
        return result;
    }
    const std::size_t first = m_construction_events.size();
    if (const auto* build = std::get_if<BuildOrder>(&command)) {
        result.build = execute_build(*m_world, *m_buildings, *build, m_city_names, m_construction_events.sink());
    } else if (const auto* demolish = std::get_if<DemolishOrder>(&command)) {
        const DemolishResult d =
            execute_demolish(*m_world, *m_buildings, *demolish, m_construction_events.sink(), m_rules.refund_share());
        result.demolished = d.removed;
        result.refund = d.refund;
    } else if (const auto* order = std::get_if<ShipOrder>(&command)) {
        result.accepted = m_fleet.order(*order);
        if (result.accepted && order->attack) {
            if (const Ship* ship = m_world->ship(order->ship)) {
                m_military.declare_war_by_attack(ship->owner, order->target);
            }
        }
    } else if (const auto* transfer = std::get_if<CargoTransfer>(&command)) {
        result.moved = m_fleet.transfer(*transfer);
        result.accepted = result.moved != 0;
    } else if (const auto* route = std::get_if<RouteEdit>(&command)) {
        result.accepted = m_fleet.edit_route(*route);
    } else if (const auto* trade = std::get_if<TradeSetting>(&command)) {
        result.accepted = m_fleet.set_trade(*trade);
    } else if (const auto* yard = std::get_if<ShipyardOrder>(&command)) {
        result.accepted = m_fleet.shipyard(*yard);
    } else if (const auto* explore = std::get_if<ExploreOrder>(&command)) {
        result.accepted = m_fleet.explore(*explore);
    } else if (const auto* dipl = std::get_if<DiplomacyOrder>(&command)) {
        result.accepted = apply_diplomacy(*m_world, *dipl);
    } else if (const auto* soldiers = std::get_if<SoldierOrder>(&command)) {
        result.accepted = m_military.order(*soldiers);
    } else if (const auto* soldier = std::get_if<SoldierCommand>(&command)) {
        result.accepted = m_military.command(*soldier);
    } else if (const auto* castle = std::get_if<CastleOrder>(&command)) {
        result.accepted = m_military.castle(*castle);
    } else if (const auto* unload = std::get_if<UnloadOrder>(&command)) {
        result.accepted = m_military.unload(*unload);
    } else if (const auto* sw = std::get_if<ProductionSwitch>(&command)) {
        result.accepted = switch_production(*sw);
    } else if (const auto* city = std::get_if<CityOrder>(&command)) {
        result.accepted = city_order(*city);
    } else if (const auto* ship = std::get_if<ShipCommand>(&command)) {
        result.accepted = m_fleet.command(*ship);
    } else if (const auto* speed = std::get_if<GameSpeedOrder>(&command)) {
        result.accepted = game_speed(*speed);
    } else if (const auto* rename = std::get_if<PlayerRename>(&command)) {
        result.accepted = rename_player(*rename);
    } else if (const auto* control = std::get_if<PlayerControl>(&command)) {
        result.accepted = control_player(*control);
    }
    handle(m_construction_events.events(), first);
    return result;
}

bool Simulation::switch_production(const ProductionSwitch& s) {
    if (s.island >= m_world->islands().size()) {
        return false;
    }
    const IslandMap& map = m_world->islands()[s.island].map;
    if (!map.contains(s.x, s.y) || (s.player != kNoPlayer && map.top(s.x, s.y).owner != s.player)) {
        return false;
    }
    const auto index = m_world->production_index(s.island, s.x, s.y);
    if (!index) {
        return false;
    }
    Production& p = m_world->productions()[*index];
    if (!p.used) {
        return false;
    }
    // M-019: both switches are commands, so every computer applies them.
    if (s.op == ProductionSwitch::Op::Pickup) {
        if (p.no_pickup() == !s.on) {
            return false;
        }
        p.flags = static_cast<std::uint8_t>(s.on ? p.flags & ~Production::kNoPickupFlag
                                                 : p.flags | Production::kNoPickupFlag);
        return true;
    }
    if (p.enabled == s.on) {
        return false;
    }
    p.enabled = s.on;
    if (s.on && !p.working()) {
        // An idle building starts its next cycle in the next second; the time it waited does not
        // count against its productivity (0x0047CFA0).
        p.idle_cycles = 0;
        p.elapsed = p.elapsed > p.countdown ? static_cast<std::uint16_t>(p.elapsed - p.countdown) : 0;
        p.countdown = 0;
    }
    return true;
}

bool Simulation::city_order(const CityOrder& o) {
    City* city = m_world->city(o.island, o.city);
    if (city == nullptr || (o.player != kNoPlayer && city->owner != o.player)) {
        return false;
    }
    switch (o.op) {
        case CityOrder::Op::TaxRate: {
            // 0x0047F130 posts the rate, 0x0047F850 measures the class against it at once.
            const auto& classes = m_buildings->resident_classes();
            if (o.resident_class >= classes.size() || o.resident_class >= city->tax_rate.size() ||
                o.rate < CityOrder::kMinTax || o.rate > CityOrder::kMaxTax) {
                return false;
            }
            city->tax_rate[o.resident_class] = o.rate;
            if (o.resident_class < city->satisfaction.size()) {
                city->satisfaction[o.resident_class] = satisfaction_of(*city, classes[o.resident_class], o.rate);
            }
            return true;
        }
        case CityOrder::Op::Rename:
            if (o.name.empty() || o.name.size() > CityOrder::kMaxNameLength) {
                return false;
            }
            city->name = o.name;
            return true;
        case CityOrder::Op::MaterialLock:
            city->material_lock = o.on;
            return true;
    }
    return false;
}

bool Simulation::game_speed(const GameSpeedOrder& order) {
    switch (order.op) {
        case GameSpeedOrder::Op::SetSpeed:
            if (order.speed == 0) {
                return false;  // pausing is Op::Pause, so that resuming keeps the speed
            }
            set_speed(order.speed);
            return true;
        case GameSpeedOrder::Op::Pause:
            m_paused = true;
            return true;
        case GameSpeedOrder::Op::Resume:
            m_paused = false;
            return true;
    }
    return false;
}

bool Simulation::rename_player(const PlayerRename& order) {
    Player* p = m_world->player(order.player);
    if (p == nullptr || order.name.empty() || order.name.size() > PlayerRename::kMaxNameLength ||
        std::any_of(order.name.begin(), order.name.end(),
                    [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
        return false;  // names come from the network too: no line breaks in lists and files
    }
    p->name = order.name;
    return true;
}

bool Simulation::control_player(const PlayerControl& order) {
    Player* p = m_world->player(order.player);
    // Only the two kinds of players change hands; the computer goes on from where the person was
    // only for a slot it played before (its AI keeps a state for the slots that began as computers).
    if (p == nullptr || !p->active() || (p->role != PlayerRole::Human && p->role != PlayerRole::Computer)) {
        return false;
    }
    p->role = order.computer ? PlayerRole::Computer : PlayerRole::Human;
    return true;
}

void Simulation::handle(const std::vector<ConstructionEvent>& events, std::size_t first) {
    bool storage = false;
    for (std::size_t i = first; i < events.size(); ++i) {
        const ConstructionEvent& e = events[i];
        switch (e.kind) {
            case ConstructionEvent::Kind::Removed:
                if (e.production) {
                    // S-002: figures of a building that is gone end their trip; what they carry
                    // is lost and what they reserved is released.
                    m_logistics.building_removed(*e.production);
                }
                break;
            case ConstructionEvent::Kind::CityFounded:
                m_ai.city_founded(e.island, e.city);
                storage = true;
                break;
            case ConstructionEvent::Kind::StorageChanged:
            case ConstructionEvent::Kind::CityTaken:
            case ConstructionEvent::Kind::CityDissolved:
                storage = true;
                break;
            case ConstructionEvent::Kind::Placed:
                break;
        }
        if (e.kind == ConstructionEvent::Kind::Placed || e.kind == ConstructionEvent::Kind::Removed) {
            m_fleet.island_changed(e.island);
            m_combat.island_changed(e.island);
        }
        m_military.update_towers(e.island);
    }
    if (storage) {
        m_logistics.update_storage();
        // A smaller warehouse loses what does not fit (0x00482120).
        for (std::size_t i = first; i < events.size(); ++i) {
            if (events[i].kind != ConstructionEvent::Kind::StorageChanged) {
                continue;
            }
            if (City* c = m_world->city(events[i].island, events[i].city)) {
                for (std::uint32_t& g : c->goods) {
                    g = std::min(g, c->capacity);
                }
            }
        }
    }
}

std::span<const Simulation::TickStep> Simulation::tick_steps() noexcept {
    // The original runs its subsystems in this order (0x00489670): growth first (0x0047CA80),
    // production (0x0047DAF0), then the cities (0x0047F8A0) and houses (0x0047B9C0), ..., the
    // computer players after the players' tick (AI_Tick 0x0042B4B0) and the figures last
    // (0x00451890). Changing the order changes the game.
    static constexpr std::array<TickStep, 15> kSteps = {{
        {"diplomacy", &Simulation::diplomacy_step},
        {"growth", &Simulation::growth_step},
        {"production", &Simulation::production_step},
        {"cities", &Simulation::cities_step},
        {"city events", &Simulation::city_events_step},
        {"houses", &Simulation::houses_step},
        {"upkeep", &Simulation::upkeep_step},
        {"disasters", &Simulation::disasters_step},
        {"islands", &Simulation::islands_step},
        {"volcanoes", &Simulation::volcanoes_step},
        {"computers", &Simulation::computers_step},
        {"combat", &Simulation::combat_step},
        {"ships", &Simulation::ships_step},
        {"soldiers", &Simulation::soldiers_step},
        {"logistics", &Simulation::logistics_step},
    }};
    return kSteps;
}

std::vector<std::string_view> Simulation::tick_step_names() {
    std::vector<std::string_view> names;
    for (const TickStep& step : tick_steps()) {
        names.push_back(step.name);
    }
    return names;
}

void Simulation::tick() {
    apply_commands();
    if (m_paused) {
        return;  // paused by a command of this tick: the game time stands still
    }
    m_time_ms += kTickMs;
    m_world->set_time_ms(m_time_ms);
    for (const TickStep& step : tick_steps()) {
        (this->*step.run)();
    }
    trim_events();
    if (m_tick_observer) {
        m_tick_observer(*m_world);
    }
}

bool Simulation::elapsed(std::uint32_t& clock, std::uint32_t period) noexcept {
    clock += kTickMs;
    if (clock < period) {
        return false;
    }
    clock -= period;
    return true;
}

void Simulation::trim_events() {
    m_command_results.trim();
    m_construction_events.trim();
    m_production_events.trim();
    m_city_events.trim();
    m_house_events.trim();
    m_island_events.trim();
    m_disaster_events.trim();
    m_volcano_events.trim();
    m_game_events.trim();
    m_ship_events.trim();
    m_military_events.trim();
}

void Simulation::diplomacy_step() {
    // Every 5 s the diplomacy counters fade (Player_Tick 0x00476350; the original takes one
    // player per call, each once in 5 s).
    if (m_time_ms % kDiplomacyTickMs != 0) {
        return;
    }
    for (std::size_t p = 0; p < m_world->players().size(); ++p) {
        m_world->diplomacy().decay(static_cast<PlayerId>(p));
    }
    pirates_reconsider(*m_world);
    players_tick();
}

void Simulation::growth_step() {
    advance_growth(*m_world, *m_buildings, kTickMs);
}

void Simulation::production_step() {
    if (!elapsed(m_production_ms, kProductionPeriodMs)) {
        return;
    }
    const std::size_t first = m_production_events.size();
    advance_production(*m_world, *m_buildings, m_production_events.sink());
    for (std::size_t i = first; i < m_production_events.size(); ++i) {
        m_logistics.handle(m_production_events.events()[i]);
    }
}

void Simulation::cities_step() {
    // The cities consume every 10 s (0x0047F8A0 comes after the production in the original).
    if (!elapsed(m_city_ms, kCityTickMs)) {
        return;
    }
    advance_cities(*m_world, *m_buildings, m_city_events.sink());
    m_military.natives_tick();
}

void Simulation::city_events_step() {
    // Every 60-67 s a city may catch the plague, send out a robber or catch fire (0x0047F8A0).
    advance_city_events(*m_world, *m_buildings, m_disaster_events.sink(), &m_robbers);
    for (const RobberSpawn& r : m_robbers) {
        m_logistics.spawn_robber(r);
    }
    m_robbers.clear();
}

void Simulation::houses_step() {
    advance_houses(*m_world, *m_buildings, kTickMs, m_house_events.sink());
}

void Simulation::upkeep_step() {
    // Soldiers' upkeep of human players every 5 s (0x00476350): a twelfth of the upkeep per minute.
    if (elapsed(m_upkeep_ms, kUpkeepPeriodMs)) {
        pay_upkeep();
    }
}

void Simulation::disasters_step() {
    // Disasters in epochs of 10 s (0x0047A020): they spread, end or burn the building down.
    if (!elapsed(m_disaster_ms, kDisasterEpochMs)) {
        return;
    }
    const std::size_t first = m_construction_events.size();
    advance_disasters(*m_world, *m_buildings, m_disaster_events.sink(), m_construction_events.sink());
    handle(m_construction_events.events(), first);
}

void Simulation::islands_step() {
    // Island epochs of 30 s (0x0046B3E0): droughts, then deer come back and settle. The phase
    // steps every 30 s; an island whose last phase differs is ticked. In a new game that happens
    // at once for islands whose template says so (docs/re/newgame.md).
    const bool epoch = elapsed(m_island_ms, Logistics::kIslandEpochMs);
    if (epoch) {
        m_world->set_island_phase(static_cast<std::uint8_t>(m_world->island_phase() + 1U));
    }
    std::vector<bool>& due = m_islands_due;
    due.assign(m_world->islands().size(), false);
    bool any = false;
    for (std::uint16_t i = 0; i < m_world->islands().size(); ++i) {
        WorldIsland& island = m_world->island(i);
        if (island.last_phase != m_world->island_phase()) {
            island.last_phase = m_world->island_phase();
            due[i] = true;
            any = true;
        }
    }
    if (any) {
        advance_island_events(
            *m_world, *m_buildings, m_rules,
            [this](std::uint16_t island, TilePos tile) { return m_logistics.field_reserved(island, tile); },
            m_island_events.sink(), &due);
    }
    if (epoch) {
        // The eruption roll; the original makes it only while no drought roll is due (S-042).
        roll_eruptions(*m_world, m_rules, m_volcano_events.sink());
    }
    if (any) {
        m_logistics.advance_islands(&due);
    }
}

void Simulation::volcanoes_step() {
    // Eruptions and lava (figures of the original, kinds 0x20 and 0x21).
    const std::size_t first = m_construction_events.size();
    const assets::FigureType* lava = m_figures->get(kFigureLava);
    advance_volcanoes(*m_world, *m_buildings, lava != nullptr && lava->speed != 0 ? lava->speed : kLavaSpeed,
                      m_volcano_events.sink(), m_disaster_events.sink(), m_construction_events.sink());
    handle(m_construction_events.events(), first);
}

void Simulation::computers_step() {
    m_ai.tick(kTickMs);
}

void Simulation::combat_step() {
    // Figures last: first who sees whom, then ships and shipyards, then soldiers, towers and the
    // shots that land.
    m_combat.update();
}

void Simulation::ships_step() {
    m_fleet.tick(kTickMs);
}

void Simulation::soldiers_step() {
    const std::size_t first = m_construction_events.size();
    m_military.tick(kTickMs, m_construction_events.sink());
    handle(m_construction_events.events(), first);
    m_military_events.append(m_military.take_events());
    // Castles fetch their weapons with a pack donkey, like shipyards their wood.
    for (const Military::FetchRequest& r : m_military.take_fetch_requests()) {
        if (!send_yard_carrier(r.island, r.x, r.y, r.ware)) {
            m_military.castle_delivery(r.island, r.x, r.y, r.ware, 0);
        }
    }
    for (const Fleet::FetchRequest& r : m_fleet.take_fetch_requests()) {
        if (!send_yard_carrier(r.island, r.x, r.y, r.ware)) {
            m_fleet.yard_delivery(r.island, r.x, r.y, r.ware, 0);
        }
    }
    m_ship_events.append(m_fleet.take_events());
}

bool Simulation::send_yard_carrier(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware) {
    const assets::BuildingType* building = nullptr;
    if (island < m_world->islands().size()) {
        const MapCell& c = m_world->islands()[island].map.top(x, y);
        building = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite);
    }
    return building != nullptr && m_logistics.send_yard_carrier(island, TilePos{x, y}, ware, building->worker_figure);
}

void Simulation::logistics_step() {
    m_logistics.tick();
    for (const Logistics::YardDelivery& d : m_logistics.take_yard_deliveries()) {
        // Shipyards and castles (each ignores what is not its own).
        const auto x = static_cast<std::uint16_t>(d.origin.x);
        const auto y = static_cast<std::uint16_t>(d.origin.y);
        m_fleet.yard_delivery(d.island, x, y, d.ware, d.amount);
        m_military.castle_delivery(d.island, x, y, d.ware, d.amount);
    }
    m_disaster_events.append(m_logistics.take_disaster_events());
}
void Simulation::pay_upkeep() {
    const std::vector<std::uint32_t> upkeep = military_upkeep(*m_world, *m_figures);
    for (std::size_t p = 0; p < upkeep.size(); ++p) {
        // Computer players pay nothing, as in the original (0x00475B00).
        Player* player = m_world->player(static_cast<PlayerId>(p));
        if (player != nullptr && player->role == PlayerRole::Human) {
            player->money = money_plus(player->money, -static_cast<std::int64_t>(upkeep[p] / 12));
        }
    }
}

void Simulation::players_tick() {
    // Player_Tick 0x00476350: each player once per 5 s; nothing more once the game is over.
    if (m_world->game_end()) {
        return;
    }
    std::size_t humans = 0;
    for (const Player& p : m_world->players()) {
        humans += p.role == PlayerRole::Human && p.status != PlayerStatus::Absent ? 1 : 0;
    }
    const bool multiplayer = humans > 1;
    for (std::size_t i = 0; i < m_world->players().size(); ++i) {
        const auto id = static_cast<PlayerId>(i);
        Player* player = m_world->player(id);
        if (player == nullptr || !player->active()) {
            continue;
        }
        Player& p = *player;
        p.purchases = p.purchases * 179 / 180;
        p.sales = p.sales * 179 / 180;
        const MissionCheck check = check_mission(*m_world, *m_buildings, *m_figures, id);
        if (check.won) {
            accomplish(id);
            return;
        }
        const bool city = owns_city(*m_world, id);
        const bool ship = owns_ship(*m_world, id);
        switch (p.role) {
            case PlayerRole::Pirates:
                // Only when a mission asks to defeat them (settings bit 5).
                if ((m_world->game_settings().flags & World::GameSettings::kPirateGoal) != 0 && !city && !ship) {
                    defeat(id, GameEvent::Reason::Retired, true);
                }
                break;
            case PlayerRole::Computer:
                // The AI root retires a computer with no city and no ship (0x00429070).
                if (!city && !ship) {
                    defeat(id, GameEvent::Reason::Retired, true);
                }
                break;
            case PlayerRole::Human:
                if (check.lost) {
                    defeat(id, GameEvent::Reason::MissionFailed, true);
                } else if (city || ship) {
                    p.empty_checks = 0;
                    // Bankrupt after 40 checks in a row; the original never resets the counter,
                    // so 40 bad checks over the whole game are enough (S-107).
                    if (p.money <= -1001 && player_balance(*m_world, *m_buildings, *m_figures, id) <= 0) {
                        if (++p.bankrupt_checks >= kDefeatChecks) {
                            defeat(id, GameEvent::Reason::Bankrupt, false);
                        }
                    } else {
                        p.bankrupt_checks = 0;
                    }
                } else if (!multiplayer || ++p.empty_checks >= kDefeatChecks) {
                    // Nothing left: at once alone, after 200 s with other humans in the game.
                    defeat(id, GameEvent::Reason::NoPossessions, true);
                }
                break;
            case PlayerRole::Trader:
            case PlayerRole::Natives:
                break;
        }
    }
}

void Simulation::accomplish(PlayerId winner) {
    // Op 0x3E (0x00476950): the winner, and every other human or computer who meets his own
    // mission at this moment; the original checks players without a mission against mission 0
    // (S-106). The game is over for everybody.
    const auto win = [&](PlayerId id) {
        Player* p = m_world->player(id);
        if (p == nullptr) {
            return;
        }
        p->won = true;
        const Mission* m = m_world->mission_of(*p);
        m_game_events.push(GameEvent{GameEvent::Kind::MissionAccomplished, id, kNoPlayer, GameEvent::Reason::None,
                                     m != nullptr ? m->success_message : std::uint16_t{0}});
    };
    win(winner);
    for (std::size_t i = 0; i < m_world->players().size(); ++i) {
        const auto id = static_cast<PlayerId>(i);
        if (id != winner && m_world->players()[i].active() &&
            check_mission(*m_world, *m_buildings, *m_figures, id).won) {
            win(id);
        }
    }
    m_world->set_game_end(World::GameEnd{m_time_ms, winner});
    m_game_events.push(GameEvent{GameEvent::Kind::GameOver, winner});
}

void Simulation::defeat(PlayerId id, GameEvent::Reason reason, bool blame) {
    // Op 3 sub 9 (0x004766A0): the player is out. Unless he went bankrupt, the player blamed for
    // it (who last destroyed his Kontor or market) counts him as defeated and, with 200
    // inhabitants of the third class or higher, may build a triumphal arch. The original does
    // not credit him on the defeated player's computer (S-105).
    Player* p = m_world->player(id);
    if (p == nullptr) {
        return;
    }
    p->status = PlayerStatus::Defeated;
    const PlayerId by = blame ? m_world->diplomacy().blamed(id) : kNoPlayer;
    m_game_events.push(GameEvent{GameEvent::Kind::PlayerDefeated, id, by, reason});
    Player* k = by != id ? m_world->player(by) : nullptr;
    if (k == nullptr || p->role == PlayerRole::Natives) {
        return;
    }
    k->opponents_defeated = static_cast<std::uint16_t>(std::min(k->opponents_defeated + 1, 0xFFFF));
    std::uint64_t upper = 0;
    for (const City& c : m_world->cities()) {
        if (c.owner == by) {
            for (std::size_t r = kArchClass; r < c.inhabitants.size(); ++r) {
                upper += c.inhabitants[r];
            }
        }
    }
    if (upper >= kArchInhabitants) {
        k->arches_allowed = static_cast<std::uint16_t>(std::min(k->arches_allowed + 1, 0xFFFF));
        m_game_events.push(GameEvent{GameEvent::Kind::ArchAllowed, by, id});
    }
}

std::vector<ShipEvent> Simulation::take_ship_events() {
    m_ship_events.append(m_fleet.take_events());  // events of commands applied outside a tick
    return m_ship_events.take();
}

}  // namespace o1602::sim
