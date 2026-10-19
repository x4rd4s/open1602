// SPDX-License-Identifier: GPL-3.0-or-later
//
// The machinery of the computer players (docs/re/ai.md). One Engine works on the shared State for
// the duration of a call; the methods are spread over several files by subsystem:
//   ai_core.cpp      scheduler, task stack, root task, (re)initialisation, saving
//   ai_world.cpp     what the AI reads from the world (tiles, cities, land)
//   ai_settle.cpp    finding an island and founding a city
//   ai_planner.cpp   task 5: refresh of the city, stages and the Need list
//   ai_sites.cpp     road segments and the building site searches
//   ai_build.cpp     the building tasks, roads, fields, survey and the maintenance sweep
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ai_state.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

// Lets GCC and Clang check the arguments of printf-style functions (index counts 'this' as 1).
#if defined(__GNUC__)
#define O1602_PRINTF_FORMAT(fmt, first) __attribute__((format(printf, fmt, first)))
#else
#define O1602_PRINTF_FORMAT(fmt, first)
#endif

namespace o1602::sim::ai {

// The ware, Kind and production numbers and the building Ids the AI refers to are in
// o1602/sim/game_data.hpp.

/// A ship that has a target or is running or turning: the AI leaves it alone.
[[nodiscard]] inline bool ship_moving(const Ship& s) noexcept {
    return !s.target.empty() || s.motion.phase == ShipMotion::Phase::Run || s.motion.phase == ShipMotion::Phase::Turn;
}

/// What the survey of a city (task 8) found to clear away: exhausted mines, and sheep farms and
/// small weavers for when cotton grows on the island.
struct SurveyFinds {
    std::vector<std::pair<std::int32_t, std::int32_t>> starved_mines;
    std::vector<std::pair<std::int32_t, std::int32_t>> sheep;
    std::vector<std::pair<std::int32_t, std::int32_t>> webstuben;
};

/// One entry of the build order of a city's stage (run_stage): something the city may need, tried
/// in the order of the stage's table.
struct StageNeed {
    enum class Kind : std::uint8_t {
        Building,      ///< need(slot, n)
        Market,        ///< need_market(n)
        Houses,        ///< need_houses(n)
        Public,        ///< need_public(slot, n, flag)
        Kontor,        ///< need_kontor(n): the Kontor's level
        Castle,        ///< need_castle()
        Monument,      ///< need_denkmal()
        Shipyard,      ///< need_shipyard()
        Processing,    ///< processing()
        IronTools,     ///< iron_tools()
        Weapons,       ///< weapons()
        Luxury,        ///< luxury(n, flag)
        ColonyLuxury,  ///< colony_luxury(): one luxury field per n tiles of land
        Tower,         ///< need_tower(1, n)
        RoadsIfMason,  ///< roads_if_mason()
        Colony,        ///< want_colony(n): n is the mode
    };
    /// A condition on the gold the player had when the stage ran.
    enum class When : std::uint8_t { Never, Always, Gold4000, Gold5000, Gold8000 };
    Kind kind = Kind::Building;
    std::uint8_t slot = 0;
    std::uint32_t n = 0;
    When flag = When::Never;   ///< Public: `cov`; Luxury: `exclusive`
    When when = When::Always;  ///< the entry is tried only then
};

/// Game ms between two steps of a computer player (the original's 50 ms main timer).
inline constexpr std::int32_t kStepMs = 50;
/// Game ms between two steps of the pirates.
inline constexpr std::int32_t kPirateStepMs = 10'000;
/// Pushes a city visit allows: with fewer than three cities, and with more.
inline constexpr std::int32_t kBudgetFew = 16;
inline constexpr std::int32_t kBudgetMany = 8;
inline constexpr std::size_t kMaxStack = 256;
inline constexpr std::int32_t kTon = 32;

/// A tile as the AI reads it: the record of the top layer, growing crops taken as their ripe
/// record (the original's normalisation of ROHSTWACHS tiles).
struct Tile {
    const assets::BuildingType* type = nullptr;
    std::int32_t kind = -1;
    std::int32_t prod = -1;
    std::uint8_t ware = 0;
    std::uint8_t raw = 0;
    std::uint8_t city = MapCell::kNoCity;
    PlayerId owner = kNoPlayer;
    bool origin = false;  ///< the reference tile of its building (0x004633E0)
    std::int32_t ox = 0;  ///< origin of the building
    std::int32_t oy = 0;
    std::uint8_t growth = 0;  ///< Randwachs * 32 / 128
    [[nodiscard]] bool valid() const noexcept { return type != nullptr; }
};

/// Accumulator of a ring iterator callback (9 dwords in the original).
struct Acc {
    std::int32_t flags = 0;  ///< [0]
    bool market = false;     ///< [1]: a Kontor or market is near
    bool road = false;       ///< [2]
    std::int32_t value = 0;  ///< [3]: a minimum distance and the like
    std::int32_t count = 0;  ///< [4]
    std::int32_t score = 0;  ///< [5]
    std::int32_t extra = 0;  ///< [6]
};

/// Which tiles a ring iterator visits.
enum class RingFilter : std::uint8_t {
    Own,       ///< tiles of the current city (0x00410A80)
    OwnOrSea,  ///< and open sea (0x00410800)
    Any,       ///< everything (0x00410C90)
};

/// A Need of the stage handlers (0x0041FE60..0x00422610): what to build and when.
enum class NeedType : std::uint8_t { A, B, C, D };

class Engine {
public:
    Engine(Simulation& sim, State& state);

