// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ships at sea (docs/re/ships.md): sailing to a target along a planned course, trading at Kontors,
// trade routes, the free trader and shipyards.
//
// Like the original (0x00452370), a ship decides what to do only at the end of each run of one or
// two tiles, after a turn step or a wait: whether it has arrived, what it trades there, where it
// sails next, whether another ship lies ahead. The original makes these decisions only on the
// owner's computer and sends the course to the others; Open1602 makes them on every computer from
// the same state and the simulation's own random numbers (ADR 0004).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/rules.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Ships the pirates may have (Pirates_MaxShips 0x0040F310): 0 to 4, by the highest level a human
/// or computer player reached.
[[nodiscard]] std::uint32_t pirate_max_ships(const World& world) noexcept;

/// Room of one hold of a ship (0x00446A60).
inline constexpr std::uint32_t kHoldCapacity = 50 * 32;

/// Whether a world tile lies within reach of a building (0x004670D0): the integer circle of radius
/// 5 around its centre. Ships trade with a Kontor, are repaired by a shipyard from there.
[[nodiscard]] bool building_in_reach(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                                     std::uint32_t bx, std::uint32_t by, std::int32_t wx, std::int32_t wy);

/// The first city whose Kontor a ship at (wx, wy) can trade with (0x0047AF50), if any.
[[nodiscard]] std::optional<std::size_t> kontor_in_reach(const World& world, const assets::BuildingTable& buildings,
                                                         std::int32_t wx, std::int32_t wy);

/// Moving a ship.
struct ShipOrder {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;  ///< Ship::id
    Target target;           ///< none = stop
    /// Given in the military mode (or as an attack): an object of another player (not the free
    /// trader) means war with him (0x0044D540).
    bool attack = false;
    /// Open1602 (ADR 0010): on the way the ship fights every enemy it meets, then sails on.
    bool attack_move = false;
    /// Waits behind the orders the ship has (Shift + right click); a ship without an order (or on
    /// its trade route) takes it at once.
    bool queue = false;
};

/// Goods between a ship and the Kontor it lies at: the player's own (no money) or another's at the
/// Kontor's prices (with a trade treaty).
struct CargoTransfer {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    std::uint8_t ware = 0;
    bool to_ship = true;       ///< load / buy; otherwise unload / sell
    std::uint32_t amount = 0;  ///< 1/32 t; the original offers 1, 5, 10 and 50 t
    std::uint8_t hold = 0xFF;  ///< unloading from one hold, 0xFF = any
};

/// Editing the trade route of a ship.
struct RouteEdit {
    enum class Op : std::uint8_t {
        SetStop,    ///< stop `stop` sails to `target` (its entries are cleared)
        ClearStop,  ///< `stop` = 0xFF clears all
        SetEntry,   ///< entry `entry` of stop `stop`
        Start,      ///< switches the route on
        Stop,       ///< and off
        OneWay,     ///< `on`: the route ends after its last stop (computer players' supply runs)
    };
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    Op op = Op::SetStop;
    std::uint8_t stop = 0;
    std::uint8_t entry = 0;
    Target target{};
    RouteEntry value{};
    bool on = true;
};

/// Kontor trade settings of one ware (city ops 0x14/0x1C-0x22).
struct TradeSetting {
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    std::uint8_t ware = 0;
    enum class Mode : std::uint8_t { None, Sell, Buy };
    Mode mode = Mode::None;
    std::optional<std::uint16_t> price;   ///< none = the default for the mode
    std::optional<std::uint32_t> amount;  ///< keep (selling) or limit (buying); none = default
};

/// "Insel erkunden": a ship standing in the rectangle of an island explores its crops and
/// deposits (button 0x9487; docs/re/ships.md#erkunden).
struct ExploreOrder {
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
};

