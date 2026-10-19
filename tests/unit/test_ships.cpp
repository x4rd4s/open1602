// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint8_t kWood = 23;
constexpr std::uint8_t kTools = 22;
constexpr std::uint32_t kTon = 32;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // ground
        "  @Nummer: 1\n  Id: 20102\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"  // sea inside the island
        "  @Nummer: 2\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n"  // Kontor
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20104\n  Kind: 14\n  Gfx: 6\n  Size: 3, 3\n"  // small shipyard
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n    Radius: 12\n    Interval: 4\n    Prodmenge: 1\n"
        "    Rohstoff: 23\n    Rohmenge: 1\n    Workstoff: 19\n    Workmenge: 0.25\n    Maxnorohst: 5\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
        "  Nummer: 29\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 6\n  Speed: 480\n  Drehtime: 0.2\n"
        "  Maxenergy: 95\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Open sea of 120 x 60 tiles with an island of ground (columns 40..45, rows 20..29) that has a
/// water bay at its centre column and a Kontor of player 0 on its west side.
struct Sea {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{120, 60};
    sim::Rules rules = sim::Rules::open1602();

    explicit Sea(bool island = false) {
        std::vector<sim::Player> players(5);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[1].role = sim::PlayerRole::Computer;
        players[1].money = 10000;
        players[4].role = sim::PlayerRole::Trader;
        world.set_players(players);
        if (!island) {
            return;
        }
        sim::IslandMap map(6, 10);
        for (std::int32_t y = 0; y < 10; ++y) {
            for (std::int32_t x = 0; x < 6; ++x) {
                REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0).ok());
            }
        }
        REQUIRE(map.place(*buildings.by_id(20103), 0, 4, 0, 0, 0, 0).ok());  // Kontor at the west coast
        for (std::uint32_t y = 0; y < 10; ++y) {
            for (std::uint32_t x = 0; x < 6; ++x) {
                map.set_city(x, y, 0, 0);
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 40, 20}).ok());
        sim::City city;
        city.island = 0;
        city.index = 0;
        city.owner = 0;
        city.name = "Port";
        city.warehouse_x = 0;
        city.warehouse_y = 4;
        city.capacity = 30 * kTon;
        city.ensure_ware(24);
        city.trade.resize(25);
        world.add_city(city);
    }

    sim::Ship& add(std::uint32_t x, std::uint32_t y, std::uint16_t figure = 21, sim::PlayerId owner = 0,
                   std::uint8_t direction = 2) {
        sim::Ship s = sim::make_ship("Test", x, y, figure, owner, direction);
        s.cargo.resize(figure == 21 ? 4 : 6);
        world.add_ship(s);
        return world.ships().back();
    }
};

/// Runs the fleet for `ms` of game time in ticks of 50 ms; returns the events.
std::vector<sim::ShipEvent> run(sim::Fleet& fleet, sim::World& world, std::uint32_t ms) {
    std::vector<sim::ShipEvent> events;
    for (std::uint32_t t = 0; t < ms; t += 50) {
        world.set_time_ms(world.time_ms() + 50);
        fleet.tick(50);
        for (const auto& e : fleet.take_events()) {
            events.push_back(e);
        }
    }
    return events;
}

std::size_t count(const std::vector<sim::ShipEvent>& events, sim::ShipEvent::Kind kind) {
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [&](const sim::ShipEvent& e) { return e.kind == kind; }));
}

}  // namespace

TEST_CASE("ships: a course over open sea runs in steps of one or two tiles") {
    Sea sea;
    sim::SeaMap map;
    map.rebuild(sea.world, sea.buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 10;
    plan.start_y = 10;
    plan.aim_x = 20;
    plan.aim_y = 13;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 20 && y == 13; };
    const sim::SeaCourse course = planner.plan(map, plan);
    REQUIRE(course.reached_goal);
    std::int32_t x = 10;
    std::int32_t y = 10;
    for (const std::uint8_t code : course.path) {
        const auto tiles = static_cast<std::int32_t>(code & 0x0FU);
        CHECK(tiles >= 1);
        CHECK(tiles <= 2);
        x += sim::step_x(static_cast<std::uint8_t>(code >> 4U)) * tiles;
        y += sim::step_y(static_cast<std::uint8_t>(code >> 4U)) * tiles;
    }
    CHECK(x == 20);
    CHECK(y == 13);
}