    // ai_core.cpp
    void tick(std::uint32_t dt_ms);
    void reset();
    void init_player(AiPlayer& p);
    /// Gives every computer slot without a state one (reset() makes them for the slots of the start).
    void adopt_new_computers();

private:
    // ---- ai_core.cpp ----
    void step(AiPlayer& p);
    void run_task(AiPlayer& p);
    void root(AiPlayer& p);
    [[nodiscard]] std::uint8_t top(const AiPlayer& p) const;
    void pop(AiPlayer& p);
    void clear_stack(AiPlayer& p);
    bool push(AiPlayer& p, std::uint8_t task, bool force);
    bool city_valid(AiPlayer& p, bool clear);
    void check_city_loss(AiPlayer& p);
    void choose_home(AiPlayer& p);
    void take_original_jobs(AiPlayer& p);
    CommandResult execute(const Command& command);
    /// Debug trace to stderr when O1602_AI_LOG is set.
    void log(const AiPlayer& p, const char* format, ...) const O1602_PRINTF_FORMAT(3, 4);

    // ---- ai_world.cpp ----
    [[nodiscard]] Tile tile(std::uint16_t island, std::int32_t x, std::int32_t y) const;
    [[nodiscard]] bool inside(std::uint16_t island, std::int32_t x, std::int32_t y) const;
    [[nodiscard]] const WorldIsland& island_of(std::uint16_t island) const { return m_world.islands()[island]; }
    [[nodiscard]] City* city(std::uint16_t island, std::uint8_t index) { return m_world.city(island, index); }
    [[nodiscard]] const City* city(std::uint16_t island, std::uint8_t index) const {
        return m_world.city(island, index);
    }
    [[nodiscard]] City* current_city(AiPlayer& p);
    [[nodiscard]] const City* current_city(const AiPlayer& p) const;
    [[nodiscard]] City* home_city(AiPlayer& p);
    [[nodiscard]] CityPlan& plan_of(AiPlayer& p, std::uint16_t island, std::uint8_t city);
    [[nodiscard]] CityPlan* current_plan(AiPlayer& p);
    [[nodiscard]] Player& player(const AiPlayer& p) {
        Player* pl = m_world.player(p.player);
        return pl != nullptr ? *pl : m_nobody;
    }
    [[nodiscard]] const Player& player(const AiPlayer& p) const {
        const Player* pl = m_world.player(p.player);
        return pl != nullptr ? *pl : m_nobody;
    }
    [[nodiscard]] std::uint32_t own_city_count(PlayerId owner) const;
    /// Inhabitants of classes `from` and higher (0x0040F430 compares with it).
    [[nodiscard]] std::uint32_t population(const City& c, std::size_t from = 0) const;
    [[nodiscard]] bool has_population(const City& c, std::size_t from, std::uint32_t n) const {
        return population(c, from) >= n;
    }
    /// Demand of a city for a consumer good, 1/32 t per minute (0x0047F7B0).
    [[nodiscard]] std::uint32_t demand(const City& c, std::uint8_t ware) const;
    [[nodiscard]] std::uint32_t kontor_level(const City& c) const;
    /// Markets and Kontors of a city (city + 0x1FA) and all its buildings (city + 0x216).
    [[nodiscard]] std::uint32_t market_count(const City& c) const;
    [[nodiscard]] std::uint32_t building_count(const City& c) const;
    /// Usable land of a city: the island's tiles minus those of other cities, coast, rock and
    /// rivers (0x0040E8B0); `percent` out.
    [[nodiscard]] std::uint32_t free_land(std::uint16_t island, std::uint8_t city, std::uint32_t* percent) const;
    /// Civilisation level 0..4 (0x0040F3A0): highest of INFRA_STUFE_2D, 3C, 4B, 5A unlocked.
    [[nodiscard]] std::uint32_t civ_level(const AiPlayer& p) const;
    [[nodiscard]] bool unlocked(const AiPlayer& p, const assets::BuildingType& b) const;
    /// Money and goods of the current city cover the building (0x0041D120).
    [[nodiscard]] bool affordable(const AiPlayer& p, const City& c, const assets::BuildingType& b) const;
    /// First producer of `ware` from `raw` (0x0040FE00): the table the original builds at AI_Init.
    [[nodiscard]] const assets::BuildingType* producer(std::uint8_t ware, std::uint8_t raw) const;
    [[nodiscard]] const assets::BuildingType* by_id(std::uint32_t id) const { return m_buildings.by_id(id); }
    /// Fertility of an island for a crop or resource (0x0046AFF0): 0, 64 or 128.
    [[nodiscard]] std::uint32_t fertility(std::uint16_t island, std::uint8_t ware) const;
    /// Crops of an island that grow fully among spices, tobacco, wine, cotton, cocoa, sugar (0x004224F0).
    [[nodiscard]] std::uint32_t full_crops(std::uint16_t island) const;
    [[nodiscard]] std::uint8_t difficulty() const { return m_world.game_settings().difficulty; }
    [[nodiscard]] std::uint64_t now_ms() const { return m_world.time_ms(); }
    [[nodiscard]] Random& rng() { return m_world.random(RandomStream::Ai); }
    [[nodiscard]] bool is_settler(PlayerId owner) const;
    [[nodiscard]] std::uint32_t island_size(std::uint16_t island) const;
    /// Goods into or out of a city without a trade (building costs, subsidies).
    void add_goods(City& c, std::uint8_t ware, std::uint32_t amount);
    std::uint32_t remove_goods(City& c, std::uint8_t ware, std::uint32_t amount);
    /// Behind the leading human (0x0040E9B0), threatened (0x0040F1D0).
    [[nodiscard]] bool behind(const AiPlayer& p) const;
    [[nodiscard]] bool threatened(const AiPlayer& p) const;
    [[nodiscard]] std::uint32_t army_strength(PlayerId owner) const;
    [[nodiscard]] std::uint32_t total_population(PlayerId owner) const;

