// SPDX-License-Identifier: GPL-3.0-or-later
// The computer players (docs/re/ai.md) on a small synthetic world, without the original data: they
// found a city with their ship through the same orders a player gives, run the same on every
// computer, keep their state through a savegame and ignore the players that are not theirs.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/ai.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kKontor = 22101;  // the Id the computer players build as their first Kontor
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;
constexpr sim::PlayerId kTrader = 2;
constexpr sim::PlayerId kNatives = 3;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"                   // ground
        "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"                   // sea
        "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"  // beach
        "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"                   // a tree
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // Roads: stone and dirt, each a family of straight, curve, T and cross.
        "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 6\n  Id: 20403\n  Kind: 1\n  Gfx: 6\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 7\n  Id: 20404\n  Kind: 1\n  Gfx: 7\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 8\n  Id: 20411\n  Kind: 1\n  Gfx: 8\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 2\n  EndObj\n"
        "  @Nummer: 9\n  Id: 20412\n  Kind: 1\n  Gfx: 9\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  @Nummer: 10\n  Id: 20413\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  @Nummer: 11\n  Id: 20414\n  Kind: 1\n  Gfx: 11\n  Size: 1, 1\n  KreuzBase: 8\n"
        // Kontor I at the beach.
        "  @Nummer: 12\n  Id: 22101\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
        "  KreuzBase: 12\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Maxlager: 30\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
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
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 90 x 90 tiles of sea with one island of 50 x 50 tiles at (30, 20): open ground, a
/// wood (columns 32..43), a beach facing east (column 46) and sea (columns 47..49). Every crop grows
/// there fully. Player 0 is human, 1 a computer with a ship east of the island, 2 the trader, 3 the
/// natives.
struct Game {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{90, 90};

    explicit Game(bool computer_ship = true, std::int32_t computer_money = 10000) {
        add_islet(2, 2);
        sim::IslandMap map(50, 50);
        for (std::int32_t y = 0; y < 50; ++y) {
            for (std::int32_t x = 0; x < 50; ++x) {
                std::uint32_t id = kGround;
                if (x > 46) {
                    id = kSea;
                } else if (x == 46) {
                    id = kBeach;
                } else if (x >= 32 && x <= 43) {
                    id = kTree;
                }
                REQUIRE(map.place(*buildings.by_id(id), x, y, id == kBeach ? 3 : 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 30, 20};
        island.header.number = static_cast<std::uint8_t>(world.islands().size());
        island.header.width = 50;
        island.header.height = 50;
        island.header.fertility = 0xFFFFFFFFU;
        REQUIRE(world.add_island(std::move(island)).ok());

        std::vector<sim::Player> players(4);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kHuman].money = 10000;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = computer_money;
        players[kTrader].role = sim::PlayerRole::Trader;
        players[kNatives].role = sim::PlayerRole::Natives;
        world.set_players(players);
        if (computer_ship) {
            add_ship(kComputer, 86, 45);
        }
    }

    /// A rock of 5 x 5 tiles of ground, too small for a colony.
    void add_islet(std::uint32_t x, std::uint32_t y) {
        sim::IslandMap map(5, 5);
        for (std::int32_t ty = 0; ty < 5; ++ty) {
            for (std::int32_t tx = 0; tx < 5; ++tx) {
                REQUIRE(map.place(*buildings.by_id(kGround), tx, ty, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland islet{assets::IslandHeader{}, std::move(map), x, y};
        islet.header.number = static_cast<std::uint8_t>(world.islands().size());
        islet.header.width = 5;
        islet.header.height = 5;
        REQUIRE(world.add_island(std::move(islet)).ok());
    }

    sim::Ship& add_ship(sim::PlayerId owner, std::uint32_t x, std::uint32_t y) {
        sim::Ship s = sim::make_ship("Ship", x, y, kShipFigure, owner, 2);
        s.cargo.resize(4);
        world.add_ship(s);
        return world.ships().back();
    }

    [[nodiscard]] std::size_t cities_of(sim::PlayerId owner) const {
        return static_cast<std::size_t>(std::count_if(world.cities().begin(), world.cities().end(),
                                                      [&](const sim::City& c) { return c.owner == owner; }));
    }
};

/// Ticks until `done` holds or `ms` of game time have passed; whether `done` held.
template <class F>
bool run_until(sim::Simulation& s, std::uint32_t ms, F&& done) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
        if (done()) {
            return true;
        }
    }
    return false;
}

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

bool same(const std::vector<sim::ComputerPlayers::View>& a, const std::vector<sim::ComputerPlayers::View>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.player != y.player || x.cities != y.cities || x.max_stage != y.max_stage || x.score != y.score ||
            x.settling != y.settling || x.settle_failed != y.settle_failed || x.stack != y.stack ||
            x.plans.size() != y.plans.size()) {
            return false;
        }
        for (std::size_t k = 0; k < x.plans.size(); ++k) {
            const auto& p = x.plans[k];
            const auto& q = y.plans[k];
            if (p.island != q.island || p.city != q.city || p.stage != q.stage || p.flags != q.flags ||
                p.slots != q.slots) {
                return false;
            }
        }
    }
    return true;
}

std::vector<std::uint8_t> saved(const sim::ComputerPlayers& ai) {
    core::ByteWriter out;
    ai.save(out);
    return out.take();
}

}  // namespace

TEST_CASE("ai: a computer with a ship founds a city through a build order") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t ship = g.world.ships().front().id;
    bool directed = false;
    const bool founded = run_until(s, 600'000, [&] {
        directed = directed || s.computers().directs(ship);
        return g.cities_of(kComputer) != 0;
    });
    REQUIRE(founded);
    CHECK(directed);  // the ship sailed under the computer's command
    CHECK(!s.computers().directs(ship));
    const sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = &c;
        }
    }
    REQUIRE(city != nullptr);
    CHECK(city->island == 1);
    REQUIRE(city->warehouse_x.has_value());
    REQUIRE(city->warehouse_y.has_value());
    // A Kontor at the coast, its warehouse.
    const sim::MapCell& at = g.world.islands()[1].map.top(*city->warehouse_x, *city->warehouse_y);
    const assets::BuildingType* kontor = g.buildings.by_sprite(at.sprite);
    REQUIRE(kontor != nullptr);
    CHECK(kontor->id == kKontor);
    CHECK(at.owner == kComputer);
    // Built like a player's Kontor: the game reports the new city.
    bool reported = false;
    for (const auto& e : s.take_construction_events()) {
        reported = reported || (e.kind == sim::ConstructionEvent::Kind::CityFounded && e.island == 1);
    }
    CHECK(reported);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(view[0].player == kComputer);
    CHECK(view[0].cities == 1);
    CHECK(!view[0].settling);
}

