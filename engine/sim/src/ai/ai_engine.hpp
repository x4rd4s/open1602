// SPDX-License-Identifier: GPL-3.0-or-later
//
// The machinery of the computer players. One Engine works on the shared State for
// the duration of a call; the methods are spread over several files by subsystem:
//   ai_core.cpp      scheduler, task stack, root task, (re)initialisation, saving
//   ai_world.cpp     what the AI reads from the world (tiles, cities, land)
//   ai_settle.cpp    finding an island and founding a city
//   ai_planner.cpp   task 5: refresh of the city, stages and the Need list
//   ai_sites.cpp     road segments and the building site searches
//   ai_build.cpp     the building tasks, roads, fields, survey and the maintenance sweep
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ai_chains.hpp"
#include "ai_overview.hpp"
#include "ai_state.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/sim/computer_slots.hpp"
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
struct Cheats;  // ai_tables.hpp

struct SurveyFinds {
    std::vector<std::pair<std::int32_t, std::int32_t>> starved_mines;
    std::vector<std::pair<std::int32_t, std::int32_t>> sheep;
    std::vector<std::pair<std::int32_t, std::int32_t>> webstuben;
};

/// One entry of the build order of a city's stage (run_stage): something the city may need, tried
/// in the order of the stage's list (computer_orders.hpp).
using StageNeed = ComputerNeed;

/// Game ms between two steps of a computer player (the original's 50 ms main timer).
inline constexpr std::int32_t kStepMs = 50;
/// Game ms between two steps of the pirates.
inline constexpr std::int32_t kPirateStepMs = 10'000;
/// Rings searched for a woodcutter whose reach covers a tile (no building reaches farther).
inline constexpr std::int32_t kWoodcutterReach = 16;
/// Pushes a city visit allows: with fewer than three cities, and with more.
inline constexpr std::int32_t kBudgetFew = 16;
inline constexpr std::int32_t kBudgetMany = 8;
inline constexpr std::size_t kMaxStack = 256;
inline constexpr auto kTon = static_cast<std::int32_t>(kStockUnitsPerTon);

/// A tile as the AI reads it: the record of the top layer, growing crops taken as their ripe
/// record (the original's normalisation of ROHSTWACHS tiles).
/// The reach of a woodcutter: its footprint and its radius.
struct WoodReach {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::uint8_t radius = 0;
};

/// Whether x, y lies in one of the reaches.
[[nodiscard]] bool in_wood_reach(const std::vector<WoodReach>& reaches, std::int32_t x, std::int32_t y);

struct Tile {
    const assets::BuildingType* type = nullptr;
    std::int32_t kind = -1;
    std::int32_t prod = -1;
    std::uint8_t ware = 0;
    std::uint8_t raw = 0;
    std::uint8_t city = MapCell::kNoCity;
    PlayerId owner = kNoPlayer;
    bool origin = false;  ///< the reference tile of its building
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

/// Open1602: how a computer plans a town, different for each player, island and city
/// (town_style()).
struct TownStyle {
    std::int32_t room = 7;          ///< tiles of the town zone a house wants, roads and the like included
    std::int32_t shift = 0;         ///< tiles the zone's middle lies inland of the first market
    std::int32_t block = 7;         ///< length of the roads that open new blocks
    std::int32_t field_margin = 0;  ///< tiles fields keep beyond the zone
};
/// Open1602: the town zone of a city (Engine::town_zone()).
struct TownZone {
    bool valid = false;
    std::int32_t cx = 0;  ///< the middle: of the market nearest the Kontor, else of the Kontor
    std::int32_t cy = 0;
    std::int32_t radius = 0;  ///< houses within it, fields outside
};
/// How far the middle of a site lies outside the town zone (negative: inside, by so much).
[[nodiscard]] std::int32_t zone_excess(const TownZone& z, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                       std::int32_t y1);
[[nodiscard]] TownStyle town_style(std::uint32_t seed, std::uint16_t island, std::uint8_t city);

/// Which tiles a ring iterator visits.
enum class RingFilter : std::uint8_t {
    Own,       ///< tiles of the current city
    OwnOrSea,  ///< and open sea
    Any,       ///< everything
};

/// How a city reaches a deposit of its island (Open1602, Engine::deposit_access): a mine needs a
/// slope at the deposit's mountain in the city's land.
enum class DepositAccess : std::uint8_t {
    Own,     ///< a slope at the mountain is the city's
    Free,    ///< none is, but one lies on free land: a market can bring it in
    Closed,  ///< every slope there belongs to another city
};

/// A Need of the stage handlers: what to build and when.
enum class NeedType : std::uint8_t { A, B, C, D };

/// Summed-area table of a predicate over an island, for counts in rectangles.
class AreaCount {
public:
    template <class F>
    AreaCount(std::int32_t w, std::int32_t h, F&& f)
        : m_w(w), m_h(h), m_sum(std::size_t(w + 1) * std::size_t(h + 1), 0) {
        for (std::int32_t y = 0; y < h; ++y) {
            std::int32_t row = 0;
            for (std::int32_t x = 0; x < w; ++x) {
                row += f(x, y) ? 1 : 0;
                at(x + 1, y + 1) = at(x + 1, y) + row;
            }
        }
    }
    [[nodiscard]] std::int32_t count(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) const {
        x0 = std::clamp(x0, 0, m_w);
        y0 = std::clamp(y0, 0, m_h);
        x1 = std::clamp(x1 + 1, 0, m_w);
        y1 = std::clamp(y1 + 1, 0, m_h);
        if (x1 <= x0 || y1 <= y0) {
            return 0;
        }
        return get(x1, y1) - get(x0, y1) - get(x1, y0) + get(x0, y0);
    }

private:
    std::int32_t& at(std::int32_t x, std::int32_t y) {
        return m_sum[std::size_t(y) * std::size_t(m_w + 1) + std::size_t(x)];
    }
    [[nodiscard]] std::int32_t get(std::int32_t x, std::int32_t y) const {
        return m_sum[std::size_t(y) * std::size_t(m_w + 1) + std::size_t(x)];
    }
    std::int32_t m_w;
    std::int32_t m_h;
    std::vector<std::int32_t> m_sum;
};

class Engine {
public:
    Engine(Simulation& sim, State& state);

    // ai_core.cpp
    void tick(std::uint32_t dt_ms);
    void reset();
    void delay_first_step(std::uint32_t steps) { m_delay_steps = steps; }
    void init_player(AiPlayer& p);
    /// Gives every computer slot without a state one (reset() makes them for the slots of the start).
    void adopt_new_computers();
    /// Whether a state read from a save or a host's snapshot can be run on this world: every
    /// value a step uses as an index or a count is in range (HI-1).
    [[nodiscard]] bool accepts(const State& state) const;
    /// The producers of the packages' goods the computers build (ai_chains.hpp).
    [[nodiscard]] const std::vector<PackageNeed>& package_needs() const noexcept { return m_package_needs; }

    /// How many cities a computer founds at most (Rules::computer_cities): four as in the original,
    /// or the world's islands shared among its players, by the difficulty.
    [[nodiscard]] std::uint32_t city_limit() const;

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
    [[nodiscard]] static bool logging() noexcept;
    /// The computer log only: a report of each city every half hour of game time.
    void report(AiPlayer& p);