    // ---- ai_settle.cpp ----
    void pick_island(AiPlayer& p);
    [[nodiscard]] bool island_allowed(const AiPlayer& p, std::uint16_t island) const;
    void task_settle_area(AiPlayer& p);
    void task_settle_site(AiPlayer& p);
    void task_settle_land(AiPlayer& p);
    void end_settlement(AiPlayer& p);
    [[nodiscard]] Target water_near(std::uint16_t island, std::int32_t x, std::int32_t y) const;
    void free_colony_cargo(AiPlayer& p, Ship& ship, bool computer);
    [[nodiscard]] Ship* settle_ship(AiPlayer& p);
    [[nodiscard]] std::vector<Ship*> own_ships(const AiPlayer& p);
    bool want_colony(AiPlayer& p, std::uint8_t mode, bool force, bool food);
    [[nodiscard]] std::uint8_t choose_colony_crop(AiPlayer& p);
    [[nodiscard]] bool crop_available(std::uint8_t crop) const;
    bool prepare_colony_ship(AiPlayer& p, std::uint8_t crop, bool food);
    [[nodiscard]] std::uint32_t colony_ship_candidate(AiPlayer& p);
    void task_abandon(AiPlayer& p);
    void unload_at(City& c, Ship& ship, bool keep_basics, bool destroy);