TEST_CASE("ships: water near hostile towers is avoided when a way round costs less") {
    Sea sea;
    sim::SeaMap map;
    map.rebuild(sea.world, sea.buildings);
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 10;
    plan.start_y = 30;
    plan.aim_x = 60;
    plan.aim_y = 30;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 60 && y == 30; };
    const auto passes = [](const sim::SeaCourse& course, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                           std::int32_t y1) {
        std::int32_t x = 10;
        std::int32_t y = 30;
        bool inside = false;
        for (const std::uint8_t code : course.path) {
            for (std::uint32_t i = 0; i < (code & 0x0FU); ++i) {
                x += sim::step_x(static_cast<std::uint8_t>(code >> 4U));
                y += sim::step_y(static_cast<std::uint8_t>(code >> 4U));
                inside = inside || (x >= x0 && x <= x1 && y >= y0 && y <= y1);
            }
        }
        return inside;
    };
    // Straight through without danger.
    sim::SeaCourse course = planner.plan(map, plan);
    REQUIRE(course.reached_goal);
    CHECK(passes(course, 30, 25, 40, 35));
    // A small square of danger: the course goes round it.
    plan.danger.push_back(sim::SeaPlan::Danger{30, 25, 40, 35});
    course = planner.plan(map, plan);
    REQUIRE(course.reached_goal);
    CHECK(!passes(course, 30, 25, 40, 35));
    // A wall of danger across the whole window: there is no way round, so the ship sails through.
    plan.danger = {sim::SeaPlan::Danger{30, -100, 33, 200}};
    course = planner.plan(map, plan);
    REQUIRE(course.reached_goal);
    CHECK(passes(course, 30, -100, 33, 200));
}

TEST_CASE("ships: courses go round islands without cutting their corners, also beyond the window") {
    Sea sea(true);
    sim::SeaMap map;
    map.rebuild(sea.world, sea.buildings);
    CHECK(!map.sailable(42, 25));  // ground
    CHECK(map.sailable(39, 25));
    CHECK(!map.sailable(-1, 5));  // outside the world: blocked (S-070)
    sim::SeaPlanner planner;
    sim::SeaPlan plan;
    plan.start_x = 30;
    plan.start_y = 25;
    plan.radius = 12;  // the goal lies outside: the course is a leg to the edge of the window
    plan.aim_x = 60;
    plan.aim_y = 25;
    plan.goal = [](std::int32_t x, std::int32_t y) { return x == 60 && y == 25; };
    sim::SeaCourse course = planner.plan(map, plan);
    REQUIRE(!course.path.empty());
    CHECK(!course.reached_goal);
    CHECK(course.end_x == 42);  // the window's edge, as close to the aim as the sea allows
    // Then the full way: around the island, never over land, never between two land tiles.
    plan.radius = 80;
    course = planner.plan(map, plan);
    REQUIRE(course.reached_goal);
    std::int32_t x = 30;
    std::int32_t y = 25;
    for (const std::uint8_t code : course.path) {
        const auto dir = static_cast<std::uint8_t>(code >> 4U);
        for (std::uint32_t i = 0; i < (code & 0x0FU); ++i) {
            const std::int32_t nx = x + sim::step_x(dir);
            const std::int32_t ny = y + sim::step_y(dir);
            CHECK(map.sailable(nx, ny));
            if ((dir & 1U) != 0) {
                CHECK(map.sailable(nx, y));
                CHECK(map.sailable(x, ny));
            }
            x = nx;
            y = ny;
        }
    }
    CHECK(x == 60);
    CHECK(y == 25);
}