    // ---- ai_world.cpp ----
    [[nodiscard]] Tile tile(std::uint16_t island, std::int32_t x, std::int32_t y) const;
    /// `tile` of cell `c` at (x, y), which the caller found on the map.
    [[nodiscard]] Tile tile_of(const MapCell& c, std::int32_t x, std::int32_t y) const;
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
    /// Inhabitants of classes `from` and higher (compares with it).
    [[nodiscard]] std::uint32_t population(const City& c, std::size_t from = 0) const;
    [[nodiscard]] bool has_population(const City& c, std::size_t from, std::uint32_t n) const {
        return population(c, from) >= n;
    }
    /// Demand of a city for a consumer good, 1/32 t per minute.
    [[nodiscard]] std::uint32_t demand(const City& c, std::uint8_t ware) const;
    /// Open1602: what the producers of `ware` in a city make, 1/32 t per minute, as their
    /// statistics show (half their full rate for one that has run for less than a minute).
    [[nodiscard]] std::uint32_t supply(const City& c, std::uint8_t ware) const;
    /// Open1602: whether a city's producers make less food than its people eat and its stock lasts
    /// less than kFoodAheadMinutes at that shortfall, whatever its ships may yet bring.
    [[nodiscard]] bool food_behind(const City& c) const;
    /// Open1602: whether a city makes too little food for more inhabitants: with less than
    /// kFoodStock in stock, its own producers make less than its inhabitants eat and a margin.
    [[nodiscard]] bool food_short(const City& c) const;
    /// Open1602: whether a city has so much of `ware` that more producers of it would only fill
    /// the warehouse (kFullStock of its room).
    [[nodiscard]] bool stock_full(const City& c, std::uint8_t ware) const;
    [[nodiscard]] std::uint32_t kontor_level(const City& c) const;
    /// Tiles of the city's Kontor each way (its record's size).
    [[nodiscard]] std::int32_t kontor_size(const City& c) const;
    /// Markets and Kontors of a city and all its buildings.
    [[nodiscard]] std::uint32_t market_count(const City& c) const;
    [[nodiscard]] std::uint32_t building_count(const City& c) const;
    /// Usable land of a city: the island's tiles minus those of other cities, coast, rock and
    /// rivers; `percent` out.
    [[nodiscard]] std::uint32_t free_land(std::uint16_t island, std::uint8_t city, std::uint32_t* percent) const;
    /// Civilisation level 0..4: highest of INFRA_STUFE_2D, 3C, 4B, 5A unlocked.
    [[nodiscard]] std::uint32_t civ_level(const AiPlayer& p) const;
    [[nodiscard]] bool unlocked(const AiPlayer& p, const assets::BuildingType& b) const;
    /// Money and goods of the current city cover the building.
    [[nodiscard]] bool affordable(const AiPlayer& p, const City& c, const assets::BuildingType& b) const;
    /// First producer of `ware` from `raw`: the table the original builds at AI_Init.
    [[nodiscard]] const assets::BuildingType* producer(std::uint8_t ware, std::uint8_t raw) const;
    [[nodiscard]] const assets::BuildingType* by_id(std::uint32_t id) const { return m_buildings.by_id(id); }
    /// Fertility of an island for a crop or resource: 0, 64 or 128.
    [[nodiscard]] std::uint32_t fertility(std::uint16_t island, std::uint8_t ware) const;
    /// Crops of an island that grow fully among spices, tobacco, wine, cotton, cocoa, sugar.
    [[nodiscard]] std::uint32_t full_crops(std::uint16_t island) const;
    [[nodiscard]] std::uint8_t difficulty() const { return m_world.game_settings().difficulty; }
    [[nodiscard]] std::uint64_t now_ms() const { return m_world.time_ms(); }
    [[nodiscard]] Random& rng() { return m_world.random(RandomStream::Ai); }
    /// Rules::bugs.classic_ai: the original's computer players with their bugs S-097 to S-099.
    [[nodiscard]] bool classic() const;
    [[nodiscard]] bool is_settler(PlayerId owner) const;
    /// Improved computers (not Rules::bugs.classic_ai) reckon with every other player, the other
    /// computers too, not only with the humans: an army by their inhabitants and their rivals'
    /// armies, castles as their home city grows (Balance's [computer_military]).
    [[nodiscard]] bool rivals() const;
    /// The money and goods the original's computers get from nowhere (cheats(), Balance's
    /// [computers]): only the original's computers (Rules::bugs.classic_ai) get them.
    [[nodiscard]] bool gifts() const;
    [[nodiscard]] std::uint32_t island_size(std::uint16_t island) const;
    /// The part of the map an island's middle lies in when the pirates aim for `nests` nests.
    [[nodiscard]] std::uint32_t island_region(std::uint16_t island, std::uint32_t nests) const;
    /// Goods into or out of a city without a trade (building costs, subsidies).
    void add_goods(City& c, std::uint8_t ware, std::uint32_t amount);
    std::uint32_t remove_goods(City& c, std::uint8_t ware, std::uint32_t amount);
    /// Behind the leading human, threatened.
    [[nodiscard]] bool behind(const AiPlayer& p) const;
    [[nodiscard]] bool threatened(const AiPlayer& p) const;
    /// A scenario keeps the computer from building while it is at peace with every human;
    /// ends the switch when it is not.
    bool peaceful(AiPlayer& p);
    /// Task 5 in the tutorial: only the money.
    void tutorial_money(AiPlayer& p);
    /// The world is the tutorial (TIMERS +0xA5): its computers stay as they are.
    [[nodiscard]] bool tutorial() const noexcept { return m_world.game_settings().tutorial; }
    [[nodiscard]] std::uint32_t army_strength(PlayerId owner) const;
    [[nodiscard]] std::uint32_t total_population(PlayerId owner) const;

