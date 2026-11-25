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
#include <tuple>
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
#include "o1602/sim/pathfinding.hpp"
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

/// What a computer's stance towards one rival is made from, at one look.
struct StanceInput {
    bool allied = false;
    bool trade_partner = false;  ///< a trader with a signed trade treaty
    std::int32_t mood = 0;
    bool near = false;
    std::uint32_t own = 0;     ///< its own strength
    std::uint32_t theirs = 0;  ///< the rival's threat
    std::uint32_t press = 0;   ///< percent of the rival's threat its own strength must reach
    std::uint8_t previous = 0;
    bool latch = false;  ///< Open1602: improved computers keep pressing a rival they do not like
};

struct StanceStep {
    std::uint8_t stance = 0;
    const char* why = "";  ///< the branch that set it
    bool held = false;     ///< kept in pressure by the latch only
};

/// The holds, free or with soldiers aboard, of a ship that takes a landing party (best_armed_ship).
inline constexpr std::uint32_t kLandingHolds = 3;

/// What war_supplied's gold term and war_carried read.
struct WarGold {
    bool improved = false;  ///< not Rules::bugs.classic_ai
    std::int64_t money = 0;
    std::int64_t reserve = 0;  ///< kWarReserve x kWarSupplyReserve
    bool pays_upkeep = true;
    std::int32_t civil_income = 0;
    std::int32_t military_cost = 0;
    std::int32_t arming_budget = 0;
    std::int64_t floor = 0;  ///< below the reserve: the broke line and the first landing's ship
    std::int32_t net_income = 0;
};

/// Whether its income carries the army at war (ai_strategy.cpp).
[[nodiscard]] bool war_carried(const WarGold& g);
/// Whether war_supplied's gold term passes: the reserve in hand, or (Open1602) an income that carries
/// the war; the original's computers pass it always, as war_supplied does.
[[nodiscard]] bool war_gold(const WarGold& g);

/// The stance towards a rival (ai_strategy.cpp).
[[nodiscard]] StanceStep stance_step(const StanceInput& in);

/// What the survey of a city (task 8) found to clear away: exhausted mines, and sheep farms and
/// small weavers for when cotton grows on the island.
struct Cheats;   // ai_tables.hpp
struct NeedDef;  // ai_tables.hpp

struct SurveyFinds {
    std::vector<std::pair<std::int32_t, std::int32_t>> starved_mines;
    /// Open1602 (F15): the other mines of an improved computer, with their slot and whether a
    /// cart of the city reaches them.
    struct Mine {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint8_t slot = 0;
        bool fetched = false;
    };
    std::vector<Mine> mines;
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
    /// How many cities `p` founds at most: the city limit, for an improved computer raised to what
    /// the game's victory asks (goal_city_count).
    [[nodiscard]] std::uint32_t city_room(const AiPlayer& p) const;
    /// The cities a victory asks of a player: the islands share (`percent` of `islands`, only when
    /// computers can win) or a mission's count (`mission_min`), whichever is more.
    [[nodiscard]] static std::uint32_t goal_city_count(std::uint32_t islands, std::uint8_t percent, bool computers_win,
                                                       std::int32_t mission_min) noexcept;
    /// What an improved computer's goal asks a colony for (gold, ore), 0 for nothing (ai_settle.cpp).
    [[nodiscard]] std::uint8_t goal_crop(const AiPlayer& p) const;

    // ai_logistics.cpp
    /// Whether `p` knows of city `c`: on an island it discovered, or with sees_all().
    [[nodiscard]] bool city_known(const AiPlayer& p, const City& c) const;

    // ai_strategy.cpp
    /// Whether rival `b` has a city near `p`'s home: on its island or within four minutes at sea,
    /// only among the cities `p` knows of; `unseen` is set when an unknown one would have counted.
    [[nodiscard]] bool rival_near(const AiPlayer& p, PlayerId b, bool* unseen = nullptr);
    /// What a war on `target` has left to take, as `p` sees it: the inhabitants of its known cities
    /// and fifty for each of them.
    [[nodiscard]] std::uint32_t war_measure(const AiPlayer& p, PlayerId target) const;

    // ai_logistics.cpp
    /// Whether `p` knows partner city `c`'s Kontor, its prices and offer: on an island it discovered,
    /// or with sees_all().
    [[nodiscard]] bool kontor_known(const AiPlayer& p, const City& c) const;
    /// What `c`'s Kontor offers of `w`: below the hardest level what its window shows (stock less
    /// what it keeps and what is reserved there, nothing under a ton), else stock less what it keeps.
    [[nodiscard]] std::uint64_t kontor_offer(const City& c, std::uint8_t w) const;
    /// Whether a city of the computer (of any computer for the original's) lies on an island that
    /// grows `crop` fully (ai_settle.cpp).
    [[nodiscard]] bool crop_available(const AiPlayer& p, std::uint8_t crop) const;
    /// Whether guard() sends soldier `s` near the Kontor to `post` (ai_military.cpp).
    [[nodiscard]] bool guard_takes(const Soldier& s, const Target& post) const;

    // ai_build.cpp
    /// Whether a Kontor's or market's carts of `p`'s city reach a mine `mine` placed at one of
    /// task_mine's placements around slope tile (x, y), with the planned `road` laid. Always for a
    /// quarry, whose stone the stonemason fetches.
    [[nodiscard]] bool mine_reached(const AiPlayer& p, const assets::BuildingType& mine, std::int32_t x, std::int32_t y,
                                    std::span<const BuildItem> road) const;
    /// The same for the one placement with origin (x0, y0) and `rotation`.
    [[nodiscard]] bool mine_reached(const AiPlayer& p, const assets::BuildingType& mine, std::int32_t x0,
                                    std::int32_t y0, std::uint8_t rotation, std::span<const BuildItem> road) const;
    /// Open1602 (F3r): the circles of the stonemasons of `p`'s city, which fetch the stone from the
    /// quarries' tiles inside them; empty without a mason.
    [[nodiscard]] std::vector<Catchment> mason_circles(const AiPlayer& p) const;
    /// Whether a quarry `quarry` placed at (x0, y0) with `rotation` has a tile in one of `circles`.
    [[nodiscard]] bool masons_reach(std::span<const Catchment> circles, const assets::BuildingType& quarry,
                                    std::int32_t x0, std::int32_t y0, std::uint8_t rotation) const;
    /// Whether one of task_mine's placements of `quarry` around slope tile (x, y) has a tile in one
    /// of `circles`; true without circles.
    [[nodiscard]] bool masons_reach(std::span<const Catchment> circles, const assets::BuildingType& quarry,
                                    std::int32_t x, std::int32_t y) const;

