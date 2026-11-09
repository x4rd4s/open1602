// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>

#include "../test_framework.hpp"

#include "o1602/editor/original_check.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/editor/scenario_rules.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// A world of the original's seven slots: four players (the second a computer with 5000 gold), the
/// trader, the pirates and the natives.
sim::World seven_slots() {
    sim::World world(40, 40);
    std::vector<sim::Player> players(7);
    for (std::size_t i = 0; i < 4; ++i) {
        players[i].role = i == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        players[i].banner = static_cast<sim::BannerId>(i);
        players[i].money = i == 1 ? 5000 : 20000;
    }
    sim::make_neutral(players[4], sim::faction::kTrader);
    sim::make_neutral(players[5], sim::faction::kPirates);
    sim::make_neutral(players[6], sim::faction::kNatives);
    world.set_players(players);
    return world;
}

}  // namespace

TEST_CASE("editor players: the slots of the humans and computers, and their values changed") {
    sim::World world = seven_slots();
    CHECK((editor::player_slots(world) == std::vector<sim::PlayerId>{0, 1, 2, 3}));
    CHECK(editor::player_banners().size() == sim::kMaxPlayers);

    editor::PlayerValues v = editor::player_values(world.players()[2]);
    v.role = sim::PlayerRole::Human;
    v.name = "Merlin";
    v.money = 12000;
    v.monuments_allowed = 2;
    v.arches_allowed = 1;
    v.banner = 1;  // the second player's: they swap
    auto op = editor::set_player(world, 2, v);
    REQUIRE(op.ok());
    (*op)->apply(world);
    const sim::Player& p = world.players()[2];
    CHECK(p.role == sim::PlayerRole::Human);
    CHECK(p.name == "Merlin");
    CHECK(p.money == 12000);
    CHECK(p.monuments_allowed == 2);
    CHECK(p.arches_allowed == 1);
    CHECK(p.banner == 1);
    CHECK(world.players()[1].banner == 2);
    (*op)->revert(world);
    CHECK(world.players()[2].name.empty());
    CHECK(world.players()[1].banner == 1);

    // Refused: the neutral slots, a role that is none of the two, a foreign banner, too much money.
    CHECK(!editor::set_player(world, 5, v).ok());
    v.role = sim::PlayerRole::Neutral;
    CHECK(!editor::set_player(world, 2, v).ok());
    v.role = sim::PlayerRole::Computer;
    v.banner = 5;
    CHECK(!editor::set_player(world, 2, v).ok());
    v.banner = 2;
    v.money = editor::kMaxStartMoney + 1;
    CHECK(!editor::set_player(world, 2, v).ok());
}

TEST_CASE("editor players: slots added behind the neutral ones up to twelve, and the last taken away") {
    sim::World world = seven_slots();
    // The first four stay.
    CHECK(!editor::remove_last_player(world).ok());
    for (std::size_t n = 4; n < sim::kMaxPlayers; ++n) {
        auto op = editor::add_player(world);
        REQUIRE(op.ok());
        (*op)->apply(world);
    }
    CHECK(editor::player_slots(world).size() == sim::kMaxPlayers);
    CHECK(!editor::add_player(world).ok());
    const sim::Player& fifth = world.players()[7];
    CHECK(fifth.role == sim::PlayerRole::Computer);
    CHECK(fifth.money == 5000);  // as the first computer
    CHECK(fifth.banner == sim::kFirstExtraBanner);
    CHECK(world.players()[4].role == sim::PlayerRole::Neutral);  // the trader keeps his slot

    // The last goes while it owns nothing.
    world.add_ship(sim::make_ship("", 3, 3, 21, sim::player_slot(sim::kMaxPlayers - 1)));
    CHECK(!editor::remove_last_player(world).ok());
    world.ships().clear();
    auto gone = editor::remove_last_player(world);
    REQUIRE(gone.ok());
    (*gone)->apply(world);
    CHECK(editor::player_slots(world).size() == sim::kMaxPlayers - 1);
    CHECK(world.players().size() == std::size_t{sim::player_slot(sim::kMaxPlayers - 2)} + 1);
}