    // ---- ai_planner.cpp ----
    void task_develop(AiPlayer& p);
    void refresh_city(AiPlayer& p);
    void buy_settings(AiPlayer& p, City& c, CityPlan& r);
    void sell_settings(AiPlayer& p, City& c, CityPlan& r);
    void production_switches(AiPlayer& p, City& c);
    void subsidy(AiPlayer& p, std::int32_t m);
    void set_stage(AiPlayer& p, CityPlan& r, std::uint8_t s);
    void nothing_to_do(AiPlayer& p);
    void run_stage(AiPlayer& p, City& c, CityPlan& r);
    bool first_need(AiPlayer& p, const City& c, const CityPlan& r, std::int32_t money,
                    std::span<const StageNeed> needs);
    void unload_ship(AiPlayer& p, City& c);
    bool colony_luxury(AiPlayer& p, const City& c, const CityPlan& r, std::uint32_t tiles_per_field);
    bool farm_animals(AiPlayer& p, const CityPlan& r);
    bool need(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov = false);
    bool need_houses(AiPlayer& p, std::uint32_t n);
    bool need_market(AiPlayer& p, std::uint32_t n);
    bool need_kontor(AiPlayer& p, std::uint32_t level);
    bool need_stone_roads(AiPlayer& p);
    bool need_shipyard(AiPlayer& p);
    bool need_tower(AiPlayer& p, std::uint8_t type, std::uint32_t n);
    bool need_denkmal(AiPlayer& p);
    bool need_castle(AiPlayer& p);
    bool need_iron_mine(AiPlayer& p, std::uint32_t n);
    bool need_gold_mine(AiPlayer& p);
    bool need_public(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov);
    bool processing(AiPlayer& p);
    bool iron_tools(AiPlayer& p);
    bool weapons(AiPlayer& p);
    bool luxury(AiPlayer& p, std::uint32_t n, bool exclusive);
    bool roads_if_mason(AiPlayer& p);
    [[nodiscard]] std::uint32_t luxury_in_stock(const AiPlayer& p, const City& c, const CityPlan& r,
                                                std::uint32_t level) const;
    [[nodiscard]] std::int32_t net_income(const AiPlayer& p) const;
    [[nodiscard]] bool incomes_ok(const AiPlayer& p, std::int32_t minimum) const;
    [[nodiscard]] std::int32_t land_rank(const AiPlayer& p) const;
    bool plan_building(AiPlayer& p, std::uint8_t task, const assets::BuildingType& b, std::uint8_t slot, bool force,
                       bool from_start = false);
    void reserve(AiPlayer& p, const assets::BuildingType& b, std::int64_t extra_money = 0);
    void refund(AiPlayer& p);

    // ---- ai_sites.cpp ----
    void build_segments(AiPlayer& p);
    void start_search(AiPlayer& p, const assets::BuildingType& b, std::uint8_t slot, bool from_start);
    template <class F>
    void rings(AiPlayer& p, RingFilter filter, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
               std::int32_t n, F&& f);
    bool advance_cursor(AiPlayer& p, std::int32_t limit);
    [[nodiscard]] bool can_place(const AiPlayer& p, const assets::BuildingType& b, std::int32_t x, std::int32_t y,
                                 std::int32_t w, std::int32_t h) const;
    bool search_houses(AiPlayer& p);
    bool search_workshop(AiPlayer& p, bool cannons);
    bool search_stonemason(AiPlayer& p);
    bool search_farm(AiPlayer& p, bool woodcutter);
    bool search_grain(AiPlayer& p);
    bool search_hunter(AiPlayer& p);
    bool search_market(AiPlayer& p);
    bool search_public(AiPlayer& p);
    bool search_military(AiPlayer& p);
    bool search_denkmal(AiPlayer& p);
    bool search_pirate_houses(AiPlayer& p);
    bool search_fisher(AiPlayer& p);
    bool search_shipyard(AiPlayer& p);
    bool search_mine(AiPlayer& p, std::uint8_t ware);
    void search_mine_wide(AiPlayer& p, std::uint8_t ware);
    std::int32_t coast_walk(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::int32_t* cx,
                            std::int32_t* cy, bool water);
    std::int32_t mountain_walk(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::uint8_t ware,
                               std::int32_t* ox, std::int32_t* oy, std::int32_t max_steps = 7, bool bind = true);
    [[nodiscard]] std::int32_t free_run(std::uint16_t island, std::uint8_t city, std::int32_t x, std::int32_t y,
                                        std::uint8_t dir) const;
    bool farm_room(AiPlayer& p, const assets::BuildingType& b, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                   std::int32_t y1);
    [[nodiscard]] std::uint16_t house_services(std::uint16_t island, std::int32_t x, std::int32_t y) const;

