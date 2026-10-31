// SPDX-License-Identifier: GPL-3.0-or-later
// Scenarios of content packages (engine/assets/include/o1602/assets/package_scenarios.hpp,
// docs/modding/scenarios.md).

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/high_scores.hpp"
#include "o1602/assets/package_scenarios.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// A package folder with a map and a script.
struct Package {
    assets::ModPackage package;
    explicit Package(const char* name) {
        package.id = "isles";
        package.name = "isles";
        package.folder = test::temp_path(name);
        std::filesystem::remove_all(package.folder);
        std::filesystem::create_directories(package.folder / "maps");
        std::ofstream(package.folder / "maps" / "north.szs") << "map";
        std::ofstream(package.folder / "north.lua") << "-- script";
    }
    ~Package() {
        std::error_code ec;
        std::filesystem::remove_all(package.folder, ec);
    }
    [[nodiscard]] std::vector<assets::PackageScenario> read(const std::string& text,
                                                            std::vector<std::string>& warnings) const {
        std::vector<assets::PackageScenario> scenarios;
        assets::read_scenarios_file(scenarios, package, text, warnings);
        return scenarios;
    }
};

}  // namespace

TEST_CASE("package scenarios: a scenario names its map, texts, difficulty, money, goals and script") {
    const Package p("o1602_scenarios_read");
    std::vector<std::string> warnings;
    const auto scenarios = p.read(
        "[north]\n"
        "file = \"maps/north.szs\"\n"
        "script = \"north.lua\"\n"
        "stars = 3\n"
        "money = 25000\n"
        "name.en = \"The North\"\n"
        "name.de = \"Der Norden\"\n"
        "briefing.en = \"Settle the north.\"\n"
        "goal.inhabitants = 2000\n"
        "goal.cities = 2\n"
        "goal.castle = true\n"
        "goal.goods.tools = 50\n"
        "goal.goods.amber = 10\n"
        "goal.city.inhabitants = 800\n"
        "goal.city.class = 3\n"
        "goal.city.count = 300\n",
        warnings);
    CHECK(warnings.empty());
    REQUIRE(scenarios.size() == 1);
    const assets::PackageScenario& s = scenarios[0];
    CHECK(s.id == "isles:north");
    CHECK(s.package == "isles");
    CHECK(s.file == p.package.folder / "maps" / "north.szs");
    CHECK(s.script == p.package.folder / "north.lua");
    CHECK(s.stars == 3);
    CHECK(s.money == 25000);
    CHECK(s.name("de") == "Der Norden");
    CHECK(s.name("fr") == "The North");
    CHECK(s.briefing("de") == "Settle the north.");
    CHECK(s.goals.inhabitants == 2000);
    CHECK(s.goals.cities == 2);
    CHECK(s.goals.castle);
    CHECK(!s.goals.cathedral);
    REQUIRE(s.goals.goods.size() == 2);
    // An original's good keeps its name under "anno1602", any other is the package's own.
    CHECK(s.goals.goods[0].first == "anno1602:tools");
    CHECK(s.goals.goods[0].second == 50);
    CHECK(s.goals.goods[1].first == "isles:amber");
    REQUIRE(s.goals.city.has_value());
    CHECK(s.goals.city->inhabitants == 800);
    CHECK(s.goals.city->resident_class == 3);
    CHECK(s.goals.city->count == 300);
    CHECK(assets::find_scenario(scenarios, "isles:north") == &scenarios[0]);
    CHECK(assets::find_scenario(scenarios, "isles:south") == nullptr);
}

TEST_CASE("package scenarios: a scenario with a wrong field, no map or a map outside is passed over") {
    const Package p("o1602_scenarios_wrong");
    std::vector<std::string> warnings;
    const auto scenarios = p.read(
        "[ok]\nfile = \"maps/north.szs\"\n"
        "[no_map]\nstars = 1\n"
        "[outside]\nfile = \"../north.szs\"\n"
        "[not_a_map]\nfile = \"north.lua\"\n"
        "[bad_stars]\nfile = \"maps/north.szs\"\nstars = 9\n"
        "[three_goods]\nfile = \"maps/north.szs\"\ngoal.goods.tools = 1\ngoal.goods.wood = 1\ngoal.goods.ore = 1\n"
        "[unknown]\nfile = \"maps/north.szs\"\ngoal.fame = 3\n"
        "[Upper]\nfile = \"maps/north.szs\"\n",
        warnings);
    REQUIRE(scenarios.size() == 1);
    CHECK(scenarios[0].id == "isles:ok");
    CHECK(!scenarios[0].goals.any());
    // Every scenario left out says why, with the file and the line.
    CHECK(warnings.size() >= 7);
    for (const std::string& w : warnings) {
        CHECK(w.starts_with("isles/scenarios.toml"));
    }
}