TEST_CASE("ai: the new city gets roads from the computer's own build orders") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    const std::int32_t after_founding = g.world.players()[kComputer].money;
    run(s, 60'000);
    std::size_t roads = 0;
    const sim::IslandMap& map = g.world.islands()[1].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
            roads += b != nullptr && b->kind == 1 && c.owner == kComputer ? 1U : 0U;
        }
    }
    CHECK(roads > 0);
    // Paid like any player's buildings, and never into debt.
    CHECK(g.world.players()[kComputer].money < after_founding);
    CHECK(g.world.players()[kComputer].money > 0);
}

TEST_CASE("ai: two runs of the same game stay the same, tick by tick") {
    Game a;
    Game b;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sim::Simulation sb(b.world, b.buildings, b.figures);
    // Through the voyage, the founding and the first roads.
    for (int step = 0; step < 12; ++step) {
        run(sa, 5'000);
        run(sb, 5'000);
        const auto ha = sa.state_hash();
        REQUIRE(ha.has_value());
        CHECK(ha == sb.state_hash());
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(saved(sa.computers()) == saved(sb.computers()));
    }
    CHECK(a.cities_of(kComputer) == 1);
}

// Lockstep needs the same game on every machine (ADR 0004). This hash is checked on every build of
// CI (GCC, Clang, MSVC, Apple Clang), so a difference between compilers or standard libraries shows
// here. A change that alters the game on purpose updates the number, and says why in its PR.
TEST_CASE("ai: a computer's first minutes give the same state on every platform") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 90'000);
    const auto hash = s.state_hash();
    REQUIRE(hash.has_value());
    constexpr std::uint64_t kExpected = 0x7e7fc0d6d363168aULL;
    if (*hash != kExpected) {
        std::printf("  state hash after 90 s: 0x%016llx\n", static_cast<unsigned long long>(*hash));
    }
    CHECK(*hash == kExpected);
    CHECK(g.cities_of(kComputer) == 1);
}

TEST_CASE("ai: a game saved while the computer is under way goes on as if never saved") {
    // Saved before, during and after the voyage of the founding ship.
    bool under_way = false;
    for (const std::uint32_t at : {4'000U, 8'000U, 15'000U, 40'000U}) {
        Game a;
        sim::Simulation sa(a.world, a.buildings, a.figures);
        run(sa, at);
        const std::uint32_t ship = a.world.ships().front().id;
        under_way = under_way || (sa.computers().view().front().settling && sa.computers().directs(ship));
        const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
        REQUIRE(loaded.ok());
        sim::World world = std::move(loaded).value();
        sim::Simulation sb(world, a.buildings, a.figures);
        CHECK(!sb.state_damaged());
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(saved(sa.computers()) == saved(sb.computers()));
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(sa.computers().directs(ship) == sb.computers().directs(ship));
        run(sa, 30'000);
        run(sb, 30'000);
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(a.cities_of(kComputer) == 1);
    }
    CHECK(under_way);
}

TEST_CASE("ai: the names for new cities and ships travel with the saved game") {
    Game a;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sa.offer_names({"Neustadt", "Altdorf"}, {"Seeadler"});
    sa.offer_names({"Newtown"}, {"Sea eagle"});  // the game has names already
    const std::vector<std::string> german{"Neustadt", "Altdorf"};
    CHECK(sa.city_names() == german);
    run(sa, 40'000);
    const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
    REQUIRE(loaded.ok());
    sim::World world = std::move(loaded).value();
    sim::Simulation sb(world, a.buildings, a.figures);
    CHECK(!sb.state_damaged());
    // An installation in another language keeps the game's names, and the computers stay alike.
    sb.offer_names({"Newtown"}, {"Sea eagle"});
    CHECK(sb.city_names() == sa.city_names());
    CHECK(sa.state_hash() == sb.state_hash());
    CHECK(a.cities_of(kComputer) == 1);
    CHECK(a.world.cities().front().name == "Neustadt");
}

TEST_CASE("ai: the state of the computers goes to bytes and back whole") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 20'000);
    const auto bytes = saved(s.computers());
    sim::ComputerPlayers copy(s);
    core::ByteReader in(bytes);
    REQUIRE(copy.load(in));
    CHECK(in.at_end());
    CHECK(same(copy.view(), s.computers().view()));
    CHECK(saved(copy) == bytes);
}

TEST_CASE("ai: damaged state is refused and leaves the computers as they were") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 20'000);
    const auto bytes = saved(s.computers());
    REQUIRE(bytes.size() > 4);

    // A copy that knows a different moment: the state before the founding.
    Game h;
    sim::Simulation t(h.world, h.buildings, h.figures);
    run(t, 2'000);
    sim::ComputerPlayers copy(s);
    {
        const auto earlier = saved(t.computers());
        core::ByteReader in(earlier);
        REQUIRE(copy.load(in));
    }
    const auto before = saved(copy);
    REQUIRE(before != bytes);

    // Every truncation of the good state.
    std::size_t accepted = 0;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        core::ByteReader in(std::span(bytes.data(), n));
        accepted += copy.load(in) ? 1U : 0U;
    }
    CHECK(accepted == 0);
    CHECK(saved(copy) == before);

    // An unknown version, and garbage after the right one.
    auto version = bytes;
    version[0] = 0x63;
    {
        core::ByteReader in(version);
        CHECK(!copy.load(in));
    }
    for (const std::uint8_t fill : {std::uint8_t{0xFF}, std::uint8_t{0x7F}, std::uint8_t{0x01}}) {
        std::vector<std::uint8_t> garbage(4096, fill);
        std::copy_n(bytes.begin(), 4, garbage.begin());
        core::ByteReader in(garbage);
        CHECK(!copy.load(in));
    }
    CHECK(saved(copy) == before);
}

TEST_CASE("ai: reset takes the computers from the world, and only them") {
    Game g;
    // A pirate slot too: the pirates are run by the same machinery.
    std::vector<sim::Player> players = g.world.players();
    players.push_back(sim::Player{});
    players.back().role = sim::PlayerRole::Pirates;
    g.world.set_players(players);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, 5'000);

    sim::ComputerPlayers fresh(s);
    fresh.reset();
    const auto view = fresh.view();
    REQUIRE(view.size() == 2);
    CHECK(view[0].player == kComputer);
    CHECK(view[1].player == 4);
    for (const auto& v : view) {
        CHECK(v.player != kHuman);
        CHECK(v.player != kTrader);
        CHECK(v.player != kNatives);
    }
    // The computer's city is known again, nothing is half done.
    CHECK(view[0].cities == 1);
    REQUIRE(view[0].plans.size() == 1);
    CHECK(view[0].plans[0].island == 1);
    CHECK(!view[0].settling);
    CHECK(view[0].stack.empty());
    CHECK(view[1].cities == 0);
}

TEST_CASE("ai: humans, the trader and the natives are left alone") {
    Game g(false);
    // The computer's slot is played by a human now; a human with a ship and money too.
    g.world.player(kComputer)->role = sim::PlayerRole::Human;
    sim::Ship& ship = g.add_ship(kHuman, 86, 45);
    const std::uint32_t id = ship.id;
    const std::uint32_t x = ship.x;
    const std::uint32_t y = ship.y;
    const std::vector<sim::Player> before = g.world.players();
    sim::Simulation s(g.world, g.buildings, g.figures);
    CHECK(s.computers().view().empty());
    run(s, 120'000);
    CHECK(s.computers().view().empty());
    CHECK(g.world.cities().empty());
    CHECK(!s.computers().directs(id));
    const sim::Ship* after = g.world.ship(id);
    REQUIRE(after != nullptr);
    CHECK(after->x == x);
    CHECK(after->y == y);
    for (std::size_t p = 0; p < before.size(); ++p) {
        CHECK(g.world.players()[p].money == before[p].money);
    }
}

TEST_CASE("ai: a computer without a ship founds nothing and harms nobody") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::int32_t money = g.world.players()[kComputer].money;
    run(s, 60'000);
    CHECK(g.world.cities().empty());
    CHECK(g.world.ships().empty());
    CHECK(g.world.players()[kComputer].money == money);
    CHECK(g.world.players()[kHuman].money == 10000);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(view[0].cities == 0);
    CHECK(!view[0].settling);
    CHECK(!s.computers().directs(0));
    CHECK(!s.computers().directs(12345));
}

TEST_CASE("ai: a computer without money founds its city all the same") {
    // The original gives a computer short of money its founding money (task 4).
    Game g(true, 0);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, 30'000);
    CHECK(g.world.players()[kComputer].money >= 0);
    CHECK(g.world.players()[kHuman].money == 10000);
}

TEST_CASE("ai: a city founded next to a young colony is remembered against the founder") {
    struct Case {
        sim::PlayerId founder = kHuman;
        std::uint16_t island = 0;  ///< where the new city lies
        bool colony = true;        ///< the computer has a colony on the islet
        std::uint32_t people = 0;  ///< its inhabitants (two classes)
        bool war = false;
    };
    // The computer's home city on the big island (island 1), a colony on the islet (island 0); then
    // somebody founds a city.
    const auto remembered = [](const Case& k) {
        Game g(false);
        const auto add_city = [&](std::uint16_t island, std::uint8_t index, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.index = index;
            c.owner = owner;
            c.inhabitants = {people - people / 2, people / 2};
            g.world.add_city(c);
        };
        add_city(1, 0, kComputer, 0);
        if (k.colony) {
            add_city(0, 0, kComputer, k.people);
        }
        if (k.war) {
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        add_city(k.island, 1, k.founder, 0);
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ComputerPlayers ai(s);
        ai.reset();
        ai.city_founded(k.island, 1);
        const auto& events = g.world.diplomacy().events(kComputer);
        return std::any_of(events.begin(), events.end(), [&](const sim::DiplomacyEvent& e) {
            return e.type == sim::DiplomacyEventType::Settled && e.partner == k.founder;
        });
    };

    const auto with = [](auto&& change) {
        Case k;
        change(k);
        return k;
    };
    CHECK(remembered(Case{}));                                         // at peace with the founder
    CHECK(remembered(with([](Case& k) { k.people = 9; })));            // nine are still a young colony
    CHECK(!remembered(with([](Case& k) { k.people = 10; })));          // ten are not
    CHECK(!remembered(with([](Case& k) { k.war = true; })));           // at war: nothing more to hold against him
    CHECK(!remembered(with([](Case& k) { k.island = 1; })));           // on the island of the home city
    CHECK(!remembered(with([](Case& k) { k.founder = kComputer; })));  // its own second city there
    CHECK(!remembered(with([](Case& k) { k.colony = false; })));       // no colony of the computer there
}

TEST_CASE("ai: a founding the computers do not know of is ignored") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ComputerPlayers ai(s);
    ai.reset();
    const auto before = saved(ai);
    ai.city_founded(1, 0);   // no such city
    ai.city_founded(99, 7);  // no such island
    CHECK(saved(ai) == before);
    for (const auto& e : g.world.diplomacy().events(kComputer)) {
        CHECK(e.type == sim::DiplomacyEventType::None);
    }
}
