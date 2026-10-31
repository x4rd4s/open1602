// SPDX-License-Identifier: GPL-3.0-or-later
// The goods statistics: the cities' ledgers, the early warning of a shortage and the sums of the
// window "Warenbilanz" (docs/architecture/0012-goods-statistics.md).

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/goods_ledger.hpp"
#include "o1602/sim/goods_statistics.hpp"
#include "o1602/sim/production_cycle.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using sim::GoodsFlow;
using sim::GoodsLedger;

namespace {

constexpr std::uint8_t kFood = 14;
constexpr std::uint8_t kCloth = 19;
constexpr std::uint32_t kTon = 32;

/// Food for everybody and cloth for the settlers (class 1).
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.6\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n  Nummer: 0\n  Id: 20101\n  Gfx: 0\nEndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world with one human player (0) and one computer player (1), a city each on island 0 and
/// one more city of the human on island 1.
sim::World make_world() {
    sim::World world(200, 200);
    for (std::uint16_t i = 0; i < 2; ++i) {
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, sim::IslandMap(20, 20), i * 50U, 0}).ok());
    }
    std::vector<sim::Player> players(2);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    world.set_players(std::move(players));
    std::vector<sim::City> cities(3);
    cities[0].island = 0;
    cities[0].index = 0;
    cities[0].owner = 0;
    cities[1].island = 0;
    cities[1].index = 1;
    cities[1].owner = 1;
    cities[2].island = 1;
    cities[2].index = 0;
    cities[2].owner = 0;
    for (sim::City& c : cities) {
        c.capacity = 30 * kTon;
        c.ensure_ware(kCloth);
    }
    world.set_cities(std::move(cities));
    return world;
}

sim::City& city(sim::World& world, std::uint16_t island, std::uint8_t index) {
    sim::City* c = world.city(island, index);
    REQUIRE(c != nullptr);
    return *c;
}

}  // namespace

TEST_CASE("goods statistics: a minute's bookings become a sample with the stock") {
    GoodsLedger l;
    l.add(kFood, GoodsFlow::Produced, 3 * kTon);
    l.add(kFood, GoodsFlow::Produced, kTon);
    l.add(kFood, GoodsFlow::Residents, 2 * kTon);
    l.add(kFood, GoodsFlow::Residents, 0);   // nothing booked, nothing changes
    CHECK(l.totals(kFood, 5).minutes == 0);  // the running minute does not count yet
    std::vector<std::uint32_t> stock(kFood + 1, 0);
    stock[kFood] = 10 * kTon;
    l.close_minute(stock);
    CHECK(l.minutes_closed() == 1);
    const sim::FlowTotals t = l.totals(kFood, 5);
    CHECK(t.minutes == 1);
    CHECK(t.of(GoodsFlow::Produced) == 4 * kTon);
    CHECK(t.in() == 4 * kTon);
    CHECK(t.out() == 2 * kTon);
    CHECK(t.net() == 2 * static_cast<std::int64_t>(kTon));
    const auto history = l.last_minutes(kFood);
    REQUIRE(history.size() == 1);
    CHECK(history[0].stock == 10 * kTon);
    CHECK(history[0].of(GoodsFlow::Residents) == 2 * kTon);
}

TEST_CASE("goods statistics: demand is not a movement") {
    sim::FlowTotals t;
    t.flow[static_cast<std::size_t>(GoodsFlow::Demand)] = 100;
    t.flow[static_cast<std::size_t>(GoodsFlow::Residents)] = 40;
    t.flow[static_cast<std::size_t>(GoodsFlow::Bought)] = 10;
    CHECK(t.in() == 10);
    CHECK(t.out() == 40);
    CHECK(!sim::is_inflow(GoodsFlow::Demand));
    CHECK(!sim::is_outflow(GoodsFlow::Demand));
}

