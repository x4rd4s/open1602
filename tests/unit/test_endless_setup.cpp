// SPDX-License-Identifier: GPL-3.0-or-later
// The start dialog of the endless game (ADR 0008): its items and values, the presets, the lines
// in settings.cfg and the screen at work.

#include <algorithm>
#include <array>
#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "endless_setup.hpp"
#include "game_settings_menu.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::EndlessSetup;
using app::SetupItem;

namespace {

constexpr std::array<const ui::Font*, 3> kNoFonts = {nullptr, nullptr, nullptr};

std::optional<std::pair<std::int32_t, std::int32_t>> find_text(app::GameSettingsMenu& menu, const std::string& text) {
    for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text == text) {
            return std::pair{d.x, d.y};
        }
    }
    return std::nullopt;
}

app::GameSettingsMenu::Result click(app::GameSettingsMenu& menu, const std::string& text,
                                    ui::MouseButton button = ui::MouseButton::Left) {
    const auto at = find_text(menu, text);
    REQUIRE(at.has_value());
    (void)menu.mouse(at->first, at->second - 4, true, button);
    return menu.mouse(at->first, at->second - 4, false, button);
}

std::string value_of(const EndlessSetup& s, SetupItem item) {
    return app::setup_value(item, app::setup_choice(s, item));
}

}  // namespace

TEST_CASE("endless setup: every item goes round its values and back") {
    for (std::size_t i = 0; i < app::kSetupItems; ++i) {
        const auto item = static_cast<SetupItem>(i);
        EndlessSetup s;
        const std::size_t n = app::setup_choices(item);
        REQUIRE(n >= 2);
        const std::size_t start = app::setup_choice(s, item);
        std::vector<std::string> seen;
        for (std::size_t k = 0; k < n; ++k) {
            seen.push_back(value_of(s, item));
            app::choose_next(s, item);
        }
        CHECK(app::setup_choice(s, item) == start);
        // Backwards: the same values in the other order, before the first the last.
        for (std::size_t k = n; k > 0; --k) {
            app::choose_previous(s, item);
            CHECK(value_of(s, item) == seen[k - 1]);
        }
        CHECK(app::setup_choice(s, item) == start);
        for (std::size_t a = 0; a < seen.size(); ++a) {
            CHECK(!seen[a].empty());
            for (std::size_t b = a + 1; b < seen.size(); ++b) {
                CHECK(seen[a] != seen[b]);  // each value has a text of its own
            }
        }
        CHECK(!app::setup_label(item).empty());
    }
}