    // ---- ai_settle.cpp ----
    void pick_island(AiPlayer& p);
    [[nodiscard]] bool knows_island(const AiPlayer& p, std::uint16_t island) const;
    /// Improved computers: the computer gave a city up on the island in the last 45 minutes.
    [[nodiscard]] bool left_lately(AiPlayer& p, std::uint16_t island);
    [[nodiscard]] bool knows_crops(const AiPlayer& p, std::uint16_t island) const;
    [[nodiscard]] Ship* explorer(AiPlayer& p);
    [[nodiscard]] bool exploring(AiPlayer& p);
    bool explore_next(AiPlayer& p);
    /// The open water on the rim of the island's rectangle nearest to the ship, if nearer than
    /// `best` (squared tiles, < 0: any); updates `best`.
    bool rim_goal(const Ship& ship, std::uint16_t island, std::int64_t& best, Target& goal) const;
    /// Improved computers: an idle ship sails to the nearest island nobody of theirs explored.
    void scout(AiPlayer& p);
    /// Open1602: sends an improved computer's ship without a job on patrol round its home island.
    void guard_idle_ship(AiPlayer& p, bool healthy);
    /// Whether a pirate ship lies near `s` (an improved computer's held ship waits for it).
    [[nodiscard]] bool pirate_near(const Ship& s) const;
    [[nodiscard]] bool island_allowed(const AiPlayer& p, std::uint16_t island) const;
    /// Improved computers: an island of a small, weaker rival that has what the colony looks for.
    [[nodiscard]] bool island_contested(const AiPlayer& p, std::uint16_t island) const;
    /// A vein of the ware (ore, gold) on the island outside every city's land.
    [[nodiscard]] bool free_deposit(std::uint16_t island, std::uint8_t w) const;
    /// What an improved computer's goal asks a colony for (gold, ore), 0 for nothing.
    [[nodiscard]] std::uint8_t goal_crop(const AiPlayer& p) const;
    /// Whether the ore veins of its own islands run low while it mines iron (improved computers).
    [[nodiscard]] bool ore_running_out(const AiPlayer& p) const;
    void task_settle_area(AiPlayer& p);
    void task_settle_site(AiPlayer& p);
    void task_settle_land(AiPlayer& p);
    void end_settlement(AiPlayer& p);
    [[nodiscard]] Target water_near(std::uint16_t island, std::int32_t x, std::int32_t y) const;
    [[nodiscard]] bool open_sea_ahead(std::uint16_t island, std::int32_t x, std::int32_t y, std::uint32_t width,
                                      std::uint32_t height, std::uint8_t rotation) const;
    void site_unreachable(AiPlayer& p);
    void free_colony_cargo(AiPlayer& p, Ship& ship, bool computer);
    [[nodiscard]] Ship* settle_ship(AiPlayer& p);
    [[nodiscard]] std::vector<Ship*> own_ships(const AiPlayer& p);
    bool want_colony(AiPlayer& p, std::uint8_t mode, bool force, bool food);
    /// Open1602 (Rules::computer_cities scaled): a computer whose cities run well wants a further
    /// island for room to grow, though every crop it asks for grows already.
    [[nodiscard]] bool wants_room(const AiPlayer& p) const;
    [[nodiscard]] std::uint8_t choose_colony_crop(AiPlayer& p);
    [[nodiscard]] bool crop_available(const AiPlayer& p, std::uint8_t crop) const;
    bool prepare_colony_ship(AiPlayer& p, std::uint8_t crop, bool food);
    /// Loads the colony ship in city `c`; true once it holds its whole load, the settlement then starts.
    bool load_colony_ship(AiPlayer& p, City& c, Ship& ship, bool food, std::uint8_t crop);
    /// Open1602: goes on loading the colony ship at home, from the logistics look.
    void continue_colony_load(AiPlayer& p);
    /// Open1602: whether the current city holds a colony ship's whole load.
    [[nodiscard]] bool colony_load_in_stock(AiPlayer& p, bool food);
    /// Open1602: whether the colony ship already holds part of its load.
    [[nodiscard]] bool colony_loading(AiPlayer& p);
    /// The tons of a ware a colony ship sets out with, of the original's `tons`.
    [[nodiscard]] std::uint32_t colony_load(std::uint32_t tons) const;
    [[nodiscard]] std::uint32_t colony_ship_candidate(AiPlayer& p);
    void task_abandon(AiPlayer& p);
    void unload_at(City& c, Ship& ship, bool keep_basics, bool destroy);