TEST_CASE("editor players: a map of four slots gets its fifth player behind three empty ones") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(4);
    world.set_players(players);
    auto op = editor::add_player(world);
    REQUIRE(op.ok());
    (*op)->apply(world);
    REQUIRE(world.players().size() == 8);
    CHECK(world.players()[4].status == sim::PlayerStatus::Absent);
    CHECK(world.players()[7].status == sim::PlayerStatus::Playing);
    CHECK(editor::player_slots(world).size() == 5);
    // The empty slots are not players with a start.
    for (const editor::LimitProblem& p : editor::original_limit_problems(world)) {
        if (p.kind == editor::LimitProblem::Kind::Players) {
            CHECK(p.count == 5);
        }
    }
    auto gone = editor::remove_last_player(world);
    REQUIRE(gone.ok());
    (*gone)->apply(world);
    CHECK(world.players().size() == 4);  // the empty slots go with it
}

TEST_CASE("editor players: relations at the start are set on both sides and kept when a player comes") {
    sim::World world(40, 40);
    std::vector<sim::Player> players(4);
    for (auto& p : players) {
        p.role = sim::PlayerRole::Computer;
    }
    world.set_players(players);
    CHECK((editor::relations(world, 0, 1) == editor::Relations{true, false}));
    world.diplomacy().relation(0, 1).damage = 77;
    auto war = editor::set_relations(world, 0, 1, editor::Relations{false, false});
    REQUIRE(war.ok());
    (*war)->apply(world);
    CHECK((editor::relations(world, 0, 1) == editor::Relations{false, false}));
    CHECK((editor::relations(world, 1, 0) == editor::Relations{false, false}));
    CHECK(world.diplomacy().relation(0, 1).damage == 0);
    auto trade = editor::set_relations(world, 2, 3, editor::Relations{true, true});
    REQUIRE(trade.ok());
    (*trade)->apply(world);
    CHECK(!editor::set_relations(world, 2, 2, {}).ok());
    CHECK(!editor::set_relations(world, 2, 5, {}).ok());
    CHECK(!editor::set_relations(world, 0, 2, editor::Relations{false, true}).ok());

    auto added = editor::add_player(world);
    REQUIRE(added.ok());
    (*added)->apply(world);
    CHECK((editor::relations(world, 1, 0) == editor::Relations{false, false}));
    CHECK((editor::relations(world, 3, 2) == editor::Relations{true, true}));
    CHECK((editor::relations(world, 0, 7) == editor::Relations{true, false}));
    auto gone = editor::remove_last_player(world);
    REQUIRE(gone.ok());
    (*gone)->apply(world);
    CHECK((editor::relations(world, 0, 1) == editor::Relations{false, false}));
    CHECK((editor::relations(world, 2, 3) == editor::Relations{true, true}));
}