TEST_CASE("endless setup: the values reach the rules and the world") {
    EndlessSetup s;
    app::choose(s, SetupItem::Refund, 2);
    CHECK(s.rules.demolish_refund == sim::Refund::Partial);
    CHECK(s.rules.refund_share() == 50);
    app::choose(s, SetupItem::Refund, 4);
    CHECK(s.rules.refund_share() == 100);
    app::choose(s, SetupItem::Ships, 3);
    CHECK(s.rules.max_ships_per_player == 0);  // unlimited
    app::choose(s, SetupItem::Droughts, 4);
    CHECK(s.rules.droughts == sim::Droughts::Frequent);
    app::choose(s, SetupItem::Volcanoes, 1);
    CHECK(s.rules.volcanoes == sim::Volcanoes::Off);
    app::choose(s, SetupItem::TraderPrices, 1);
    CHECK(s.rules.trader_prices == sim::TraderPrices::Market);
    app::choose(s, SetupItem::Resources, 1);
    CHECK(!s.rules.guarantee_resources);
    CHECK(!s.rules.classic_ai);  // Open1602's computer players by default
    app::choose(s, SetupItem::ClassicAi, 1);
    CHECK(s.rules.classic_ai);
    CHECK(s.rules.pirates == sim::Pirates::Game);  // as the level by default: none on the two easy ones
    CHECK(value_of(s, SetupItem::Pirates) == ui::tr("setup.pirates.game"));
    app::choose(s, SetupItem::Pirates, 2);
    CHECK(s.rules.pirates == sim::Pirates::On);
    CHECK(app::write_setup(s).find("endless.pirates = on\n") != std::string::npos);
    EndlessSetup read;
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.pirates == sim::Pirates::On);
    CHECK(app::read_setup_line(read, "pirates", "off"));
    CHECK(read.rules.pirates == sim::Pirates::Off);
    CHECK(app::read_setup_line(read, "pirates", "many"));  // not a value: unchanged
    CHECK(read.rules.pirates == sim::Pirates::Off);

    // The world's items count only for a world of its own.
    CHECK(!app::setup_item_used(s, SetupItem::Players));
    CHECK(app::setup_item_used(s, SetupItem::Resources));
    app::choose(s, SetupItem::Map, 1);
    CHECK(s.own_world);
    CHECK(app::setup_item_used(s, SetupItem::Players));
    app::choose(s, SetupItem::Players, 4);
    CHECK(s.world.players == 6);
    // The size follows the players at first, as in a multiplayer game.
    CHECK(s.auto_size);
    CHECK(s.world_settings().width == sim::settings_for_players(6).width);
    CHECK(s.world_settings().height == sim::settings_for_players(6).height);
    app::choose(s, SetupItem::WorldSize, 4);
    CHECK(!s.auto_size);
    CHECK(s.world_settings().width == 1000);
    CHECK(s.world_settings().height == 700);
    app::choose(s, SetupItem::Villages, 0);
    CHECK(s.world.natives == sim::WorldSettings::Amount::None);
    app::choose(s, SetupItem::Starts, 1);
    CHECK(s.world.starts == sim::WorldSettings::Starts::Together);
    app::choose(s, SetupItem::Islands, 99);  // no such value: unchanged
    CHECK(s.world.islands == sim::WorldSettings::Amount::Normal);

    // A setting between the offered values shows the nearest.
    EndlessSetup odd;
    odd.rules.max_ships_per_player = 150;
    odd.rules.demolish_refund = sim::Refund::Partial;
    odd.rules.refund_percent = 60;
    CHECK(app::setup_choice(odd, SetupItem::Ships) == 1);   // 100
    CHECK(app::setup_choice(odd, SetupItem::Refund) == 2);  // 50 %
}

TEST_CASE("endless setup: the original's rules and Open1602's defaults") {
    const EndlessSetup original = EndlessSetup::original();
    CHECK(original.rules == sim::Rules::original());
    CHECK(!original.own_world);
    CHECK(EndlessSetup{}.rules == sim::Rules::open1602());
    CHECK(!(original == EndlessSetup{}));
}

TEST_CASE("endless setup: settings.cfg keeps the last choice") {
    app::AppSettings a;
    app::choose(a.endless, SetupItem::Refund, 1);
    app::choose(a.endless, SetupItem::Units, 3);
    app::choose(a.endless, SetupItem::GameSpeed, 0);
    app::choose(a.endless, SetupItem::Droughts, 2);
    app::choose(a.endless, SetupItem::Map, 1);
    app::choose(a.endless, SetupItem::WorldSize, 5);
    app::choose(a.endless, SetupItem::Players, 6);
    app::choose(a.endless, SetupItem::IslandSizes, 2);
    app::choose(a.endless, SetupItem::VolcanoIslands, 3);
    const std::string text = app::write_settings(a);
    CHECK(text.find("endless.world = 1400x980\n") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.endless == a.endless);
    CHECK(back.endless.rules.refund_percent == 25);

    // Wrong values keep the setting; other keys are no business of the setup.
    EndlessSetup s;
    CHECK(app::read_setup_line(s, "world", "100x100"));
    CHECK(s.world.width == 500);
    CHECK(app::read_setup_line(s, "islands", "none"));  // an endless game needs islands
    CHECK(s.world.islands == sim::WorldSettings::Amount::Normal);
    CHECK(app::read_setup_line(s, "droughts", "always"));
    CHECK(s.rules.droughts == sim::Droughts::Game);
    CHECK(app::read_setup_line(s, "players", "40"));
    CHECK(s.world.players == 4);
    CHECK(!app::read_setup_line(s, "colour", "red"));
    CHECK(s == EndlessSetup{});

    // The automatic size is kept as such.
    CHECK(app::write_setup(EndlessSetup{}).find("endless.world = auto\n") != std::string::npos);
    CHECK(app::read_setup_line(s, "world", "700x490"));
    CHECK(!s.auto_size);
    CHECK(app::read_setup_line(s, "world", "auto"));
    CHECK(s.auto_size);
    CHECK(s.world.width == 700);  // the last fixed size stays for the next time
}