TEST_CASE("ships: a ship sails at Speed / 200 tiles per second and turns in place") {
    Sea sea;
    sim::Ship& ship = sea.add(10, 10);
    const std::uint32_t id = ship.id;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    CHECK(sea.world.ship(id)->health == 50 * kTon);  // full health for a ship without one
    CHECK(fleet.speed_milli(*sea.world.ship(id)) == 2500);
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(20, 10)}));
    // Ten tiles at 2.5 tiles per second: 4 s.
    auto events = run(fleet, sea.world, 3500);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 0);
    CHECK(sea.world.ship(id)->x == 19);  // 10.5 + 3.5 s * 2.5 tiles/s = 19.25
    events = run(fleet, sea.world, 700);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 1);
    CHECK(sea.world.ship(id)->x == 20);
    CHECK(sea.world.ship(id)->pos_x == 20 * sim::kShipUnit + sim::kShipUnit / 2);

    // Facing east, sailing north-west: three 45 degree steps take two turn times.
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(14, 4)}));
    run(fleet, sea.world, 250);  // the decision at the end of the wait and the first step
    CHECK(sea.world.ship(id)->direction != 2);
    run(fleet, sea.world, 6000);
    CHECK(sea.world.ship(id)->direction == 7);
    CHECK(sea.world.ship(id)->x == 14);
    CHECK(sea.world.ship(id)->y == 4);
}

TEST_CASE("ships: damage and a full hold slow a ship down") {
    Sea sea;
    sim::Ship& ship = sea.add(10, 10);
    for (sim::CargoSlot& c : ship.cargo) {
        c = sim::CargoSlot{kWood, sim::kHoldCapacity, 0};
    }
    const std::uint32_t id = ship.id;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    CHECK(fleet.speed_milli(*sea.world.ship(id)) == 1250);  // half speed
    sea.world.ship(id)->health = 25 * kTon;                 // half health on top: a third
    CHECK(fleet.speed_milli(*sea.world.ship(id)) == 833);
}

TEST_CASE("ships: an order onto land ends at the coast instead of being given up") {
    Sea sea(true);
    sim::Ship& ship = sea.add(30, 25);
    const std::uint32_t id = ship.id;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    REQUIRE(fleet.order(sim::ShipOrder{0, id, sim::Target::position(42, 25)}));
    const auto events = run(fleet, sea.world, 20000);
    CHECK(count(events, sim::ShipEvent::Kind::GaveUp) == 0);
    CHECK(count(events, sim::ShipEvent::Kind::Arrived) == 1);
    const sim::Ship* s = sea.world.ship(id);
    CHECK(s->x >= 37);
    CHECK(s->x < 40);
    CHECK(s->target.empty());
}

TEST_CASE("ships: loading and unloading at the own Kontor in whole tons, nothing lost") {
    Sea sea(true);
    sim::Ship& ship = sea.add(38, 25);
    const std::uint32_t id = ship.id;
    sim::City& city = *sea.world.city(0, 0);
    city.goods[kWood] = 250 * kTon + 10;
    city.goods[kTools] = 20 * kTon;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    REQUIRE(sim::building_in_reach(sea.world, sea.buildings, 0, 0, 4, 38, 25));
    CHECK(!sim::building_in_reach(sea.world, sea.buildings, 0, 0, 4, 33, 25));
    // Four holds of 50 t: 200 t of wood fit, the rest stays in the Kontor.
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, true, 250 * kTon}) == 200 * kTon);
    CHECK(city.goods[kWood] == 50 * kTon + 10);
    CHECK(sea.world.ship(id)->load(kWood) == 200 * kTon);
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kTools, true, 10 * kTon}) == 0);  // no room
    // Unloading: only what the warehouse has room for (30 t per ware).
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, false, 50 * kTon}) == 0);  // already over
    city.goods[kWood] = 5 * kTon;
    CHECK(fleet.transfer(sim::CargoTransfer{0, id, kWood, false, 50 * kTon}) == 25 * kTon);
    CHECK(city.goods[kWood] == 30 * kTon);
    CHECK(sea.world.ship(id)->load(kWood) == 175 * kTon);
    // Somebody else's ship may not use this Kontor without a treaty.
    sim::Ship& other = sea.add(38, 26, 21, 0);
    other.owner = 2;
    CHECK(fleet.transfer(sim::CargoTransfer{2, other.id, kWood, true, 10 * kTon}) == 0);
}

