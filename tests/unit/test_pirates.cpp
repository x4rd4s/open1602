// SPDX-License-Identifier: GPL-3.0-or-later
// Pirates and natives (docs/re/pirates.md).

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kTon = 32;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20103\n  Kind: 35\n  Gfx: 2\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxware: 6\n  EndObj\n"  // Kontor
        "  @Nummer: 2\n  Id: 20104\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 8\n    Prodmenge: 1\n    Figurnr: 33\n    Nativflg: 1\n"
        "  EndObj\n"                                                       // natives' warrior hut
        "  @Nummer: 3\n  Id: 20401\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n"  // road
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
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Nummer: 31\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 5\n  Maxkanon: 10\n  Speed: 450\n"
        "  Drehtime: 0.2\n  Maxenergy: 95\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Nummer: 33\n  Gfx: 0\n  Blocknr: 1\n  Speed: 280\n  Shotradius: 1\n  Worktime: 0.8\n  Shottime: 0.6\n"
        "  Maxenergy: 18\n  Hitpoint: 1.2\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Sea of 120 x 60 tiles; an island (12 x 10 at 60, 20) with the pirates' nest (Kontor at its west
/// coast). Players: 0 human, 1 computer, 2 pirates, 3 natives.
struct World {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{120, 60};
    sim::Rules rules = sim::Rules::open1602();
    std::unique_ptr<sim::Fleet> fleet;
    std::unique_ptr<sim::Combat> combat;
    std::unique_ptr<sim::Military> military;
    std::vector<sim::ShipEvent> events;

    explicit World(bool nest = true) {
        std::vector<sim::Player> players(4);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 20'000;
        players[1].role = sim::PlayerRole::Computer;
        players[2].role = sim::PlayerRole::Pirates;
        players[3].role = sim::PlayerRole::Natives;
        world.set_players(players);
        sim::IslandMap map(12, 10);
        for (std::int32_t y = 0; y < 10; ++y) {
            for (std::int32_t x = 0; x < 12; ++x) {
                REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0).ok());
            }
        }
        if (nest) {
            REQUIRE(map.place(*buildings.by_id(20103), 0, 4, 0, 0, 2, 0).ok());
            for (std::uint32_t y = 0; y < 10; ++y) {
                for (std::uint32_t x = 0; x < 12; ++x) {
                    map.set_city(x, y, 0, 2);
                }
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 60, 20}).ok());
        if (nest) {
            sim::City city;
            city.island = 0;
            city.index = 0;
            city.owner = 2;
            city.warehouse_x = 0;
            city.warehouse_y = 4;
            city.capacity = 30 * kTon;
            city.ensure_ware(24);
            city.trade.resize(25);
            world.add_city(city);
        }
    }

    void start() {
        fleet = std::make_unique<sim::Fleet>(world, buildings, figures, rules);
        combat = std::make_unique<sim::Combat>(world, buildings, figures);
        military = std::make_unique<sim::Military>(world, buildings, figures, rules, *combat);
        military->set_fleet(fleet.get());
        fleet->set_combat(combat.get());
    }

    void run(std::uint32_t ms) {
        std::vector<sim::ConstructionEvent> construction;
        for (std::uint32_t t = 0; t < ms; t += 50) {
            world.set_time_ms(world.time_ms() + 50);
            combat->update();
            fleet->tick(50);
            military->tick(50, &construction);
            for (const auto& e : fleet->take_events()) {
                events.push_back(e);
            }
            (void)military->take_events();
        }
    }

    [[nodiscard]] std::size_t count(sim::ShipEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
    [[nodiscard]] std::size_t pirate_ships() const {
        return static_cast<std::size_t>(
            std::count_if(world.ships().begin(), world.ships().end(), [](const sim::Ship& s) { return s.owner == 2; }));
    }
};

}  // namespace

