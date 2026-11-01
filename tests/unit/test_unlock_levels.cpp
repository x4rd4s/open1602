// SPDX-License-Identifier: GPL-3.0-or-later
// Unlock levels as data: levels.toml changes the original's and adds levels after them, buildings
// of a package need them, the players reach them and a game keeps them (engine/assets/include/
// o1602/assets/unlock_levels.hpp, docs/modding/levels.md).

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/assets/unlock_levels.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// Two classes, the market (level 1), level 2A (16, settlers) and level 5B (32), a house and a
/// chapel that needs level 2.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"HAUS_PRODTYP", 2}, {"BGRUPPE", 5}, {"BAUINFRA", 7}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "EndObj\n"
        "Objekt: BAUINFRA\n"
        "  @Nummer: 1\n  BGruppe: 0\n  Minwohn: 30\n"
        "  @Nummer: 16\n  BGruppe: 1\n  Minwohn: 50\n"
        "  @Nummer: 32\n  BGruppe: 1\n  Minwohn: 2500\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20509\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 1\n  EndObj\n"
        "  @Nummer: 1\n  Id: 20531\n  Gfx: 20\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n    Bauinfra: 2\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("levels: a package changes the original's levels and adds some after them") {
    const auto buildings = make_buildings();
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    REQUIRE(levels.size() == 33);
    CHECK(levels[16].min_inhabitants == 50);
    std::vector<std::string> warnings;
    const bool changed = assets::apply_levels_file(levels, "spice",
                                                   "[\"anno1602:level_2a\"]\n"
                                                   "min_inhabitants = 80\n"
                                                   "[harbour]\n"
                                                   "class = \"anno1602:settlers\"\n"
                                                   "min_inhabitants = 400\n"
                                                   "[\"anno1602:1\"]\n"
                                                   "class = \"anno1602:settlers\"\n",
                                                   warnings, buildings.resident_classes());
    CHECK(changed);
    CHECK(warnings.empty());
    REQUIRE(levels.size() == 34);
    CHECK(levels[16].min_inhabitants == 80);
    CHECK(levels[16].resident_class == 1);
    CHECK(levels[1].resident_class == 1);
    CHECK(levels[1].min_inhabitants == 30);
    CHECK(levels[33].id == "spice:harbour");
    CHECK(levels[33].resident_class == 1);
    CHECK(levels[33].min_inhabitants == 400);

    CHECK(assets::level_of(levels, "spice:harbour") == 33);
    CHECK(assets::level_of(levels, "anno1602:market") == 1);
    CHECK(assets::level_of(levels, "anno1602:level_5b") == 32);
    CHECK(assets::level_of(levels, "anno1602:16") == 16);
    CHECK(!assets::level_of(levels, "anno1602:33"));  // a package's level has its own id
    CHECK(!assets::level_of(levels, "anno1602:0"));
    CHECK(!assets::level_of(levels, "spice:nothing"));

    // A package's level is changed by its id, as the original's are.
    warnings.clear();
    assets::apply_levels_file(levels, "harder", "[\"spice:harbour\"]\nmin_inhabitants = 500\n", warnings,
                              buildings.resident_classes());
    CHECK(warnings.empty());
    CHECK(levels[33].min_inhabitants == 500);
}

TEST_CASE("levels: a table that is wrong is passed over with a warning") {
    const auto buildings = make_buildings();
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    std::vector<std::string> warnings;
    const bool changed = assets::apply_levels_file(levels, "spice",
                                                   "[\"anno1602:level_9z\"]\n"  // line 1: no such level
                                                   "min_inhabitants = 1\n"
                                                   "[Harbour]\n"  // line 3: upper case
                                                   "[harbour]\n"  // line 4: no class
                                                   "min_inhabitants = 1\n"
                                                   "[\"anno1602:market\"]\n"
                                                   "class = \"anno1602:kings\"\n"  // line 7: no such class
                                                   "[\"anno1602:chapel\"]\n"
                                                   "min_inhabitants = -1\n"  // line 9: below its limit
                                                   "colour = 3\n",           // line 10: no field of a level
                                                   warnings, buildings.resident_classes());
    CHECK(!changed);
    CHECK(levels == buildings.infra_levels());
    REQUIRE(warnings.size() == 6);
    CHECK(warnings[0] ==
          "spice/levels.toml line 1: [anno1602:level_9z]: names none of the original's unlock levels "
          "(anno1602:<name or number>) and none a package added");
    CHECK(warnings[1] ==
          "spice/levels.toml line 3: [Harbour]: a level's name holds only lower case letters, digits and '_'");
    CHECK(warnings[2] == "spice/levels.toml line 4: spice:harbour needs a 'class' and 'min_inhabitants'");
    CHECK(warnings[3] ==
          "spice/levels.toml line 7: 'class' names no class (anno1602:<name or number> or <package>:<name>)");
    CHECK(warnings[4] == "spice/levels.toml line 9: 'min_inhabitants' is a number from 0 to 10000000");
    CHECK(warnings[5] == "spice/levels.toml line 10: 'colour' is no field of an unlock level and is passed over");

    // At most 255 levels.
    warnings.clear();
    std::string many;
    for (int k = 0; k < 224; ++k) {
        many += "[l" + std::to_string(k) + "]\nclass = \"anno1602:0\"\nmin_inhabitants = 1\n";
    }
    assets::apply_levels_file(levels, "spice", many, warnings, buildings.resident_classes());
    CHECK(levels.size() == assets::kMaxUnlockLevel + 1);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "spice/levels.toml line 670: spice:l223: a game has at most 255 unlock levels");
}