TEST_CASE("ships: a trade route loads where it should and delivers at the next stop") {
    Sea sea(true);
    sim::City second = *sea.world.city(0, 0);
    // A second city of the same player on another island far east.
    sim::IslandMap map(4, 4);
    for (std::int32_t y = 0; y < 4; ++y) {
        for (std::int32_t x = 0; x < 4; ++x) {
            REQUIRE(map.place(*sea.buildings.by_id(20101), x, y, 0, 0).ok());
        }
    }
    REQUIRE(map.place(*sea.buildings.by_id(20103), 0, 1, 0, 0, 0, 0).ok());
    REQUIRE(sea.world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 90, 30}).ok());
    second.island = 1;
    second.warehouse_x = 0;
    second.warehouse_y = 1;
    second.goods.assign(25, 0);
    sea.world.add_city(second);
    sea.world.city(0, 0)->goods[kWood] = 30 * kTon;
    sim::Ship& ship = sea.add(30, 25);
    const std::uint32_t id = ship.id;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    using Op = sim::RouteEdit::Op;
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::SetStop, 0, 0, sim::Target::city_of(0, 0)}));
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::SetEntry, 0, 0, {}, sim::RouteEntry{kWood, false, 20 * kTon}}));
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::SetStop, 1, 0, sim::Target::city_of(1, 0)}));
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::SetEntry, 1, 0, {}, sim::RouteEntry{kWood, true, 50 * kTon}}));
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::Start}));
    // Goods only, and no other player's ship as a stop: goods between ships move without payment.
    CHECK(!fleet.edit_route(sim::RouteEdit{0, id, Op::SetEntry, 2, 0, {}, sim::RouteEntry{25, false, kTon}}));
    sim::Ship& stranger = sea.add(40, 25);
    stranger.owner = 1;
    const std::uint32_t stranger_id = stranger.id;
    CHECK(!fleet.edit_route(sim::RouteEdit{0, id, Op::SetStop, 2, 0, sim::Target::other_ship(stranger_id)}));
    sea.world.ship(stranger_id)->owner = 0;
    CHECK(fleet.edit_route(sim::RouteEdit{0, id, Op::SetStop, 2, 0, sim::Target::other_ship(stranger_id)}));
    REQUIRE(fleet.edit_route(sim::RouteEdit{0, id, Op::ClearStop, 2}));
    auto events = run(fleet, sea.world, 60000);
    CHECK(count(events, sim::ShipEvent::Kind::RouteWarning) == 0);
    // At the first stop it filled up to 20 t; at the second it delivered all of it.
    CHECK(sea.world.city(1, 0)->goods[kWood] >= 20 * kTon);
    CHECK(sea.world.city(0, 0)->goods[kWood] <= 10 * kTon);
    CHECK(sea.world.ship(id)->route.active);
}

TEST_CASE("ships: the free trader buys cheap goods and comes back with tools") {
    Sea sea(true);
    sim::City& city = *sea.world.city(0, 0);
    city.goods[kWood] = 30 * kTon;
    city.trade[kWood] = sim::City::Trade{true, false, 10, 0, 5 * kTon, 0};     // sells wood at 10, keeps 5 t
    city.trade[kTools] = sim::City::Trade{false, true, 0, 120, 0, 20 * kTon};  // buys tools at 120
    // The only Kontor: after his visit he has nowhere else to go, so his goods do not age further.
    sim::Ship& trader = sea.add(30, 25, 29, 4);
    trader.trader = true;
    trader.trader_goods.resize(25);
    trader.trader_goods[kTools] = sim::TraderGood{70, 0, 4 * kTon};
    const std::uint32_t id = trader.id;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    const std::int32_t money = sea.world.players()[0].money;
    const auto events = run(fleet, sea.world, 30000);
    CHECK(count(events, sim::ShipEvent::Kind::Traded) >= 2);
    // He took 10 t of wood (at most 10 t a visit) and sold 4 t of tools.
    CHECK(sea.world.ship(id)->trader_goods[kWood].amount == 10 * kTon);
    CHECK(sea.world.ship(id)->trader_goods[kTools].amount == 0);
    CHECK(city.goods[kWood] == 20 * kTon);
    CHECK(city.goods[kTools] == 4 * kTon);
    CHECK(sea.world.players()[0].money == money + 10 * 10 - 4 * 120);
    CHECK(sea.world.players()[4].money == 0);  // his own money never changes
}