TEST_CASE("endless setup: the automatic size grows with more than four players") {
    EndlessSetup s;
    s.own_world = true;
    for (std::uint8_t players = 2; players <= 4; ++players) {
        s.world.players = players;
        CHECK(s.world_settings().width == 500);
        CHECK(s.world_settings().height == 350);
    }
    s.world.players = 8;
    const sim::WorldSettings eight = s.world_settings();
    CHECK(eight.width == sim::settings_for_players(8).width);
    CHECK(eight.width > 500);
    CHECK(eight.players == 8);
    CHECK(app::setup_value(s, SetupItem::WorldSize).find(std::to_string(eight.width)) != std::string::npos);

    // Up to twelve players, as in multiplayer; a fixed size stays as chosen.
    app::choose(s, SetupItem::Players, 10);
    CHECK(s.world.players == 12);
    CHECK(s.world_settings().width == sim::settings_for_players(12).width);
    app::choose(s, SetupItem::WorldSize, 2);
    CHECK(s.world_settings().width == 500);
    CHECK(s.world_settings().players == 12);
}

TEST_CASE("endless setup: the dialog chooses, resets and starts") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{});
    // A click on a value takes the next one.
    CHECK(click(menu, value_of(menu.setup(), SetupItem::Refund)) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup().rules.demolish_refund == sim::Refund::Partial);
    // The world's buttons do nothing on the original's maps.
    const std::string players = value_of(menu.setup(), SetupItem::Players);
    (void)click(menu, players);
    CHECK(menu.setup().world.players == 4);
    (void)click(menu, ui::tr("setup.map.original"));
    CHECK(menu.setup().own_world);
    (void)click(menu, players);
    CHECK(menu.setup().world.players == 5);
    // A right click takes the previous value; buttons that are no setting do not answer it.
    CHECK(click(menu, value_of(menu.setup(), SetupItem::Players), ui::MouseButton::Right) ==
          app::GameSettingsMenu::Result::None);
    CHECK(menu.setup().world.players == 4);
    (void)click(menu, value_of(menu.setup(), SetupItem::Refund), ui::MouseButton::Right);
    CHECK(menu.setup().rules.demolish_refund == EndlessSetup{}.rules.demolish_refund);
    CHECK(click(menu, ui::tr("setup.start"), ui::MouseButton::Right) == app::GameSettingsMenu::Result::None);
    CHECK(click(menu, ui::tr("setup.back"), ui::MouseButton::Right) == app::GameSettingsMenu::Result::None);

    CHECK(click(menu, ui::tr("setup.original")) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup() == EndlessSetup::original());
    (void)click(menu, ui::tr("setup.defaults"));
    CHECK(menu.setup() == EndlessSetup{});
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);
    CHECK(click(menu, ui::tr("setup.back")) == app::GameSettingsMenu::Result::Back);
}