    // ---- ai_planner.cpp ----
    void task_develop(AiPlayer& p);
    void refresh_city(AiPlayer& p);
    void buy_settings(AiPlayer& p, City& c, CityPlan& r);
    void sell_settings(AiPlayer& p, City& c, CityPlan& r);
    void production_switches(AiPlayer& p, City& c);
    /// Deep in debt: tears down the city's producers of wares none of the computer's cities needs.
    void shed_producers(AiPlayer& p, City& c);
    void shed_public(AiPlayer& p, City& c);
    using WareSet = std::array<bool, 256>;
    [[nodiscard]] WareSet needed_wares(const AiPlayer& p, bool soon) const;
    /// Whether it switches off what it does not need: in debt, or for its army's upkeep.
    [[nodiscard]] bool saving_costs(const AiPlayer& p) const;
    void tax_rates(AiPlayer& p, City& c);
    /// Open1602: an improved computer keeps its houses from rising while the city lacks food or a
    /// good its upper classes want, as a player would.
    void material_lock(AiPlayer& p, City& c);
    void set_material_lock(AiPlayer& p, City& c, bool on);
    [[nodiscard]] std::int64_t stage_gold(std::uint32_t gold) const;
    void subsidy(AiPlayer& p, std::int32_t m);
    void set_stage(AiPlayer& p, CityPlan& r, std::uint8_t s);
    void nothing_to_do(AiPlayer& p);
    void run_stage(AiPlayer& p, City& c, CityPlan& r);
    /// Open1602: `n` grown with the free land of a city past the settlers (kGrowLand), so that a
    /// computer fills a large island; `n` itself for the original's computers and smaller cities.
    /// `any_stage`: for a colony too (its caps on fields and farms).
    [[nodiscard]] std::uint32_t grown(const CityPlan& r, std::uint32_t n, bool any_stage = false) const;
    /// Open1602: the houses of an order, `grown` only while at most an eighth of the city's houses
    /// hold pioneers, so that a city grows tall before it grows wide. `home`: the home city of an
    /// improved computer that earns, grown from kGrowLand tiles of land on.
    [[nodiscard]] std::uint32_t houses_wanted(const City& c, const CityPlan& r, std::uint32_t n,
                                              bool home = false) const;
    /// `grow`: with the numbers `grown` by the city's land (houses: `houses_wanted`).
    bool first_need(AiPlayer& p, const City& c, const CityPlan& r, std::int32_t money, std::span<const StageNeed> needs,
                    bool grow = false);
    void unload_ship(AiPlayer& p, City& c);
    bool colony_luxury(AiPlayer& p, const City& c, const CityPlan& r, std::uint32_t tiles_per_field);
    bool farm_animals(AiPlayer& p, const CityPlan& r);
    bool need(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov = false);
    /// Open1602: how many workshops of slot `k` the producers of their raw material in a city keep
    /// busy (no limit for a slot without such producers).
    [[nodiscard]] std::uint32_t processors_fed(const City& c, const CityPlan& r, const ComputerSlot& k) const;
    /// Open1602: a food producer for a city that makes too little food (food_short), on its own
    /// island before anything else of its order: true if one was planned.
    /// `ahead`: also for a city not yet short, which is to grow.
    bool more_food(AiPlayer& p, const City& c, const CityPlan& r, bool ahead = false);
    /// Open1602: whether a city's own food comes in with a quarter to spare.
    [[nodiscard]] bool food_ahead(const City& c) const;
    /// Open1602: food_ahead, and every class living in the city is content and has its goods.
    [[nodiscard]] bool supplies_growth(const City& c) const;
    /// Open1602: more food or cloth for a home city whose supply falls short (`growing`: with a
    /// quarter of food to spare); true when a producer was planned.
    bool supply_ahead(AiPlayer& p, const City& c, const CityPlan& r, bool growing);
    /// Open1602: whether a discontent class living in the city (any, with `content_too`) lacks one
    /// of its goods (not food).
    [[nodiscard]] bool goods_lacking(const City& c, bool content_too = false) const;
    /// Open1602: how long a good's stock lasts its people (stock * 256 / demand), the most for one
    /// they do not use or that the city makes enough of.
    [[nodiscard]] std::uint64_t stock_lasts(const City& c, std::uint8_t w) const;
    /// Open1602: how much alcohol or cloth all of an owner's cities use beyond what they make,
    /// 1/32 t a minute; 0 for other goods.
    [[nodiscard]] std::uint32_t critical_short(PlayerId owner, std::uint8_t w) const;
    /// What all of the owner's cities use of `w` beyond what they make, 1/32 t a minute.
    [[nodiscard]] std::uint32_t goods_short(PlayerId owner, std::uint8_t w) const;
    /// Open1602: the good goods_lacking finds, 0 none; with `content_too` only goods the city makes.
    [[nodiscard]] std::uint8_t lacking_ware(const City& c, bool content_too) const;
    /// Open1602: the gold an improved computer keeps for its first yard, 0 none.
    [[nodiscard]] std::int64_t yard_savings(AiPlayer& p);
    /// Improved computers: the gold kept for a colony whose crop its people want and none of its
    /// islands grows, else 0.
    [[nodiscard]] std::int64_t colony_savings(AiPlayer& p);
    /// Improved computers: its supply and gold carry a war it would start or join.
    [[nodiscard]] bool war_supplied(AiPlayer& p);
    /// Improved computers: settlers in the home city want a good whose crop none of the computer's
    /// islands grows, and it may found a colony for it.
    [[nodiscard]] bool colony_crop_missing(AiPlayer& p);
    /// Open1602: what an improved rival's home city lacks of food (with a quarter to spare) or cloth
    /// a minute that its island has no site left to make and its other cities do not make beyond
    /// their own use, 1/32 t a minute; 0 otherwise.
    [[nodiscard]] std::uint32_t home_shortfall(AiPlayer& p, std::uint8_t w);
    /// Open1602: a colony of an improved rival builds food or sheep farms for its home city's
    /// shortfall (home_shortfall).
    [[nodiscard]] bool grow_for_home(AiPlayer& p, const City& c, const CityPlan& r);
    static constexpr std::uint32_t kColonySaveFrom = 300;  ///< inhabitants of the home city
    static constexpr std::uint32_t kSpareKeepMinutes = 10;
    static constexpr std::uint32_t kHomeFirstMinutes = 10;  ///< supply_destination
    /// Open1602: what class `k` of the city would use more of `w` once half the class below rose
    /// into it, 1/32 t a minute (an improved rival plans its goods ahead of the rise).
    [[nodiscard]] std::uint32_t ascent_extra(const City& c, std::size_t k, std::uint8_t w) const;
    /// demand with ascent_extra of every class the city's houses can rise into next.
    [[nodiscard]] std::uint32_t demand_ahead(const City& c, std::uint8_t w) const;
    /// goods_short with demand_ahead: what the owner's cities will use beyond what they make.
    [[nodiscard]] std::uint32_t goods_ahead(PlayerId owner, std::uint8_t w) const;
    /// Whether the goods of class `k` are made and in store for the houses to rise into it.
    [[nodiscard]] bool ascent_ready(AiPlayer& p, const City& c, std::size_t k);
    /// A good of class `k` without an own source: none made and little in store (0 for none).
    [[nodiscard]] std::uint8_t source_missing(AiPlayer& p, const City& c, std::size_t k);
    /// Improved computers: a workshop for `w` in the current city where it has the workshop's raw
    /// material in store and none yet; whether one was planned.
    bool workshop_for(AiPlayer& p, const City& c, std::uint8_t w);
    /// The lowest class that needs the public building of slot `def` (0: none from the citizens on).
    [[nodiscard]] std::size_t gate_class(const ComputerSlot& def) const;
    static constexpr std::uint32_t kAscentStockMinutes = 5;
    /// Open1602: what one producer of `b` makes in a city, 1/32 t a minute (made_per_minute).
    [[nodiscard]] std::uint32_t made_per_minute(const City& c, const assets::BuildingType& b) const;
    /// The producers of the packages' goods the city lacks (ai_chains.hpp): true if one was planned.
    bool package_chains(AiPlayer& p, const City& c, const CityPlan& r);
    bool package_need(AiPlayer& p, const City& c, const CityPlan& r, const PackageNeed& n, std::uint32_t wanted);
    /// An order's entry for the package's good `w`: `wanted` producers, if the computers build any.
    bool package_order(AiPlayer& p, const City& c, const CityPlan& r, std::uint8_t w, std::uint32_t wanted);
    /// The money and goods the computers get from nowhere, from the game's balance.
    [[nodiscard]] Cheats cheats() const;
    /// The slots of the game's city records (Balance::computer_slots, else the original's).
    [[nodiscard]] const std::vector<ComputerSlot>& slots() const {
        return computer_slots_of(m_world.balance().computer_slots);
    }
    /// The slot of the package's Need that makes `w`, if any.
    [[nodiscard]] std::optional<std::uint8_t> package_slot(std::uint8_t w) const;
    bool need_houses(AiPlayer& p, std::uint32_t n);
    bool need_market(AiPlayer& p, std::uint32_t n);
    bool need_kontor(AiPlayer& p, std::uint32_t level);
    /// Open1602: a colony's Kontor follows the computer's best one when that is of use (kColonyKontorMoney).
    bool colony_kontor(AiPlayer& p, const City& c);
    bool need_stone_roads(AiPlayer& p);
    bool need_shipyard(AiPlayer& p);
    bool need_tower(AiPlayer& p, std::uint8_t type, std::uint32_t n);
    bool need_denkmal(AiPlayer& p);
    bool need_castle(AiPlayer& p);
    bool need_iron_mine(AiPlayer& p, std::uint32_t n);
    bool need_gold_mine(AiPlayer& p);
    /// Whether the current city can mine deposit `d` from its land; one it could claim first
    /// leaves its free slope in `free_x`, `free_y` unless an earlier one did.
    bool reachable_deposit(AiPlayer& p, const assets::IslandHeader::Deposit& d, std::int32_t& free_x,
                           std::int32_t& free_y);
    bool claim_land(AiPlayer& p, std::uint8_t slot, std::int32_t x, std::int32_t y);
    bool need_public(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov);
    bool processing(AiPlayer& p);
    bool iron_tools(AiPlayer& p);
    bool weapons(AiPlayer& p);
    bool luxury(AiPlayer& p, std::uint32_t n, bool exclusive);
    bool roads_if_mason(AiPlayer& p);
    [[nodiscard]] std::uint32_t luxury_in_stock(const AiPlayer& p, const City& c, const CityPlan& r,
                                                std::uint32_t level) const;
    /// Per minute: taxes less running costs, less the army's upkeep where it pays one.
    [[nodiscard]] std::int32_t net_income(const AiPlayer& p) const;
    /// net_income with the trade balance (sales less purchases).
    [[nodiscard]] std::int32_t earned_income(const AiPlayer& p) const;
    /// Taxes less the running costs of the cities, without the army.
    [[nodiscard]] std::int32_t civil_income(const AiPlayer& p) const;
    /// The army's upkeep per minute (military_upkeep), 0 for the original's computers.
    [[nodiscard]] std::int32_t military_cost(const AiPlayer& p) const;
    /// The upkeep a minute the computer allows its army (war economy): a share of its civil
    /// income in peace; at war or under threat also what its gold can carry for a while.
    [[nodiscard]] std::int32_t army_budget(const AiPlayer& p) const;
    /// army_budget while it arms for a war of its own.
    [[nodiscard]] std::int32_t arming_budget(const AiPlayer& p) const;
    /// What one more soldier would cost it a minute.
    [[nodiscard]] std::int32_t unit_upkeep_estimate(const AiPlayer& p) const;
    /// Whether a war it wages or fears allows a deficit for a while.
    [[nodiscard]] bool war_economy(const AiPlayer& p) const;
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
    bool search_claim(AiPlayer& p);
    bool mine_path(AiPlayer& p, std::uint8_t ware, std::vector<BuildItem>& road);
    /// Open1602: when no road's end leads to a fishing hut's site (search_fisher), the shortest way
    /// over the city's open ground from its roads or its Kontor to a beach tile with fish, up to
    /// kFisherPath tiles; the road along it goes to `road`, the site to the search.
    bool fisher_path(AiPlayer& p, const assets::BuildingType& hut, std::vector<BuildItem>& road);
    /// Open1602: the road that joins a building of the city at x, y (w x h tiles) to the city's
    /// roads, when none touches it yet: the shortest way over the city's open ground from any of
    /// its roads to a tile beside the building, up to kJoinPath tiles, goes to `road`. False when
    /// no such way exists; true with `road` empty when a road touches the building already.
    bool join_path(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h,
                   std::vector<BuildItem>& road);
    /// Open1602: lays join_path's road for a building of the city; whether a road touches it then.
    bool join_roads(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h);
    /// Open1602: join_roads for the shipyard just built at x, y (not with the original's computers).
    void join_yard(AiPlayer& p, std::int32_t x, std::int32_t y);
    [[nodiscard]] DepositAccess deposit_access(const AiPlayer& p, const assets::IslandHeader::Deposit& d,
                                               std::int32_t& free_x, std::int32_t& free_y);
    [[nodiscard]] std::int32_t market_distance(const AiPlayer& p, std::int32_t x, std::int32_t y);
    bool search_public(AiPlayer& p);
    bool search_military(AiPlayer& p);
    bool search_denkmal(AiPlayer& p);
    bool search_pirate_houses(AiPlayer& p);
    bool search_fisher(AiPlayer& p);
    /// The shoals, markets and other huts around a fishing hut's site (search_fisher).
    [[nodiscard]] Acc fisher_site(AiPlayer& p, std::int32_t cx, std::int32_t cy);
    bool search_shipyard(AiPlayer& p);
    bool search_mine(AiPlayer& p, std::uint8_t ware);
    bool search_mine_wide(AiPlayer& p, std::uint8_t ware);
    std::int32_t coast_walk(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::int32_t* cx,
                            std::int32_t* cy, bool water);
    std::int32_t mountain_walk(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::uint8_t ware,
                               std::int32_t* ox, std::int32_t* oy, std::int32_t max_steps = 7, bool bind = true,
                               bool commit = true);
    [[nodiscard]] std::int32_t free_run(std::uint16_t island, std::uint8_t city, std::int32_t x, std::int32_t y,
                                        std::uint8_t dir) const;
    /// Open1602: the place of the island with the most unclaimed land fields could take about it,
    /// near the city's markets (kFieldLandTiles at least); false without one.
    [[nodiscard]] bool field_land(const AiPlayer& p, std::int32_t& x, std::int32_t& y) const;
    /// Open1602: an improved computer claims open land for a farm's slot with a market (field_land).
    /// Open1602: a market at the edge of the city's land towards (x, y), with a road to it.
    bool claim_by_path(AiPlayer& p, std::int32_t x, std::int32_t y);
    /// Open1602: a walk over the city's open ground from its roads (walk_ground), for claim_by_path
    /// and farm_by_path.
    struct GroundWalk {
        std::int32_t width = 0;
        std::vector<std::int32_t> prev;  ///< the tile before on the way, by y * width + x; -1 a road, -2 not reached
        std::vector<std::int32_t> dist;  ///< tiles from the nearest road
        /// Whether the way to `end` crosses the rectangle (x0, y0, w, h).
        [[nodiscard]] bool crosses(std::int32_t end, std::int32_t x0, std::int32_t y0, std::int32_t w,
                                   std::int32_t h) const;
        /// The road along the way to `end`, from the city's road on.
        [[nodiscard]] std::vector<BuildItem> road_to(std::int32_t end) const;
    };
    bool walk_ground(AiPlayer& p, GroundWalk& walk,
                     const std::function<void(std::int32_t i, std::int32_t x, std::int32_t y)>& visit);
    bool farm_by_path(AiPlayer& p, const assets::BuildingType& b, bool grain);
    bool claim_fields(AiPlayer& p, std::uint8_t slot);
    /// Open1602: tiles of the city or of nobody within `r` of (x, y) that fields could take.
    [[nodiscard]] std::int32_t open_land(std::uint16_t island, std::uint8_t city, std::int32_t x, std::int32_t y,
                                         std::int32_t r) const;
    bool farm_room(AiPlayer& p, const assets::BuildingType& b, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                   std::int32_t y1);
    [[nodiscard]] assets::ServiceBits house_services(std::uint16_t island, std::int32_t x, std::int32_t y) const;

