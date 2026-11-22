// SPDX-License-Identifier: GPL-3.0-or-later
// Renaming ships and cities (engine/app/src/name_edit.hpp): what the windows show while a name is
// typed, which name becomes a command, that every computer carries it out alike, and that the
// savegame keeps it.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../test_framework.hpp"
#include "name_edit.hpp"

#include "o1602/sim/command_io.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/ui/panel.hpp"
#include "o1602/ui/text_caret.hpp"

using namespace o1602;

namespace {

constexpr sim::PlayerId kMe = 0;
constexpr sim::PlayerId kOther = 1;
constexpr int kBackspace = 8;
constexpr int kEnter = 13;
constexpr int kEscape = 27;

sim::World& make_world(sim::World& world) {
    std::vector<sim::Player> players(2);
    for (std::size_t p = 0; p < players.size(); ++p) {
        players[p].role = sim::PlayerRole::Human;
        players[p].banner = static_cast<sim::BannerId>(p);
        players[p].name = "Spieler " + std::to_string(p);
    }
    world.set_players(players);
    assets::IslandHeader header;
    header.width = 8;
    header.height = 8;
    REQUIRE(world.add_island(sim::WorldIsland{header, sim::IslandMap(8, 8), 40, 40}).ok());
    world.add_ship(sim::make_ship("Mine", 10, 10, 21, kMe));
    world.add_ship(sim::make_ship("Theirs", 20, 10, 21, kOther));
    for (sim::PlayerId owner : {kMe, kOther}) {
        sim::City city;
        city.island = 0;
        city.index = owner;
        city.owner = owner;
        city.name = owner == kMe ? "Neustadt" : "Fremdburg";
        world.add_city(city);
    }
    return world;
}

/// Two players, each with a ship and a city on the one island.
struct Game {
    sim::World world{64, 64};
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    sim::Simulation simulation;

    Game() : simulation(make_world(world), buildings, figures) {}

    [[nodiscard]] app::NameTarget my_ship() const { return app::NameTarget::of(world.ships()[0]); }
    [[nodiscard]] app::NameTarget their_ship() const { return app::NameTarget::of(world.ships()[1]); }
    [[nodiscard]] app::NameTarget my_city() const { return app::NameTarget::of(world.cities()[0]); }
    [[nodiscard]] app::NameTarget their_city() const { return app::NameTarget::of(world.cities()[1]); }
    [[nodiscard]] const std::string& name(const app::NameTarget& t) const { return *app::name_of(world, t); }

    /// Carries a command out at the next tick, as the game does with the player's.
    bool apply(const sim::Command& c) {
        simulation.submit(c);
        simulation.tick();
        const auto results = simulation.take_command_results();
        return results.size() == 1 && results[0].accepted;
    }
};

}  // namespace

TEST_CASE("names: a ship's name is typed in its window and becomes the ship's on Enter") {
    Game g;
    app::NameEdit edit;
    CHECK(!edit.start(g.world, kMe, g.my_ship()).has_value());
    REQUIRE(edit.active());
    // While typing, the window shows the text with the caret at its end; other names stay as they are.
    CHECK(edit.shown(g.my_ship(), "Mine") == "Mine");
    CHECK((edit.mark(g.my_ship()) == ui::TextMark{4, 4, 4}));
    CHECK(!edit.mark(g.their_ship()).has_value());
    CHECK(edit.shown(g.their_ship(), "Theirs") == "Theirs");
    for (int i = 0; i < 4; ++i) {
        CHECK(!edit.key(kBackspace).has_value());
    }
    CHECK(edit.text().empty());
    edit.type("Seeadler");
    CHECK(edit.shown(g.my_ship(), "Mine") == "Seeadler");
    const auto command = edit.key(kEnter);
    CHECK(!edit.active());
    REQUIRE(command.has_value());
    const auto* rename = std::get_if<sim::ShipCommand>(&*command);
    REQUIRE(rename != nullptr);
    CHECK(rename->op == sim::ShipCommand::Op::Rename);
    CHECK(rename->player == kMe);
    CHECK(rename->ship == g.my_ship().ship);
    CHECK(rename->name == "Seeadler");
    // The world is the one place of the name: every window reads the new one from there.
    CHECK(g.apply(*command));
    CHECK(g.name(g.my_ship()) == "Seeadler");
    CHECK(edit.shown(g.my_ship(), g.name(g.my_ship())) == "Seeadler");
}