    // ai_military.cpp
    /// Open1602 (F5): when the cell where ship `s`'s soldiers would land at `p`'s city `c` is cut
    /// off from the city's buildings, the water nearest to the ship from which they would land on
    /// land walkable to the city; nothing when they land there now or no such water is found.
    [[nodiscard]] std::optional<Target> landing_shore(const AiPlayer& p, const Ship& s, const City& c) const;
    /// The beach tile (island coordinates) a landing against city `c` makes for: near its markets
    /// (or its Kontor), away from hostile towers, soldiers and ships; none when no beach is free.
    /// With `wall`, the hostile wall on the beach a breach shoots at, chosen the same way.
    /// With `any`, a free beach even when nothing lets the soldiers stand within the unload range.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> landing_beach(const AiPlayer& p, const City& c,
                                                                                     bool wall = false,
                                                                                     bool any = false) const;
    /// What a breach shoots at: a wall on the beach, else the tree or wall that seals the landing.
    /// Whether `p` would attack city `t` now (attack_ok); for tests.
    /// The strength `p` arms to (army_target); for tests.
    [[nodiscard]] std::uint32_t army_target_for(const AiPlayer& p) const { return army_target(p); }
    /// The war plan's step for tests (war_plan).
    void war_plan_for(AiPlayer& p) { war_plan(p, false); }
    [[nodiscard]] bool attack_ok_for(AiPlayer& p, const City& t) {
        std::size_t convoy = 1;
        return attack_ok(p, t, convoy);
    }
    /// Sends `p`'s attack on city `t` (start_attack); for tests.
    void start_attack_for(AiPlayer& p, const City& t, std::size_t convoy = 1) { start_attack(p, t, convoy); }
    /// Open1602: whether a computer that pays upkeep adds a unit or tower, `next` its upkeep with
    /// it: below its core army while its civil income carries that, beyond it within three
    /// quarters of its army budget; never in debt, where a war's losses were refilled to the core
    /// on credit and the upkeep took half the civil income (gate f24, l3 g12-404-p3 P7).
    /// Open1602 (Merlin, 2026-11-24): whether a computer whose war lacks `lacking` soldiers for a
    /// party may raise them beyond its army budget: its gold above the broke line pays their upkeep
    /// (`unit_upkeep` each a minute) and its cities' deficit (`net` below zero) for half an hour.
    [[nodiscard]] static bool party_paid(std::int64_t money, std::uint32_t lacking, std::int32_t unit_upkeep,
                                         std::int32_t net) noexcept;
    /// party_paid for `p`'s war party (war_party_lacking).
    [[nodiscard]] bool party_paid(const AiPlayer& p) const;
    /// What a route stop still loads of a ware: its `amount` less what is `aboard`, nothing at a
    /// stop a one-way run has `passed`.
    [[nodiscard]] static std::uint32_t still_loads(std::uint32_t amount, std::uint32_t aboard, bool passed) noexcept;
    /// Whether a city leaves a citizens' stage on gold alone (`rich`): the original's computers at
    /// once, an improved computer's once its citizens have begun to come (`risen`).
    [[nodiscard]] static bool gold_exit(bool improved, bool rich, bool risen) noexcept;
    /// Whether a city at stage citizens moves on: its citizens are there, and it holds the gold or
    /// (an improved computer) earns.
    [[nodiscard]] static bool citizens_rise(bool citizens, bool gold, bool earning) noexcept;
    [[nodiscard]] static bool may_add_unit(bool below_core, std::int64_t money, std::int32_t next, std::int32_t civil,
                                           std::int32_t budget) noexcept;
    using WareSet = std::array<bool, 256>;
    /// The wares `player`'s cities sold in the last hour, by their goods statistics (GoodsLedger,
    /// the window "Warenbilanz" a player reads): a buyer takes them, so in debt they earn gold.
    [[nodiscard]] static WareSet sold_wares(const World& world, PlayerId player);
    /// Whether a producer of type `b` may be torn down in debt when its ware is not in `keep`:
    /// workshops, mines, pastures, hunters and fishers. A farm with fields only goes off
    /// (production_switches): torn down, its fields would be swept with it and cost the whole
    /// plantation again once the ware sells or is wanted (gate f25, g4-202 P1).
    [[nodiscard]] static bool may_shed(const assets::BuildingType& b, const WareSet& keep) noexcept;
    /// Whether a luxury crop is planted on an island of `fertility` (island_fertility): where it
    /// grows fully; where it grows half only as a last resort: while the computer's cities lack
    /// its good now (`lacking`, goods_short), trade does not cover it (`traded`: a partner offers
    /// it and the city has not run dry), no island that grows it fully is open for a colony
    /// (`colony`, fertile_island_open), and it can pay for the plantation now (`pays`).
    [[nodiscard]] static bool plants_on(std::uint32_t fertility, bool lacking, bool traded, bool colony,
                                        bool pays) noexcept;
    /// Whether another player's city leaves room to found beside it: not an island the original keeps
    /// off (`kept_off`), enough free land (`room`), and the owner has two cities or more and its city
    /// no people yet, unless the improved computer chose the island as contested (`contested`).
    [[nodiscard]] static bool leaves_room(bool kept_off, bool contested, bool small_owner, bool room,
                                          bool populated) noexcept;
    /// Whether an island's `free` land leaves room to found beside another city (blocker).
    [[nodiscard]] static bool room_beside(std::uint32_t free) noexcept;
    /// Whether improved pirates pass over an island in their search: one they turned back from
    /// lately (`left`) or whose cities leave no room (`blocked`); the original's pirates never do.
    [[nodiscard]] static bool pirate_island_skipped(bool improved, bool left, bool blocked) noexcept;
    /// Whether a ship with `soldiers` aboard is kept out of the idle ships (improved computers).
    [[nodiscard]] static bool troops_aboard_idle(bool improved, std::uint32_t soldiers) noexcept;
    /// Improved computers: the island search may pick an island below the city limit, or above it
    /// one its war freed (`freed`: one is still free, `chosen_freed`: this one).
    [[nodiscard]] static bool beyond_limit(bool below_limit, bool freed, bool chosen_freed) noexcept;
    /// Improved computers: a war is started only when the computer could settle the island a razed
    /// city frees: `room` below its city limit and a colony its budget accepts (`affordable`).
    [[nodiscard]] static bool war_settles(bool room, bool affordable) noexcept;
    /// Whether `held` of a good (1/32 t) covers a `shortfall` (1/32 t a minute) for `minutes`.
    [[nodiscard]] static bool stock_covers(std::uint32_t shortfall, std::uint64_t held, std::uint64_t minutes) noexcept;
    /// Improved computers: a war for a crop island makes sense when the cities lack the crop's good
    /// (`lacking`), the computer could found the colony on the freed island (`settles`: below its
    /// city limit and the budget accepts it), and no free island grows the crop (`free`).
    [[nodiscard]] static bool crop_war_motive(bool lacking, bool settles, bool free) noexcept;
    /// Improved computers: whether its cities lack a crop's good for a war: used beyond what they
    /// make (`short_of`), none of its islands grows a crop for it (`grown`, crop_available), and a
    /// city that uses it has none (`dry`).
    [[nodiscard]] static bool crop_lacking(bool short_of, bool grown, bool dry) noexcept;
    /// Improved computers' first castle (t64): a rival `near` it and not at `peace` shows a stronger
    /// army (`theirs`, as seen) than its `own`.
    [[nodiscard]] static bool outmatched_by(bool near, bool peace, std::uint32_t theirs, std::uint32_t own) noexcept;
    /// Improved computers: a ship under the white flag takes no order while a pirate is near.
    [[nodiscard]] static bool flag_keeps(bool improved, bool held, bool pirate_near) noexcept;
    /// Improved computers: the colony ship at its site waits for the Kontor's gold instead of trying.
    [[nodiscard]] static bool founding_waits(bool improved, bool pirates, std::int64_t money,
                                             std::int64_t cost) noexcept;
    /// The ships' missions of `p`, once (missions()); for tests.
    void run_missions(AiPlayer& p) { missions(p); }
    /// The orders of `a`'s soldiers on `island` against `enemies`, once (attack_orders); for tests.
    bool run_attack_orders(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies) {
        return attack_orders(a, island, enemies);
    }
    /// Open1602: the fleet attack_ok weighs the shelling of a city's towers with: the one
    /// shell_towers will have, the warships free then (free_warships) without the troop ship (the
    /// first landing ship), or the troop ship alone when none is.
    [[nodiscard]] std::vector<Ship*> attack_fleet(AiPlayer& p);
    /// Open1602: the ids of the ships a convoy may take, in the order it takes them (landing_ships).
    [[nodiscard]] std::vector<std::uint32_t> landing_ship_ids(AiPlayer& p) {
        std::vector<std::uint32_t> out;
        for (const Ship* s : landing_ships(p)) {
            out.push_back(s->id);
        }
        return out;
    }
    /// Open1602: how many hostile towers lie in the way of a party from cell (cx, cy) to city `c`
    /// (towers_over_way).
    /// Improved computers: soldiers unloaded from `s` now land on an island where `owner` has a city.
    [[nodiscard]] bool lands_on_target(const AiPlayer& p, const Ship& s, PlayerId owner) const;
    /// Improved computers: open water within the unload range from which soldiers land there.
    [[nodiscard]] std::optional<Target> target_landing_water(const AiPlayer& p, const Ship& s, PlayerId owner) const;
    [[nodiscard]] std::size_t towers_in_the_way(const AiPlayer& p, const City& c, std::int32_t cx,
                                                std::int32_t cy) const;
    /// Open1602 (3b): with `s`, a covering tower it can shell (shells_from_water) when a beach is free.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> breach_target(const AiPlayer& p, const City& c,
                                                                                     const Ship* s = nullptr) const;
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> landing_blocker(const AiPlayer& p,
                                                                                       const City& c) const;
    void log_sealed(const AiPlayer& p, const City& c, std::uint32_t beaches) const;
    /// Open1602: a wall standing on the beach, and the ruin it leaves when shot down.
    [[nodiscard]] bool beach_wall(std::uint16_t island, std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] bool beach_gap(std::uint16_t island, std::uint32_t x, std::uint32_t y) const;
    /// Whether ship `s` sent on `mission` fights on its way (an attack move): raids, attacks and
    /// landings of improved computers, not while soldiers are aboard (TM).
    [[nodiscard]] bool attack_move_for(const Ship& s, std::uint8_t mission) const;
    /// Open1602 (BD): when no tile `soldiers` (of `p`, at city `c`) can walk to lies within boarding
    /// range of ship `s`, the water nearest to the ship from which one does; nothing when one does now
    /// or no such water is found.
    [[nodiscard]] std::optional<Target> boarding_water(const AiPlayer& p, const Ship& s, const City& c,
                                                       const std::vector<std::uint32_t>& soldiers) const;
    /// Open1602 (F4): whether units released from `p`'s castle `e` on `island` can walk from the
    /// cell they step out on to the city's Kontor (true too where the check does not apply).
    [[nodiscard]] bool way_out(const AiPlayer& p, std::uint16_t island, const CastleEntry& e) const;

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
    [[nodiscard]] bool outmatched(const AiPlayer& p);
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
    /// The island the scenario chose for the first city of `p` (Player::first_island), if it may
    /// still have one there; a random choice is drawn now. Nothing: the search as always.
    [[nodiscard]] std::optional<std::uint16_t> first_island_choice(AiPlayer& p);
    /// Whether another computer goes to `island` or the scenario chose it for one.
    [[nodiscard]] bool first_island_aimed(const AiPlayer& p, std::uint16_t island) const;
    /// Whether `island` is the one the scenario chose for `p`; the choice is spent then.
    bool spend_first_island(AiPlayer& p, std::uint16_t island);
    [[nodiscard]] bool knows_island(const AiPlayer& p, std::uint16_t island) const;
    /// Improved computers: the computer gave a city up on the island in the last 45 minutes.
    [[nodiscard]] bool left_lately(AiPlayer& p, std::uint16_t island);
    /// An island `p` turned back from or gave a city up on within kLeftIslandMs, with or without a
    /// home city (left_lately asks for one).
    [[nodiscard]] bool left_recently(AiPlayer& p, std::uint16_t island);
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
    /// Open1602 (G4b): an improved rival's one colony decision a step. Of the crops the original's
    /// order of motives offers (colony_candidates), those its budget carries (colony_commitment, as
    /// before), the one that ranks highest by colony_rank (Settlement::plan_crop); the plan keeps
    /// its gold and a ship (plan_ship).
    void colony_plan(AiPlayer& p, bool asked);
    /// G4b: the buildings of the chain a colony for `crop` builds, with their counts.
    struct Chain {
        std::vector<std::pair<const assets::BuildingType*, std::uint32_t>> parts;
    };
    [[nodiscard]] Chain colony_chain(const AiPlayer& p, std::uint8_t crop) const;
    /// G4b: the running costs a minute of that chain.
    [[nodiscard]] std::int64_t chain_upkeep(const AiPlayer& p, std::uint8_t crop) const;
    /// The crops a colony may be founded for, in the order choose_colony_crop takes the first of.
    [[nodiscard]] std::vector<std::uint8_t> colony_candidates(AiPlayer& p);
    /// G4b: the rank of a colony for `crop` (0: any island) among the candidates, gold a minute: the
    /// taxes of the city it becomes, less its chain's running costs, plus what its good lets the
    /// home city's houses rise into. Ranks only; it refuses nothing.
    [[nodiscard]] std::int64_t colony_rank(const AiPlayer& p, std::uint8_t crop);
    /// Whether a city founded (`founded`, nullptr before the founding) with `land` free tiles and
    /// `buildings` becomes a colony without houses (stage kColony).
    [[nodiscard]] bool becomes_colony(const AiPlayer& p, const City* founded, std::uint32_t land,
                                      std::uint32_t buildings) const;
    /// Open1602 (Rules::computer_cities scaled): a computer whose cities run well wants a further
    /// island for room to grow, though every crop it asks for grows already.
    [[nodiscard]] bool wants_room(const AiPlayer& p) const;
    /// Improved computers: an island its war freed has no city yet.
    [[nodiscard]] bool freed_island(const AiPlayer& p) const;
    /// The owner of another city on `island` that leaves no room to found beside it, or kNoPlayer;
    /// `room`: enough free land.
    [[nodiscard]] PlayerId blocker(const AiPlayer& p, std::uint16_t island, bool room) const;
    [[nodiscard]] std::uint8_t choose_colony_crop(AiPlayer& p);
    /// Improved computers: whether city `c`'s farm for `crop` found no site after repeated tries.
    [[nodiscard]] bool crop_blocked(const AiPlayer& p, const City& c, std::uint8_t crop) const;
    /// Whether the island grows `crop`: a full field (128), or for ore a big or small vein.
    [[nodiscard]] bool grows(std::uint16_t island, std::uint8_t crop) const;
    /// Improved computers: an island it knows grows the crop fully and has no city yet, so a
    /// colony for the crop has room there, and its budget would found that colony now.
    [[nodiscard]] bool fertile_island_open(const AiPlayer& p, std::uint8_t crop) const;
    /// Improved computers: its budget would found a colony for `crop` now.
    [[nodiscard]] bool colony_affordable(const AiPlayer& p, std::uint8_t crop) const;
    /// Improved computers: an island it knows grows `crop` fully and has no city yet.
    [[nodiscard]] bool fertile_island_free(const AiPlayer& p, std::uint8_t crop) const;
    /// Improved computers: `b` holds a city on an island that grows fully a crop whose good the
    /// computer's cities lack now, with no free island for it, while it could settle the island
    /// its war frees (below its city limit, budget accepts the colony): a reason to conquer.
    [[nodiscard]] bool holds_wanted_crop(const AiPlayer& p, PlayerId b) const;
    /// The taxes a minute of `people` of class k in `city`, at its tax rate (consumption.hpp, tax_income).
    [[nodiscard]] std::int64_t class_taxes(const City& city, std::size_t k, std::uint64_t people) const;
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
    /// With `sales` also the wares its cities sold in the last hour (sold_wares): kept standing in
    /// debt (shed_producers), not kept running (production_switches).
    [[nodiscard]] WareSet needed_wares(const AiPlayer& p, bool soon, bool sales) const;
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
    /// The luxury fields a colony plants by the highest stage the player reached (colony_luxury,
    /// colony_chain).
    [[nodiscard]] static std::uint32_t colony_fields(std::uint8_t max_stage) noexcept;
    bool farm_animals(AiPlayer& p, const CityPlan& r);
    bool need(AiPlayer& p, std::uint8_t which, std::uint32_t n, bool cov = false);
    /// The gold a need's fields cost besides its building (64 fields' worth, as need() reserves).
    [[nodiscard]] std::int64_t fields_money(const NeedDef& d) const;
    /// Whether need() would pay for building `b` of need `d` now (affordable, and the fields).
    [[nodiscard]] bool slot_affordable(const AiPlayer& p, const City& c, const NeedDef& d,
                                       const assets::BuildingType& b) const;
    /// slot_affordable for the building of slot `which` in the current city.
    [[nodiscard]] bool slot_affordable_now(const AiPlayer& p, std::uint8_t which) const;
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
    /// Whether `p` has room to settle an island its war frees (war_settles' `room`).
    [[nodiscard]] bool settle_room(const AiPlayer& p) const;
    /// Open1602: whether its income carries the army at war (civil income, upkeep within arming_budget).
    [[nodiscard]] bool war_carried(const AiPlayer& p) const;
    [[nodiscard]] WarGold war_gold_input(const AiPlayer& p) const;
    /// What the first landing's ship costs: 0 with an idle armed ship, the yard's warship's price,
    /// -1 without a yard (ai_strategy.cpp).
    [[nodiscard]] std::int64_t landing_ship_cost(const AiPlayer& p) const;
    /// Whether `s` can take a landing party now: armed, room for one, and free (ai_military.cpp).
    [[nodiscard]] bool takes_landing(const AiPlayer& p, const Ship& s, const ShipNote& n) const;
    /// Whether the computer's yard builds the small ships (ai_logistics.cpp).
    [[nodiscard]] bool small_yard(const AiPlayer& p) const;
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
    /// `circles`: only a slope where a quarry reaches one of them (masons_reach).
    bool mine_path(AiPlayer& p, std::uint8_t ware, std::vector<BuildItem>& road,
                   std::span<const Catchment> circles = {});
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
    /// Open1602 (F3r): the stonemasons' circles a quarry of the current search must reach (empty
    /// for other mines, a classic computer or a city without a mason).
    [[nodiscard]] std::vector<Catchment> quarry_circles(const AiPlayer& p, std::uint8_t ware) const;
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
    /// RC1: per tile of a city's island, the city's markets and Kontors (a bit each, its first 64)
    /// whose carts reach the tile on roads (`reach`) and whose circle holds it (`inside`).
    struct CartReach {
        std::uint16_t island = 0;
        std::uint8_t city = MapCell::kNoCity;
        std::uint64_t revisions = 0;  ///< island_revision() it was taken at
        std::int32_t width = 0;
        std::vector<std::uint64_t> reach;
        std::vector<std::uint64_t> inside;
    };
    [[nodiscard]] const CartReach& cart_reach(const AiPlayer& p) const;
    /// Whether the carriers of a shipyard `b` placed at (x, y) with `rotation` reach a market or
    /// the Kontor of p's city (Logistics::send_yard_carrier's search).
    [[nodiscard]] bool yard_reaches(const AiPlayer& p, const assets::BuildingType& b, std::int32_t x, std::int32_t y,
                                    std::uint8_t rotation) const;
    /// Logs what each improved rival's ships brought its cities (only the log).
    void log_supply();
    /// Buildings of one id: how many, and what one is worth in gold (only the log).
    struct BuildingCount {
        std::uint32_t id = 0;
        std::uint32_t count = 0;
        std::uint64_t value = 0;
    };
    /// A landing party the log follows, from its landing to its end (only the log).
    struct LandingWatch {
        PlayerId player = kNoPlayer;
        PlayerId owner = kNoPlayer;  ///< the target city's owner at the landing
        std::uint16_t island = 0;
        std::uint32_t city = 0;  ///< index into World::cities()
        std::uint32_t ship = 0;
        std::uint64_t landed_ms = 0;
        std::uint64_t next_ms = 0;
        std::uint32_t first_id = 0;  ///< the party: soldiers with an id from first_id to last_id
        std::uint32_t last_id = 0;
        std::uint32_t towers = 0;  ///< hostile towers on the island at the landing
        /// The hostile soldiers on the island at the landing (Soldier::id), to tell the defenders who
        /// come later by where they come from.
        std::vector<std::uint32_t> foes;
        /// The attacker's ships off the island at the landing: the carrier and its escort.
        std::vector<std::uint32_t> ships;
        /// The defender's buildings on the island at the landing.
        std::vector<BuildingCount> buildings;
    };
    /// The buildings of `owner` on `island` by building id, with their count and build value in
    /// gold (build_value; only the log).
    [[nodiscard]] std::vector<BuildingCount> buildings_of(std::uint16_t island, PlayerId owner) const;
    /// The attacker's ships within reach of `island` (as landing_beach counts the hostile ones).
    [[nodiscard]] std::vector<std::uint32_t> ships_off(std::uint16_t island, PlayerId player) const;
    /// Logs what a landing meets: the party by type, the towers and soldiers of the defence.
    void log_landing(const AiPlayer& p, const Ship& s, std::uint32_t first_id, PlayerId owner);
    /// Each minute, how the landing parties fare; once the party is gone, its end.
    void watch_landings();
    /// RC1: whether a cart of the city's markets or Kontor would fetch from a building at (x0, y0,
    /// x1, y1) with `road` (new road tiles from the city's roads to it) built: the market's circle
    /// holds the new road and a tile of the building, and its carts reach them on roads.
    [[nodiscard]] bool cart_reaches(const AiPlayer& p, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                    std::int32_t y1, std::span<const BuildItem> road = {}) const;
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
    /// Open1602 (3b): the hull (1/32) a ship can lose before healthy() no longer takes it.
    [[nodiscard]] std::uint32_t hull_to_spare(const Ship& s) const;
    /// Whether the ship is healthy enough for a job (kHealthy, as the logistics picks ships).
    [[nodiscard]] bool healthy(const Ship& s) const;
    [[nodiscard]] bool ship_at_city(const Ship& s, const City& c) const;
    bool ship_idle(AiPlayer& p, const Ship& s);
    void send_to_city(AiPlayer& p, Ship& s, const City& c);
    /// Stops the ship where it is, its route and job ended.
    void stop_ship(AiPlayer& p, Ship& s);
    void arm_one(AiPlayer& p, City& c, Ship& s);
    /// Open1602: the cannons a city keeps for a stone tower or castle it may still build.
    [[nodiscard]] std::uint32_t cannons_kept(AiPlayer& p, const City& c);
    /// Open1602: cannons onto every warship that lies at one of the computer's Kontors.
    void arm_at_kontors(AiPlayer& p);
    mutable std::vector<std::pair<std::uint32_t, bool>> m_cannon_holds;  ///< arm_one's log only
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
    /// Whether the budget allows the plan (Budget::allows); a refusal is logged as `what`. `own` is
    /// gold that reserved() keeps for this very plan, not counted twice.
    [[nodiscard]] bool can_commit(const AiPlayer& p, const Commitment& c, Spend what, std::int64_t own = 0) const;
    /// The gold reserved() keeps for the colony plan (colony_plan), which a colony's budget test
    /// counts as its own.
    [[nodiscard]] std::int64_t planned_colony_gold(const AiPlayer& p) const;
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
    /// The middle of an island, in world tiles.
    [[nodiscard]] static std::pair<std::int32_t, std::int32_t> island_point(const WorldIsland& isl) noexcept;
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
    /// With `whole`, from the whole census whatever the computer sees (only the log compares it).
    [[nodiscard]] std::uint32_t army_target(const AiPlayer& p, bool whole = false) const;
    /// Open1602 (rivals): the army a computer keeps whoever else is armed, by its inhabitants, and
    /// the share of the strongest army of another computer, at peace with it or not.
    [[nodiscard]] std::uint32_t rival_army_target(const AiPlayer& p, bool whole = false) const;
    /// Open1602 (feedback 16, K5): the soldiers the war lacks for its next city (Strategy::war_party)
    /// while that city still stands and is the target's; 0 otherwise.
    [[nodiscard]] std::uint32_t war_party_lacking(const AiPlayer& p) const;
    /// Whether a sees b at least as strong as itself: strength(a, a) against strength(a, b).
    [[nodiscard]] bool stronger(PlayerId a, PlayerId b) const;
    /// The original's computers and an improved one on the hardest level count every unit of
    /// another player; an improved one on the other levels only what a human in its place sees.
    [[nodiscard]] bool sees_all() const;
    /// The weighted army of `owner` as `viewer` sees it: its own whole, of another player with
    /// sees_all() the whole, else the soldiers on islands the viewer discovered (not those in holds
    /// or castles).
    [[nodiscard]] std::uint32_t seen_army(PlayerId viewer, PlayerId owner) const;
    /// Whether a ship of `figure` is of a warship type (also a package's), as its hull shows.
    [[nodiscard]] bool warship(std::uint32_t figure) const;
    /// The cannons of `owner`'s ships afloat as `viewer` sees them: mounted ones of its own or with
    /// sees_all(), else what it can tell of each ship's (guessed_cannons; a ship's window shows no
    /// cannons, its hull its type).
    [[nodiscard]] std::uint32_t seen_cannons(PlayerId viewer, PlayerId owner) const;
    /// Open1602 (G5/B): what fights at sea show of ships' cannons (State::sightings).
    void watch_fights();
    /// The cannons `viewer` takes a foreign ship `s` of type `t` to carry: what a fight showed,
    /// else its type's maximum times the share of their maximum the viewer's own ships of its class
    /// (warships or merchant ships) carry.
    [[nodiscard]] std::uint32_t guessed_cannons(const Ship& s, const assets::FigureType& t, std::uint32_t mounted,
                                                std::uint32_t most) const;
    /// The cannons on `player`'s own warships (`warships`) or merchant ships and their types' maximum.
    void own_cannons(PlayerId player, bool warships, std::uint32_t& mounted, std::uint32_t& most) const;
    /// A ship that can carry cannons: not a cart, of a type with places for them.
    [[nodiscard]] const assets::FigureType* armable(const Ship& s) const;
    [[nodiscard]] std::uint32_t soldiers_on_island(PlayerId owner, std::uint16_t island) const;
    std::vector<Soldier*> soldiers_near(PlayerId owner, const City& c, std::int32_t range, std::size_t max);
    [[nodiscard]] std::uint32_t soldiers_aboard(const Ship& s) const;
    /// Improved computers: a ship without a mission and with soldiers aboard takes them home.
    void troops_home(AiPlayer& p, ShipNote& n);
    [[nodiscard]] std::uint32_t free_holds(const Ship& s) const;
    bool queue_unit(AiPlayer& p, const CastleEntry& castle, std::uint32_t family);
    bool guard_post(AiPlayer& p, City& c, CityPlan& r, std::int32_t& x, std::int32_t& y, bool& at_yard);
    void guard(AiPlayer& p, City& c, std::int32_t x, std::int32_t y, std::uint32_t n, bool castle_island);
    bool market_tower(AiPlayer& p, City& c, std::int32_t mx, std::int32_t my);
    void recruit(AiPlayer& p);
    void buy_pirate_peace(AiPlayer& p);
    void ship_send(AiPlayer& p, Ship& s, const Target& t, std::uint8_t mission, std::uint32_t param);
    /// The city a mission's parameter names (island << 8 | city).
    [[nodiscard]] City* param_city(std::uint32_t param);
    void mission_end(AiPlayer& p, Ship& s, ShipNote& n);
    /// The soldiers board() orders aboard at `at`: those near its Kontor not headed into a building.
    [[nodiscard]] std::vector<std::uint32_t> boarders(const AiPlayer& p, const City& at);
    void board(AiPlayer& p, Ship& s, const City& at, std::uint32_t max);
    /// Open1602 (BD): sends ship `s` to boarding_water() for the soldiers board() would order at
    /// `home`, if any; whether it did.
    bool move_to_board(AiPlayer& p, Ship& s, ShipNote& n, const City& home);
    void missions(AiPlayer& p);
    void plan_landing(AiPlayer& p, Ship& s, ShipNote& n);
    /// Open1602: a warship (or the troop ship itself) shoots a gap into a walled coast.
    void breach(AiPlayer& p, Ship& s, ShipNote& n, const City& t);
    /// Open1602 (3b): a hostile tower over a landing, as covering_towers() sees it.
    struct TowerCover {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint32_t health = 0;  ///< what is left of the building
        std::uint32_t threat = 0;  ///< its fire, 1/32 HP per second
        std::uint64_t value = 0;   ///< its build value, gold
        bool city_rule = false;    ///< counted only as its city has no market or Kontor (F15), logged
    };
    /// Open1602: how many of `towers` count only by the no-market-or-Kontor rule, for the logs.
    [[nodiscard]] static std::size_t by_city_rule(const std::vector<TowerCover>& towers);
    /// Open1602: the straight way of cells from (cx, cy) to the market or Kontor of `c`.
    [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> way_to(const City& c, std::int32_t cx,
                                                                            std::int32_t cy) const;
    /// Open1602 (3b): the hostile towers whose reach covers the landing at `beach` or the way
    /// from it to the city, the nearest first.
    /// Open1602: covering_towers() for the way from cell (cx, cy); with `goals`, also the towers
    /// covering any standing market or Kontor of the city, which a party attacks all at once.
    [[nodiscard]] std::vector<TowerCover> towers_over_way(const AiPlayer& p, const City& c, std::int32_t cx,
                                                          std::int32_t cy, bool goals = false) const;
    [[nodiscard]] std::vector<TowerCover> covering_towers(const AiPlayer& p, const City& c,
                                                          std::pair<std::int32_t, std::int32_t> beach) const;
    /// Open1602 (3b): enough free warships shell `towers` while troop ship `s` waits, when that
    /// pays in gold; false when the troops land at once.
    /// Open1602 (3b): what shelling `towers` of `t` with `fleet` (the strongest first) would take.
    struct Shelling {
        std::vector<TowerCover> targets;  ///< those the ships reach from the water
        std::size_t ships = 0;            ///< the first ships of the fleet it takes; 0: none suffices
        std::uint64_t loss = 0;           ///< gold, the ships sunk
        std::uint64_t gain = 0;           ///< gold, the towers and the soldiers aboard `troops`
        const char* refusal = nullptr;    ///< why not, or none
    };
    /// The towers of `towers` that `plan` leaves standing: those out of the ships' reach.
    [[nodiscard]] static std::vector<TowerCover> unshelled(const std::vector<TowerCover>& towers, const Shelling& plan);
    [[nodiscard]] Shelling plan_shelling(const AiPlayer& p, const City& t, const std::vector<TowerCover>& towers,
                                         const std::vector<Ship*>& fleet, const Ship* troops) const;
    /// Open1602 (3b): shells `towers` while troop ship `s` waits, waits for a warship, or calls the
    /// landing off; always handles the landing.
    void shell_towers(AiPlayer& p, Ship& s, ShipNote& n, const City& t, const std::vector<TowerCover>& towers,
                      std::pair<std::int32_t, std::int32_t> beach);
    /// Open1602: how many of a party (red figure, health, 0 full) it takes to wreck `towers` of
    /// city `c` on land, against their fire and that of the hostile soldiers on the island: the
    /// party loses a unit to that fire every unit's health in turn, and fires till then, after its
    /// walk from cell `from` (the beach) through their reach. SIZE_MAX when the party does no harm.
    [[nodiscard]] std::size_t storm_needs(const AiPlayer& p, const City& c, const std::vector<TowerCover>& towers,
                                          const std::vector<std::pair<std::uint32_t, std::uint32_t>>& units,
                                          std::pair<std::int32_t, std::int32_t> from,
                                          std::size_t* plain = nullptr) const;
    /// Open1602: the half-tile cells (tile centres) of the soldiers' walk from cell `from` to the
    /// nearest tile within `reach8` of `f`; empty when no walk leads there.
    [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> walk_path(
        const AiPlayer& p, std::uint16_t island, std::pair<std::int32_t, std::int32_t> from, const Fighter& f,
        std::int32_t reach8) const;
    /// Open1602 (3b): whether ship `s` can shell the building at (x, y) of `island` from the open
    /// water nearest to it (water_near), within its Shotradius.
    [[nodiscard]] bool shells_from_water(const Ship& s, std::uint16_t island, std::uint32_t x, std::uint32_t y) const;
    /// Open1602: the healthy armed ships without a mission or troops, the strongest first.
    std::vector<Ship*> free_warships(AiPlayer& p);
    /// Open1602: the first of free_warships() (breach).
    Ship* free_warship(AiPlayer& p);
    Ship* best_armed_ship(AiPlayer& p, const Target& ref, std::uint32_t max_same);
    /// `convoy`: how many ships take the party (1 but where towers no fleet wrecks are stormed).
    bool attack_ok(AiPlayer& p, const City& t, std::size_t& convoy);
    void start_attack(AiPlayer& p, const City& t, std::size_t convoy);
    /// Open1602: the ships that take a party, as best_armed_ship picks one (takes_landing, well
    /// armed), the strongest first.
    [[nodiscard]] std::vector<Ship*> landing_ships(AiPlayer& p);
    /// Open1602: whether a ware a supply ship's route brings to a city of the computer's runs out
    /// there within kHomeFirstMinutes (supply_urgency), so that the ship stays on it.
    [[nodiscard]] bool supply_run_urgent(const AiPlayer& p, const Ship& s) const;
    /// Open1602: whether the soldiers aboard `s`, off an island of `owner`, are enough alone to
    /// storm the towers over their way to a city there (storm_needs).
    [[nodiscard]] bool storms_alone(const AiPlayer& p, const Ship& s, PlayerId owner);
    /// Open1602: the fewest of `ships`, the first first, whose holds take a party from `home` big
    /// enough to storm `towers` of `t` (storm_needs); `ships` 0 when none is, with the last needs
    /// and party size.
    struct Convoy {
        std::size_t ships = 0;
        std::size_t needs = SIZE_MAX;
        std::size_t has = 0;
        std::size_t plain = SIZE_MAX;  ///< `needs` without the walk under fire (logged)
    };
    /// Open1602 (feedback 16, K5): what attack_ok's convoy `cv` for city `t` lacks, kept as
    /// Strategy::war_party while `t` is of the war's target.
    void note_war_party(AiPlayer& p, const City& t, const Convoy& cv);
    [[nodiscard]] Convoy plan_convoy(const AiPlayer& p, const City& t, const City& home,
                                     const std::vector<TowerCover>& towers, const std::vector<Ship*>& ships,
                                     std::pair<std::int32_t, std::int32_t> from);
    void raid_outpost(AiPlayer& p, const City& t);
    void target_scan(AiPlayer& p);
    [[nodiscard]] std::vector<PlayerId> island_enemies(const AiPlayer& a, std::uint16_t island) const;
    void peacetime_orders(AiPlayer& a, std::uint16_t island);
    bool attack_orders(AiPlayer& a, std::uint16_t island, const std::vector<PlayerId>& enemies);
    /// Open1602: the island tiles the soldiers can walk to from where they stand (closed gates of
    /// others count as walls), one flag per tile of the island map.
    [[nodiscard]] std::vector<std::uint8_t> land_reach(PlayerId owner, std::uint16_t island,
                                                       const std::vector<std::uint32_t>& soldiers) const;
    /// Open1602: marks in `reach` the tiles `owner`'s soldiers can walk to from the tiles in `open`.
    void walk_flood(PlayerId owner, std::uint16_t island, std::vector<std::uint8_t>& reach,
                    std::vector<std::pair<std::int32_t, std::int32_t>> open) const;
    /// Open1602: the tiles of `c`'s island from which `p`'s soldiers can walk up to the city's
    /// buildings, or with `kontor` to its Kontor, one flag per tile.
    [[nodiscard]] std::vector<std::uint8_t> walk_reach_to(const AiPlayer& p, const City& c, bool kontor) const;
    /// Open1602 (F15): adds the towers of `hostile` to `goals`, the nearest to the middle of
    /// `soldiers` first.
    void tower_goals(std::uint16_t island, const City& hostile, const std::vector<std::uint32_t>& soldiers,
                     std::vector<std::pair<std::int32_t, std::int32_t>>& goals) const;
    /// Open1602: keeps the goals of an attack the soldiers can walk up to; with none of them, the
    /// first obstacle on the cheapest way to one (`way_in`). False, and `goals` as they were, when
    /// neither is found.
    bool reachable_goals(const AiPlayer& a, std::uint16_t island, const City& hostile,
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
    /// The army and the fleet's cannons of `owner` as `viewer` sees them, weighted as stronger() does.
    [[nodiscard]] std::uint32_t strength(PlayerId viewer, PlayerId owner) const;
    /// strength(), a human's raised on the harder levels (Balance::ai_human_threat_percent).
    [[nodiscard]] std::uint32_t threat_of(PlayerId viewer, PlayerId owner) const;
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
    /// cart_reach() of the last city asked, and its finder.
    mutable CartReach m_cart_reach;
    mutable PathFinder m_reach_finder;
    /// The step colony_plan last ran in, per player.
    std::vector<std::uint64_t> m_colony_plan_ms;
    /// The game minute of the last refusal logged, per player and kind of plan (only the log).
    mutable std::vector<std::array<std::uint64_t, kSpends>> m_refusals_logged;
    /// The game minute (+1) each player's ship last logged its wait for the founding's gold.
    std::vector<std::uint64_t> m_found_wait_logged;
    /// Why need_shipyard() builds no yard: the last reason logged per player (only the log).
    enum class YardWait : std::uint8_t {
        Unlogged,
        None,
        NoHome,
        Enough,
        Idle,
        Bought,
        Missing,
        Money,
        Pending,
        Waiting,
        Survey,
        Away,
        Elsewhere,
        Unsurveyed,
        Weaving,
        Attempts,
        Locked,
        Afford
    };
    mutable std::vector<YardWait> m_yard_logged;
    /// Player and city whose sealed beaches were logged (only the log).
    mutable std::vector<std::pair<PlayerId, std::uint32_t>> m_sealed_logged;
    /// Player and city attack_ok passes over for towers out of the fleet's reach (only the log).
    mutable std::vector<std::pair<PlayerId, std::uint32_t>> m_skips_logged;
    /// Per player: war_supplied's gold held back by the floor alone at the last look (only the log).
    std::vector<bool> m_floor_logged;
    /// Per player, the yard sites of the running search refused because the yard's carriers would
    /// reach no store (only the log).
    std::vector<std::uint32_t> m_yard_unreached;
    void log_yard(const AiPlayer& p, YardWait why) const;
    /// The castles way_out() keeps units in, as player, island, x, y (only the log: a castle is
    /// logged when its way out closes, not at every recruit pass).
    mutable std::vector<std::tuple<PlayerId, std::uint16_t, std::uint16_t, std::uint16_t>> m_castles_kept;
    /// The landing parties log_landing() follows (only the log).
    std::vector<LandingWatch> m_landings;
    /// Per city, the minutes of statistics log_supply() last wrote at (only the log).
    std::vector<std::uint32_t> m_supply_logged;
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