    // ---- ai_logistics.cpp ----
    void logistics(AiPlayer& p);
    /// Open1602: frees a colony ship that waited too long for its load.
    void colony_load_timeout(AiPlayer& p);
    ShipNote& note(AiPlayer& p, std::uint32_t ship);
    void forget_ships(AiPlayer& p);
    [[nodiscard]] const assets::FigureType* ship_type(const Ship& s) const;
    /// Health of a ship, 128 = undamaged.
    [[nodiscard]] std::uint32_t health128(const Ship& s) const;
    [[nodiscard]] bool ship_at_city(const Ship& s, const City& c) const;
    bool ship_idle(AiPlayer& p, const Ship& s);
    void send_to_city(AiPlayer& p, Ship& s, const City& c);
    /// Stops the ship where it is, its route and job ended.
    void stop_ship(AiPlayer& p, Ship& s);
    void arm_one(City& c, Ship& s);
    bool yard_service(AiPlayer& p);
    Ship* pick_logistics_ship(AiPlayer& p, bool healthy);
    Ship* pick_patrol_ship(AiPlayer& p, bool healthy);
    std::uint32_t patrols(AiPlayer& p, std::uint16_t island);
    void patrol_route(AiPlayer& p, Ship& s, std::uint16_t island);
    /// Where a ware for a city on `home_island` comes from. Improved computers weigh the way from
    /// `to` (the home city when null).
    std::optional<std::size_t> supply_source(AiPlayer& p, std::uint16_t home_island, std::uint8_t ware,
                                             std::uint16_t* price, const City* to = nullptr);
    bool home_supply_route(AiPlayer& p, Ship& s, City& home);
    /// Improved computers: how soon a city runs out of a ware a supply ship brings, the lower the
    /// sooner (stock_lasts); building materials under half the kept stock count as out.
    /// kNoUrgency for a ware it makes enough of or does not use.
    [[nodiscard]] std::uint64_t supply_urgency(const City& c, std::uint8_t ware, std::uint32_t target) const;
    /// Improved computers: the urgency of the most urgent ware `c` lacks that no supply ship brings
    /// it yet and that it can get (kNoUrgency for none); of `only` alone when given.
    [[nodiscard]] std::uint64_t supply_need(AiPlayer& p, const City& c, std::uint8_t only = 0);
    /// Improved computers: the own city the next supply run goes to: the one that runs out of a
    /// ware soonest, the home city when none does.
    City* supply_destination(AiPlayer& p, City& home);
    /// Improved computers: whether one of the computer's cities runs out of food within
    /// kHomeFirstMinutes and a ship could bring it some (supply_need).
    [[nodiscard]] bool food_runs_out(AiPlayer& p);
    /// The wares the computer's other supply ships (not `ship`) and the trade cart bring to `to`;
    /// for the original's computers to any city.
    [[nodiscard]] std::vector<std::uint8_t> routed_wares(AiPlayer& p, std::uint32_t ship, const City& to);
    /// Improved computers: what an own city can give of `w` to another own city (0 for nothing).
    /// Improved computers: the budget every plan of theirs that costs gold goes by (ai_budget.cpp):
    /// their gold, what plans under way set aside (reserved), what it does a minute and the cushion.
    [[nodiscard]] Budget gold_budget(const AiPlayer& p) const;
    /// What the computer pays out a minute: running costs, upkeep and the inhabitants' goods bought.
    [[nodiscard]] std::int64_t outflow(const AiPlayer& p) const;
    /// The taxes it can count on: those of its cities whose inhabitants have their food.
    [[nodiscard]] std::int64_t reliable_income(const AiPlayer& p) const;
    /// Whether a city's statistics do not yet cover the budget's horizon.
    [[nodiscard]] bool young_colony(const City& c) const;
    /// What the young colonies will cost a minute beyond what they cost now.
    [[nodiscard]] std::int64_t young_colonies_gap(const AiPlayer& p) const;
    /// The gold a plan under way keeps: a founding's while its ship loads or sails; 0 for the
    /// original's computers and the pirates.
    [[nodiscard]] std::int64_t reserved(const AiPlayer& p) const;
    /// Whether the budget allows the plan (Budget::allows); a refusal is logged as `what`.
    [[nodiscard]] bool can_commit(const AiPlayer& p, const Commitment& c, Spend what) const;
    /// What `amount` (1/32 t) of `w` costs bought, at the price the computer's cities paid lately.
    [[nodiscard]] std::int64_t price_paid(const AiPlayer& p, std::uint8_t w, std::uint64_t amount) const;
    /// What the home city gains a minute once it has `good`: the taxes of the inhabitants it lets
    /// rise, less the other goods they then buy.
    [[nodiscard]] std::int64_t gate_value(const AiPlayer& p, std::uint8_t good) const;
    /// A colony for `crop` as a plan of the budget (0: for room or ore, it opens no class).
    [[nodiscard]] Commitment colony_commitment(const AiPlayer& p, std::uint8_t crop) const;
    /// Improved computers: what one of their cities beyond the home city costs a minute beyond its
    /// taxes, on average over those that cost more than they pay, uninhabited ones included
    /// (want_colony).
    [[nodiscard]] std::int64_t colony_deficit(const AiPlayer& p) const;
    /// Improved computers: their cities as the statistics windows show them, for each island and
    /// all of them (ai_overview.hpp). Taken from the cities' last closed minutes, so it is built
    /// once a game minute (and when the computer's cities change) and read from m_overviews.
    [[nodiscard]] const Overview& overview(const AiPlayer& p) const;
    [[nodiscard]] Overview build_overview(const AiPlayer& p) const;
    /// One good of the computer's cities (of one island, or all) over the last `minutes` as the
    /// window "Warenbilanz" shows it.
    [[nodiscard]] WareView ware_view(const AiPlayer& p, std::uint8_t ware, std::uint32_t minutes,
                                     std::optional<std::uint16_t> island = std::nullopt) const;
    [[nodiscard]] std::uint32_t own_spare(const City& c, std::uint8_t w, std::uint32_t reserve, bool home) const;
    /// Whether `c` is the computer's home city.
    [[nodiscard]] bool is_home(const AiPlayer& p, const City& c) const {
        return p.home_valid && c.owner == p.player && c.island == p.home_island && c.index == p.home_city;
    }
    /// Improved computers: what is left to buy of `w` at a partner's city `c` (0 for nothing).
    [[nodiscard]] std::uint32_t partner_spare(AiPlayer& p, const City& c, std::uint8_t w);
    /// Improved computers: what one supply run brings of a ware, by the computer's ships.
    [[nodiscard]] std::uint32_t supply_load(AiPlayer& p);
    /// Improved computers: how many supply runs bring ware `w` to `to` at a time from city `src`,
    /// `seconds` away, so that the city does not run short while they sail.
    [[nodiscard]] std::uint32_t supply_ships_for(AiPlayer& p, const City& to, std::uint8_t w, std::size_t src,
                                                 std::uint32_t seconds);
    static constexpr std::uint32_t kMaxRunsPerWare = 4;
    static constexpr std::uint32_t kMaxSupplyShips = 8;
    static constexpr std::uint32_t kMinSupplyRun = 10 * kTon;  ///< the least a supply run loads
    static constexpr std::uint64_t kNoUrgency = std::numeric_limits<std::uint64_t>::max();
    /// The wares a supply run brings to `home`, and how much of each it keeps (`target`).
    [[nodiscard]] std::vector<std::uint8_t> supply_wares(const AiPlayer& p, const City& home,
                                                         std::uint32_t* target) const;
    /// Improved computers: each city's trade cart fetches a ware the city lacks from a partner's
    /// city on its island.
    void cart_supply(AiPlayer& p);
    void update_fleet_target(AiPlayer& p);
    /// Improved computers: where a city lies on the map, its Kontor (else its island's middle).
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> city_point(const City& c) const;
    /// Improved computers: the seconds a ship of the computer takes from (x0, y0) to (x1, y1), an
    /// estimate from the distance and the speed of its first ship.
    [[nodiscard]] std::uint32_t sail_seconds(const AiPlayer& p, std::pair<std::int32_t, std::int32_t> from,
                                             std::pair<std::int32_t, std::int32_t> to);
    /// Improved computers: the stock a city keeps of a ware that a ship brings `seconds` away: the
    /// further, the more, so that it lasts until the ship is back.
    [[nodiscard]] static std::uint32_t supply_buffer(std::uint32_t target, std::uint32_t seconds);
    /// Improved computers: the ships the home city's supply takes, by the wares it lacks and how
    /// far they come from.
    [[nodiscard]] std::int32_t supply_ships_wanted(AiPlayer& p, const City& home);
    void order_ship(AiPlayer& p);
    /// The original's: the cheapest offered ship, for a computer without ships or yard.
    void buy_first_ship(AiPlayer& p);
    /// Open1602: a ship another player offers, bought when it is worth it (Rules::computers_buy_ships).
    bool buy_ship(AiPlayer& p);