TEST_CASE("editor players: the start towards the natives, the pirates and the trader") {
    using editor::Neutral;
    using editor::NeutralRelations;
    sim::World world = seven_slots();
    CHECK(
        (editor::neutral_parties(world) == std::vector<Neutral>{Neutral::Natives, Neutral::Pirates, Neutral::Trader}));
    CHECK(!editor::has_native_villages(world));
    sim::City village;
    village.owner = 6;
    world.add_city(village);
    CHECK(editor::has_native_villages(world));
    // As a new game starts: trade and peace with the natives, war with the pirates, the trader calls.
    CHECK((editor::neutral_relations(world, 0, Neutral::Natives) == NeutralRelations{true, true, 200}));
    CHECK(!editor::neutral_relations(world, 0, Neutral::Pirates).peace);
    CHECK(editor::neutral_relations(world, 0, Neutral::Pirates).trade);
    CHECK((editor::neutral_relations(world, 2, Neutral::Trader) == NeutralRelations{true, true, 200}));

    // The natives: peace without trade and more goodwill in their village; it is undone whole.
    world.diplomacy().relation(6, 1).damage = 50;
    auto natives = editor::set_neutral_relations(world, 1, Neutral::Natives, NeutralRelations{true, false, 1000});
    REQUIRE(natives.ok());
    (*natives)->apply(world);
    CHECK((editor::neutral_relations(world, 1, Neutral::Natives) == NeutralRelations{true, false, 1000}));
    CHECK(!world.diplomacy().may_trade(1, 6));
    CHECK(!world.diplomacy().may_trade(6, 1));
    CHECK(world.diplomacy().relation(6, 1).damage == 0);
    REQUIRE(world.cities()[0].goodwill.size() == 7);
    CHECK(world.cities()[0].goodwill[1] == 1000);
    CHECK(world.cities()[0].goodwill[0] == sim::kNativeGoodwillStart);  // the others' as a game starts
    (*natives)->revert(world);
    CHECK(world.diplomacy().may_trade(1, 6));
    CHECK(world.cities()[0].goodwill.empty());
    auto war = editor::set_neutral_relations(world, 0, Neutral::Natives, NeutralRelations{false, false, 200});
    REQUIRE(war.ok());
    (*war)->apply(world);
    CHECK(world.diplomacy().hostile(0, 6));
    CHECK(world.diplomacy().hostile(6, 0));
    // No trade with the natives at war, no goodwill beyond the game's.
    CHECK(!editor::set_neutral_relations(world, 0, Neutral::Natives, NeutralRelations{false, true, 200}).ok());
    CHECK(!editor::set_neutral_relations(world, 0, Neutral::Natives, {true, true, sim::kMaxNativeGoodwill + 1}).ok());

    // The pirates: a peace holds as a bought one, so they keep it when they look at the player again.
    auto peace = editor::set_neutral_relations(world, 2, Neutral::Pirates, NeutralRelations{true, false, 200});
    REQUIRE(peace.ok());
    (*peace)->apply(world);
    CHECK(!world.diplomacy().hostile(5, 2));
    CHECK(!world.diplomacy().may_trade(2, 5));  // the player's ships stay away from their nests
    sim::pirates_reconsider(world);
    CHECK(!world.diplomacy().hostile(2, 5));
    CHECK(world.diplomacy().hostile(3, 5));  // the others stay at war
    auto again = editor::set_neutral_relations(world, 2, Neutral::Pirates, NeutralRelations{false, true, 200});
    REQUIRE(again.ok());
    (*again)->apply(world);
    sim::pirates_reconsider(world);
    CHECK(world.diplomacy().hostile(2, 5));
    CHECK(world.diplomacy().may_trade(2, 5));  // their nests trade at war too
    auto peace_again = editor::set_neutral_relations(world, 2, Neutral::Pirates, NeutralRelations{true, true, 200});
    REQUIRE(peace_again.ok());
    (*peace_again)->apply(world);

    // The trader passes the Kontors of a player by: the scenario switch of the original.
    auto trader = editor::set_neutral_relations(world, 3, Neutral::Trader, NeutralRelations{true, false, 200});
    REQUIRE(trader.ok());
    (*trader)->apply(world);
    CHECK(world.players()[3].scenario.has(sim::scenario_flag::kNoTrader));
    CHECK(!editor::neutral_relations(world, 3, Neutral::Trader).trade);
    CHECK(!editor::set_neutral_relations(world, 3, Neutral::Trader, NeutralRelations{false, false, 200}).ok());
    // Only humans and computers, only towards parties the world has.
    CHECK(!editor::set_neutral_relations(world, 4, Neutral::Natives, {}).ok());
    CHECK(!editor::set_neutral_relations(world, 9, Neutral::Natives, {}).ok());

    // What a scenario saves comes back when it is opened or played (the village has no island to
    // be saved on; the goodwill of cities comes back as the save game's own tests show).
    world.remove_city(0, 0);
    auto bytes = sim::write_game(world, assets::BuildingTable{});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, assets::BuildingTable{});
    REQUIRE(loaded.ok());
    CHECK((editor::neutral_relations(*loaded, 0, Neutral::Natives) == NeutralRelations{false, false, 200}));
    CHECK(editor::neutral_relations(*loaded, 2, Neutral::Pirates).peace);
    CHECK(!editor::neutral_relations(*loaded, 3, Neutral::Trader).trade);
    sim::pirates_reconsider(*loaded);
    CHECK(!loaded->diplomacy().hostile(2, 5));

    sim::World alone(40, 40);
    std::vector<sim::Player> two(2);
    alone.set_players(two);
    CHECK(editor::neutral_parties(alone).empty());
    CHECK(!editor::set_neutral_relations(alone, 0, Neutral::Pirates, {}).ok());
}