/// Further orders of the ship panel (docs/re/ships.md#ship-window): cannons, cargo overboard, the
/// name, offering the ship for sale and buying a ship another player offers.
struct ShipCommand {
    enum class Op : std::uint8_t {
        MountCannon,    ///< 1 t of cannons from a hold becomes a mounted cannon (op 0x19 +1)
        UnmountCannon,  ///< "Kanonenbestückung reduzieren": a mounted cannon back into a hold as 1 t
        Dump,           ///< "Waren über Bord werfen": the goods of `hold` (0xFF = all holds) are lost
        Rename,         ///< `name`, 1 to kMaxNameLength bytes (Windows-1252)
        OfferForSale,   ///< at Preis * `factor` / 128 (factor 0..384; op 0x20/0x21)
        WithdrawSale,
        Buy,  ///< `player` buys the ship at its price from its owner (0x84D)
        /// The white flag up (`on`: the ship stops and decides nothing more, never fires) or down
        /// (op 0x23, `Ship_SetHold 0x0044E980`).
        WhiteFlag,
        Scuttle,  ///< "Schiff versenken": it sinks without an attacker (op 0x0C, attacker 7)
        /// Back and forth between where it lies and the world tile `x`, `y`, fighting what it
        /// meets (two position stops and the route on, 0x00440BD0).
        Patrol,
        /// Open1602: stop and stay; fire at what comes in range, chase nothing (until the next order).
        Hold,
    };
    static constexpr std::size_t kMaxNameLength = 40;
    static constexpr std::uint16_t kMaxSaleFactor = 384;
    PlayerId player = kNoPlayer;
    std::uint32_t ship = 0;
    Op op = Op::MountCannon;
    std::uint8_t hold = 0xFF;  ///< MountCannon and Dump: the hold, 0xFF = the first fitting / all
    std::uint16_t factor = 128;
    std::string name;
    bool on = true;       ///< WhiteFlag
    std::uint16_t x = 0;  ///< Patrol: world tile
    std::uint16_t y = 0;
};

/// Whether and why a ship can explore an island now (the button of the ship panel).
enum class ExploreState : std::uint8_t {
    Possible,    ///< standing in an island's rectangle with something left to find
    Running,     ///< exploring now
    Done,        ///< the island is fully explored by the owner
    Moving,      ///< the ship is sailing or turning
    NoIsland,    ///< not inside the rectangle of an island
    NotAllowed,  ///< not the player's ship, or sinking
};

/// Orders to a shipyard (building ops 0x1A, 0x1B, 0x26) and to a ship to go there for repair.
struct ShipyardOrder {
    enum class Op : std::uint8_t {
        Build,  ///< `ship_type` 0..3: HANDEL1, HANDEL2, KRIEG1, KRIEG2
        Cancel,
        Enable,  ///< switch on or off with `on`
        Repair,  ///< ship `ship` (lying in reach) is repaired, or released with on = false
    };
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint16_t x = 0;  ///< origin of the shipyard
    std::uint16_t y = 0;
    Op op = Op::Build;
    std::uint8_t ship_type = 0;
    bool on = true;
    std::uint32_t ship = 0;
};

/// What happened to ships, for messages and the display.
struct ShipEvent {
    enum class Kind : std::uint8_t {
        Arrived,           ///< reached its target
        Traded,            ///< goods changed hands at a Kontor
        Refused,           ///< a transfer found nothing to move
        IslandDiscovered,  ///< `player` sees `island` for the first time
        NativesFound,      ///< MELDUNG 0x2F: a native village on it
        PiratesFound,      ///< 0x30: a pirate hideout on it
        RouteWarning,      ///< 0x5D: three rounds of a route with refused transfers
        GaveUp,            ///< found no way to its target
        TraderArrived,     ///< a free trader appeared on the map
        ShipLaunched,      ///< 6 / 11: a shipyard finished a ship
        ShipRepaired,      ///< 7
        FleetFull,         ///< 0x47: the player has too many ships for another order
        Attacked,          ///< 8, or 5 when `amount` is 1 (pirates)
        Sunk,              ///< 9: `player` lost the ship; `city` holds the attacker
        WhiteFlag,         ///< a computer's ship surrendered to the pirates
        Fired,             ///< a broadside; `ware` holds the bearing
        PirateAppeared,    ///< a new pirate ship at the edge of the map
        Plundered,         ///< pirates took a surrendered ship's goods (`amount` 1) or found nothing
        CropsExplored,     ///< 0x2A: the crops of `island` are explored (centre of the island in x, y)
        DepositFound,      ///< 0x2B iron ore / 0x2C gold (`ware`) at the mountain x, y of `island`
        ExploreFinished,   ///< the exploring of `island` has ended (for the progress display)
        OfferedForSale,    ///< 10: `player` offers the ship for `money`
        Sold,              ///< the ship went from `player` to the player in `city` for `money`
    };
    Kind kind = Kind::Arrived;
    std::uint32_t ship = 0;
    PlayerId player = kNoPlayer;
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    std::uint8_t ware = 0;
    std::int64_t amount = 0;
    std::int64_t money = 0;
    std::uint16_t x = 0;  ///< island tile for messages with a place
    std::uint16_t y = 0;
};

class Combat;