TEST_CASE("package scenarios: the installed packages' scenarios in their order") {
    const Package p("o1602_scenarios_load");
    std::ofstream(p.package.folder / "scenarios.toml", std::ios::binary)
        << "\xEF\xBB\xBF[north]\nfile = \"maps/north.szs\"\n";
    assets::ModPackage other = p.package;
    other.id = "empty";
    other.folder = p.package.folder / "maps";  // no scenarios.toml
    std::vector<std::string> warnings;
    const std::vector<assets::ModPackage> packages{other, p.package};
    const auto scenarios = assets::load_package_scenarios(packages, warnings);
    CHECK(warnings.empty());
    REQUIRE(scenarios.size() == 1);
    CHECK(scenarios[0].id == "isles:north");
}

TEST_CASE("package scenarios: the scenario's money and goals become every human's mission") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(3);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    players[2].role = sim::PlayerRole::Human;
    for (sim::Player& p : players) {
        p.money = 1000;
    }
    world.set_players(players);

    assets::PackageScenario s;
    s.id = "isles:north";
    s.money = 50'000;
    s.briefings.emplace_back("en", "Settle.");
    s.briefings.emplace_back("de", "Siedelt.");
    s.goals.inhabitants = 1500;
    s.goals.balance = 200;
    s.goals.defeated = 1;
    s.goals.cathedral = true;
    s.goals.goods = {{"anno1602:tools", 40}, {"isles:amber", 5}};
    s.goals.city = assets::ScenarioCityGoal{600, 2, 100};
    std::vector<std::string> warnings;
    sim::apply_package_scenario(world, s, "de", &warnings);

    // The good the game does not know is left out with a warning.
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("isles:amber") != std::string::npos);
    CHECK(world.players()[0].money == 50'000);
    CHECK(world.players()[1].money == 1000);
    CHECK(world.players()[2].money == 50'000);
    REQUIRE(world.missions().size() == 1);
    CHECK(world.players()[0].mission == 0);
    CHECK(world.players()[2].mission == 0);
    CHECK(world.players()[1].mission == sim::kNoMission);
    const sim::Mission& m = world.missions()[0];
    CHECK(m.text == "Siedelt.");
    CHECK(m.goals == (sim::goal::kInhabitants | sim::goal::kBalance | sim::goal::kDefeatedCount |
                      sim::goal::kCathedral | sim::goal::kGoods1 | sim::goal::kCity1));
    CHECK(m.min_inhabitants == 1500);
    CHECK(m.min_balance == 200);
    CHECK(m.min_defeated == 1);
    CHECK(m.goods[0].ware == assets::ware::kTools);
    CHECK(m.goods[0].amount == 40U * 32U);
    CHECK(m.cities[0] == (sim::Mission::CityGoal{600, 2, 100}));
}

TEST_CASE("package scenarios: without goals and money the map's missions stay") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    players[0].money = 1234;
    sim::Mission own;
    own.goals = sim::goal::kCastle;
    world.set_missions({own});
    players[0].mission = 0;
    world.set_players(players);
    assets::PackageScenario s;
    s.id = "isles:north";
    sim::apply_package_scenario(world, s, "en", nullptr);
    CHECK(world.players()[0].money == 1234);
    REQUIRE(world.missions().size() == 1);
    CHECK(world.missions()[0].goals == sim::goal::kCastle);
}

TEST_CASE("package scenarios: the scenario a game was started from survives the own savegame") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    world.set_players(players);
    const assets::BuildingTable buildings;
    {
        auto bytes = sim::write_game(world, buildings);
        REQUIRE(bytes.ok());
        auto back = sim::read_game(*bytes, buildings);
        REQUIRE(back.ok());
        CHECK(back->package_scenario().empty());
    }
    world.set_package_scenario("isles:north");
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto back = sim::read_game(*bytes, buildings);
    REQUIRE(back.ok());
    CHECK(back->package_scenario() == "isles:north");
}

TEST_CASE("package campaigns: parts name scenarios of the package, in order, without gaps") {
    const Package p("o1602_campaigns_read");
    std::vector<std::string> warnings;
    const auto scenarios = p.read(
        "[north]\nfile = \"maps/north.szs\"\n"
        "[south]\nfile = \"maps/north.szs\"\n",
        warnings);
    REQUIRE(scenarios.size() == 2);
    std::vector<assets::PackageCampaign> campaigns;
    assets::read_campaigns_file(campaigns, p.package, scenarios,
                                "[frost]\n"
                                "name.en = \"Frost\"\n"
                                "briefing.de = \"Zwei Winter.\"\n"
                                "stars = 2\n"
                                "carry.money = true\n"
                                "part.2 = \"south\"\n"
                                "part.1 = \"north\"\n"
                                "[gap]\npart.1 = \"north\"\npart.3 = \"south\"\n"
                                "[none]\nstars = 1\n"
                                "[unknown]\npart.1 = \"east\"\n"
                                "[too_far]\npart.6 = \"north\"\n",
                                warnings);
    REQUIRE(campaigns.size() == 1);
    const assets::PackageCampaign& c = campaigns[0];
    CHECK(c.id == "isles:frost");
    CHECK(c.package == "isles");
    CHECK(c.name("de") == "Frost");
    CHECK(c.briefing("de") == "Zwei Winter.");
    CHECK(c.stars == 2);
    CHECK(c.carry_money);
    REQUIRE(c.parts.size() == 2);
    CHECK(c.parts[0] == "isles:north");
    CHECK(c.parts[1] == "isles:south");
    CHECK(warnings.size() >= 4);
    CHECK(assets::find_campaign(campaigns, "isles:frost") == &campaigns[0]);
    CHECK(assets::find_campaign(campaigns, "isles:gap") == nullptr);
}

