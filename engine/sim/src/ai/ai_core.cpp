// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scheduler, task stack, root task and (re)initialisation of the computer players
// (docs/re/ai.md, "Flow and tick").
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "ai_archive.hpp"
#include "ai_engine.hpp"

#include "o1602/sim/ai.hpp"

namespace o1602::sim {

namespace ai {

namespace {

constexpr std::uint64_t kReinitMs = 3'600'000;  // 0x00429070: re-init after 3600 s
constexpr std::uint32_t kDiplomacyMs = 30'000;  // P+0x83 > 5: six phases of 5 s
constexpr std::uint32_t kStateVersion = 1;

/// Crop the first colony of a computer must grow fully (0x00429070), by its slot.
std::uint8_t first_crop(PlayerId player) {
    switch (player % 4) {
        case 1:
            return ware::kSpiceField;
        case 2:
            return ware::kTobaccoField;
        case 3:
            return ware::kWineField;
        default:
            return ware::kOre;
    }
}

}  // namespace

Engine::Engine(Simulation& sim, State& state)
    : m_sim(sim), m_world(*sim.m_world), m_buildings(*sim.m_buildings), m_state(state) {
    // The producer table of AI_Init (0x0042B710): per ware up to four records in file order; a
    // record without raw material takes only the first place (the original's quirk, kept: it decides
    // which record a Need picks).
    m_producers.resize(256);
    for (const assets::BuildingType& b : m_buildings.types()) {
        if (b.ware <= 1 || b.id == 0) {
            continue;
        }
        auto& list = m_producers[b.ware];
        if (b.raw_ware != 0) {
            if (list.size() < 4) {
                list.push_back(&b);
            }
        } else if (list.empty()) {
            list.push_back(&b);
        }
    }
}

CommandResult Engine::execute(const Command& command) {
    return m_sim.execute(command);
}

void Engine::log(const AiPlayer& p, const char* format, ...) const {
    static const bool enabled = std::getenv("O1602_AI_LOG") != nullptr;
    if (!enabled) {
        return;
    }
    std::fprintf(stderr, "[ai %u %llus] ", p.player, static_cast<unsigned long long>(now_ms() / 1000));
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

// ---------------------------------------------------------------------------------------------
// Scheduler

void Engine::reset() {
    m_state.players.clear();
    for (std::size_t i = 0; i < m_world.players().size(); ++i) {
        const Player& pl = m_world.players()[i];
        if (pl.role != PlayerRole::Computer && pl.role != PlayerRole::Pirates) {
            continue;
        }
        AiPlayer p;
        p.player = static_cast<PlayerId>(i);
        p.pirates = pl.role == PlayerRole::Pirates;
        init_player(p);
        m_state.players.push_back(std::move(p));
    }
    m_state.islands.assign(m_world.islands().size(), IslandWatch{});
    for (IslandWatch& w : m_state.islands) {
        w.next_ms = now_ms() + 1000ULL * rng().below(16);
    }
    m_state.island_cursor = 0;
    m_state.max_score = 0;
    m_state.initialised = true;
}

void Engine::tick(std::uint32_t dt_ms) {
    if (!m_state.initialised) {
        reset();
    }
    if (m_state.islands.size() != m_world.islands().size()) {
        m_state.islands.resize(m_world.islands().size());
    }
    const auto dt = static_cast<std::int32_t>(dt_ms);
    island_controller();
    for (AiPlayer& p : m_state.players) {
        const Player* pl = m_world.player(p.player);
        if (pl == nullptr || !pl->active()) {
            continue;  // defeated players do nothing (controller 9)
        }
        if (p.pirates) {
            if (pl->role != PlayerRole::Pirates) {
                continue;
            }
            // The pirates take one step every 10 s (0x00429FD0).
            p.t_main -= dt;
            if (p.t_main <= 0) {
                p.t_main = kPirateStepMs;
                pirate_step(p);
            }
            continue;
        }
        if (pl->role != PlayerRole::Computer) {
            continue;  // the slot was taken over by a human
        }
        // Each computer steps every 50 ms of game time. The original lets only one slot act per
        // simulation slice, so its pace followed the frame rate and the number of computers (S-095).
        p.t_main -= dt;
        if (p.t_main <= 0) {
            step(p);
            p.t_main = std::max(p.t_main + kStepMs, -kStepMs);
        }
        if (p.t_6s >= 0) {
            p.t_6s -= dt;
        }
        if (p.t_10s >= 0) {
            p.t_10s -= dt;
        }
        if (p.has_plan && p.t_6s < 0) {
            buy_pirate_peace(p);
            recruit(p);
            p.t_6s = 6'000;  // absolute: the period is 6 s plus the wait
        }
        if (p.has_plan && p.t_10s < 0) {
            logistics(p);  // additive: the 10-s phase is kept
            p.t_10s += 10'000;
        }
        // A treaty that changed is answered at once (the original sets P+0x83 to 6 then).
        const std::size_t players = m_world.players().size();
        p.seen_treaties.resize(players, 0xFF);
        for (std::size_t b = 0; b < players; ++b) {
            const Relation& rel = m_world.diplomacy().relation(p.player, static_cast<PlayerId>(b));
            const auto now = static_cast<std::uint8_t>(static_cast<std::uint8_t>(rel.peace) << 4 |
                                                       static_cast<std::uint8_t>(rel.trade));
            if (p.seen_treaties[b] != now) {
                p.diplomacy_due = p.diplomacy_due || p.seen_treaties[b] != 0xFF;
                p.seen_treaties[b] = now;
            }
        }
        p.diplomacy_ms += dt_ms;
        if (p.diplomacy_ms >= kDiplomacyMs || p.diplomacy_due) {
            p.diplomacy_ms = 0;
            p.diplomacy_due = false;
            diplomacy(p);
            check_city_loss(p);
        }
    }
}

bool Engine::directs(std::uint32_t ship) const {
    for (const AiPlayer& p : m_state.players) {
        if (p.settle.active && p.settle.ship == ship) {
            return true;
        }
    }
    return false;
}

void Engine::step(AiPlayer& p) {
    const std::uint8_t id = top(p);
    run_task(p);
    if (id != kRoot && !p.pirates) {
        target_scan(p);  // one foreign city after every step (0x004298F0)
    }
    m_state.max_score = std::max(m_state.max_score, p.score);
}

void Engine::run_task(AiPlayer& p) {
    const std::uint8_t id = top(p);
    if (id > kSettleLand) {
        if (!city_valid(p, true)) {
            return;
        }
        if (!p.pirates) {
            sweep(p);
        }
    }
    switch (id) {
        case kRoot:
            root(p);
            return;
        case kSettleArea:
            task_settle_area(p);
            break;
        case kSettleSite:
            task_settle_site(p);
            break;
        case kSettleLand:
            task_settle_land(p);
            break;
        case kDevelop:
            if (p.pirates) {
                pirate_develop(p);
            } else {
                task_develop(p);
            }
            break;
        case kRoads:
            task_roads(p);
            break;
        case kLayout:
            task_layout(p);
            break;
        case kSurvey:
            task_survey(p);
            break;
        case kAbandon:
            task_abandon(p);
            break;
        case kKontorUpgrade:
            task_kontor_upgrade(p);
            break;
        case kPaving:
            task_paving(p);
            break;
        case kTowers:
            task_towers(p);
            break;
        case kPirateHouses:
            if (p.pirates) {
                task_pirate_houses(p);
            } else {
                pop(p);
            }
            break;
        default:
            if (id >= kWoodcutter && id <= kDenkmal) {
                task_building(p, id);
            } else {
                pop(p);  // unknown ids never stay on the stack
            }
            break;
    }
}

// ---------------------------------------------------------------------------------------------
// Task stack (0x0040FB80..0x0040FC30)

std::uint8_t Engine::top(const AiPlayer& p) const {
    return p.stack.empty() ? std::uint8_t{kRoot} : p.stack.back();
}

void Engine::pop(AiPlayer& p) {
    if (!p.stack.empty()) {
        p.stack.pop_back();
    }
}

void Engine::clear_stack(AiPlayer& p) {
    p.stack.clear();
}

bool Engine::push(AiPlayer& p, std::uint8_t task, bool force) {
    if (p.stack.size() >= kMaxStack) {
        return false;
    }
    if (!force && p.budget <= 0) {
        return false;
    }
    if (std::find(p.stack.begin(), p.stack.end(), task) != p.stack.end()) {
        return false;
    }
    p.stack.push_back(task);
    if (!force) {
        --p.budget;
    }
    return true;
}

bool Engine::city_valid(AiPlayer& p, bool clear) {
    const City* c = p.has_city ? city(p.island, p.city) : nullptr;
    if (c != nullptr && c->owner == p.player) {
        return true;
    }
    if (clear) {
        // Open1602 gives back what a dropped building plan had taken (the original loses it).
        refund(p);
        clear_stack(p);
        p.has_city = false;
        p.budget = 0;
    }
    return false;
}

void Engine::check_city_loss(AiPlayer& p) {
    // 0x0042A270: fewer cities than at the last init -> start over.
    if (own_city_count(p.player) < p.cities) {
        init_player(p);
    }
}

// ---------------------------------------------------------------------------------------------
// Root task (0x00429070)

void Engine::root(AiPlayer& p) {
    if (p.settle.active) {
        if (p.settle.sailing) {
            // The ship is on its way. The original keeps task 4 on the stack meanwhile, so all its
            // cities wait, S-097; Open1602 lets them develop and lands when the ship stops.
            const Ship* ship = settle_ship(p);
            if (ship == nullptr) {
                end_settlement(p);
            } else if (ship->target.empty() && ship->motion.phase != ShipMotion::Phase::Run &&
                       ship->motion.phase != ShipMotion::Phase::Turn) {
                push(p, kSettleLand, true);
                return;
            }
        } else {
            pick_island(p);
            if (!p.stack.empty()) {
                return;  // an island to look at
            }
            if (p.settle.failed) {
                p.settle.ship = 0;
                p.settle.previous_crop = p.settle.crop;
                p.settle.crop = 0;
                p.settle.active = false;
            } else if (!p.has_city) {
                return;
            }
        }
    }
    if (now_ms() - p.init_ms > kReinitMs) {
        init_player(p);
    }
    if (p.has_city && p.budget > 0) {
        push(p, kDevelop, false);
        return;
    }
    // Round robin over the cities of the world: the next city of this owner.
    const auto& cities = m_world.cities();
    bool selected = false;
    for (std::size_t n = 0; n < cities.size(); ++n) {
        p.city_cursor = static_cast<std::uint32_t>((p.city_cursor + 1) % cities.size());
        const City& c = cities[p.city_cursor];
        if (c.owner == p.player) {
            p.has_city = true;
            p.island = c.island;
            p.city = c.index;
            selected = true;
            break;
        }
    }
    if (selected) {
        CityPlan& r = plan_of(p, p.island, p.city);
        p.has_plan = true;
        p.budget = p.cities < 3 ? kBudgetFew : kBudgetMany;
        if ((r.flags & city_flag::kAbandon) != 0) {
            push(p, kAbandon, false);
            return;
        }
        p.skip_layout = false;
        r.flags &= ~city_flag::kSurveyed;
        kontor_restore(p);
    } else {
        p.has_city = false;
    }
    if (!p.has_city) {
        const std::vector<Ship*> ships = own_ships(p);
        if (ships.empty()) {
            // Retired by the players' check every 5 s (Simulation::players_tick, 0x00476290).
            return;
        }
        if (p.settle.active) {
            return;  // the ship is under way
        }
        if (!p.settle.failed) {
            // Colonise: the island cursor starts at a random island of the first sixteen (the
            // original takes the clock, GetTickCount() & 15, S-095).
            p.settle.cursor = static_cast<std::uint16_t>(rng().below(16));
            p.settle.need = p.pirates ? 150 : 1800;
            p.settle.floor = 300;
            p.settle.crop = p.pirates ? 0 : first_crop(p.player);
            p.settle.active = true;
            pick_island(p);
        }
        // A computer whose search failed for good hands its ship to the pirates in the original
        // (ship op 5 value 5); Open1602 keeps it (S-097).
        return;
    }
    if (push(p, kDevelop, false)) {
        const CityPlan* r = current_plan(p);
        if (r != nullptr && (r->flags & city_flag::kSurveyed) == 0) {
            push(p, kSurvey, true);
        }
        if (!p.skip_layout) {
            push(p, kLayout, true);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// (Re)initialisation (0x0040F580)

void Engine::init_player(AiPlayer& p) {
    refund(p);
    const PlayerId id = p.player;
    const bool pirates = p.pirates;
    // The jobs of the ships stay: the original keeps them on the ships themselves (ship +0x1A7,
    // +0x1AC, +0x1AE), which its re-initialisation does not touch.
    std::vector<ShipNote> jobs = std::move(p.ships);
    p = AiPlayer{};
    p.player = id;
    p.pirates = pirates;
    p.ships = std::move(jobs);
    forget_ships(p);
    take_original_jobs(p);
    p.settle.need = pirates ? 150 : 1800;
    p.settle.floor = 300;
    p.t_6s = 3000;
    p.t_main = static_cast<std::int32_t>(rng().below(4096));
    p.t_10s = 20000;
    p.init_ms = now_ms();
    p.refresh_ms = now_ms();
    p.previous_mood.assign(m_world.players().size(), 0);
    for (std::size_t b = 0; b < m_world.players().size(); ++b) {
        p.previous_mood[b] = m_world.diplomacy().mood(m_world, id, static_cast<PlayerId>(b), 0, now_ms());
    }
    for (const City& c : m_world.cities()) {
        if (c.owner != id) {
            continue;
        }
        ++p.cities;
        CityPlan& r = plan_of(p, c.island, c.index);
        r.land = free_land(c.island, c.index, &r.land_percent);
    }
    choose_home(p);
    const std::size_t ships = own_ships(p).size();
    p.ships_per_city = p.cities < 2 ? 1 : static_cast<std::int32_t>(ships / p.cities + 1);
    if (pirates) {
        p.t_main = ships == 0 && p.cities == 0 ? 360'000 : 10'000;
        p.t_6s = 120'000;
    }
}

void Engine::city_founded(std::uint16_t island, std::uint8_t index) {
    // 0x004294C0 with event 0x0E (from 0x00465170): a computer with a city on the island, at peace
    // with the founder, whose home city lies on another island and whose city here has fewer than
    // 10 inhabitants (0x0040F430(city, 0, 10) == 0) takes it ill (event 5).
    const City* founded = city(island, index);
    if (founded == nullptr) {
        return;
    }
    const PlayerId founder = founded->owner;
    for (AiPlayer& p : m_state.players) {
        if (p.pirates || p.player == founder || !p.home_valid || p.home_island == island) {
            continue;
        }
        if (m_world.diplomacy().relation(p.player, founder).peace != Treaty::Signed) {
            continue;
        }
        for (const City& c : m_world.cities()) {
            if (c.owner == p.player && c.island == island && population(c) < 10) {
                m_world.diplomacy().add_event(p.player, founder, DiplomacyEventType::Settled, now_ms());
                log(p, "%u founded a city next to ours on island %u", founder, island);
                break;
            }
        }
    }
}

void Engine::take_original_jobs(AiPlayer& p) {
    // An original savegame keeps the jobs on its ships (SHIP4 +0x24 mission, +0x3E parameter,
    // +0x40 counter). Taken over: missions with a city (1 fetch troops, 2 go home, 5 and 6 attack,
    // parameter island number * 32 + city), and with mission 0 a patrol (a looping route round the
    // island in the parameter) or a cooldown. The rest starts over.
    for (Ship& s : m_world.ships()) {
        if (s.owner != p.player || !s.original_job) {
            continue;
        }
        const Ship::OriginalJob job = *s.original_job;
        s.original_job.reset();
        ShipNote& n = note(p, s.id);
        const auto island = m_world.island_index(job.param / 32U);
        switch (job.mission) {
            case 1:
            case 2:
            case 5:
            case 6:
                if (island) {
                    n.mission = static_cast<std::uint8_t>(job.mission);
                    n.param = std::uint32_t{*island} << 8 | (job.param % 32U);
                    n.counter = job.counter;
                }
                break;
            case 0:
                if (const auto patrol = m_world.island_index(job.param); patrol && s.route.active && !s.route.one_way) {
                    n.patrol = true;
                    n.patrol_island = *patrol;
                    n.counter = job.counter;
                } else {
                    n.cooldown = static_cast<std::uint8_t>(std::min<std::uint32_t>(job.param, 255));
                }
                break;
            default:
                break;
        }
    }
}

void Engine::choose_home(AiPlayer& p) {
    // The last city with at least 10 inhabitants of class 2 or higher, unless a later one has more
    // free land (0x0040F580).
    const auto& cities = m_world.cities();
    std::uint32_t best = 0;
    std::optional<std::size_t> home;
    for (std::size_t i = 0; i < cities.size(); ++i) {
        const City& c = cities[i];
        if (c.owner != p.player) {
            continue;
        }
        const CityPlan& r = plan_of(p, c.island, c.index);
        if (has_population(c, 2, 10) || r.land > best) {
            home = i;
            best = r.land;
        }
    }
    p.home_valid = home.has_value();
    if (home) {
        p.home_island = cities[*home].island;
        p.home_city = cities[*home].index;
        p.city_cursor = static_cast<std::uint32_t>(*home == 0 ? cities.size() - 1 : *home - 1);
    }
}

}  // namespace ai

// ---------------------------------------------------------------------------------------------
// ComputerPlayers

ComputerPlayers::ComputerPlayers(Simulation& simulation)
    : m_state(std::make_unique<ai::State>()), m_engine(std::make_unique<ai::Engine>(simulation, *m_state)) {}

ComputerPlayers::~ComputerPlayers() = default;

void ComputerPlayers::tick(std::uint32_t dt_ms) {
    m_engine->tick(dt_ms);
}

void ComputerPlayers::reset() {
    m_engine->reset();
}

void ComputerPlayers::save(core::ByteWriter& out) const {
    out.u32(ai::kStateVersion);
    ai::Saver saver(out);
    saver(*m_state);
}

bool ComputerPlayers::load(core::ByteReader& in) {
    StateIn reader(in);
    if (reader.u32() != ai::kStateVersion || !reader.ok()) {
        return false;
    }
    ai::State state;
    ai::Loader loader(reader);
    loader(state);
    if (!reader.ok()) {
        return false;
    }
    *m_state = std::move(state);
    return true;
}

bool ComputerPlayers::directs(std::uint32_t ship) const {
    return m_engine->directs(ship);
}

void ComputerPlayers::city_founded(std::uint16_t island, std::uint8_t city) {
    m_engine->city_founded(island, city);
}

std::vector<ComputerPlayers::View> ComputerPlayers::view() const {
    std::vector<View> out;
    for (const ai::AiPlayer& p : m_state->players) {
        View v;
        v.player = p.player;
        v.cities = p.cities;
        v.max_stage = p.max_stage;
        v.score = p.score;
        v.settling = p.settle.active;
        v.settle_failed = p.settle.failed;
        v.stack = p.stack;
        for (const ai::CityPlan& r : p.plans) {
            View::City c{r.island, r.city, r.stage, r.flags, {}};
            for (const ai::SlotState& s : r.slots) {
                c.slots.push_back({s.count, s.attempts, s.fail_level});
            }
            v.plans.push_back(std::move(c));
        }
        out.push_back(std::move(v));
    }
    return out;
}

}  // namespace o1602::sim