TEST_CASE("editor players: a lasting peace with the pirates holds until the player attacks them") {
    using editor::Neutral;
    using editor::NeutralRelations;
    sim::World world = seven_slots();
    CHECK(!editor::set_neutral_relations(world, 0, Neutral::Pirates, {false, true, 200, true}).ok());
    CHECK(!editor::set_neutral_relations(world, 0, Neutral::Natives, {true, true, 200, true}).ok());
    auto lasting = editor::set_neutral_relations(world, 0, Neutral::Pirates, NeutralRelations{true, true, 200, true});
    REQUIRE(lasting.ok());
    (*lasting)->apply(world);
    CHECK(editor::neutral_relations(world, 0, Neutral::Pirates).lasting);
    CHECK(world.players()[0].scenario.has(sim::scenario_flag::kPiratePeace));
    CHECK(editor::original_limit_problems(world).back().kind == editor::LimitProblem::Kind::PiratePeace);
    // Long after any bought peace would have faded, and bribed against the player: still peace.
    world.set_time_ms(10ULL * 60 * 60 * 1000);
    world.diplomacy().add_event(5, 0, sim::DiplomacyEventType::BribedAgainst, world.time_ms());
    sim::pirates_reconsider(world);
    CHECK(!world.diplomacy().hostile(5, 0));
    // What a scenario saves keeps it, though not in the part the original reads.
    auto bytes = sim::write_game(world, assets::BuildingTable{});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, assets::BuildingTable{});
    REQUIRE(loaded.ok());
    CHECK(editor::neutral_relations(*loaded, 0, Neutral::Pirates).lasting);
    // An attack of the player breaks it for good.
    world.diplomacy().set_peace(world, 0, 5, sim::Treaty::None, world.time_ms());
    sim::pirates_reconsider(world);
    CHECK(world.diplomacy().hostile(5, 0));
    CHECK(!world.players()[0].scenario.has(sim::scenario_flag::kPiratePeace));
    // A plain peace takes the switch away again.
    auto plain = editor::set_neutral_relations(world, 0, Neutral::Pirates, NeutralRelations{true, true, 200, false});
    REQUIRE(plain.ok());
    (*plain)->apply(world);
    CHECK(!editor::neutral_relations(world, 0, Neutral::Pirates).lasting);
}

TEST_CASE("editor: a scenario's own settings are a step of undo") {
    sim::World world(40, 40);
    CHECK(!editor::scenario_rules(world).has_value());
    auto op = editor::set_scenario_rules(world, sim::Rules::original());
    REQUIRE(op != nullptr);
    op->apply(world);
    REQUIRE(editor::scenario_rules(world).has_value());
    CHECK(*editor::scenario_rules(world) == sim::Rules::original());
    CHECK(editor::set_scenario_rules(world, sim::Rules::original()) == nullptr);  // nothing to do
    op->revert(world);
    CHECK(!world.scenario_info().has_value());
}

TEST_CASE("editor players: a slot given a package's faction of the settlers' behaviour") {
    sim::World world = seven_slots();
    std::vector<sim::Faction> factions = sim::base_factions();
    sim::Faction league;
    league.id = "hansa:league";
    factions.push_back(league);
    sim::Faction corsairs;
    corsairs.id = "hansa:corsairs";
    corsairs.behaviour = sim::FactionBehaviour::Pirates;
    factions.push_back(corsairs);
    world.set_factions(factions);
    const auto league_id = static_cast<sim::FactionId>(factions.size() - 2);
    const auto corsairs_id = static_cast<sim::FactionId>(factions.size() - 1);
    CHECK((editor::slot_factions(world) == std::vector<sim::FactionId>{sim::faction::kSettlers, league_id}));

    editor::PlayerValues v = editor::player_values(world.players()[1]);
    CHECK(v.faction == sim::faction::kSettlers);
    v.faction = league_id;
    auto op = editor::set_player(world, 1, v);
    REQUIRE(op.ok());
    (*op)->apply(world);
    CHECK(world.players()[1].faction == league_id);
    CHECK(world.faction(world.players()[1]).id == "hansa:league");
    (*op)->revert(world);
    CHECK(world.players()[1].faction == sim::faction::kSettlers);

    // Refused: a faction of another behaviour, one the world does not have.
    v.faction = corsairs_id;
    CHECK(!editor::set_player(world, 1, v).ok());
    v.faction = static_cast<sim::FactionId>(factions.size());
    CHECK(!editor::set_player(world, 1, v).ok());
}