TEST_CASE("names: the caret moves, letters are selected and replaced as in every text field") {
    Game g;
    app::NameEdit edit;
    (void)edit.start(g.world, kMe, g.my_ship());
    CHECK(!edit.key(ui::kKeyHome).has_value());
    edit.type("Die ");
    CHECK(edit.text() == "Die Mine");
    CHECK((edit.mark(g.my_ship()) == ui::TextMark{4, 4, 4}));
    CHECK(!edit.key(ui::kKeyDelete).has_value());
    CHECK(edit.text() == "Die ine");
    CHECK(!edit.key(ui::kKeyEnd | ui::kKeyShift).has_value());
    CHECK(edit.selected_text() == "ine");
    CHECK((edit.mark(g.my_ship()) == ui::TextMark{7, 4, 7}));
    edit.type(
        "M\xF6"
        "we");
    CHECK(edit.text() ==
          "Die M\xF6"
          "we");
    // Ctrl+A and a letter replace the whole name; a name typed again starts with the caret at its end.
    CHECK(!edit.key(ui::kKeySelectAll).has_value());
    CHECK(edit.selected_text() ==
          "Die M\xF6"
          "we");
    edit.type("K");
    CHECK(edit.text() == "K");
    edit.cancel();
    (void)edit.start(g.world, kMe, g.my_ship());
    CHECK((edit.mark(g.my_ship()) == ui::TextMark{4, 4, 4}));
    CHECK(edit.selected_text().empty());
    // Typed at the caret, the name still stops at 16 characters.
    CHECK(!edit.key(ui::kKeyHome).has_value());
    edit.type(std::string(20, 'x'));
    CHECK(edit.text() == std::string(12, 'x') + "Mine");
}

TEST_CASE("names: only own cities and own ships that are not sinking can be renamed") {
    Game g;
    app::NameEdit edit;
    CHECK(app::can_rename(g.world, kMe, g.my_ship()));
    CHECK(app::can_rename(g.world, kMe, g.my_city()));
    CHECK(!app::can_rename(g.world, kMe, g.their_ship()));
    CHECK(!app::can_rename(g.world, kMe, g.their_city()));
    CHECK(!app::can_rename(g.world, sim::kNoPlayer, g.my_city()));
    (void)edit.start(g.world, kMe, g.their_city());
    CHECK(!edit.active());
    (void)edit.start(g.world, kMe, g.their_ship());
    CHECK(!edit.active());
    app::NameTarget gone;
    gone.ship = 999;
    CHECK(app::name_of(g.world, gone) == nullptr);
    CHECK(!app::can_rename(g.world, kMe, gone));

    // The simulation checks the same, for commands from the network.
    CHECK(!g.apply(app::rename_command(kMe, g.their_ship(), "Beute")));
    CHECK(!g.apply(app::rename_command(kMe, g.their_city(), "Beute")));
    CHECK(g.name(g.their_city()) == "Fremdburg");
    CHECK(g.apply(app::rename_command(kMe, g.my_city(), "Hafenstadt")));
    CHECK(g.name(g.my_city()) == "Hafenstadt");

    const app::NameTarget mine = g.my_ship();
    g.world.ship(mine.ship)->sinking = true;
    CHECK(!app::can_rename(g.world, kMe, mine));
    (void)edit.start(g.world, kMe, mine);
    CHECK(!edit.active());
    CHECK(!g.apply(app::rename_command(kMe, mine, "Wrack")));
}

TEST_CASE("names: Escape, an empty or an unchanged name keep the old one") {
    Game g;
    app::NameEdit edit;
    (void)edit.start(g.world, kMe, g.my_city());
    edit.type("Anders");
    CHECK(!edit.key(kEscape).has_value());
    CHECK(!edit.active());
    CHECK(edit.shown(g.my_city(), "Neustadt") == "Neustadt");

    (void)edit.start(g.world, kMe, g.my_city());
    while (!edit.text().empty()) {
        (void)edit.key(kBackspace);
    }
    edit.type("   ");
    CHECK(!edit.key(kEnter).has_value());  // nothing but spaces: the city keeps its name

    (void)edit.start(g.world, kMe, g.my_city());
    CHECK(!edit.key(kEnter).has_value());  // nothing changed, nothing to send

    // Spaces at the ends go.
    (void)edit.start(g.world, kMe, g.my_city());
    edit.type(" ");
    CHECK(!edit.key(kEnter).has_value());  // "Neustadt " is "Neustadt"
    (void)edit.start(g.world, kMe, g.my_city());
    while (!edit.text().empty()) {
        (void)edit.key(kBackspace);
    }
    edit.type("  Bucht ");
    const auto trimmed = edit.key(kEnter);
    REQUIRE(trimmed.has_value());
    CHECK(std::get<sim::CityOrder>(*trimmed).name == "Bucht");
}