TEST_CASE("package campaigns: won parts open the next, kept in the progress file beside the original's") {
    assets::CampaignProgress progress;
    CHECK(progress.package_parts_open("isles:frost") == 0);
    CHECK(progress.record_package_win("isles:frost", 0));
    CHECK(!progress.record_package_win("isles:frost", 0));  // nothing further
    CHECK(progress.package_parts_open("isles:frost") == 1);
    CHECK(progress.record_win(2, 1, 0xFF));
    progress.package_campaigns["my mods:long way"] = 3;  // a package's id may hold spaces
    const assets::CampaignProgress back = assets::parse_progress(assets::write_progress(progress));
    CHECK(back.package_campaigns == progress.package_campaigns);
    CHECK(back.campaigns == progress.campaigns);
    // Lines that are not whole are passed over.
    CHECK(assets::parse_progress("package 2\npackage x isles:a\npackage 1 \n").package_campaigns.empty());
}

TEST_CASE("package campaigns: what a won part leaves to the next comes back from its file") {
    CHECK(assets::campaign_carry_file("isles:frost", 0) == std::filesystem::path("isles_frost.1.carry"));
    assets::CampaignCarry carry;
    carry.money = -1234567890123;
    carry.table = {1, 2, 3, 4};
    const auto bytes = assets::write_campaign_carry(carry);
    CHECK(assets::read_campaign_carry(bytes) == carry);
    const std::vector<std::uint8_t> other{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    CHECK(!assets::read_campaign_carry(other));
    CHECK(!assets::read_campaign_carry(std::span(bytes).first(6)));
}

TEST_CASE("package campaigns: the part and the scripts' campaign tables survive the own savegame") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    world.set_players(players);
    world.set_package_scenario("isles:south");
    world.set_campaign_part(sim::World::CampaignPart{"isles:frost", 1, {7, 8, 9}});
    sim::ScriptState plain;
    plain.package = "isles";
    plain.data = {1};
    sim::ScriptState part;
    part.package = "isles:south";
    part.data = {2};
    part.campaign = {3, 4};
    world.set_script_states({plain, part});
    const assets::BuildingTable buildings;
    auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto back = sim::read_game(*bytes, buildings);
    REQUIRE(back.ok());
    CHECK(back->campaign_part() == world.campaign_part());
    CHECK(back->script_states() == world.script_states());
}

TEST_CASE("package scenarios: slots of the map go to the factions the scenario names") {
    const Package p("o1602_scenarios_slots");
    std::vector<std::string> warnings;
    const auto read = p.read(
        "[north]\nfile = \"maps/north.szs\"\n"
        "slot.2 = \"corsairs\"\nslot.3 = \"anno1602:natives\"\nslot.0 = \"corsairs\"\nslot.x = \"corsairs\"\n"
        "[south]\nfile = \"maps/north.szs\"\nslot.4 = \"corsairs\"\n",
        warnings);
    REQUIRE(read.size() == 1);  // the north names slots that are none
    CHECK(read[0].id == "isles:south");
    CHECK(warnings.size() == 3);
    REQUIRE(read[0].slots.size() == 1);
    CHECK(read[0].slots[0].first == 3);
    CHECK(read[0].slots[0].second == "isles:corsairs");

    sim::World world(40, 40);
    std::vector<sim::Faction> factions = sim::base_factions();
    sim::Faction corsairs = factions[sim::faction::kPirates];
    corsairs.id = "isles:corsairs";
    factions.push_back(corsairs);
    sim::Faction league = factions[sim::faction::kSettlers];
    league.id = "isles:league";
    factions.push_back(league);
    std::vector<sim::Player> players(5);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    sim::make_neutral(players[2], sim::faction::kNatives);
    world.set_players(players);
    world.set_factions(factions);
    assets::PackageScenario s;
    s.id = "isles:south";
    s.slots = {{0, "isles:corsairs"},  // a person's slot stays the settlers'
               {1, "isles:corsairs"},
               {2, "isles:league"},
               {3, "isles:nobody"},
               {9, "isles:league"}};
    warnings.clear();
    sim::apply_package_scenario(world, s, "en", &warnings);
    CHECK(warnings.size() == 3);
    CHECK(world.players()[0].faction == sim::faction::kSettlers);
    CHECK(world.players()[0].role == sim::PlayerRole::Human);
    CHECK(world.players()[1].faction == 4);
    CHECK(world.players()[1].role == sim::PlayerRole::Neutral);
    CHECK(world.players()[2].faction == 5);
    CHECK(world.players()[2].role == sim::PlayerRole::Computer);  // the settlers' behaviour: the computer steers
}