/// The holds a ship uses: as many as its type has (Maxware), the first of `ship.cargo`. Ships from
/// the original's files keep its eight slots; the ones beyond the type's stay empty. Everything
/// that puts goods or soldiers into a hold looks only at these.
[[nodiscard]] std::size_t usable_holds(const Ship& ship, const assets::FigureTable& figures);

class Fleet {
public:
    Fleet(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures, const Rules& rules);

    /// One tick of game time: every ship moves; at the end of a run it decides what to do.
    void tick(std::uint32_t dt_ms);
    /// Something was built or demolished on an island: its water may have changed.
    void island_changed(std::uint16_t island);

    bool order(const ShipOrder& order);
    /// Goods moved, or nothing (then a Refused event).
    std::uint32_t transfer(const CargoTransfer& transfer);
    bool edit_route(const RouteEdit& edit);
    bool set_trade(const TradeSetting& setting);
    bool shipyard(const ShipyardOrder& order);
    /// Starts exploring the island the ship stands in (ExploreState::Possible only).
    bool explore(const ExploreOrder& order);
    /// Cannons, cargo overboard, name, sale (ship_orders.cpp).
    bool command(const ShipCommand& command);
    /// What buying the ship costs (Preis * sale factor / 128).
    [[nodiscard]] std::int32_t sale_price(const Ship& ship) const;
    [[nodiscard]] ExploreState explore_state(const Ship& ship, PlayerId player) const;
    /// The island whose rectangle holds the ship's tile (0x0046A570), if any.
    [[nodiscard]] std::optional<std::uint16_t> island_at_ship(const Ship& ship) const;
    /// A shot hit ship `ship` (0x00445930): it loses health and sails slower, sinks at 0. A
    /// computer's ship hit by pirates surrenders.
    void hit(std::uint32_t ship, std::uint32_t damage, PlayerId attacker, bool by_pirates);
    /// The ship goes down: it turns to an odd heading, sinks for Stirbtime and is gone with its cargo.
    void sink(Ship& ship, PlayerId attacker);
    /// Who sees whom and target choice; without it ships do not fight.
    void set_combat(Combat* combat) noexcept { m_combat = combat; }
    /// Ships the AI steers itself (ComputerPlayers::directs): idle pirates of these choose nothing.
    void set_directed(std::function<bool(std::uint32_t)> directed) { m_directed = std::move(directed); }

    /// Ships and shipyards a player has or has ordered (the original's limit of 33 counts both).
    [[nodiscard]] std::uint32_t fleet_size(PlayerId player) const;
    [[nodiscard]] const SeaMap& sea() const noexcept { return m_sea; }
    /// Speed of a ship in tiles per second times 1000 (for the display).
    [[nodiscard]] std::uint32_t speed_milli(const Ship& ship) const;

    [[nodiscard]] std::vector<ShipEvent> take_events() {
        std::vector<ShipEvent> e;
        e.swap(m_events);
        return e;
    }

    /// Names for new ships (Text.cod [SHIPS]), handed out in turn.
    void set_ship_names(std::vector<std::string> names) { m_ship_names = std::move(names); }
    [[nodiscard]] const std::vector<std::string>& ship_names() const noexcept { return m_ship_names; }
    /// A pack donkey brought material to a shipyard (the logistics call this).
    void yard_delivery(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware, std::uint32_t amount);
    /// Asks the logistics for a pack donkey: shipyard (island, x, y) needs `ware`.
    struct FetchRequest {
        std::uint16_t island = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint8_t ware = 0;
    };
    [[nodiscard]] std::vector<FetchRequest> take_fetch_requests() {
        std::vector<FetchRequest> r;
        r.swap(m_fetch);
        return r;
    }