TEST_CASE("names: at most 16 characters, no control characters") {
    Game g;
    app::NameEdit edit;
    (void)edit.start(g.world, kMe, g.my_ship());
    edit.type("\t\x01\x7F");
    CHECK(edit.text() == "Mine");
    edit.type(" der K\xF6nigin von England");  // Windows-1252, as the fonts have it
    CHECK(edit.text().size() == app::NameEdit::kMaxLength);
    CHECK(edit.text() == "Mine der K\xF6nigin");
    // A longer name from an old savegame starts cut to what can be typed.
    g.world.ship(g.my_ship().ship)->name = std::string(30, 'x');
    edit.cancel();
    (void)edit.start(g.world, kMe, g.my_ship());
    CHECK(edit.text() == std::string(app::NameEdit::kMaxLength, 'x'));

    // The simulation refuses names that would break the lines of lists and files.
    CHECK(sim::valid_name("Seeadler", 16));
    CHECK(!sim::valid_name("", 16));
    CHECK(!sim::valid_name(std::string(17, 'a'), 16));
    CHECK(!sim::valid_name("Zwei\nZeilen", 16));
    CHECK(!g.apply(app::rename_command(kMe, g.my_city(), "Zwei\nZeilen")));
    CHECK(!g.apply(sim::PlayerRename{kMe, "Tab\tulator"}));
}

TEST_CASE("names: a click elsewhere, closing the window or another name keep the typed name") {
    Game g;
    app::NameEdit edit;
    // A click on the name again goes on typing.
    (void)edit.start(g.world, kMe, g.my_city());
    edit.type("X");
    edit.begin_click();
    CHECK(!edit.start(g.world, kMe, g.my_city()).has_value());
    CHECK(!edit.end_click().has_value());
    CHECK(edit.text() == "NeustadtX");
    // A click on another gadget of the window keeps the name.
    edit.begin_click();
    const auto clicked = edit.end_click();
    REQUIRE(clicked.has_value());
    CHECK(std::get<sim::CityOrder>(*clicked).name == "NeustadtX");
    CHECK(!edit.active());

    // While a window shows the target, typing goes on from frame to frame; once none does (the
    // window closed, another selection), the name is kept.
    (void)edit.start(g.world, kMe, g.my_city());
    edit.type("Y");
    (void)edit.shown(g.my_city(), "Neustadt");
    CHECK(!edit.end_frame(g.world).has_value());
    CHECK(edit.active());
    const auto closed = edit.end_frame(g.world);
    REQUIRE(closed.has_value());
    CHECK(std::get<sim::CityOrder>(*closed).name == "NeustadtY");

    // Starting another name keeps the first.
    (void)edit.start(g.world, kMe, g.my_city());
    edit.type("Z");
    const auto first = edit.start(g.world, kMe, g.my_ship());
    REQUIRE(first.has_value());
    CHECK(std::get<sim::CityOrder>(*first).name == "NeustadtZ");
    REQUIRE(edit.target().has_value());
    CHECK(*edit.target() == g.my_ship());
}

TEST_CASE("names: a ship lost while its name is typed drops the name") {
    Game g;
    app::NameEdit edit;
    (void)edit.start(g.world, kMe, g.my_ship());
    edit.type("Neu");
    (void)edit.shown(g.my_ship(), "Mine");
    g.world.ship(g.my_ship().ship)->owner = kOther;  // bought by the other player
    CHECK(!edit.end_frame(g.world).has_value());
    CHECK(!edit.active());
    CHECK(g.name(g.my_ship()) == "Mine");
}

TEST_CASE("names: a rename is a command like any other: the same on every computer, kept by the savegame") {
    Game a;
    Game b;
    CHECK(a.simulation.state_hash() == b.simulation.state_hash());
    app::NameEdit edit;
    (void)edit.start(a.world, kMe, a.my_ship());
    edit.type(" II");
    const auto ship = edit.key(kEnter);
    (void)edit.start(a.world, kMe, a.my_city());
    edit.type(" am Meer");
    const auto city = edit.key(kEnter);
    REQUIRE(ship.has_value());
    REQUIRE(city.has_value());
    // Both computers get the commands as the network carries them, for the same tick.
    for (const sim::Command& c : {*ship, *city}) {
        const auto decoded = sim::decode_command(sim::encode_command(c));
        REQUIRE(decoded.has_value());
        a.simulation.schedule(2, c);
        b.simulation.schedule(2, *decoded);
    }
    for (int t = 0; t < 3; ++t) {
        a.simulation.tick();
        b.simulation.tick();
    }
    CHECK(a.name(a.my_ship()) == "Mine II");
    CHECK(b.name(b.my_ship()) == "Mine II");
    CHECK(b.name(b.my_city()) == "Neustadt am Meer");
    CHECK(a.simulation.state_hash() == b.simulation.state_hash());

    auto bytes = sim::write_game(a.world, a.buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, a.buildings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->ships().size() == 2);
    CHECK(loaded->ships()[0].name == "Mine II");
    REQUIRE(loaded->cities().size() == 2);
    CHECK(loaded->cities()[0].name == "Neustadt am Meer");
    CHECK(loaded->cities()[1].name == "Fremdburg");
}