TEST_CASE("endless setup: the dialog speaks English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        CHECK(ui::tr("setup.title") != "setup.title");
        for (std::size_t i = 0; i < app::kSetupItems; ++i) {
            const auto item = static_cast<SetupItem>(i);
            CHECK(app::setup_label(item).find("setup.") == std::string::npos);
            for (std::size_t c = 0; c < app::setup_choices(item); ++c) {
                CHECK(app::setup_value(item, c).find("setup.") == std::string::npos);
            }
        }
    }
    CHECK(ui::tr("setup.start") == "Start game");
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("game setup: what a scenario, a multiplayer game and a savegame offer") {
    using app::SetupScope;
    // A scenario's world is given: only the rules.
    for (const SetupItem item : {SetupItem::Map, SetupItem::WorldSize, SetupItem::Players, SetupItem::Islands,
                                 SetupItem::Starts, SetupItem::Resources}) {
        CHECK(!app::setup_item_offered(SetupScope::Scenario, item));
    }
    for (const SetupItem item :
         {SetupItem::Refund, SetupItem::TraderPrices, SetupItem::Ships, SetupItem::Units, SetupItem::GameSpeed,
          SetupItem::Droughts, SetupItem::Volcanoes, SetupItem::ClassicAi, SetupItem::Pirates}) {
        CHECK(app::setup_item_offered(SetupScope::Scenario, item));
        CHECK(app::setup_item_open(EndlessSetup{}, SetupScope::Scenario, item));
        CHECK(!app::setup_item_open(EndlessSetup{}, SetupScope::Savegame, item));  // to look at only
    }
    // A multiplayer endless game: everything but the players, which are the lobby's.
    EndlessSetup mp = app::multiplayer_setup(EndlessSetup::defaults(SetupScope::MultiplayerEndless), 4);
    CHECK(!mp.own_world);
    CHECK(mp.world.starts == sim::WorldSettings::Starts::Together);
    CHECK(app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Map));
    CHECK(!app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Players));
    CHECK(app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::ClassicAi));
    // More than four always get a world of their own.
    mp = app::multiplayer_setup(mp, 7);
    CHECK(mp.own_world);
    CHECK(mp.world.players == 7);
    CHECK(mp.world_settings().width == sim::settings_for_players(7).width);
    CHECK(!app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Map));
    CHECK(app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Islands));

    // A scenario compares the rules only.
    EndlessSetup a;
    EndlessSetup b;
    b.own_world = true;
    CHECK(app::same_setup(a, b, SetupScope::Scenario));
    CHECK(!app::same_setup(a, b, SetupScope::Endless));
}

TEST_CASE("game setup: settings.cfg keeps the scenario's and the host's choice apart") {
    app::AppSettings a;
    app::choose(a.scenario, SetupItem::ClassicAi, 1);
    app::choose(a.scenario, SetupItem::TraderPrices, 1);
    app::choose(a.multiplayer, SetupItem::Map, 1);
    app::choose(a.multiplayer, SetupItem::Villages, 0);
    const std::string text = app::write_settings(a);
    CHECK(text.find("scenario.ai = classic\n") != std::string::npos);
    CHECK(text.find("multiplayer.own_world = on\n") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.scenario == a.scenario);
    CHECK(back.multiplayer == a.multiplayer);
    CHECK(back.endless == EndlessSetup{});
    // A host's world of its own starts together by default, as the original's multiplayer maps.
    CHECK(app::AppSettings{}.multiplayer.world.starts == sim::WorldSettings::Starts::Together);

    // The same lines, as text with any prefix.
    EndlessSetup read = EndlessSetup::defaults(app::SetupScope::MultiplayerEndless);
    app::read_setup(read, "# a comment\n" + app::write_setup(a.multiplayer, "x.") + "x.colour = red\nbroken\n", "x.");
    CHECK(read == a.multiplayer);
}

TEST_CASE("game setup: the dialog of a scenario shows the rules only") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{}, app::SetupScope::Scenario);
    CHECK(find_text(menu, ui::tr("setup.title.scenario")).has_value());
    CHECK(find_text(menu, ui::tr("setup.group.opponents")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.group.world")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.map")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.resources")).has_value());
    (void)click(menu, ui::tr("setup.ai.open1602"));
    CHECK(menu.setup().rules.classic_ai);
    (void)click(menu, ui::tr("setup.original"));
    CHECK(menu.setup().rules == sim::Rules::original());
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);
}