TEST_CASE("ships: a shipyard builds a ship from wood and cloth and repairs one lying in reach") {
    Sea sea(true);
    // The small shipyard on the east coast of the island (tiles 3..5, 0..2).
    sim::WorldIsland& isl = sea.world.island(0);
    REQUIRE(isl.map.place(*sea.buildings.by_id(20104), 3, 0, 0, 0, 0, 0).ok());
    sim::CostBuilding yard{sim::CostBuilding::Kind::Shipyard, 0, 3, 0, false};
    sea.world.add_cost_building(yard);
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    using Op = sim::ShipyardOrder::Op;
    // The small yard builds no big ships (S-005); the small trade ship it does.
    CHECK(!fleet.shipyard(sim::ShipyardOrder{0, 0, 3, 0, Op::Build, 1}));
    REQUIRE(fleet.shipyard(sim::ShipyardOrder{0, 0, 3, 0, Op::Build, 0}));
    CHECK(sea.world.cost_buildings()[0].enabled);  // an order switches the yard on
    CHECK(fleet.fleet_size(0) == 1);
    // The pack donkey would bring the material; here it comes straight away, as much as needed.
    std::size_t asked = 0;
    for (int second = 0; second < 140 && sea.world.ships().empty(); ++second) {
        run(fleet, sea.world, 1000);
        for (const auto& r : fleet.take_fetch_requests()) {
            ++asked;
            fleet.yard_delivery(r.island, r.x, r.y, r.ware, 6 * kTon);
        }
    }
    CHECK(asked > 0);
    REQUIRE(sea.world.ships().size() == 1);
    const sim::Ship& ship = sea.world.ships().front();
    CHECK(ship.figure == 21);
    CHECK(ship.owner == 0);
    CHECK(sea.world.cost_buildings()[0].order == sim::CostBuilding::kNoOrder);
    CHECK(!sea.world.cost_buildings()[0].enabled);  // done: switched off again
    CHECK(fleet.sea().sailable(ship.x, ship.y));
    CHECK(sea.world.ship(ship.id)->health == 50 * kTon);

    // Repair: 1.5 * Maxenergy / Rohmenge = 3 health per full cycle of 4 s.
    const std::uint32_t id = ship.id;
    sea.world.ship(id)->health = 20 * kTon;
    if (!sim::building_in_reach(sea.world, sea.buildings, 0, 3, 0, ship.x, ship.y)) {
        sea.world.ship(id)->place(47, 1);
    }
    // Not at another player's yard: it would pay with its own material.
    sea.world.ship(id)->owner = 1;
    CHECK(!fleet.shipyard(sim::ShipyardOrder{1, 0, 3, 0, Op::Repair, 0, true, id}));
    sea.world.ship(id)->owner = 0;
    REQUIRE(fleet.shipyard(sim::ShipyardOrder{0, 0, 3, 0, Op::Repair, 0, true, id}));
    std::size_t repaired = 0;
    for (int second = 0; second < 60; ++second) {
        for (const auto& e : run(fleet, sea.world, 1000)) {
            repaired += e.kind == sim::ShipEvent::Kind::ShipRepaired ? 1U : 0U;
        }
        for (const auto& r : fleet.take_fetch_requests()) {
            fleet.yard_delivery(r.island, r.x, r.y, r.ware, 6 * kTon);
        }
        if (second == 20) {
            // About five cycles: 15 more.
            CHECK(sea.world.ship(id)->health >= 32 * kTon);
            CHECK(sea.world.ship(id)->health <= 38 * kTon);
            CHECK(sea.world.ship(id)->repairing);
        }
    }
    CHECK(sea.world.ship(id)->health == 50 * kTon);
    CHECK(!sea.world.ship(id)->repairing);
    CHECK(repaired == 1);
}

sim::ShipCommand cmd(sim::PlayerId player, std::uint32_t ship, sim::ShipCommand::Op op, std::uint8_t hold = 0xFF) {
    sim::ShipCommand c;
    c.player = player;
    c.ship = ship;
    c.op = op;
    c.hold = hold;
    return c;
}

