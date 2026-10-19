// SPDX-License-Identifier: GPL-3.0-or-later
// What the local player is told of the game's events (engine/app/src/event_presenter.hpp).

#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "event_presenter.hpp"

#include "o1602/audio/game_audio.hpp"

using namespace o1602;

namespace {

constexpr sim::PlayerId kMe = 0;
constexpr sim::PlayerId kRival = 1;

/// Two players, an island at (100, 50) with a city of each.
struct Game {
    sim::World world{300, 200};
    app::EventPresenter presenter{
        [](const sim::Player& p, sim::PlayerId id) { return "player " + std::to_string(id) + " " + p.name; }};

    Game() {
        std::vector<sim::Player> players(2);
        players[kMe].role = sim::PlayerRole::Human;
        players[kRival].role = sim::PlayerRole::Computer;
        players[kRival].banner = 1;
        world.set_players(players);
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, sim::IslandMap(20, 10), 100, 50}).ok());
        for (const sim::PlayerId owner : {kMe, kRival}) {
            sim::City c;
            c.island = 0;
            c.index = owner;
            c.owner = owner;
            c.name = owner == kMe ? "Mine" : "Theirs";
            world.add_city(c);
        }
    }
};

const app::Announcement* announcement(const app::Notice& n) {
    return std::get_if<app::Announcement>(&n);
}

}  // namespace

TEST_CASE("event presenter: city, disaster, drought and diplomacy messages for the local player only") {
    Game g;
    sim::CityEvent hunger;
    hunger.kind = sim::CityEvent::Kind::Hunger;
    hunger.owner = kMe;
    sim::CityEvent theirs = hunger;
    theirs.owner = kRival;
    theirs.city = 1;
    sim::CityEvent shortage;
    shortage.kind = sim::CityEvent::Kind::Shortage;
    shortage.owner = kMe;
    const auto city = g.presenter.city_events(g.world, kMe, {hunger, theirs, shortage});
    REQUIRE(city.size() == 1);
    CHECK(announcement(city[0])->message == 0x33);

    sim::DisasterEvent fire;
    fire.island = 0;
    fire.x = 3;
    fire.y = 4;
    fire.owner = kMe;
    sim::DisasterEvent spread = fire;
    spread.spread = true;
    sim::DisasterEvent plague = fire;
    plague.disaster = sim::Disaster::Kind::Plague;
    const auto disasters = g.presenter.disaster_events(g.world, kMe, {fire, spread, plague});
    REQUIRE(disasters.size() == 2);
    CHECK(announcement(disasters[0])->message == 0x02);
    CHECK((announcement(disasters[0])->tile == std::make_pair(103.0, 54.0)));  // in world tiles
    CHECK(announcement(disasters[1])->message == 0x01);

    sim::IslandEvent drought;
    drought.island = 0;
    const auto islands = g.presenter.island_events(g.world, kMe, {drought});
    REQUIRE(islands.size() == 1);
    CHECK(announcement(islands[0])->message == 0x03);

    sim::DiplomacyNotice offer;
    offer.kind = sim::DiplomacyNotice::Kind::TradeOffered;
    offer.to = kMe;
    sim::DiplomacyNotice other = offer;
    other.to = kRival;
    const auto diplomacy = g.presenter.diplomacy_notices(kMe, {offer, other});
    REQUIRE(diplomacy.size() == 1);
    CHECK(announcement(diplomacy[0])->message == 0x51);
}

TEST_CASE("event presenter: a rival's new city shows in the rival's colour, an own one says nothing") {
    Game g;
    sim::ConstructionEvent founded;
    founded.kind = sim::ConstructionEvent::Kind::CityFounded;
    founded.island = 0;
    founded.city = 1;  // the rival's
    sim::ConstructionEvent own = founded;
    own.city = 0;
    const auto notices = g.presenter.construction_events(g.world, kMe, {own, founded});
    REQUIRE(notices.size() == 1);
    const app::Announcement* a = announcement(notices[0]);
    CHECK(a->message == 0x0E);
    CHECK(a->about == std::optional<sim::PlayerId>(kRival));
    CHECK((a->tile == std::make_pair(110.0, 55.0)));  // the island's middle
}

TEST_CASE("event presenter: an own defeat plays its video directly, the defeat videos alternate") {
    Game g;
    sim::GameEvent defeated;
    defeated.kind = sim::GameEvent::Kind::PlayerDefeated;
    defeated.player = kMe;
    const auto first = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    REQUIRE(first.size() == 3);
    CHECK(std::get<app::ScoreUpdate>(first[0]).defeated == kMe);
    CHECK(announcement(first[1])->message == 0x53);
    CHECK(std::get<app::DirectVideo>(first[2]).number == 1010);
    const auto second = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    CHECK(std::get<app::DirectVideo>(second[2]).number == 2020);
    defeated.reason = sim::GameEvent::Reason::Bankrupt;
    const auto bankrupt = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    CHECK(std::get<app::DirectVideo>(bankrupt[2]).number == 998);

    // Somebody else's defeat: a message only.
    defeated.player = kRival;
    const auto rival = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    REQUIRE(rival.size() == 2);
    CHECK(announcement(rival[1])->message == 0x59);
}

