// SPDX-License-Identifier: GPL-3.0-or-later
// Soldiers, towers and building damage (docs/re/military.md).

#include <algorithm>
#include <memory>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/military.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kHouse = 20103;
constexpr std::uint32_t kDoctor = 20104;
constexpr std::uint32_t kTowerBuilding = 20105;
constexpr std::uint32_t kForest = 20106;
constexpr std::uint32_t kCastle = 20107;
constexpr std::uint32_t kKontor = 20108;
constexpr std::uint32_t kKontorDestroyed = 20109;
constexpr std::uint32_t kNativeHut = 20110;
constexpr std::uint16_t kSword = 1;
constexpr std::uint16_t kMusket = 9;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // open ground
        "  @Nummer: 1\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"  // a house
        "  @Nummer: 2\n  Id: 20104\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n  Maxenergy: 50\n  NoShotFlg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 22\n  EndObj\n"  // doctor (KLINIK)
        "  @Nummer: 3\n  Id: 20105\n  Kind: 6\n  Gfx: 10\n  Size: 1, 1\n  Maxenergy: 55\n  NoShotFlg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 16\n    Figurnr: 38\n  EndObj\n"                 // Wachturm
        "  @Nummer: 4\n  Id: 20106\n  Kind: 10\n  Gfx: 11\n  Size: 1, 1\n  NoShotFlg: 1\n"  // forest
        "  @Nummer: 5\n  Id: 20107\n  Kind: 14\n  Gfx: 20\n  Size: 2, 2\n  Maxenergy: 100\n  NoShotFlg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 3\n    Prodmenge: 0.5\n    Interval: 10\n  EndObj\n"
        "  @Nummer: 6\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor
        "  @Nummer: 7\n  Id: 20109\n  Kind: 35\n  Gfx: 40\n  Size: 2, 2\n  NoShotFlg: 1\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n  Destroyflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // destroyed Kontor
        "  @Nummer: 8\n  Id: 20110\n  Kind: 35\n  Gfx: 50\n  Size: 2, 2\n  Maxenergy: 5\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Nativflg: 1\n  EndObj\n"  // natives' main hut
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
        "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Shotradius: 0.75\n  Maxstepcnt: 1\n"
        "  Rohstoff: 11\n  Rohmenge: 3\n  Worktime: 0.8\n  Shottime: 0.6\n  Maxenergy: 20\n  Hitpoint: 1.0\n"
        "  Nummer: 9\n  Kind: 3\n  Gfx: 0\n  Blocknr: 1\n  Speed: 210\n  Shotradius: 4\n  Maxstepcnt: 2\n"
        "  Worktime: 2.0\n  Shottime: 0.7\n  Maxenergy: 15\n  Hitpoint: 2.4\n"
        "  Nummer: 38\n  Gfx: 0\n  Blocknr: 1\n  Shotradius: 7.5\n  Worktime: 3.0\n  Shottime: 0.6\n"
        "  Maxenergy: 24\n  Hitpoint: 4.0\n"
        // A small ship with two holds.
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 1\n  Maxware: 2\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 80 x 60 tiles with an island of open ground (30 x 20 at 10, 10). Player 0 is human,
/// 1 a computer player, 2 the natives; nobody is at war yet.
struct Field {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{80, 60};
    sim::Rules rules = sim::Rules::open1602();
    std::unique_ptr<sim::Combat> combat;
    std::unique_ptr<sim::Military> military;
    std::vector<sim::MilitaryEvent> events;
    std::vector<sim::ConstructionEvent> construction;