TEST_CASE("game setup: the host's dialog keeps the lobby's players, the others only look") {
    app::GameSettingsMenu menu;
    const EndlessSetup six = app::multiplayer_setup(EndlessSetup::defaults(app::SetupScope::MultiplayerEndless), 6);
    menu.open(six, app::SetupScope::MultiplayerEndless, app::GameSettingsMenu::Mode::Apply);
    CHECK(find_text(menu, ui::tr("setup.title.multiplayer")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.start")).has_value());
    (void)click(menu, "6");  // the players: the lobby's
    CHECK(menu.setup().world.players == 6);
    (void)click(menu, ui::tr("setup.map.own"));  // more than four: always a world of their own
    CHECK(menu.setup().own_world);
    (void)click(menu, ui::tr("setup.original"));
    CHECK(menu.setup().rules == sim::Rules::original());
    CHECK(menu.setup().world.players == 6);
    CHECK(menu.setup().own_world);
    CHECK(click(menu, ui::tr("setup.apply")) == app::GameSettingsMenu::Result::Start);

    // Looking only: the values are text, no presets, no start.
    menu.open(six, app::SetupScope::MultiplayerEndless, app::GameSettingsMenu::Mode::View);
    const std::string refund = value_of(six, SetupItem::Refund);
    CHECK(click(menu, refund) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup() == six);
    CHECK(!find_text(menu, ui::tr("setup.original")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.apply")).has_value());
    CHECK(click(menu, ui::tr("setup.back")) == app::GameSettingsMenu::Result::Back);
    // A savegame's settings can only be looked at, whatever the caller asks.
    menu.open(EndlessSetup{}, app::SetupScope::Savegame, app::GameSettingsMenu::Mode::Apply);
    CHECK(menu.mode() == app::GameSettingsMenu::Mode::View);
}

TEST_CASE("game setup: the texts of every kind of game in English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        for (const std::string_view key : {"setup.title.multiplayer", "setup.title.scenario", "setup.title.savegame",
                                           "setup.hint.multiplayer", "setup.hint.scenario", "setup.hint.savegame",
                                           "setup.apply", "setup.custom", "setup.change", "setup.view"}) {
            CHECK(ui::tr(key) != key);
        }
        CHECK(ui::tr("setup.summary", {"x"}).find('x') != std::string::npos);
    }
    CHECK(ui::tr("setup.apply") == "Apply");
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("endless setup: the gold islands, automatic or a fixed number") {
    EndlessSetup s;
    CHECK(s.world.gold_islands == 0);
    CHECK(value_of(s, SetupItem::GoldIslands) == ui::tr("setup.gold_islands.auto"));
    CHECK(!app::setup_item_used(s, SetupItem::GoldIslands));  // the original's maps keep their gold
    s.own_world = true;
    CHECK(app::setup_item_used(s, SetupItem::GoldIslands));
    app::choose(s, SetupItem::GoldIslands, 3);
    CHECK(s.world.gold_islands == 3);
    CHECK(value_of(s, SetupItem::GoldIslands) == "3");
    CHECK(s.world_settings().gold_islands == 3);
    CHECK(!(s == EndlessSetup{}));
    app::choose(s, SetupItem::GoldIslands, app::setup_choices(SetupItem::GoldIslands) - 1);
    CHECK(s.world.gold_islands == 40);  // the most, for the largest maps
    app::choose(s, SetupItem::GoldIslands, 3);

    // settings.cfg, and with it the lobby, keeps the choice.
    CHECK(app::write_setup(s).find("endless.gold_islands = 3\n") != std::string::npos);
    CHECK(app::write_setup(EndlessSetup{}).find("endless.gold_islands = auto\n") != std::string::npos);
    EndlessSetup back;
    app::read_setup(back, app::write_setup(s, "multiplayer."), "multiplayer.");
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "gold_islands", "auto"));
    CHECK(back.world.gold_islands == 0);
    CHECK(app::read_setup_line(back, "gold_islands", "7"));  // between the dialog's values
    CHECK(back.world.gold_islands == 7);
    CHECK(app::setup_choice(back, SetupItem::GoldIslands) == app::setup_choice(s, SetupItem::GoldIslands) + 3);
    CHECK(app::read_setup_line(back, "gold_islands", "0"));  // not a number of islands: unchanged
    CHECK(app::read_setup_line(back, "gold_islands", "999"));
    CHECK(app::read_setup_line(back, "gold_islands", "many"));
    CHECK(back.world.gold_islands == 7);
}