TEST_CASE("event presenter: the multiplayer victory speaks to the winner on track 1, to the others on track 2") {
    Game g;
    sim::GameEvent won;
    won.kind = sim::GameEvent::Kind::MissionAccomplished;
    won.player = kMe;
    won.message = audio::kMessageMultiplayerWin;
    const auto mine = g.presenter.game_events(g.world, kMe, {won}, 0);
    REQUIRE(mine.size() == 1);
    CHECK(announcement(mine[0])->tracks == 0x03);
    CHECK(announcement(mine[0])->message == audio::kMessageMultiplayerWin);  // banner 0: red

    // The rival won the race: the game is over for this player.
    sim::Mission race;
    race.success_message = audio::kMessageMultiplayerWin;
    g.world.set_missions({race});
    g.world.player(kRival)->mission = 0;
    sim::GameEvent over;
    over.kind = sim::GameEvent::Kind::GameOver;
    over.player = kRival;
    const auto theirs = g.presenter.game_events(g.world, kMe, {over}, 0);
    REQUIRE(theirs.size() == 2);
    CHECK(std::get<app::ScoreUpdate>(theirs[0]).progress);
    CHECK(announcement(theirs[1])->tracks == 0x05);
    CHECK(announcement(theirs[1])->message == audio::kMessageMultiplayerWin + 1);  // the rival's banner
}

TEST_CASE("event presenter: the end screen waits for the delay of running game time, or for the videos") {
    Game g;
    sim::GameEvent won;
    won.kind = sim::GameEvent::Kind::MissionAccomplished;
    won.player = kMe;
    won.message = 0x54;
    (void)g.presenter.game_events(g.world, kMe, {won}, 1000);
    CHECK(!g.presenter.end_screen_due(3000, false, false));
    CHECK(!g.presenter.end_screen_due(10'000, false, true));  // paused: the time does not count
    CHECK(!g.presenter.end_screen_due(12'000, false, false));
    const auto due = g.presenter.end_screen_due(13'000, false, false);
    REQUIRE(due.has_value());
    CHECK(*due == sim::GameOutcome::Won);
    CHECK(!g.presenter.end_screen_due(20'000, false, false));  // once

    // Shown once per game: a second victory does not bring it again.
    g.presenter.end_screen_shown();
    (void)g.presenter.game_events(g.world, kMe, {won}, 30'000);
    CHECK(!g.presenter.end_screen_due(40'000, false, false));

    // A new game; the defeat's video plays: the screen follows as soon as it ended.
    g.presenter.reset_game();
    sim::GameEvent defeated;
    defeated.kind = sim::GameEvent::Kind::PlayerDefeated;
    defeated.player = kMe;
    (void)g.presenter.game_events(g.world, kMe, {defeated}, 50'000);
    CHECK(!g.presenter.end_screen_due(50'100, true, false));
    CHECK(!g.presenter.end_screen_due(90'000, true, false));
    const auto lost = g.presenter.end_screen_due(90'010, false, false);
    REQUIRE(lost.has_value());
    CHECK(*lost == sim::GameOutcome::Lost);

    // Back to the menu: nothing follows.
    g.presenter.reset_game();
    (void)g.presenter.game_events(g.world, kMe, {defeated}, 0);
    g.presenter.drop_end_screen();
    CHECK(!g.presenter.end_screen_due(100'000, false, false));
}

TEST_CASE("event presenter: notes stay in the status line for a few seconds, the chat keeps 100 lines") {
    Game g;
    CHECK(g.presenter.notice(0) == nullptr);
    g.presenter.notify("Gespeichert", 1000);
    REQUIRE(g.presenter.notice(1000) != nullptr);
    CHECK(*g.presenter.notice(4999) == "Gespeichert");
    CHECK(g.presenter.notice(1000 + app::EventPresenter::kNoticeMs) == nullptr);

    std::deque<app::ChatLine> chat;
    for (int i = 0; i < 105; ++i) {
        app::add_chat_line(&chat, app::ChatLine{"", std::to_string(i), sim::kNoPlayer, 0}, false);
    }
    CHECK(chat.size() == app::kChatKept);
    CHECK(chat.front().text == "5");
    CHECK(chat.back().text == "104");
    app::add_chat_line(nullptr, app::ChatLine{}, false);  // no panel: nothing to keep
}