    /// State that is not in the world (clocks, the trader's tools), for Open1602's savegame.
    void save(core::ByteWriter& out) const;
    bool load(core::ByteReader& in);

private:
    struct Decision;
    void advance(Ship& ship, std::uint32_t budget_us);
    void decide(Ship& ship);
    /// Arrival at the target (unloading, trading, the stop of a route); how long the ship stays.
    std::optional<std::uint32_t> arrive_at_target(Ship& ship);
    /// The next target of a ship without an order: its route, the trader's or the pirates' choice.
    void choose_next_target(Ship& ship);
    [[nodiscard]] bool ship_ahead(const Ship& ship) const;
    void start_wait(Ship& ship, std::uint32_t ms);
    bool start_next_run(Ship& ship);
    bool plan(Ship& ship, std::int32_t radius);
    bool local_detour(Ship& ship);
    [[nodiscard]] bool at_target(const Ship& ship, const Target& target) const;
    [[nodiscard]] bool target_valid(const Target& target) const;
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> target_point(const Target& target) const;
    void update_island(Ship& ship);
    void explore_tick();
    void explore_step(Ship& ship);
    void stop_exploring(Ship& ship);
    void finish_order(Ship& ship);
    void route_next_stop(Ship& ship);
    bool route_city_stop(Ship& ship, const RouteStop& stop);
    bool route_ship_stop(Ship& ship, const RouteStop& stop);
    std::uint32_t move_goods(Ship& ship, City& city, std::uint8_t ware, bool to_ship, std::uint32_t amount,
                             std::uint8_t hold, bool limits, std::uint16_t price);
    [[nodiscard]] bool may_trade(PlayerId ship_owner, PlayerId city_owner) const;
    [[nodiscard]] const assets::FigureType* type_of(const Ship& ship) const;
    [[nodiscard]] std::size_t holds(const Ship& ship) const;
    [[nodiscard]] std::uint32_t space_for(const Ship& ship, std::uint8_t ware, std::uint8_t hold = 0xFF) const;
    [[nodiscard]] std::uint32_t run_us(const Ship& ship, std::uint8_t direction, std::uint8_t tiles) const;
    void add_cargo(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint16_t price);
    std::uint32_t remove_cargo(Ship& ship, std::uint8_t ware, std::uint32_t amount, std::uint8_t hold);

    // Naval combat (naval.cpp)
    bool naval_combat(Ship& ship);
    [[nodiscard]] std::optional<std::uint8_t> bearing_to(const Ship& ship, const Target& target) const;

    // Pirates (pirates.cpp)
    [[nodiscard]] PlayerId pirates() const;
    [[nodiscard]] std::vector<std::size_t> pirate_bases() const;
    void pirates_tick(std::uint32_t dt_ms);
    void pirates_step();
    void pirate_nest_sales(City& nest);
    void pirate_choose(Ship& ship);
    bool plunder(Ship& pirate, Ship& victim);

    // Free trader (trader.cpp)
    void trader_tick(std::uint32_t dt_ms);
    void trader_choose(Ship& ship);
    bool trader_trade(Ship& ship, City& city);
    void trader_restock(Ship& ship, std::uint32_t dt_ms);
    [[nodiscard]] std::uint16_t trader_max_price(std::uint8_t ware) const;
    [[nodiscard]] std::uint32_t trader_buy_amount(const Ship& ship, const City& city, std::uint8_t ware) const;
    [[nodiscard]] std::uint32_t trader_sell_amount(const Ship& ship, const City& city, std::uint8_t ware) const;
    void spawn_trader();

    // Shipyards (shipyards.cpp)
    void yards_tick(std::uint32_t dt_ms);
    void yard_cycle(CostBuilding& yard);
    bool launch(CostBuilding& yard);
    CostBuilding* yard_at(std::uint16_t island, std::uint16_t x, std::uint16_t y);
    std::string next_ship_name();

    World* m_world;
    Combat* m_combat = nullptr;
    std::function<bool(std::uint32_t)> m_directed;
    const assets::BuildingTable* m_buildings;
    const assets::FigureTable* m_figures;
    const Rules* m_rules;
    SeaMap m_sea;
    SeaPlanner m_planner;
    std::vector<ShipEvent> m_events;
    std::vector<std::uint32_t> m_ids;  ///< scratch of tick(): the ships in their order
    std::vector<FetchRequest> m_fetch;
    std::vector<std::string> m_ship_names;
    std::uint32_t m_name_index = 0;
    std::uint32_t m_trader_ms = 0;        ///< game time since the last check for a new free trader
    std::uint32_t m_yard_ms = 0;          ///< game time since the last shipyard second
    std::uint32_t m_pirate_ms = 0;        ///< game time since the pirates' last step
    std::uint32_t m_pirate_spawn_ms = 0;  ///< time until the next pirate ship may appear
    bool m_pirate_armed = false;          ///< the last new pirate ship has its cannons
    /// Tools the free trader got from nowhere (TIMERS +0x98) and the quota after which he gets
    /// only one ton at a time (+0x9C, 400 t).
    std::uint32_t m_tools_given = 0;
    std::uint32_t m_tools_quota = 400 * 32;
    /// Market statistics of the last full pass over the cities, per ware (0x0049AF28): demand and
    /// offers of all Kontors with the price sums.
    struct Market {
        std::uint64_t demand = 0;
        std::uint64_t demand_value = 0;
        std::uint64_t offer = 0;
        std::uint64_t offer_value = 0;
    };
    std::vector<Market> m_market;
};

}  // namespace o1602::sim