    Field() {
        std::vector<sim::Player> players(3);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[1].role = sim::PlayerRole::Computer;
        players[2].role = sim::PlayerRole::Natives;
        world.set_players(players);
        sim::IslandMap map(30, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    }

    void start() {
        combat = std::make_unique<sim::Combat>(world, buildings, figures);
        military = std::make_unique<sim::Military>(world, buildings, figures, rules, *combat);
    }

    /// Places a building of `owner` (city `city` of the owner) at island tile (x, y).
    void build(std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId owner, std::uint8_t city = 0) {
        sim::IslandMap& map = world.island(0).map;
        REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0, owner, city).ok());
        if (world.city(0, city) == nullptr) {
            sim::City c;
            c.island = 0;
            c.index = city;
            c.owner = owner;
            world.add_city(c);
        }
    }

    sim::Soldier& add(std::uint16_t figure, sim::PlayerId owner, std::int32_t cx, std::int32_t cy) {
        sim::Soldier s;
        s.place_cell(cx, cy);
        s.figure = figure;
        s.owner = owner;
        s.island = 0;
        world.add_soldier(s);
        return world.soldiers().back();
    }

    void war(sim::PlayerId a, sim::PlayerId b) {
        CHECK(world.diplomacy().set_peace(world, a, b, sim::Treaty::None, world.time_ms()));
    }

    void run(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += 50) {
            world.set_time_ms(world.time_ms() + 50);
            combat->update();
            military->tick(50, &construction);
            for (const auto& e : military->take_events()) {
                events.push_back(e);
            }
        }
    }

    [[nodiscard]] std::size_t count(sim::MilitaryEvent::Kind kind) const {
        return static_cast<std::size_t>(
            std::count_if(events.begin(), events.end(), [&](const auto& e) { return e.kind == kind; }));
    }
};

}  // namespace

TEST_CASE("military: a soldier walks to a place over open ground at its speed") {
    Field f;
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 12, 2 * 15).id;  // world tile (12, 15)
    sim::SoldierOrder order{0, {id}, sim::Target::spot(2 * 20, 2 * 15)};
    REQUIRE(f.military->order(order));
    // 8 tiles on open ground (Wegspeed 100 % here): 16 cells of 0.5 / (0.16 * 260 / 32) s = 6.15 s.
    f.run(5'000);
    const sim::Soldier* s = f.world.soldier(id);
    REQUIRE(s != nullptr);
    CHECK(s->cell_x() < 40);
    f.run(2'000);
    CHECK(s->cell_x() == 40);
    CHECK(s->cell_y() == 30);
    CHECK(s->target.empty());
}

TEST_CASE("military: next to a unit with a lower number a soldier walks slower (also across squares)") {
    // Cells 47 and 48 lie in different squares of the units' grid (16 cells, review AU7).
    const auto walked = [](bool crowded) {
        Field f;
        f.start();
        if (crowded) {
            f.add(kSword, 0, 47, 30);  // stands; the lower number
        }
        const std::uint32_t id = f.add(kSword, 0, 48, 30).id;
        REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(60, 30)}));
        f.run(300);
        return f.world.soldier(id)->pos_x;
    };
    CHECK(walked(true) < walked(false));
}

TEST_CASE("military: soldiers walk around forest and buildings") {
    Field f;
    // A wall of forest across the island with a gap at the bottom.
    for (std::int32_t y = 0; y < 17; ++y) {
        REQUIRE(f.world.island(0).map.place(*f.buildings.by_id(kForest), 15, y, 0, 0).ok());
    }
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 20, 2 * 12).id;  // island tile (10, 2)
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(2 * 30, 2 * 12)}));
    f.run(40'000);
    const sim::Soldier* s = f.world.soldier(id);
    REQUIRE(s != nullptr);
    CHECK(s->cell_x() == 60);
    CHECK(s->cell_y() == 24);
}