TEST_CASE("goods statistics: the hour is kept by minutes, twelve hours by quarters") {
    GoodsLedger l;
    std::vector<std::uint32_t> stock(kCloth + 1, 0);
    for (std::uint32_t m = 0; m < 200; ++m) {
        l.add(kCloth, GoodsFlow::Produced, m);  // 0, 1, 2, ... units in minute m
        stock[kCloth] = m;
        l.close_minute(stock);
    }
    CHECK(l.minutes_closed() == 200);
    const auto minutes = l.last_minutes(kCloth);
    REQUIRE(minutes.size() == GoodsLedger::kMinutes);
    CHECK(minutes.front().of(GoodsFlow::Produced) == 140);
    CHECK(minutes.back().of(GoodsFlow::Produced) == 199);
    CHECK(minutes.back().stock == 199);
    // The last five minutes: 195 + ... + 199.
    CHECK(l.totals(kCloth, 5).of(GoodsFlow::Produced) == 985);
    CHECK(l.totals(kCloth, 60).minutes == 60);
    // Quarters: 13 complete ones (195 minutes); each holds the average minute.
    const auto quarters = l.last_quarters(kCloth);
    REQUIRE(quarters.size() == 13);
    CHECK(quarters.back().of(GoodsFlow::Produced) == (180 + 194) / 2);  // minutes 180..194
    CHECK(quarters.back().stock == 194);
    const sim::FlowTotals two_hours = l.totals(kCloth, 120);
    CHECK(two_hours.minutes == 120);
    CHECK(two_hours.of(GoodsFlow::Produced) == 15 * (187 + 172 + 157 + 142 + 127 + 112 + 97 + 82));
}

TEST_CASE("goods statistics: a sample stops at its largest value, the running count does not wrap") {
    GoodsLedger l;
    l.add(kFood, GoodsFlow::Bought, UINT32_MAX - 5);
    l.add(kFood, GoodsFlow::Bought, 100);
    l.close_minute({});
    CHECK(l.totals(kFood, 1).of(GoodsFlow::Bought) == GoodsLedger::kMaxSample);
}

TEST_CASE("goods statistics: the inhabitants' consumption and demand are booked") {
    const auto b = make_buildings();
    sim::City c;
    c.capacity = 30 * kTon;
    c.inhabitants = {0, 100};
    c.ensure_ware(kCloth);
    c.goods[kFood] = 900;
    c.goods[kCloth] = 900;
    for (int i = 0; i < 6; ++i) {
        sim::consume(c, b, nullptr);
    }
    c.ledger.close_minute(c.goods);
    const sim::FlowTotals food = c.ledger.totals(kFood, 1);
    CHECK(food.of(GoodsFlow::Residents) == 900 - c.goods[kFood]);
    CHECK(food.of(GoodsFlow::Residents) > 0);
    CHECK(food.of(GoodsFlow::Demand) >= food.of(GoodsFlow::Residents));
}

TEST_CASE("goods statistics: how long a stock lasts") {
    sim::FlowTotals t;
    t.minutes = 5;
    t.flow[static_cast<std::size_t>(GoodsFlow::Residents)] = 10 * kTon;  // 2 t a minute
    CHECK(sim::minutes_left(8 * kTon, t) == 4U);
    t.flow[static_cast<std::size_t>(GoodsFlow::Produced)] = 10 * kTon;
    CHECK(!sim::minutes_left(8 * kTon, t));  // it does not fall
}

TEST_CASE("goods statistics: what was withheld for a full warehouse counts as production") {
    sim::FlowTotals t;
    t.minutes = 5;
    t.flow[static_cast<std::size_t>(GoodsFlow::Produced)] = 4 * kTon;
    t.flow[static_cast<std::size_t>(GoodsFlow::Withheld)] = 6 * kTon;
    t.flow[static_cast<std::size_t>(GoodsFlow::Residents)] = 8 * kTon;
    // Not a movement: the stock changes by in minus out only.
    CHECK(!sim::is_inflow(GoodsFlow::Withheld));
    CHECK(!sim::is_outflow(GoodsFlow::Withheld));
    CHECK(t.net() == -4 * static_cast<std::int64_t>(kTon));
    CHECK(t.production() == 10 * kTon);
    CHECK(t.balance() == 2 * static_cast<std::int64_t>(kTon));
    CHECK(!sim::minutes_left(30 * kTon, t));  // a full store does not run out
    sim::GoodsSample g;
    g.flow[static_cast<std::size_t>(GoodsFlow::Produced)] = kTon;
    g.flow[static_cast<std::size_t>(GoodsFlow::Withheld)] = kTon;
    g.flow[static_cast<std::size_t>(GoodsFlow::Residents)] = 3 * kTon;
    CHECK(g.balance() == -static_cast<std::int64_t>(kTon));
}