TEST_CASE("levels: a building of a package needs a level a package added, which a city reaches") {
    auto buildings = make_buildings();
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    std::vector<std::string> warnings;
    assets::apply_levels_file(levels, "spice", "[harbour]\nclass = \"anno1602:settlers\"\nmin_inhabitants = 400\n",
                              warnings, buildings.resident_classes());
    std::vector<assets::OwnBuilding> own;
    assets::apply_own_buildings_file(buildings, own, "spice",
                                     "[dock]\n"
                                     "like = \"anno1602:20531\"\n"
                                     "level = \"spice:harbour\"\n"
                                     "[stall]\n"
                                     "like = \"anno1602:20531\"\n"
                                     "level = \"\"\n"
                                     "[pier]\n"
                                     "like = \"anno1602:20531\"\n"
                                     "level = \"spice:nothing\"\n",  // line 9: no such level
                                     warnings, {}, {}, levels);
    REQUIRE(own.size() == 2);
    CHECK(buildings.by_id(own[0].number)->infra == 33);
    CHECK(buildings.by_id(own[1].number)->infra == 0);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] ==
          "spice/own_buildings.toml line 9: 'level' names no unlock level (anno1602:<name or number> or "
          "<package>:<name>, \"\" for always)");

    // The game's table takes the levels; a city of 400 settlers reaches the new one.
    const auto original = buildings;
    sim::apply_infra_levels(buildings, original, levels);
    REQUIRE(buildings.infra_levels().size() == 34);
    sim::World world(60, 40);
    world.set_players(std::vector<sim::Player>(1));
    sim::City city;
    city.owner = 0;
    city.inhabitants = {0, 399};
    sim::unlock_levels(world, buildings, city, nullptr);
    CHECK(world.player(0)->unlocked(16));
    CHECK(!world.player(0)->unlocked(33));
    city.inhabitants = {0, 400};
    std::vector<sim::CityEvent> events;
    sim::unlock_levels(world, buildings, city, &events);
    CHECK(world.player(0)->unlocked(33));
    CHECK(world.player(0)->infra_more[0] == 1U);
    REQUIRE(!events.empty());
    CHECK(events.back().infra == 33);

    sim::apply_infra_levels(buildings, original, {});
    CHECK(buildings.infra_levels().size() == 33);
}

TEST_CASE("levels: a player holds levels up to 255") {
    sim::Player p;
    p.set_unlocked(33, true);
    p.set_unlocked(255, true);
    p.set_unlocked(256, true);  // no such level
    CHECK(p.unlocked(33));
    CHECK(p.unlocked(255));
    CHECK(!p.unlocked(254));
    CHECK(!p.unlocked(256));
    CHECK(p.infra_mask == 0);
    CHECK(p.infra_more[0] == 1U);
    CHECK(p.infra_more[6] == 1U << 30);
    p.set_unlocked(33, false);
    CHECK(!p.unlocked(33));
}

TEST_CASE("levels: a game keeps its levels and what its players reached") {
    const auto buildings = make_buildings();
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    std::vector<std::string> warnings;
    assets::apply_levels_file(levels, "spice",
                              "[harbour]\nclass = \"anno1602:settlers\"\nmin_inhabitants = 400\n"
                              "message = \"spice:harbour_open\"\n"
                              "[\"anno1602:market\"]\nmin_inhabitants = 60\nmessage = \"\"\n",
                              warnings, buildings.resident_classes());
    REQUIRE(warnings.empty());
    REQUIRE(levels[33].message == std::optional<std::string>("spice:harbour_open"));
    REQUIRE(levels[1].message == std::optional<std::string>(""));
    CHECK(!levels[2].message);
    sim::World world(60, 40);
    world.set_players(std::vector<sim::Player>(2));
    world.set_infra_levels(levels);
    world.player(1)->set_unlocked(33, true);
    const auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, buildings);
    REQUIRE(read.ok());
    CHECK(read->infra_levels() == levels);
    CHECK(!read->player(0)->unlocked(33));
    CHECK(read->player(1)->unlocked(33));

    // A world without levels of its own writes none.
    const auto plain = sim::write_game(sim::World(60, 40), buildings);
    REQUIRE(plain.ok());
    auto read_plain = sim::read_game(*plain, buildings);
    REQUIRE(read_plain.ok());
    CHECK(read_plain->infra_levels().empty());

    // The original's savegame has no room for a 33rd level.
    CHECK(!sim::savegame_chunks(world, buildings).ok());
}

TEST_CASE("levels: a level names the message a player gets on reaching it") {
    const auto buildings = make_buildings();
    std::vector<assets::InfraLevel> levels = buildings.infra_levels();
    std::vector<std::string> warnings;
    assets::apply_levels_file(levels, "spice",
                              "[\"anno1602:tavern\"]\n"
                              "message = \"anno1602:0x3D\"\n"
                              "[\"anno1602:church\"]\n"
                              "message = \"church\"\n"  // line 4: no package
                              "[\"anno1602:school\"]\n"
                              "message = 3\n",  // line 6: no text
                              warnings, buildings.resident_classes());
    CHECK(levels[4].message == std::optional<std::string>("anno1602:0x3D"));
    CHECK(!levels[5].message);
    CHECK(!levels[3].message);
    REQUIRE(warnings.size() == 2);
    const std::string expected = "'message' names no message (<package>:<name> or anno1602:<number>, \"\" for none)";
    CHECK(warnings[0] == "spice/levels.toml line 4: " + expected);
    CHECK(warnings[1] == "spice/levels.toml line 6: " + expected);
}