TEST_CASE("military: hostile swordsmen fight until one falls; at peace nobody fights") {
    Field f;
    f.start();
    const std::uint32_t a = f.add(kSword, 0, 40, 30).id;
    sim::Soldier& wounded = f.add(kSword, 1, 43, 30);
    wounded.health = 500;  // equal units at full health would kill each other with the same blow
    const std::uint32_t b = wounded.id;
    f.run(10'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);  // at peace
    f.war(0, 1);
    f.run(60'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
    const sim::Soldier* sa = f.world.soldier(a);
    const sim::Soldier* sb = f.world.soldier(b);
    CHECK(sa != nullptr);
    CHECK(sb == nullptr);
    const sim::Soldier* survivor = sa;
    REQUIRE(survivor != nullptr);
    CHECK(survivor->health > 0);
    CHECK(survivor->health < 640);
    // The first hit does full damage: Hitpoint 1.0 = 32.
    const auto hit = std::find_if(f.events.begin(), f.events.end(),
                                  [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::Hit; });
    CHECK(hit != f.events.end());
    // The loser's player remembers who killed his unit.
    CHECK(f.world.diplomacy().blamed(1) == 0);
    CHECK(f.world.diplomacy().relation(0, 1).damage + f.world.diplomacy().relation(1, 0).damage > 0);
}

TEST_CASE("military: a musketeer shoots from afar, a computer's swordsman closes in") {
    Field f;
    f.start();
    f.war(0, 1);
    const std::uint32_t musket = f.add(kMusket, 0, 40, 30).id;
    const std::uint32_t sword = f.add(kSword, 1, 52, 30).id;
    f.run(2'000);
    // The swordsman came closer (computer players' units chase); the musketeer stays.
    const sim::Soldier* m = f.world.soldier(musket);
    const sim::Soldier* s = f.world.soldier(sword);
    REQUIRE(m != nullptr);
    REQUIRE(s != nullptr);
    CHECK(m->cell_x() == 40);
    CHECK(s->cell_x() < 52);
    f.run(60'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
    // A musket shot does 2.4 HP (76/32) at full health.
    bool musket_hit = false;
    for (const auto& e : f.events) {
        musket_hit = musket_hit || (e.kind == sim::MilitaryEvent::Kind::Hit && e.unit == sim::Target::soldier(musket));
    }
    CHECK(musket_hit);
}

TEST_CASE("military: an attack order on a building means war; the building falls, damage heals") {
    Field f;
    f.build(kHouse, 20, 5, 1);
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 26, 2 * 15).id;  // island tile (16, 5)
    sim::SoldierOrder order{0, {id}, sim::Target::building(0, 20, 5), true};
    REQUIRE(f.military->order(order));
    CHECK(f.world.diplomacy().hostile(0, 1));
    // 20 HP; a sword does 40 % against buildings (12 per hit), about one hit a second.
    f.run(30'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::WarDeclared) == 1);

    CHECK(f.military->building_damage(0, 20, 5) > 0);
    CHECK(f.military->building_damage(0, 20, 5) < 20 * 32);
    f.run(60'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 1);
    CHECK(f.world.islands()[0].map.top(20, 5).sprite != f.buildings.by_id(kHouse)->gfx);
    CHECK(f.world.soldiers().front().target.empty());  // nothing left to attack

    // Damage heals by 1 HP every 10 s.
    f.build(kHouse, 24, 10, 1, 1);
    f.military->damage_building(0, 24, 10, 100, true, 0, &f.construction);
    CHECK(f.military->building_damage(0, 24, 10) == 100);
    f.run(10'000);
    CHECK(f.military->building_damage(0, 24, 10) == 68);
}

TEST_CASE("military: a cannon tower fires at hostile soldiers in range") {
    Field f;
    f.build(kTowerBuilding, 10, 10, 1);
    for (std::uint32_t y = 0; y < 20; ++y) {
        for (std::uint32_t x = 0; x < 30; ++x) {
            if (f.world.islands()[0].map.top(x, y).city == sim::MapCell::kNoCity) {
                f.world.island(0).map.set_city(x, y, 0, 1);
            }
        }
    }
    f.start();
    f.military->update_towers(0);
    REQUIRE(f.world.towers().size() == 1);
    CHECK(f.world.towers()[0].owner == 1);
    CHECK(f.world.towers()[0].figure == 38);
    // A musketeer of player 0 at 6 tiles: in the tower's range (7.5) but out of its own (4).
    f.add(kMusket, 0, 53, 41);
    f.run(5'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);  // at peace
    f.war(0, 1);
    // 4.0 HP every 3 s: a musketeer (15 HP) falls with the fourth hit.
    f.run(8'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 0);
    f.run(4'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 1);
}

TEST_CASE("military: a doctor heals a wounded soldier") {
    Field f;
    f.build(kDoctor, 20, 5, 0);
    f.start();
    sim::Soldier& s = f.add(kSword, 0, 2 * 26, 2 * 15);
    s.health = 100;
    const std::uint32_t id = s.id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 5)}));
    f.run(20'000);
    REQUIRE(f.world.soldier(id) != nullptr);
    CHECK(f.world.soldier(id)->health == 640);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierHealed) == 1);
}

TEST_CASE("military: disbanding costs the unit's price and removes it") {
    Field f;
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 40, 30).id;
    REQUIRE(f.military->command(sim::SoldierCommand{0, id, sim::SoldierCommand::Op::Disband, 0, {}}));
    f.run(2'000);
    CHECK(f.world.soldier(id) == nullptr);
    CHECK(f.count(sim::MilitaryEvent::Kind::SoldierKilled) == 0);
}

TEST_CASE("military: patrols walk between two points") {
    Field f;
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 40, 30).id;
    REQUIRE(f.military->command(
        sim::SoldierCommand{0, id, sim::SoldierCommand::Op::SetWaypoint, 0, sim::Target::spot(40, 30)}));
    REQUIRE(f.military->command(
        sim::SoldierCommand{0, id, sim::SoldierCommand::Op::SetWaypoint, 1, sim::Target::spot(50, 30)}));
    REQUIRE(f.military->command(sim::SoldierCommand{0, id, sim::SoldierCommand::Op::Patrol, 0, {}}));
    std::int32_t max_x = 0;
    std::int32_t min_x_after = 100;
    for (int i = 0; i < 40; ++i) {
        f.run(500);
        const sim::Soldier* s = f.world.soldier(id);
        REQUIRE(s != nullptr);
        max_x = std::max(max_x, s->cell_x());
        if (max_x == 50) {
            min_x_after = std::min(min_x_after, s->cell_x());
        }
    }
    CHECK(max_x == 50);
    CHECK(min_x_after < 50);
}

TEST_CASE("military: a castle trains units with weapons and sends them out") {
    Field f;
    f.build(kCastle, 20, 5, 0);
    f.world.add_cost_building(sim::CostBuilding{sim::CostBuilding::Kind::Castle, 0, 20, 5, true});
    f.world.cost_buildings().back().weapons[0] = 3 * 32;  // 3 t swords
    f.start();
    using Op = sim::CastleOrder::Op;
    // Only soldiers: a tower (figure 38) has hit points too, but no castle trains it.
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, 38, 0, {}}));
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    const sim::CostBuilding& castle = f.world.cost_buildings().back();
    REQUIRE(castle.slots.size() == 2);
    CHECK(castle.slots[0].armed);
    CHECK(!castle.slots[1].armed);  // no swords left
    CHECK(castle.weapons[0] == 0);
    CHECK(f.military->army_size(0) == 2);
    // The second asks for a pack donkey.
    f.run(2'000);
    const auto fetch = f.military->take_fetch_requests();
    REQUIRE(fetch.size() == 1);
    CHECK(fetch[0].ware == 11);
    // 16 points every 10 s, 320 needed: the first unit is done after 200 s; it cannot leave before.
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Release, 0, 0, sim::Target::spot(40, 40)}));
    f.run(200'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::UnitTrained) == 1);
    CHECK(!castle.slots[0].training);
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Release, 0, 0, sim::Target::spot(40, 40)}));
    REQUIRE(f.world.soldiers().size() == 1);
    CHECK(f.world.soldiers()[0].health == 640);
    CHECK(f.world.soldiers()[0].target == sim::Target::spot(40, 40));
    CHECK(castle.slots.size() == 1);
    // The second has its points but no weapons; the swords arrive and it is done.
    f.run(200'000);
    CHECK(castle.slots[0].training);
    f.military->castle_delivery(0, 20, 5, 11, 3 * 32);
    f.run(1'000);
    CHECK(!castle.slots[0].training);
    // Cancelling gives the weapons back.
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Cancel, 0, 0, {}}));
    CHECK(castle.weapons[0] == 3 * 32);
    CHECK(castle.slots.empty());
}