namespace {

constexpr std::uint32_t kWeaver = 20503;
constexpr std::uint32_t kFisher = 20601;

/// A weaver (1 t wool for 1 t cloth in 20 s, store 4 t) and a fishery with a boat (Figurnr 5).
assets::BuildingTable make_producers() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Gfx: 0\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Maxnorohst: 5\n"
        "    Ware: 19\n    Rohstoff: 18\n    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20601\n  Gfx: 10\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 6\n    Maxlager: 4\n    Radius: 3\n    Figurnr: 5\n"
        "    Ware: 14\n    Rohstoff: 14\n    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 30\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("goods statistics: a producer idle only for a full warehouse books what it would make") {
    const auto buildings = make_producers();
    sim::World world = make_world();
    sim::IslandMap& map = world.island(0).map;
    REQUIRE(map.place(*buildings.by_id(kWeaver), 4, 4, 0, 0, 0, 0).ok());
    sim::Production weaver;
    weaver.island = 0;
    weaver.x = 4;
    weaver.y = 4;
    weaver.raw = 4 * kTon;
    weaver.stock = 4 * kTon;  // its own store is full
    world.productions().push_back(weaver);
    sim::City& c = city(world, 0, 0);
    c.goods[kCloth] = c.capacity;  // and so is the warehouse
    for (int s = 0; s < 200; ++s) {
        sim::advance_production(world, buildings, nullptr);
    }
    const sim::Production& p = world.productions().back();
    CHECK(p.rate == 0);
    CHECK(sim::waits_for_room(p, *buildings.by_id(kWeaver), c));
    c.ledger.close_minute(c.goods);
    sim::FlowTotals t = c.ledger.totals(kCloth, 1);
    CHECK(t.of(GoodsFlow::Produced) == 0);
    CHECK(t.of(GoodsFlow::Withheld) >= 9 * kTon);  // 1 t in 20 s, over about 200 s
    CHECK(t.of(GoodsFlow::Withheld) <= 11 * kTon);
    CHECK(t.of(GoodsFlow::Workshops) == 0);
    const sim::GoodsScope scope{0, std::nullopt};
    const std::vector<sim::GoodsProducers> producers = sim::goods_producers(world, buildings, scope, kCloth);
    REQUIRE(producers.size() == 1);
    CHECK(producers[0].waiting_for_room == 1);
    CHECK(producers[0].working == 0);

    // With room in the warehouse it only waits for a cart: nothing more is withheld.
    c.goods[kCloth] = 10 * kTon;
    CHECK(!sim::waits_for_room(world.productions().back(), *buildings.by_id(kWeaver), c));
    for (int s = 0; s < 100; ++s) {
        sim::advance_production(world, buildings, nullptr);
    }
    c.ledger.close_minute(c.goods);
    CHECK(c.ledger.totals(kCloth, 1).of(GoodsFlow::Withheld) == 0);
    // Room for less than a cart's ton counts as full; without raw material it would not work anyway.
    c.goods[kCloth] = c.capacity - kTon + 1;
    CHECK(sim::waits_for_room(world.productions().back(), *buildings.by_id(kWeaver), c));
    world.productions().back().raw = 0;
    CHECK(!sim::waits_for_room(world.productions().back(), *buildings.by_id(kWeaver), c));
    world.productions().back().raw = kTon / 2 - 1;  // less than half a cycle's
    CHECK(!sim::waits_for_room(world.productions().back(), *buildings.by_id(kWeaver), c));
}

TEST_CASE("goods statistics: a fishery at full speed counts the walks of its boat") {
    const auto buildings = make_producers();
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc =
        assets::cod::parse("Objekt: FIGUR\n  Nummer: 5\n  Gfx: 0\n  Speed: 400\n  Worktime: 2.0\nEndObj\n", types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());
    // 4 tiles out at 2 tiles a second, 2 s at work: 4 s per ton of fish before the 30 s of the cycle.
    CHECK(sim::full_speed_seconds(*buildings.by_id(kFisher), &figures.value()) == 34);
    CHECK(sim::full_speed_seconds(*buildings.by_id(kFisher), nullptr) == 30);
    CHECK(sim::full_speed_seconds(*buildings.by_id(kWeaver), &figures.value()) == 20);
}