    // ---- ai_logistics.cpp ----
    void logistics(AiPlayer& p);
    ShipNote& note(AiPlayer& p, std::uint32_t ship);
    void forget_ships(AiPlayer& p);
    [[nodiscard]] const assets::FigureType* ship_type(const Ship& s) const;
    /// Health of a ship, 128 = undamaged.
    [[nodiscard]] std::uint32_t health128(const Ship& s) const;
    [[nodiscard]] bool ship_at_city(const Ship& s, const City& c) const;
    bool ship_idle(AiPlayer& p, const Ship& s);
    void send_to_city(AiPlayer& p, Ship& s, const City& c);
    void arm_one(City& c, Ship& s);
    bool yard_service(AiPlayer& p);
    Ship* pick_logistics_ship(AiPlayer& p, bool healthy);
    Ship* pick_patrol_ship(AiPlayer& p, bool healthy);
    std::uint32_t patrols(AiPlayer& p, std::uint16_t island);
    void patrol_route(AiPlayer& p, Ship& s, std::uint16_t island);
    std::optional<std::size_t> supply_source(AiPlayer& p, std::uint16_t home_island, std::uint8_t ware,
                                             std::uint16_t* price);
    bool home_supply_route(AiPlayer& p, Ship& s, City& home);
    void update_fleet_target(AiPlayer& p);
    void order_ship(AiPlayer& p);