TEST_CASE("military: the unit limit is a rule, 250 by default (L-008)") {
    Field f;
    f.build(kCastle, 20, 5, 0);
    f.world.add_cost_building(sim::CostBuilding{sim::CostBuilding::Kind::Castle, 0, 20, 5, true});
    CHECK(f.rules.max_army == 250);
    CHECK(sim::Rules::original().max_army == 100);
    f.rules.max_army = 2;
    f.start();
    using Op = sim::CastleOrder::Op;
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    REQUIRE(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    CHECK(!f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    f.run(50);
    CHECK(f.count(sim::MilitaryEvent::Kind::ArmyFull) == 1);
    f.rules.max_army = 0;
    CHECK(f.military->castle(sim::CastleOrder{0, 0, 20, 5, Op::Recruit, kSword, 0, {}}));
    CHECK(f.military->army_size(0) == 3);
}

TEST_CASE("military: soldiers go on board a ship and land elsewhere") {
    Field f;
    f.start();
    // A ship of player 0 at sea just west of the island (world tile 8, 15).
    sim::Ship ship = sim::make_ship("Boat", 8, 15, 21, 0);
    ship.cargo.resize(4);
    f.world.add_ship(ship);
    const std::uint32_t ship_id = f.world.ships().back().id;
    const std::uint32_t id = f.add(kSword, 0, 2 * 14, 2 * 15).id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::other_ship(ship_id)}));
    f.run(10'000);
    CHECK(f.world.soldier(id) == nullptr);
    const sim::Ship* boat = f.world.ship(ship_id);
    REQUIRE(boat != nullptr);
    CHECK(boat->cargo[0].ware == 25);
    CHECK(boat->cargo[0].amount == 1600);
    CHECK(f.military->army_size(0) == 1);
    REQUIRE(f.military->unload(sim::UnloadOrder{0, ship_id}));
    REQUIRE(f.world.soldiers().size() == 1);
    CHECK(f.world.soldiers()[0].health == 640);
    CHECK(f.world.soldiers()[0].figure == kSword);
    CHECK(boat->cargo[0].ware == 0);
}