    // ---- ai_military.cpp ----
    [[nodiscard]] std::array<std::uint32_t, 4> army_counts(PlayerId owner) const;
    [[nodiscard]] std::array<std::uint32_t, 4> unit_weights() const;
    [[nodiscard]] std::uint32_t weighted_army(PlayerId owner) const;
    [[nodiscard]] std::uint32_t army_target(const AiPlayer& p) const;
    /// Open1602 (rivals): the army a computer keeps whoever else is armed, by its inhabitants, and
    /// the share of the strongest army of another computer, at peace with it or not.
    [[nodiscard]] std::uint32_t rival_army_target(const AiPlayer& p) const;
    [[nodiscard]] bool stronger(PlayerId a, PlayerId b) const;
    [[nodiscard]] std::uint32_t soldiers_on_island(PlayerId owner, std::uint16_t island) const;
    std::vector<Soldier*> soldiers_near(PlayerId owner, const City& c, std::int32_t range, std::size_t max);
    [[nodiscard]] std::uint32_t soldiers_aboard(const Ship& s) const;
    [[nodiscard]] std::uint32_t free_holds(const Ship& s) const;
    bool queue_unit(AiPlayer& p, const CastleEntry& castle, std::uint32_t family);
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
    /// Open1602: the island tiles the soldiers can walk to from where they stand (closed gates of
    /// others count as walls), one flag per tile of the island map.
    [[nodiscard]] std::vector<std::uint8_t> land_reach(PlayerId owner, std::uint16_t island,
                                                       const std::vector<std::uint32_t>& soldiers) const;
    /// Open1602: keeps the goals of an attack the soldiers can walk up to; with none of them, the
    /// first obstacle on the cheapest way to one (`way_in`).
    void reachable_goals(const AiPlayer& a, std::uint16_t island, const City& hostile,
                         const std::vector<std::uint32_t>& soldiers,
                         std::vector<std::pair<std::int32_t, std::int32_t>>& goals) const;
    /// Open1602: the tree, field, wall, gate or building of `hostile`'s owner that stands first on
    /// the cheapest way from the tiles in `reach` to a tile with a goal (`goal_at` >= 0), by its
    /// origin; none if no way leads there.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> way_in(
        const AiPlayer& a, std::uint16_t island, const City& hostile, const std::vector<std::uint8_t>& reach,
        const std::vector<std::int16_t>& goal_at) const;
    /// Open1602: sends the soldiers on `island` against the soldiers of `enemies` that stand in
    /// one of its cities there; whether it gave an order.
    bool defend(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies);
    /// The island the soldier controller looks at next (the original: simply the next one).
    [[nodiscard]] std::optional<std::uint16_t> next_watched_island();
    void island_controller();
    void diplomacy(AiPlayer& p);
    // Strategy (ai_strategy.cpp), improved computers only.
    /// Draws the character once, then looks at every rival: the stances and the goal.
    void plan_strategy(AiPlayer& p);
    /// The war plan: starts a war on a rival it presses, looks every ten minutes whether it gets
    /// anywhere and makes peace when it does not.
    void war_plan(AiPlayer& p, bool broke);
    /// Declares war on `target` with a war plan (its checks and its end).
    void start_war(AiPlayer& p, PlayerId target);
    /// Alliances: offers, answers and ends them; asks allies for help and helps them.
    void alliances(AiPlayer& p, std::uint64_t every_ms, bool broke);
    /// Deep in debt: gives up the city that loses most (not the home city).
    void restructure(AiPlayer& p);
    /// For the log: the events a remembers of b.
    [[nodiscard]] std::string event_text(PlayerId a, PlayerId b) const;
    void end_war(AiPlayer& p, const char* why);
    [[nodiscard]] std::uint32_t war_measure(PlayerId target) const;
    /// The armies and the fleets' cannons of `owner`, weighted as stronger() does.
    [[nodiscard]] std::uint32_t strength(PlayerId owner) const;
    /// strength(), a human's raised on the harder levels (Balance::ai_human_threat_percent).
    [[nodiscard]] std::uint32_t threat_of(PlayerId owner) const;
    [[nodiscard]] std::uint8_t stance_of(const AiPlayer& p, PlayerId other) const;
    /// The gold in hand an improved computer founds a colony with: less while it is after land.
    [[nodiscard]] std::int32_t colony_money(const AiPlayer& p) const;
    /// The share in percent of the army target the character and the goal ask for.
    [[nodiscard]] std::uint32_t army_percent(const AiPlayer& p) const;
    void pirate_step(AiPlayer& p);
    void pirate_develop(AiPlayer& p);

public:
    [[nodiscard]] bool directs(std::uint32_t ship) const;
    void city_founded(std::uint16_t island, std::uint8_t city);
    void land_freed(std::uint16_t island);
    void founding_query(PlayerId asker, std::uint16_t island);

private:
    // ---- ai_build.cpp ----
    void task_building(AiPlayer& p, std::uint8_t task);
    void task_farm(AiPlayer& p, std::uint8_t task);
    void task_workshop(AiPlayer& p);
    void task_houses(AiPlayer& p);
    void task_public(AiPlayer& p, std::uint8_t task);
    void task_market(AiPlayer& p);
    void task_mine(AiPlayer& p);
    void task_claim_land(AiPlayer& p);
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
    /// False for a tile that is no building origin of the city (the survey has nothing there).
    bool survey_tile(AiPlayer& p, const City& c, CityPlan& r, std::uint32_t x, std::uint32_t y, SurveyFinds& finds);
    void sweep(AiPlayer& p);
    /// Open1602: pulls down the producer at (x, y) if the city has no use for it (more workshops
    /// than their raw material keeps busy, or one switched off while its good fills the warehouse)
    /// and that pays by the game's refund rule (worth_demolishing); true if it did.
    bool demolish_surplus(AiPlayer& p, const City& c, CityPlan& r, const Tile& t, std::int32_t x, std::int32_t y);
    /// Open1602: notes the farm at (x, y), which has too little room for its fields, to be moved
    /// (Rules::moving) when the city plans its next building; true if it stays for that.
    bool move_farm_later(AiPlayer& p, const City& c, CityPlan& r, const Tile& t, std::int32_t x, std::int32_t y);
    /// Open1602: plans the search for a site to move the farm noted by move_farm_later to; true if
    /// it was planned.
    bool relocate_farm(AiPlayer& p, const City& c, CityPlan& r);
    /// Some human still in the game has no peace treaty with the computer.
    [[nodiscard]] bool at_war_with_a_human(const AiPlayer& p) const;
    /// The wall or gate the sweep puts on tile (x, y) of the current city, if any. At war: walls
    /// on the border away from other cities and on the coast. At peace: walls on the border
    /// towards other cities and on the coast, gates where a wall crosses a road. A road under a
    /// new wall is demolished at once.
    std::optional<BuildItem> wall_item(AiPlayer& p, const City& c, const Tile& t, std::int32_t x, std::int32_t y,
                                       bool war);
    /// A wall of the city with no coast within two tiles and no neighbour outside the city.
    [[nodiscard]] bool inner_wall(AiPlayer& p, std::int32_t x, std::int32_t y);
    void kontor_restore(AiPlayer& p);
    std::size_t build(AiPlayer& p, std::vector<BuildItem> items, bool orient);
    std::int32_t road_line(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t len, std::uint8_t dir, bool force);
    std::int32_t road_three(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::int32_t right,
                            std::int32_t left, std::int32_t back, bool force);
    void plaza_ring(AiPlayer& p);
    /// The city's own woodcutters whose reach may cover a tile within `n` rings of the rectangle.
    [[nodiscard]] std::vector<WoodReach> woodcutters_near(AiPlayer& p, std::int32_t x0, std::int32_t y0,
                                                          std::int32_t x1, std::int32_t y1, std::int32_t n);
    void fill_fields(AiPlayer& p, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                     std::int32_t rings_n, std::uint8_t field_ware, std::uint8_t radius);
    void slot_built(AiPlayer& p, std::uint8_t slot);
    void slot_ok(AiPlayer& p, std::uint8_t slot);
    std::uint16_t slot_fail(AiPlayer& p, std::uint8_t slot);
    [[nodiscard]] std::int32_t slot_present(const AiPlayer& p, std::uint8_t slot) const;
    std::size_t demolish_tile(AiPlayer& p, std::int32_t x, std::int32_t y);  ///< buildings removed