TEST_CASE("editor players: on a map of open sea the human is never a slot that only holds a place") {
    // A new map without islands has no players: five added fill the slots 0 to 3 and 7, the slots
    // 4 to 6 between them only hold places. The human is the fifth.
    sim::World world(40, 40);
    for (int n = 0; n < 5; ++n) {
        auto op = editor::add_player(world);
        REQUIRE(op.ok());
        (*op)->apply(world);
    }
    REQUIRE(world.players().size() == 8);
    editor::PlayerValues v = editor::player_values(world.players()[7]);
    v.role = sim::PlayerRole::Human;
    v.money = 30000;
    auto human = editor::set_player(world, 7, v);
    REQUIRE(human.ok());
    (*human)->apply(world);
    world.add_ship(sim::make_ship("", 3, 3, 21, 7));
    CHECK(sim::first_human_player(world) == 7);

    // So the game started from the scenario plays it too, and the empty slots stay empty.
    auto bytes = sim::write_game(world, assets::BuildingTable{});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, assets::BuildingTable{});
    REQUIRE(loaded.ok());
    CHECK(sim::first_human_player(*loaded) == 7);
    CHECK(loaded->players()[7].money == 30000);
    for (sim::PlayerId slot = 4; slot < 7; ++slot) {
        CHECK(loaded->players()[slot].status == sim::PlayerStatus::Absent);
    }
    CHECK((editor::player_slots(*loaded) == editor::player_slots(world)));
}

TEST_CASE("editor players: the natives and the pirates come to a map that lacks them") {
    using editor::Neutral;
    sim::World world(40, 40);
    for (int n = 0; n < 2; ++n) {
        auto op = editor::add_player(world);
        REQUIRE(op.ok());
        (*op)->apply(world);
    }
    CHECK(editor::neutral_parties(world).empty());
    CHECK(editor::can_add_party(world, Neutral::Natives));
    CHECK(editor::can_add_party(world, Neutral::Pirates));
    auto pirates = editor::add_party(world, Neutral::Pirates);
    REQUIRE(pirates.ok());
    (*pirates)->apply(world);
    REQUIRE(world.players().size() == 6);
    CHECK(world.acts_as(editor::party_slot(Neutral::Pirates), sim::FactionBehaviour::Pirates));
    CHECK(world.players()[4].status == sim::PlayerStatus::Absent);  // the trader's place
    CHECK(!editor::can_add_party(world, Neutral::Pirates));
    CHECK(!editor::add_party(world, Neutral::Pirates).ok());
    auto natives = editor::add_party(world, Neutral::Natives);
    REQUIRE(natives.ok());
    (*natives)->apply(world);
    CHECK(world.acts_as(editor::party_slot(Neutral::Natives), sim::FactionBehaviour::Natives));
    CHECK((editor::neutral_parties(world) == std::vector<Neutral>{Neutral::Natives, Neutral::Pirates}));
    // As in a new game: at war with the pirates, at peace and trading with the natives.
    CHECK(!editor::neutral_relations(world, 0, Neutral::Pirates).peace);
    CHECK(editor::neutral_relations(world, 1, Neutral::Natives).peace);
    CHECK(editor::neutral_relations(world, 1, Neutral::Natives).trade);
    // The players among themselves as they were; undone, the slot is gone.
    CHECK(editor::relations(world, 0, 1).peace);
    (*natives)->revert(world);
    CHECK(editor::can_add_party(world, Neutral::Natives));

    // A map of the original's seven slots has them.
    sim::World full = seven_slots();
    CHECK(!editor::can_add_party(full, Neutral::Natives));
    CHECK(!editor::can_add_party(full, Neutral::Pirates));
}