TEST_CASE("game setup: every group of the dialog ends above its hint") {
    for (const auto scope :
         {app::SetupScope::Endless, app::SetupScope::MultiplayerEndless, app::SetupScope::Scenario}) {
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{}, scope);
        // The hint's first line lies below every item's row.
        const std::string hint = ui::tr(scope == app::SetupScope::Endless    ? "setup.hint"
                                        : scope == app::SetupScope::Scenario ? "setup.hint.scenario"
                                                                             : "setup.hint.multiplayer");
        std::int32_t hint_y = INT32_MAX;
        std::int32_t rows_y = 0;
        for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
            if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                continue;
            }
            if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {  // fitted to its room
                hint_y = std::min(hint_y, d.y);
            } else if (d.text == app::setup_label(SetupItem::Pirates)) {  // the last row of the lower column
                rows_y = d.y;
            }
        }
        REQUIRE(hint_y != INT32_MAX);
        CHECK(rows_y > 0);
        CHECK(rows_y + 27 <= hint_y);  // a row is 32 high, the hint's line 22: texts sit in their middles
        if (scope != app::SetupScope::Scenario) {
            CHECK(find_text(menu, ui::tr("setup.gold_islands")).has_value());
        }
    }
}

TEST_CASE("endless setup: the victory conditions, all off by default") {
    EndlessSetup s;
    CHECK(!s.rules.victory.active());
    CHECK(app::victory_summary(s.rules.victory) == ui::tr("setup.victory.none"));
    CHECK(app::victory_text(s.rules.victory).empty());
    for (std::size_t i = 0; i < app::kSetupItems; ++i) {
        const auto item = static_cast<SetupItem>(i);
        if (app::victory_item(item) && item != SetupItem::WinClass && item != SetupItem::WinAll &&
            item != SetupItem::WinComputers) {
            CHECK(value_of(s, item) == ui::tr("setup.off"));
        }
    }
    // A few conditions: they reach the rules.
    app::choose(s, SetupItem::WinInhabitants, 3);
    app::choose(s, SetupItem::WinClass, 3);
    app::choose(s, SetupItem::WinClassInhabitants, 4);
    app::choose(s, SetupItem::WinTimeLimit, 2);
    app::choose(s, SetupItem::WinMonument, 2);
    const sim::VictoryConditions& v = s.rules.victory;
    CHECK(v.inhabitants == 5000);
    CHECK(v.resident_class == 4);  // aristocrats
    CHECK(v.class_inhabitants == 1000);
    CHECK(v.time_limit_minutes == 120);
    CHECK(v.monument == sim::Monument::Cathedral);
    CHECK(app::victory_count(v) == 4);
    CHECK(app::victory_summary(v) == ui::tr("setup.victory.many", {"4"}));
    CHECK(app::setup_item_used(s, SetupItem::WinClass));
    CHECK(app::setup_item_used(s, SetupItem::WinAll));
    CHECK(!(s == EndlessSetup{}));

    // settings.cfg, and with it the lobby, keeps them; any number, not only the dialog's.
    EndlessSetup back;
    app::read_setup(back, app::write_setup(s, "multiplayer."), "multiplayer.");
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "victory.inhabitants", "7500"));
    CHECK(back.rules.victory.inhabitants == 7500);
    CHECK(app::setup_choice(back, SetupItem::WinInhabitants) != 0);  // shown as a value that is on
    CHECK(app::read_setup_line(back, "victory.islands", "150"));     // beyond 100 %: unchanged
    CHECK(back.rules.victory.islands_percent == 0);
    CHECK(app::read_setup_line(back, "victory.monument", "both"));
    CHECK(back.rules.victory.monument == sim::Monument::Both);
    CHECK(app::read_setup_line(back, "victory.combine", "any"));
    CHECK(!back.rules.victory.all);
    CHECK(!app::read_setup_line(back, "victory.unknown", "1"));

    // The mission window's sentence names them with their values.
    ui::set_language("de");
    const std::string text = app::victory_text(back.rules.victory);
    CHECK(text.find("Siegbedingungen") == 0);
    CHECK(text.find("7.500") != std::string::npos);
    CHECK(text.find("2 Std.") != std::string::npos);
    ui::set_language(ui::kFallbackLanguage);
    CHECK(app::victory_text(back.rules.victory).find("7,500") != std::string::npos);
}