    Simulation& m_sim;
    World& m_world;
    const assets::BuildingTable& m_buildings;
    State& m_state;
    Player m_nobody;  ///< stands in for a player slot that is gone
    /// Producers by ware: up to four records per ware, in file order.
    std::vector<std::vector<const assets::BuildingType*>> m_producers;
    /// The producers of the packages' goods, slots kSlots and up (ai_chains.hpp).
    std::vector<PackageNeed> m_package_needs;

    /// Soldiers and ships counted once per AI tick (TB-9): the computers ask for them per island
    /// and per player many times a tick. Used only inside tick(), where the units change only
    /// through execute(), which drops it; elsewhere the queries walk the lists as before.
    struct Census {
        bool valid = false;
        std::size_t players = 0;
        std::size_t islands = 0;
        std::vector<std::uint32_t> on_island;               ///< [owner * islands + island], not away
        std::vector<std::array<std::uint32_t, 4>> army;     ///< [owner] by family: living soldiers
        std::vector<std::array<std::uint32_t, 4>> aboard;   ///< [owner] by family: soldiers in holds
        std::vector<std::uint32_t> cannons;                 ///< [owner]: cannons of ships afloat
        std::vector<std::vector<std::uint32_t>> own_ships;  ///< [owner]: slots of own_ships()
    };
    [[nodiscard]] const Census* census() const;
    mutable Census m_census;
    /// overview() by player: built from the cities' closed minutes only, for the minutes closed
    /// before `minute` and the cities in `cities`, so a saved game rebuilds the same one.
    struct OverviewCache {
        bool valid = false;
        std::uint64_t minute = 0;
        std::vector<std::uint32_t> cities;  ///< the indices of the computer's cities
        Overview view;
    };
    mutable std::vector<OverviewCache> m_overviews;
    /// gold_budget's flows per player, worked out once per game time step: the running costs and the
    /// goods bought are summed over every city and minute.
    struct BudgetCache {
        bool valid = false;
        std::uint64_t ms = 0;
        std::int64_t net = 0;
        std::int64_t outflow = 0;
        std::int64_t reliable = 0;
    };
    mutable std::vector<BudgetCache> m_budgets;
    /// The game minute of the last refusal logged, per player and kind of plan (only the log).
    mutable std::vector<std::array<std::uint64_t, kSpends>> m_refusals_logged;
    /// Free land from which `grown` grows a stage's orders, during the grow pass of `run_stage`.
    std::uint32_t m_grow_from = 0;  ///< 0: kGrowFrom
    /// The order and plan of can_place(), reused so that a site test allocates nothing.
    mutable BuildOrder m_place_order;
    mutable BuildPlan m_place_plan;
    /// Free green land and trees of an island whose Kontor site is sought (task 3), kept per
    /// island while its tiles stay the same: each region and each computer asks again (TB-13).
    struct SiteCounts {
        std::uint64_t revision = 0;  ///< the newest block revision of the island's map
        AreaCount green;
        AreaCount trees;
        AreaCount fish;     ///< fish shoals, growing or ripe
        AreaCount fishers;  ///< fishing huts of anyone
    };
    const SiteCounts& site_counts(std::uint16_t island);
    /// Open1602: whether a fishing hut on the beach tile (x, y) finds as many shoals and no other
    /// hut as search_fisher wants; fisher_room: whether such a tile lies near a Kontor at (cx, cy).
    [[nodiscard]] static bool fisher_spot(const SiteCounts& counts, std::int32_t x, std::int32_t y);
    [[nodiscard]] bool fisher_room(std::uint16_t island, const SiteCounts& counts, std::int32_t cx, std::int32_t cy);
    std::vector<std::optional<SiteCounts>> m_site_counts;
    /// The free land regions of an island (task 2) and its free beach tiles (task 3), kept per
    /// island while its tiles stay the same: every pass of the island search tests the islands
    /// again (TB-13).
    struct AreaScan {
        std::uint64_t revision = 0;  ///< 0: none yet
        std::vector<Region> regions;
        std::int32_t total = 0;
        std::vector<std::pair<std::int32_t, std::int32_t>> beach;  ///< row by row, left to right
    };
    const AreaScan& area_scan(std::uint16_t island);
    /// The regions of free settling ground of an island (runs of 5 both ways, connected), their
    /// total area and the beach tiles of no city.
    void scan_area(std::uint16_t island, AreaScan& out) const;
    std::vector<AreaScan> m_area_scans;
    /// Whether the water tile (x, y) of an island joins the sea around it (Open1602): false on a
    /// lake inside the island, and on land. Water stays where it is during a game, so each
    /// island's map of it is made once, at the first question.
    [[nodiscard]] bool open_water(std::uint16_t island, std::int32_t x, std::int32_t y);
    std::vector<std::vector<std::uint8_t>> m_open_water;  ///< per island, row by row: 1 open water
    /// The newest block revision of an island's map: equal while none of its tiles changed.
    [[nodiscard]] std::uint64_t island_revision(std::uint16_t island) const;
    /// city_defenceless, from one scan of the island per revision for all its cities (TB-9).
    struct Defences {
        bool valid = false;
        std::uint64_t revision = 0;
        std::array<bool, 256> defended{};  ///< by city number
    };
    [[nodiscard]] bool defenceless(std::uint16_t island, std::uint8_t city);
    std::vector<Defences> m_defences;  ///< by island
    /// Open1602: where a city's houses go. A disc about the market nearest the Kontor, as large as
    /// the room its houses and the next ones need (with roads and public buildings), on the island's
    /// free and town land; kept while the island's tiles stay the same, and found again from them
    /// alone after a game is loaded.
    const TownZone& town_zone(AiPlayer& p);
    /// The town style of the current city, drawing the computer's AiPlayer::town_seed first if it
    /// has none yet.
    TownStyle town_style_of(AiPlayer& p);
    /// What the whole-island scans of a city found, kept while the island's tiles stay the same
    /// (TB-11): the building origins of the last survey (the next one visits only them, in the
    /// same order) and the road segments of the last layout.
    struct CityScans {
        std::uint16_t island = 0;
        std::uint8_t city = 0;
        std::uint64_t survey_revision = 0;  ///< 0: none yet; revisions start at 1
        std::vector<std::pair<std::uint16_t, std::uint16_t>> survey_tiles;
        std::uint64_t layout_revision = 0;
        std::vector<Segment> segments;
        std::uint64_t zone_revision = 0;
        TownZone zone;
    };
    CityScans& city_scans(std::uint16_t island, std::uint8_t city);
    std::vector<CityScans> m_city_scans;
    bool m_in_tick = false;
    /// The computer log only (not saved): what a player's last site search saw, and when its cities
    /// were last reported.
    struct SiteTally {
        std::int32_t sites = 0;   ///< free sites looked at
        std::int32_t road = 0;    ///< of them by a road
        std::int32_t market = 0;  ///< in reach of a market or the Kontor
        std::int32_t both = 0;
        std::int32_t best = std::numeric_limits<std::int32_t>::min();  ///< best score of those with both
    };
    SiteTally& site_tally(const AiPlayer& p);
    void tally_site(const AiPlayer& p, bool road, bool market, std::int32_t score);
    void log_no_site(const AiPlayer& p, std::uint32_t building);
    std::vector<SiteTally> m_site_tally;
    std::vector<std::uint64_t> m_report_ms;
    /// The computer log only: the stage order's entry a player's last look stopped at.
    std::vector<std::int32_t> m_last_need;
    /// A measurement's delay of each computer's first step (delay_first_step), and whom it reached.
    std::uint32_t m_delay_steps = 0;
    std::vector<bool> m_delayed;
};

/// Ring iterator: ring d (Chebyshev distance d + 1) around the
/// rectangle, top row, then the sides top to bottom, then the bottom row.
template <class F>
void Engine::rings(AiPlayer& p, RingFilter filter, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                   std::int32_t n, F&& f) {
    if (p.island >= m_world.islands().size()) {
        return;
    }
    const IslandMap& map = m_world.islands()[p.island].map;
    const auto visit = [&](std::int32_t x, std::int32_t y, std::int32_t d) {
        if (!map.contains(x, y)) {
            return;
        }
        const MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        // The city comes from the cell alone: tiles of other cities skip the building lookup.
        if (filter == RingFilter::Own && c.city != p.city) {
            return;
        }
        const Tile t = tile_of(c, x, y);
        switch (filter) {
            case RingFilter::Own:
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