TEST_CASE("military: a ship from the original's files takes as many soldiers as its type has holds") {
    Field f;
    f.start();
    // Eight cargo slots as the original keeps them for every ship; the type has two holds.
    sim::Ship ship = sim::make_ship("Boat", 8, 15, 21, 0);
    ship.cargo.resize(8);
    f.world.add_ship(ship);
    const std::uint32_t ship_id = f.world.ships().back().id;
    std::vector<std::uint32_t> ids;
    for (std::int32_t i = 0; i < 3; ++i) {
        ids.push_back(f.add(kSword, 0, 2 * 14, 2 * (14 + i)).id);
    }
    REQUIRE(f.military->order(sim::SoldierOrder{0, ids, sim::Target::other_ship(ship_id)}));
    f.run(10'000);
    const sim::Ship* boat = f.world.ship(ship_id);
    REQUIRE(boat != nullptr);
    CHECK(sim::usable_holds(*boat, f.figures) == 2);
    CHECK(boat->cargo[0].ware == 25);
    CHECK(boat->cargo[1].ware == 25);
    for (std::size_t h = 2; h < boat->cargo.size(); ++h) {
        CHECK(boat->cargo[h].ware == 0);
    }
    CHECK(f.world.soldiers().size() == 1);  // the third stays ashore
}

TEST_CASE("military: a Kontor over the destroyed Kontor of a defenceless city takes the city over") {
    Field f;
    f.build(kKontorDestroyed, 20, 5, 1);
    for (std::uint32_t y = 0; y < 12; ++y) {
        for (std::uint32_t x = 14; x < 28; ++x) {
            if (f.world.islands()[0].map.top(x, y).city == sim::MapCell::kNoCity) {
                f.world.island(0).map.set_city(x, y, 0, 1);
            }
        }
    }
    f.world.city(0, 0)->warehouse_x = 20;
    f.world.city(0, 0)->warehouse_y = 5;
    CHECK(sim::city_defenceless(f.world, f.buildings, 0, 0));
    sim::BuildOrder order;
    order.player = 0;
    order.items = {sim::BuildItem{kKontor, 20, 5, 0, 0}};
    std::vector<sim::ConstructionEvent> events;
    const sim::BuildPlan plan = sim::execute_build(f.world, f.buildings, order, {}, &events);
    CHECK(plan.takeover);
    REQUIRE(plan.placed == 1);
    CHECK(f.world.city(0, 0)->owner == 0);
    CHECK(f.world.islands()[0].map.top(15, 2).owner == 0);
    CHECK(std::any_of(events.begin(), events.end(),
                      [](const auto& e) { return e.kind == sim::ConstructionEvent::Kind::CityTaken; }));
    // A city with a standing Kontor cannot be taken.
    Field g;
    g.build(kKontor, 20, 5, 1);
    CHECK(!sim::city_defenceless(g.world, g.buildings, 0, 0));
    sim::BuildOrder again;
    again.player = 0;
    again.items = {sim::BuildItem{kKontor, 20, 5, 0, 0}};
    CHECK(!sim::plan_build(g.world, g.buildings, again).takeover);
}

TEST_CASE("military: natives curse who destroyed their village") {
    Field f;
    f.build(kNativeHut, 20, 5, 2, 1);
    f.start();
    const std::uint32_t id = f.add(kSword, 0, 2 * 26, 2 * 15).id;
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::building(0, 20, 5), true}));
    f.run(40'000);
    CHECK(f.count(sim::MilitaryEvent::Kind::BuildingDestroyed) == 1);
    CHECK(f.world.diplomacy().blamed(2) == 0);
    REQUIRE(f.count(sim::MilitaryEvent::Kind::NativesCurse) == 1);
    const auto curse = std::find_if(f.events.begin(), f.events.end(),
                                    [](const auto& e) { return e.kind == sim::MilitaryEvent::Kind::NativesCurse; });
    CHECK(curse->player == 0);
    CHECK(curse->pirates == f.world.islands()[0].drought.curse_pending);
}