    // ---- ai_military.cpp ----
    [[nodiscard]] std::array<std::uint32_t, 4> army_counts(PlayerId owner) const;
    [[nodiscard]] std::array<std::uint32_t, 4> unit_weights() const;
    [[nodiscard]] std::uint32_t weighted_army(PlayerId owner) const;
    [[nodiscard]] std::uint32_t army_target(const AiPlayer& p) const;
    [[nodiscard]] bool stronger(PlayerId a, PlayerId b) const;
    [[nodiscard]] std::uint32_t soldiers_on_island(PlayerId owner, std::uint16_t island) const;
    std::vector<Soldier*> soldiers_near(PlayerId owner, const City& c, std::int32_t range, std::size_t max);
    [[nodiscard]] std::uint32_t soldiers_aboard(const Ship& s) const;
    [[nodiscard]] std::uint32_t free_holds(const Ship& s) const;
    void queue_unit(AiPlayer& p, const CastleEntry& castle, std::uint32_t family);
    bool guard_post(AiPlayer& p, City& c, CityPlan& r, std::int32_t& x, std::int32_t& y, bool& at_yard);
    void guard(AiPlayer& p, City& c, std::int32_t x, std::int32_t y, std::uint32_t n, bool castle_island);
    bool market_tower(AiPlayer& p, City& c, std::int32_t mx, std::int32_t my);
    void recruit(AiPlayer& p);
    void buy_pirate_peace(AiPlayer& p);
    void ship_send(AiPlayer& p, Ship& s, const Target& t, std::uint8_t mission, std::uint32_t param);
    void mission_end(AiPlayer& p, Ship& s, ShipNote& n);
    void board(AiPlayer& p, Ship& s, const City& at, std::uint32_t max);
    void missions(AiPlayer& p);
    void plan_landing(AiPlayer& p, Ship& s, ShipNote& n);
    Ship* best_armed_ship(AiPlayer& p, const Target& ref, std::uint32_t max_same);
    bool attack_ok(AiPlayer& p, const City& t);
    void start_attack(AiPlayer& p, const City& t);
    void raid_outpost(AiPlayer& p, const City& t);
    void target_scan(AiPlayer& p);
    [[nodiscard]] std::vector<PlayerId> island_enemies(const AiPlayer& a, std::uint16_t island) const;
    void peacetime_orders(AiPlayer& a, std::uint16_t island);
    bool attack_orders(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies);
    void island_controller();
    void diplomacy(AiPlayer& p);
    void pirate_step(AiPlayer& p);
    void pirate_develop(AiPlayer& p);

public:
    [[nodiscard]] bool directs(std::uint32_t ship) const;
    void city_founded(std::uint16_t island, std::uint8_t city);

private:
    // ---- ai_build.cpp ----
    void task_building(AiPlayer& p, std::uint8_t task);
    void task_farm(AiPlayer& p, std::uint8_t task);
    void task_workshop(AiPlayer& p);
    void task_houses(AiPlayer& p);
    void task_public(AiPlayer& p, std::uint8_t task);
    void task_market(AiPlayer& p);
    void task_mine(AiPlayer& p);
    void task_fisher(AiPlayer& p);
    void task_shipyard(AiPlayer& p);
    void task_shipyard_upgrade(AiPlayer& p);
    void task_kontor_upgrade(AiPlayer& p);
    void task_paving(AiPlayer& p);
    void task_towers(AiPlayer& p);
    void task_military(AiPlayer& p);
    void task_denkmal(AiPlayer& p);
    void task_pirate_houses(AiPlayer& p);
    void task_roads(AiPlayer& p);
    void task_layout(AiPlayer& p);
    void task_survey(AiPlayer& p);
    void survey_tile(AiPlayer& p, const City& c, CityPlan& r, std::uint32_t x, std::uint32_t y, SurveyFinds& finds);
    void sweep(AiPlayer& p);
    void kontor_restore(AiPlayer& p);
    std::size_t build(AiPlayer& p, std::vector<BuildItem> items, bool orient);
    std::int32_t road_line(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t len, std::uint8_t dir, bool force);
    std::int32_t road_three(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::int32_t right,
                            std::int32_t left, std::int32_t back, bool force);
    void plaza_ring(AiPlayer& p);
    void fill_fields(AiPlayer& p, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                     std::int32_t rings_n, std::uint8_t field_ware, std::uint8_t radius);
    void slot_built(AiPlayer& p, std::uint8_t slot);
    void slot_ok(AiPlayer& p, std::uint8_t slot);
    std::uint16_t slot_fail(AiPlayer& p, std::uint8_t slot);
    [[nodiscard]] std::int32_t slot_present(const AiPlayer& p, std::uint8_t slot) const;
    void demolish_tile(AiPlayer& p, std::int32_t x, std::int32_t y);

    Simulation& m_sim;
    World& m_world;
    const assets::BuildingTable& m_buildings;
    State& m_state;
    Player m_nobody;  ///< stands in for a player slot that is gone
    /// Producers by ware (0x006C2B20): up to four records per ware, in file order.
    std::vector<std::vector<const assets::BuildingType*>> m_producers;
};

/// Ring iterator (0x00410A80 and relatives): ring d (Chebyshev distance d + 1) around the
/// rectangle, top row, then the sides top to bottom, then the bottom row.
template <class F>
void Engine::rings(AiPlayer& p, RingFilter filter, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                   std::int32_t n, F&& f) {
    const auto visit = [&](std::int32_t x, std::int32_t y, std::int32_t d) {
        if (!inside(p.island, x, y)) {
            return;
        }
        const Tile t = tile(p.island, x, y);
        switch (filter) {
            case RingFilter::Own:
                if (t.city != p.city) {
                    return;
                }
                break;
            case RingFilter::OwnOrSea:
                if (t.city != p.city && t.kind != kind::kSea) {
                    return;
                }
                break;
            case RingFilter::Any:
                break;
        }
        f(t, x, y, d);
    };
    for (std::int32_t d = 0; d < n; ++d) {
        const std::int32_t l = x0 - d - 1;
        const std::int32_t r = x1 + d + 1;
        const std::int32_t t = y0 - d - 1;
        const std::int32_t b = y1 + d + 1;
        for (std::int32_t x = l; x <= r; ++x) {
            visit(x, t, d);
        }
        for (std::int32_t y = t + 1; y < b; ++y) {
            visit(l, y, d);
            visit(r, y, d);
        }
        for (std::int32_t x = l; x <= r; ++x) {
            visit(x, b, d);
        }
    }
}

}  // namespace o1602::sim::ai