TEST_CASE("pirates: they come once a player has 200 citizens, one ship at a time, armed by difficulty") {
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 0, 1500});
    w.start();
    w.run(60'000);
    CHECK(w.pirate_ships() == 0);               // nobody has reached the citizens
    w.world.player(0)->infra_mask |= 1U << 24;  // citizens: two pirate ships at most
    for (int i = 0; i < 600 && w.pirate_ships() == 0; ++i) {
        w.run(50);
    }
    REQUIRE(w.pirate_ships() == 1);
    CHECK(w.count(sim::ShipEvent::Kind::PirateAppeared) == 1);
    const sim::Ship* pirate = nullptr;
    for (const sim::Ship& s : w.world.ships()) {
        pirate = s.owner == 2 ? &s : pirate;
    }
    REQUIRE(pirate != nullptr);
    // Appeared at an edge of the map; the next step arms it: difficulty 2, two ships: 4 cannons.
    CHECK((pirate->x == 0 || pirate->y == 0 || pirate->x == 119 || pirate->y == 59));
    w.run(20'000);
    pirate = nullptr;
    for (const sim::Ship& s : w.world.ships()) {
        pirate = s.owner == 2 ? &s : pirate;
    }
    REQUIRE(pirate != nullptr);
    CHECK(pirate->cannons == 4);
    // The next one comes 1500 s later.
    w.run(600'000);
    CHECK(w.pirate_ships() == 1);
    w.run(1'000'000);
    CHECK(w.pirate_ships() == 2);
}

TEST_CASE("pirates: none with the setting no pirates") {
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 1, 1500});
    w.world.player(0)->infra_mask |= 1U << 30;
    w.start();
    w.run(120'000);
    CHECK(w.pirate_ships() == 0);
}

TEST_CASE("pirates: a surrendered ship is plundered and the loot goes to the nest") {
    World w;
    w.start();
    w.world.diplomacy().set_peace(w.world, 2, 1, sim::Treaty::None, 0);
    sim::Ship pirate = sim::make_ship("Pirat", 40, 25, 31, 2, 2);
    pirate.cargo.resize(5);
    pirate.cannons = 6;
    w.world.add_ship(pirate);
    sim::Ship merchant = sim::make_ship("Kauffahrer", 44, 25, 21, 1, 0);
    merchant.cargo.resize(4);
    merchant.cargo[0] = sim::CargoSlot{21, 40 * kTon, 0};  // jewellery: worth taking
    merchant.cargo[1] = sim::CargoSlot{8, 40 * kTon, 0};   // grain: not worth it
    w.world.add_ship(merchant);
    const std::uint32_t victim = w.world.ships().back().id;
    w.run(120'000);
    CHECK(w.count(sim::ShipEvent::Kind::WhiteFlag) == 1);
    CHECK(w.count(sim::ShipEvent::Kind::Plundered) == 1);
    const sim::Ship* m = w.world.ship(victim);
    REQUIRE(m != nullptr);
    CHECK(m->load(21) == 0);
    CHECK(m->load(8) == 40 * kTon);
    CHECK(w.world.city(0, 0)->stock(21) == 30 * kTon);  // the nest's room is 30 t
    CHECK(!w.world.diplomacy().hostile(2, 1));
}

TEST_CASE("pirates: their mood decides war and peace; buying peace at their Kontor") {
    World w;
    w.start();
    CHECK(w.world.diplomacy().hostile(2, 0));
    sim::pirates_reconsider(w.world);
    CHECK(w.world.diplomacy().hostile(2, 0));
    // Trade with them makes friends.
    w.world.diplomacy().add_trade(2, 0, 2000);
    sim::pirates_reconsider(w.world);
    CHECK(!w.world.diplomacy().hostile(2, 0));
    // Damage done to them makes enemies again.
    w.world.diplomacy().add_damage(2, 0, 10'000);
    sim::pirates_reconsider(w.world);
    CHECK(w.world.diplomacy().hostile(2, 0));
    // A ship at the nest buys peace: 20 000 gold -> base 1500 (2000 above 20 000), mood below 0.
    sim::Ship ship = sim::make_ship("Bote", 58, 24, 21, 0);
    ship.cargo.resize(4);
    w.world.add_ship(ship);
    const std::uint32_t price = sim::pirate_peace_price(w.world, 2, 0);
    CHECK(price >= 1500);
    sim::DiplomacyOrder order;
    order.player = 0;
    order.kind = sim::DiplomacyOrder::Kind::BuyPeace;
    order.ship = w.world.ships().back().id;
    REQUIRE(sim::apply_diplomacy(w.world, order));
    CHECK(w.world.players()[0].money == static_cast<std::int32_t>(20'000 - price));
    CHECK(!w.world.diplomacy().hostile(2, 0));
    // Not from far away.
    w.world.ships().back().place(10, 10);
    CHECK(!sim::apply_diplomacy(w.world, order));
}

namespace {

/// A natives' village (player 3) on the island with a warrior hut at (5, 4) holding eight trained
/// spearmen; `roads` are island tiles of road with their owner, placed before the territory.
struct Road {
    std::int32_t x;
    std::int32_t y;
    sim::PlayerId owner;
};
void add_village(World& w, std::initializer_list<Road> roads) {
    sim::IslandMap& map = w.world.island(0).map;
    REQUIRE(map.place(*w.buildings.by_id(20104), 5, 4, 0, 0, 3, 0).ok());
    for (const Road& r : roads) {
        REQUIRE(map.place(*w.buildings.by_id(20401), r.x, r.y, 0, 0).ok());
    }
    for (std::uint32_t y = 0; y < 10; ++y) {
        for (std::uint32_t x = 0; x < 12; ++x) {
            map.set_city(x, y, 0, 3);
        }
    }
    for (const Road& r : roads) {
        map.set_city(static_cast<std::uint32_t>(r.x), static_cast<std::uint32_t>(r.y), 0, r.owner);
    }
    sim::City village;
    village.island = 0;
    village.index = 0;
    village.owner = 3;
    village.goodwill = {200, 200, 0, 0};
    village.capacity = 30 * kTon;
    village.ensure_ware(24);
    w.world.add_city(village);
    sim::CostBuilding hut{sim::CostBuilding::Kind::Castle, 0, 5, 4, true};
    for (int i = 0; i < 8; ++i) {
        hut.slots.push_back(sim::CastleSlot{33, false, true, 18 * 32, 320});
    }
    w.world.add_cost_building(hut);
    w.start();
}

}  // namespace

TEST_CASE("natives: goodwill fades towards 49; warriors come out of their huts") {
    World w(false);
    add_village(w, {{8, 5, 3}});

    std::vector<sim::CityEvent> city_events;
    for (int i = 0; i < 6; ++i) {
        sim::advance_cities(w.world, w.buildings, &city_events);
    }
    CHECK(w.world.city(0, 0)->goodwill[0] == 188);  // 200 * (127/128)^6, truncated each time
    for (int i = 0; i < 400; ++i) {
        sim::advance_cities(w.world, w.buildings, &city_events);
    }
    CHECK(w.world.city(0, 0)->goodwill[0] == 49);
    CHECK(w.world.city(0, 0)->goodwill[2] == 0);

    // Three warriors every third tick; the slots train new ones.
    for (int i = 0; i < 3; ++i) {
        w.military->natives_tick();
    }
    CHECK(w.world.soldiers().size() == 3);
    CHECK(w.world.cost_buildings().back().slots.size() == 8);
    CHECK(w.world.cost_buildings().back().slots.back().training);
    // After an attack (and not in the first 15 minutes): seven, every tick.
    w.world.set_time_ms(20 * 60'000);
    w.world.city(0, 0)->last_attacked_ms = w.world.time_ms() - 60'000;
    w.military->natives_tick();
    CHECK(w.world.soldiers().size() == 7);
}

TEST_CASE("natives: a warrior goes to the first road of the natives a search within 7 reaches") {
    // 0x00467820: from the hut at (5, 4), over open ground; the road of another player next to the
    // hut is walked on but is no goal, the far road of the natives is reached after the near one.
    const auto targets = [] {
        World w(false);
        add_village(w, {{4, 4, 1}, {8, 5, 3}, {1, 9, 3}});
        const sim::Random before = w.world.random(sim::RandomStream::Military);
        for (int i = 0; i < 3; ++i) {
            w.military->natives_tick();
        }
        CHECK(w.world.random(sim::RandomStream::Military) == before);  // no random number drawn
        std::vector<sim::Target> t;
        for (const sim::Soldier& s : w.world.soldiers()) {
            t.push_back(s.target);
        }
        return t;
    };
    const std::vector<sim::Target> first = targets();
    REQUIRE(first.size() == 3);
    for (const sim::Target& t : first) {
        CHECK(t == sim::Target::tile_spot(60 + 8, 20 + 5));  // world half tiles of island tile (8, 5)
    }
    CHECK(targets() == first);
}

TEST_CASE("natives: without a road of theirs within 7 tiles no warrior comes out") {
    // Only roads of another player, one next to the hut: they are walked on but are no goal, and
    // no random tile stands in for the missing one.
    World w(false);
    add_village(w, {{4, 4, 1}, {9, 8, 1}});
    for (int i = 0; i < 6; ++i) {
        w.military->natives_tick();
    }
    CHECK(w.world.soldiers().empty());
    // Every place keeps its trained warrior.
    for (const sim::CastleSlot& slot : w.world.cost_buildings().back().slots) {
        CHECK(!slot.training);
    }
}

TEST_CASE("pirates: random places at sea are clamped to the map, as Pirate_RandomSeaPos") {
    // 90 tiles around a ship on a map 60 tiles high: most draws fall outside and are clamped to
    // the first or last row (0x004432F0) instead of being thrown away.
    World w;
    w.world.set_game_settings(sim::World::GameSettings{2, 0, 1500});
    w.world.player(0)->infra_mask |= 1U << 24;
    w.start();
    std::vector<sim::Target> targets;
    for (int i = 0; i < 4000 && targets.size() < 12; ++i) {
        w.run(500);
        for (const sim::Ship& s : w.world.ships()) {
            if (s.owner == 2 && s.target.kind == sim::Target::Kind::Position &&
                (targets.empty() || !(targets.back() == s.target))) {
                targets.push_back(s.target);
            }
        }
    }
    REQUIRE(targets.size() >= 4);
    std::size_t on_edge = 0;
    for (const sim::Target& t : targets) {
        CHECK(t.x < 120);
        CHECK(t.y < 60);
        on_edge += t.y == 0 || t.y == 59 ? 1U : 0U;
    }
    CHECK(on_edge >= 2);
}