TEST_CASE("ships: cannons go on and off board, goods overboard, a new name") {
    Sea sea;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    sim::Ship& ship = sea.add(10, 10);
    const std::uint32_t id = ship.id;
    ship.cargo[0] = sim::CargoSlot{13, 7 * kTon + 5, 100};  // 7 t of cannons and a bit
    ship.cargo[1] = sim::CargoSlot{kWood, 20 * kTon, 0};
    using Op = sim::ShipCommand::Op;
    // Six cannons at most (Maxkanon 6); each takes a ton from the hold.
    for (int i = 0; i < 6; ++i) {
        CHECK(fleet.command(cmd(0, id, Op::MountCannon)));
    }
    CHECK(!fleet.command(cmd(0, id, Op::MountCannon)));
    CHECK(sea.world.ship(id)->cannons == 6);
    CHECK(sea.world.ship(id)->cargo[0].amount == kTon + 5);
    // Not someone else's ship.
    CHECK(!fleet.command(cmd(1, id, Op::UnmountCannon)));
    CHECK(fleet.command(cmd(0, id, Op::UnmountCannon)));
    CHECK(sea.world.ship(id)->cannons == 5);
    CHECK(sea.world.ship(id)->cargo[0].amount == 2 * kTon + 5);
    // Overboard: one hold, then everything.
    CHECK(fleet.command(cmd(0, id, Op::Dump, 1)));
    CHECK(sea.world.ship(id)->cargo[1].ware == 0);
    CHECK(fleet.command(cmd(0, id, Op::Dump)));
    CHECK(sea.world.ship(id)->cargo[0].ware == 0);
    CHECK(!fleet.command(cmd(0, id, Op::Dump)));  // nothing left
    // Without cannons in a hold none can be mounted; taking one off needs a free hold.
    CHECK(!fleet.command(cmd(0, id, Op::MountCannon)));
    for (auto& slot : sea.world.ship(id)->cargo) {
        slot = sim::CargoSlot{kWood, kTon, 0};
    }
    CHECK(!fleet.command(cmd(0, id, Op::UnmountCannon)));
    sim::ShipCommand rename = cmd(0, id, Op::Rename);
    rename.name = "Seeadler";
    CHECK(fleet.command(rename));
    CHECK(sea.world.ship(id)->name == "Seeadler");
    rename.name.clear();
    CHECK(!fleet.command(rename));
}

TEST_CASE("ships: a ship offered for sale changes hands for its price") {
    Sea sea;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    sim::Ship& ship = sea.add(10, 10);
    const std::uint32_t id = ship.id;
    using Op = sim::ShipCommand::Op;
    // Not for sale yet; then at three quarters of its price (Preis 1000).
    CHECK(!fleet.command(cmd(1, id, Op::Buy)));
    sim::ShipCommand offer = cmd(0, id, Op::OfferForSale);
    offer.factor = 96;
    CHECK(fleet.command(offer));
    CHECK(fleet.sale_price(*sea.world.ship(id)) == 750);
    // Too poor to buy it.
    sea.world.player(1)->money = 700;
    CHECK(!fleet.command(cmd(1, id, Op::Buy)));
    sea.world.player(1)->money = 10000;
    // Not with soldiers on board.
    sea.world.ship(id)->cargo[0] = sim::CargoSlot{25, kTon, 0};
    CHECK(!fleet.command(cmd(1, id, Op::Buy)));
    sea.world.ship(id)->cargo[0] = sim::CargoSlot{};
    // The owner cannot buy his own ship; another player can.
    CHECK(!fleet.command(cmd(0, id, Op::Buy)));
    const std::int32_t seller = sea.world.player(0)->money;
    CHECK(fleet.command(cmd(1, id, Op::Buy)));
    CHECK(sea.world.ship(id)->owner == 1);
    CHECK(!sea.world.ship(id)->for_sale);
    CHECK(sea.world.player(1)->money == 10000 - 750);
    CHECK(sea.world.player(0)->money == seller + 750);
    const auto events = fleet.take_events();
    CHECK(std::any_of(events.begin(), events.end(),
                      [](const sim::ShipEvent& e) { return e.kind == sim::ShipEvent::Kind::Sold && e.money == 750; }));
    // Withdrawing an offer.
    CHECK(fleet.command(cmd(1, id, Op::OfferForSale)));
    CHECK(fleet.command(cmd(1, id, Op::WithdrawSale)));
    CHECK(!fleet.command(cmd(0, id, Op::Buy)));
}