TEST_CASE("goods statistics: the early warning comes once, before the store is empty") {
    const auto b = make_buildings();
    sim::World world = make_world();
    std::vector<sim::CityEvent> events;
    const auto minute = [&](std::uint32_t used) {
        for (std::size_t i = 0; i < world.cities().size(); ++i) {
            sim::City& c = city(world, world.cities()[i].island, world.cities()[i].index);
            const std::uint32_t taken = std::min(used, c.goods[kCloth]);
            c.goods[kCloth] -= taken;
            c.book(kCloth, GoodsFlow::Residents, taken);
        }
        sim::close_goods_minute(world, b, &events);
    };
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        city(world, world.cities()[i].island, world.cities()[i].index).goods[kCloth] = 20 * kTon;
    }
    minute(2 * kTon);  // too early for a warning (kWarnMinMinutes)
    CHECK(events.empty());
    for (int m = 0; m < 4; ++m) {
        minute(2 * kTon);  // 18, 16, 14, 12 t left at 2 t a minute: 6 minutes and more
    }
    CHECK(events.empty());
    minute(2 * kTon);             // 8 t: four minutes
    REQUIRE(events.size() == 2);  // both cities of the human, none of the computer
    CHECK(events[0].kind == sim::CityEvent::Kind::RunningLow);
    CHECK(events[0].ware == kCloth);
    CHECK(events[0].owner == 0);
    CHECK(events[0].minutes == 4);
    CHECK(city(world, 0, 0).ledger.warned(kCloth));
    CHECK(!city(world, 0, 1).ledger.warned(kCloth));
    events.clear();
    minute(2 * kTon);  // still short: no second warning
    CHECK(events.empty());
    // Supplies arrive: the warning is taken back and may come again.
    sim::City& c = city(world, 0, 0);
    c.goods[kCloth] += 25 * kTon;
    c.book(kCloth, GoodsFlow::ShippedIn, 25 * kTon);
    sim::close_goods_minute(world, b, &events);
    CHECK(!c.ledger.warned(kCloth));
}

TEST_CASE("goods statistics: the sums of a player's cities, all or one island's") {
    sim::World world = make_world();
    city(world, 0, 0).book(kCloth, GoodsFlow::Produced, 3 * kTon);
    city(world, 0, 1).book(kCloth, GoodsFlow::Produced, 50 * kTon);  // the computer's
    city(world, 1, 0).book(kCloth, GoodsFlow::Produced, 5 * kTon);
    city(world, 1, 0).book(kCloth, GoodsFlow::ShippedIn, kTon);
    city(world, 0, 0).goods[kCloth] = 7 * kTon;
    city(world, 1, 0).goods[kCloth] = 2 * kTon;
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        sim::City& c = city(world, world.cities()[i].island, world.cities()[i].index);
        c.ledger.close_minute(c.goods);
    }
    CHECK((sim::goods_islands(world, 0) == std::vector<std::uint16_t>{0, 1}));
    CHECK((sim::goods_islands(world, 1) == std::vector<std::uint16_t>{0}));
    const sim::GoodsScope all{0, std::nullopt};
    const sim::GoodsScope second{0, std::uint16_t{1}};
    CHECK(sim::goods_totals(world, all, kCloth, 5).of(GoodsFlow::Produced) == 8 * kTon);
    CHECK(sim::goods_totals(world, all, kCloth, 5).in() == 9 * kTon);
    CHECK(sim::goods_totals(world, second, kCloth, 5).of(GoodsFlow::Produced) == 5 * kTon);
    CHECK(sim::goods_stock(world, all, kCloth) == 9 * kTon);
    CHECK(sim::goods_capacity(world, second) == 30 * kTon);
    const auto history = sim::goods_history(world, all, kCloth, false);
    REQUIRE(history.size() == 1);
    CHECK(history[0].stock == 9 * kTon);
    CHECK(history[0].in() == 9 * kTon);
}

TEST_CASE("goods statistics: a city taken by another player starts afresh") {
    GoodsLedger l;
    l.add(kFood, GoodsFlow::Produced, kTon);
    l.close_minute({});
    l.set_warned(kFood, true);
    l.clear();
    CHECK(l.minutes_closed() == 0);
    CHECK(!l.used(kFood));
    CHECK(!l.warned(kFood));
}

TEST_CASE("goods statistics: a city's figures are kept by minute and by quarter") {
    sim::CityHistory h;
    for (std::int32_t m = 0; m < 75; ++m) {
        sim::CitySample s;
        s.set(sim::CityFigure::Taxes, m);
        s.set_class(1, 2 * m);
        h.close_minute(s);
    }
    const std::vector<sim::CitySample> minutes = h.last_minutes();
    REQUIRE(minutes.size() == sim::CityHistory::kMinutes);
    CHECK(minutes.front().of(sim::CityFigure::Taxes) == 15);
    CHECK(minutes.back().of(sim::CityFigure::Taxes) == 74);
    CHECK(minutes.back().of_class(1) == 148);
    const std::vector<sim::CitySample> quarters = h.last_quarters();
    REQUIRE(quarters.size() == 5);
    CHECK(quarters.front().of(sim::CityFigure::Taxes) == 7);  // 0..14 on average
    CHECK(quarters.back().of(sim::CityFigure::Taxes) == 67);  // 60..74
    sim::CitySample big;
    big.set(sim::CityFigure::Sales, std::int64_t{1} << 40);
    CHECK(big.of(sim::CityFigure::Sales) == INT32_MAX);
}