TEST_CASE("military: a group keeps its shape; queued orders follow one after another") {
    Field f;
    f.start();
    const std::uint32_t a = f.add(kSword, 0, 2 * 12, 2 * 15).id;
    const std::uint32_t b = f.add(kSword, 0, 2 * 12, 2 * 17).id;
    // b one tile below a: offsets in 1/8 tile around the spot.
    sim::SoldierOrder order{0, {a, b}, sim::Target::spot(2 * 20, 2 * 15)};
    order.offsets = {{0, -4}, {0, 4}};
    REQUIRE(f.military->order(order));
    CHECK(f.world.soldier(a)->target == sim::Target::spot(40, 29));
    CHECK(f.world.soldier(b)->target == sim::Target::spot(40, 31));
    // Two more places queued for a: it walks to them in turn.
    sim::SoldierOrder next{0, {a}, sim::Target::spot(2 * 20, 2 * 20)};
    next.queue = true;
    REQUIRE(f.military->order(next));
    next.target = sim::Target::spot(2 * 14, 2 * 20);
    REQUIRE(f.military->order(next));
    CHECK(f.world.soldier(a)->queue.size() == 2);
    CHECK(f.world.soldier(a)->target == sim::Target::spot(40, 29));
    f.run(40'000);
    const sim::Soldier* s = f.world.soldier(a);
    CHECK(s->queue.empty());
    CHECK(s->cell_x() == 28);
    CHECK(s->cell_y() == 40);
    // A plain order clears the queue.
    REQUIRE(f.military->order(next));
    REQUIRE(f.military->order(next));
    CHECK(!f.world.soldier(a)->queue.empty());
    REQUIRE(f.military->order(sim::SoldierOrder{0, {a}, sim::Target::spot(2 * 12, 2 * 15)}));
    CHECK(f.world.soldier(a)->queue.empty());
}

TEST_CASE("military: an attack ends a pending offer of peace too") {
    Field f;
    f.start();
    f.war(0, 1);
    f.world.diplomacy().relation(0, 1).peace = sim::Treaty::Offered;
    f.world.diplomacy().relation(1, 0).peace = sim::Treaty::Received;
    const sim::Soldier& enemy = f.add(kSword, 1, 2 * 25, 2 * 15);
    CHECK(f.military->declare_war_by_attack(0, sim::Target::soldier(enemy.id)));
    CHECK(f.world.diplomacy().relation(0, 1).peace == sim::Treaty::None);
    CHECK(!f.military->declare_war_by_attack(0, sim::Target::soldier(enemy.id)));  // at war already
}

TEST_CASE("military: an attack move fights on the way; holding the position chases nobody") {
    for (const bool attack_move : {false, true}) {
        Field f;
        f.start();
        f.war(0, 1);
        const std::uint32_t id = f.add(kSword, 0, 2 * 12, 2 * 15).id;
        sim::Soldier& enemy = f.add(kSword, 1, 2 * 20, 2 * 18);
        enemy.health = 200;
        enemy.hold = true;  // it stays where it is
        sim::SoldierOrder order{0, {id}, sim::Target::spot(2 * 28, 2 * 15)};
        order.attack_move = attack_move;
        REQUIRE(f.military->order(order));
        f.run(30'000);
        // A melee unit of the player walking somewhere leaves enemies off its way alone (original);
        // with the attack move it fights them and goes on.
        CHECK((f.count(sim::MilitaryEvent::Kind::Shot) > 0) == attack_move);
        f.run(30'000);
        const sim::Soldier* s = f.world.soldier(id);
        REQUIRE(s != nullptr);
        CHECK(s->cell_x() == 56);
        CHECK(s->cell_y() == 30);
    }
    // Holding the position: no chase of an enemy three tiles away.
    Field f;
    f.start();
    f.war(0, 1);
    const std::uint32_t id = f.add(kSword, 0, 2 * 12, 2 * 15).id;
    f.add(kSword, 1, 2 * 15, 2 * 15).hold = true;
    sim::SoldierCommand hold;
    hold.player = 0;
    hold.soldier = id;
    hold.op = sim::SoldierCommand::Op::Hold;
    REQUIRE(f.military->command(hold));
    f.run(10'000);
    CHECK(f.world.soldier(id)->cell_x() == 24);
    CHECK(f.count(sim::MilitaryEvent::Kind::Shot) == 0);
    // The next order ends it.
    REQUIRE(f.military->order(sim::SoldierOrder{0, {id}, sim::Target::spot(2 * 12, 2 * 17)}));
    CHECK(!f.world.soldier(id)->hold);
}

TEST_CASE("military: a unit sees the nearest of many enemies, nearest first (S-114)") {
    Field f;
    f.war(0, 1);
    const std::uint32_t me = f.add(kSword, 0, 50, 58).id;
    // Forty enemies to the north, 8 to 10 tiles away, and one right beside.
    for (std::int32_t k = 0; k < 40; ++k) {
        f.add(kSword, 1, 40 + k % 10, 38 + k / 10);
    }
    const std::uint32_t beside = f.add(kSword, 1, 52, 58).id;
    f.start();
    f.combat->update();
    const auto list = f.combat->hostiles(sim::Target::soldier(me));
    REQUIRE(list.size() == sim::Combat::kMaxNeighbours);
    CHECK(list[0].unit == sim::Target::soldier(beside));
    for (std::size_t i = 1; i < list.size(); ++i) {
        CHECK(list[i - 1].distance8 <= list[i].distance8);
    }
    // Each of the enemies sees the one unit of player 0.
    const auto theirs = f.combat->hostiles(sim::Target::soldier(beside));
    REQUIRE(theirs.size() == 1);
    CHECK(theirs[0].unit == sim::Target::soldier(me));
}