TEST_CASE("game setup: victory conditions for endless games, not for scenarios") {
    using app::SetupScope;
    CHECK(app::victory_offered(SetupScope::Endless));
    CHECK(app::victory_offered(SetupScope::MultiplayerEndless));
    CHECK(app::victory_offered(SetupScope::Savegame));   // to look at
    CHECK(!app::victory_offered(SetupScope::Scenario));  // a scenario has its own missions
    CHECK(!app::setup_item_offered(SetupScope::Scenario, SetupItem::WinInhabitants));
    CHECK(app::setup_item_open(EndlessSetup{}, SetupScope::Endless, SetupItem::WinInhabitants));
    CHECK(!app::setup_item_open(EndlessSetup{}, SetupScope::Savegame, SetupItem::WinInhabitants));

    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{}, SetupScope::Scenario);
    CHECK(!find_text(menu, ui::tr("setup.victory")).has_value());
}

TEST_CASE("game setup: the page of the victory conditions") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{});
    const std::string open = ui::tr("setup.victory.open", {ui::tr("setup.victory.none")});
    CHECK(!find_text(menu, ui::tr("setup.win.inhabitants")).has_value());
    CHECK(click(menu, open) == app::GameSettingsMenu::Result::None);
    CHECK(menu.victory_page());
    CHECK(find_text(menu, ui::tr("setup.title.victory")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.original")).has_value());  // the presets are the rules'

    // A click on a condition turns it on.
    (void)click(menu, value_of(menu.setup(), SetupItem::WinInhabitants));
    CHECK(menu.setup().rules.victory.inhabitants == 1000);
    // Many conditions show "off": the one in the row of its label.
    const auto label = find_text(menu, app::setup_label(SetupItem::WinEliminate));
    REQUIRE(label.has_value());
    (void)menu.mouse(label->first + 260, label->second - 4, true);
    (void)menu.mouse(label->first + 260, label->second - 4, false);
    CHECK(menu.setup().rules.victory.eliminate);
    // "Back" leads to the settings, where the button counts them.
    CHECK(click(menu, ui::tr("setup.back")) == app::GameSettingsMenu::Result::None);
    CHECK(!menu.victory_page());
    CHECK(find_text(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.many", {"2"})})).has_value());
    // The presets change the rules, the conditions stay.
    (void)click(menu, ui::tr("setup.original"));
    CHECK(menu.setup().rules.classic_ai);
    CHECK(menu.setup().rules.victory.inhabitants == 1000);
    // Escape goes back to the settings first, then out.
    (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.many", {"2"})}));
    CHECK(menu.back());
    CHECK(!menu.back());
    // "All off" switches every condition off.
    (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.many", {"2"})}));
    (void)click(menu, ui::tr("setup.victory.off"));
    CHECK(!menu.setup().rules.victory.active());
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);

    // A savegame's conditions can be looked at, not changed.
    EndlessSetup saved;
    saved.rules.victory.score = 25'000;
    menu.open(saved, app::SetupScope::Savegame);
    (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.one")}));
    CHECK(menu.victory_page());
    CHECK(find_text(menu, value_of(saved, SetupItem::WinScore)).has_value());
    CHECK(!find_text(menu, ui::tr("setup.victory.off")).has_value());
    // Opened anew, the dialog starts on its first page.
    menu.open(saved, app::SetupScope::Endless);
    CHECK(!menu.victory_page());
}

TEST_CASE("game setup: the victory page's groups end above its hint, in English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{});
        (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.none")}));
        REQUIRE(menu.victory_page());
        std::int32_t lowest = 0;
        std::int32_t hint_y = INT32_MAX;
        const std::string hint = ui::tr("setup.hint.victory");
        for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
            if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                continue;
            }
            if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {
                hint_y = std::min(hint_y, d.y);
            }
            for (std::size_t i = 0; i < app::kSetupItems; ++i) {
                const auto item = static_cast<SetupItem>(i);
                if (app::victory_item(item) && d.text == app::setup_label(item)) {
                    lowest = std::max(lowest, d.y);
                }
            }
        }
        REQUIRE(hint_y != INT32_MAX);
        CHECK(lowest > 0);
        CHECK(lowest + 27 <= hint_y);
        CHECK(ui::tr("setup.hint.victory") != "setup.hint.victory");
    }
    ui::set_language(ui::kFallbackLanguage);
}