TEST_CASE("ships: white flag, scuttling and patrols") {
    Sea sea;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    const std::uint32_t id = sea.add(10, 10).id;
    using Op = sim::ShipCommand::Op;
    // The white flag stops the ship and holds it; only the owner raises it.
    CHECK(fleet.order(sim::ShipOrder{0, id, sim::Target::position(30, 10)}));
    CHECK(!fleet.command(cmd(1, id, Op::WhiteFlag)));
    CHECK(fleet.command(cmd(0, id, Op::WhiteFlag)));
    CHECK(sea.world.ship(id)->held);
    CHECK(sea.world.ship(id)->target.empty());
    CHECK(count(fleet.take_events(), sim::ShipEvent::Kind::WhiteFlag) == 1);
    sim::ShipCommand lower = cmd(0, id, Op::WhiteFlag);
    lower.on = false;
    CHECK(fleet.command(lower));
    CHECK(!sea.world.ship(id)->held);
    // A patrol: from here to the point and back, the route on.
    sim::ShipCommand patrol = cmd(0, id, Op::Patrol);
    patrol.x = 20;
    patrol.y = 10;
    CHECK(fleet.command(patrol));
    const sim::Ship& s = *sea.world.ship(id);
    CHECK(s.route.patrol());
    CHECK(s.route.stops[0].target == sim::Target::position(10, 10));
    CHECK(s.route.stops[1].target == sim::Target::position(20, 10));
    run(fleet, sea.world, 20000);
    CHECK(sea.world.ship(id)->x > 10);
    patrol.x = 60000;  // outside the world
    CHECK(!fleet.command(patrol));
    // Scuttled: it sinks and is gone after the sinking time.
    CHECK(!fleet.command(cmd(1, id, Op::Scuttle)));
    CHECK(fleet.command(cmd(0, id, Op::Scuttle)));
    CHECK(sea.world.ship(id)->sinking);
    CHECK(!sea.world.ship(id)->route.active);
    run(fleet, sea.world, 20000);
    CHECK(sea.world.ship(id) == nullptr);
}

TEST_CASE("ships: a Kontor trades in no more wares than its Maxware allows") {
    Sea sea(true);
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    // The test Kontor has no Maxware: no limit.
    for (std::uint8_t w = 2; w <= 10; ++w) {
        CHECK(fleet.set_trade(sim::TradeSetting{0, 0, 0, w, sim::TradeSetting::Mode::Sell, {}, {}}));
    }
    // A Kontor I (3 to sell, 4 to buy).
    assets::BuildingType kontor = *sea.buildings.by_id(20103);
    kontor.max_wares = 3;
    kontor.max_wares2 = 4;
    sim::City& city = *sea.world.city(0, 0);
    CHECK(sim::trim_trade_settings(city, kontor));  // nine wares on sale: all off
    CHECK(std::none_of(city.trade.begin(), city.trade.end(), [](const sim::City::Trade& t) { return t.sell; }));
    for (std::uint8_t w = 2; w <= 4; ++w) {
        city.trade[w].sell = true;
    }
    CHECK(!sim::trim_trade_settings(city, kontor));  // three are allowed
    CHECK(city.trade[2].sell);
}

TEST_CASE("ships: queued orders are sailed one after another; any other order clears them") {
    Sea sea;
    sim::Fleet fleet(sea.world, sea.buildings, sea.figures, sea.rules);
    const std::uint32_t id = sea.add(10, 10).id;
    CHECK(fleet.order(sim::ShipOrder{0, id, sim::Target::position(16, 10)}));
    sim::ShipOrder next{0, id, sim::Target::position(16, 16)};
    next.queue = true;
    CHECK(fleet.order(next));
    next.target = sim::Target::position(10, 16);
    CHECK(fleet.order(next));
    CHECK(sea.world.ship(id)->queue.size() == 2);
    run(fleet, sea.world, 60000);
    const sim::Ship& s = *sea.world.ship(id);
    CHECK(s.queue.empty());
    CHECK(s.x == 10);
    CHECK(s.y == 16);
    // A queued order to an idle ship is taken at once; a plain one clears the queue.
    CHECK(fleet.order(next));
    CHECK(sea.world.ship(id)->queue.empty());
    next.target = sim::Target::position(20, 20);
    CHECK(fleet.order(next));
    CHECK(sea.world.ship(id)->queue.size() == 1);
    CHECK(fleet.order(sim::ShipOrder{0, id, sim::Target::position(12, 12)}));
    CHECK(sea.world.ship(id)->queue.empty());
}
