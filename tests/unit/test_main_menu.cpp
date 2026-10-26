// SPDX-License-Identifier: GPL-3.0-or-later
// The main menu and the screens before a game (BT-1) without the original data: a folder with the
// four files an installation needs, synthetic BASE, HOST, MISSION and FARBWAHL layouts with one
// gadget per button the menu knows, a few scenario files and the GAME texts of the list headings.

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "main_menu.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"

using namespace o1602;
using app::MainMenu;
using app::MenuEvent;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};
constexpr std::int32_t kWidth = 20;
constexpr std::int32_t kHeight = 10;

/// The gadgets of a synthetic layout: each a centred text box of its own place.
struct Layout {
    std::vector<std::int32_t> ids;

    [[nodiscard]] std::string gad() const {
        std::string out = "Objekt: GADGET\n";
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto [x, y] = place(i);
            out += "  @Nummer: " + std::to_string(i) + "\n  Id: " + std::to_string(ids[i]) +
                   "\n  Kind: 6\n  Pos: " + std::to_string(x) + ", " + std::to_string(y) +
                   "\n  Size: " + std::to_string(kWidth) + ", " + std::to_string(kHeight) + "\n";
        }
        return out + "EndObj\n";
    }

    [[nodiscard]] static std::pair<std::int32_t, std::int32_t> place(std::size_t i) {
        return {static_cast<std::int32_t>(i % 20) * (kWidth + 10), 100 + static_cast<std::int32_t>(i / 20) * 20};
    }

    [[nodiscard]] std::pair<std::int32_t, std::int32_t> centre(std::int32_t id) const {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) {
                const auto [x, y] = place(i);
                return {x + kWidth / 2, y + kHeight / 2};
            }
        }
        REQUIRE(false);
        return {};
    }
};

std::vector<std::int32_t> range(std::int32_t first, std::int32_t count) {
    std::vector<std::int32_t> out;
    for (std::int32_t k = 0; k < count; ++k) {
        out.push_back(first + k);
    }
    return out;
}

Layout base_layout() {
    return {{30002, 30003, 30004, 30005, 30006}};
}

Layout host_layout() {
    Layout l{{31002, 31003, 31004, 31005, 31011, 31012}};
    for (const std::int32_t first : {31021, 31041, 31061}) {
        const auto rows = range(first, 14);
        l.ids.insert(l.ids.end(), rows.begin(), rows.end());
    }
    return l;
}

Layout mission_layout() {
    Layout l{{32001, 32002, 32004, 32005, 32006, 32007, 32031, 32041, 32091, 32092, 32093, 32094}};
    for (const auto& [first, count] :
         {std::pair{32011, 5}, {32021, 5}, {32051, 4}, {32061, 4}, {32071, 4}, {32081, 4}}) {
        const auto ids = range(first, count);
        l.ids.insert(l.ids.end(), ids.begin(), ids.end());
    }
    return l;
}

Layout colour_layout() {
    Layout l{{35002, 35003}};
    for (const std::int32_t first : {35011, 35021}) {
        const auto ids = range(first, 4);
        l.ids.insert(l.ids.end(), ids.begin(), ids.end());
    }
    return l;
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

/// An installation with the menu's layouts and the scenarios `scenarios` (file names in Szenes).
struct Game {
    std::filesystem::path root;
    std::optional<assets::GameDirectory> dir;
    assets::TextTable texts;

    Game(const std::string& name, const std::vector<std::string>& scenarios) {
        root = test::temp_path(name);
        std::filesystem::remove_all(root);
        for (const char* file : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            write(root / file, "");
        }
        write(root / "GADDATA" / "BASE.GAD", base_layout().gad());
        write(root / "GADDATA" / "HOST.GAD", host_layout().gad());
        write(root / "GADDATA" / "MISSION.GAD", mission_layout().gad());
        write(root / "GADDATA" / "FARBWAHL.GAD", colour_layout().gad());
        for (const std::string& s : scenarios) {
            write(root / "Szenes" / s, "not a scenario");  // no header: an adventure without stars
        }
        auto opened = assets::GameDirectory::open(root);
        REQUIRE(opened.ok());
        dir = std::move(opened).value();
        std::string game = "[GAME]\n";
        for (int i = 0; i < 89; ++i) {
            game += i == 87 ? "----Abenteuer----\n" : "Text " + std::to_string(i) + "\n";
        }
        texts = assets::TextTable::parse(game);
    }
    ~Game() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
};

std::vector<MenuEvent> click(MainMenu& menu, const Layout& layout, std::int32_t id) {
    const auto [x, y] = layout.centre(id);
    auto events = menu.mouse(x, y, true);
    for (MenuEvent& e : menu.mouse(x, y, false)) {
        events.push_back(std::move(e));
    }
    return events;
}

/// The texts the screen shows, in the order of its gadgets.
std::vector<std::string> texts(MainMenu& menu) {
    std::vector<std::string> out;
    for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && !d.text.empty()) {
            out.push_back(d.text);
        }
    }
    return out;
}

bool shows(MainMenu& menu, const std::string& text) {
    const auto all = texts(menu);
    return std::find(all.begin(), all.end(), text) != all.end();
}

}  // namespace

TEST_CASE("main menu: the buttons of the main menu answer with their events") {
    Game g("o1602-test-menu-base", {});
    MainMenu menu(*g.dir, &g.texts);
    REQUIRE(menu.usable());
    CHECK(menu.screen() == MainMenu::Screen::Base);
    const auto one = [&](std::int32_t id) {
        const auto events = click(menu, base_layout(), id);
        REQUIRE(events.size() == 1);
        return events[0].kind;
    };
    CHECK(one(30003) == MenuEvent::Kind::Multiplayer);
    CHECK(one(30004) == MenuEvent::Kind::Credits);
    CHECK(one(30005) == MenuEvent::Kind::Intro);
    CHECK(one(30006) == MenuEvent::Kind::Quit);
    CHECK(menu.screen() == MainMenu::Screen::Base);
    CHECK(!menu.back());  // Escape in the main menu: nothing to go back to
}

TEST_CASE("main menu: the list of scenarios, a mission screen with its parts, the rules before the start") {
    Game g("o1602-test-menu-list", {"Insel0.szs", "Insel1.szs", "Riff.szs"});
    MainMenu menu(*g.dir, &g.texts);
    CHECK(click(menu, base_layout(), 30002).empty());
    REQUIRE(menu.screen() == MainMenu::Screen::Host);
    // One entry per name without its digits, under the heading of the adventures.
    const auto list = texts(menu);
    REQUIRE(list.size() >= 3);
    CHECK(list[0] == "----Abenteuer----");
    CHECK(list[1] == "Insel");
    CHECK(list[2] == "Riff");
    // The heading is not a scenario.
    CHECK(click(menu, host_layout(), 31021).empty());
    CHECK(menu.screen() == MainMenu::Screen::Host);
    // Insel: a mission screen with two parts, the first chosen.
    click(menu, host_layout(), 31022);
    REQUIRE(menu.screen() == MainMenu::Screen::Mission);
    const auto parts = texts(menu);
    CHECK(std::count_if(parts.begin(), parts.end(),
                        [](const std::string& t) { return t.find('1') != std::string::npos; }) >= 1);
    CHECK(std::count_if(parts.begin(), parts.end(),
                        [](const std::string& t) { return t.find('2') != std::string::npos; }) >= 1);
    click(menu, mission_layout(), 32012);  // the second part
    // Start: the rules first (Open1602's own dialog), Escape back to the mission.
    CHECK(click(menu, mission_layout(), 32001).empty());
    CHECK(menu.screen() == MainMenu::Screen::Settings);
    CHECK(menu.back());
    CHECK(menu.screen() == MainMenu::Screen::Mission);
    click(menu, mission_layout(), 32002);  // cancel
    CHECK(menu.screen() == MainMenu::Screen::Host);
    CHECK(menu.back());
    CHECK(menu.screen() == MainMenu::Screen::Base);
    // "Neues Spiel" on the end screen finds the scenario again by its name.
    CHECK(menu.open_mission("insel"));
    CHECK(menu.screen() == MainMenu::Screen::Mission);
    CHECK(!menu.open_mission("Nowhere"));
    CHECK(menu.screen() == MainMenu::Screen::Base);
}

TEST_CASE("main menu: a long list scrolls by fourteen rows") {
    std::vector<std::string> files;
    for (char c = 'A'; c <= 'T'; ++c) {
        files.push_back(std::string("Karte") + c + ".szs");
    }
    Game g("o1602-test-menu-scroll", files);
    MainMenu menu(*g.dir, &g.texts);
    click(menu, base_layout(), 30002);
    REQUIRE(menu.screen() == MainMenu::Screen::Host);
    CHECK(shows(menu, "KarteA"));
    CHECK(!shows(menu, "KarteT"));  // heading and 20 scenarios: 21 entries, 14 rows
    click(menu, host_layout(), 31012);
    CHECK(shows(menu, "KarteT"));
    CHECK(!shows(menu, "KarteA"));
    click(menu, host_layout(), 31011);
    CHECK(shows(menu, "KarteA"));
}

TEST_CASE("main menu: the savegames to load, and the last game to continue") {
    Game g("o1602-test-menu-load", {});
    MainMenu menu(*g.dir, &g.texts);
    app::SaveSlots slots;
    slots.set(1, "Mein Spiel", 1);
    menu.set_slots(slots);
    click(menu, base_layout(), 30002);
    click(menu, host_layout(), 31003);  // "Spiel laden"
    CHECK(shows(menu, "Mein Spiel"));
    CHECK(click(menu, host_layout(), 31021).empty());  // a free slot loads nothing
    const auto load = click(menu, host_layout(), 31022);
    REQUIRE(load.size() == 1);
    CHECK(load[0].kind == MenuEvent::Kind::LoadSlot);
    CHECK(load[0].slot == 1);
    // The first row is the last game, loaded like the others.
    slots.set(app::kLastGameSlot, std::string(app::kLastGameName), 1);
    menu.set_slots(slots);
    click(menu, host_layout(), 31003);
    CHECK(shows(menu, "lastgame"));
    const auto first = click(menu, host_layout(), 31021);
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == MenuEvent::Kind::LoadSlot);
    CHECK(first[0].slot == app::kLastGameSlot);
    const auto last = click(menu, host_layout(), 31004);
    REQUIRE(last.size() == 1);
    CHECK(last[0].kind == MenuEvent::Kind::ContinueLast);
    click(menu, host_layout(), 31005);  // back to the main menu
    CHECK(menu.screen() == MainMenu::Screen::Base);
}

TEST_CASE("main menu: without its layouts the menu is not usable") {
    Game g("o1602-test-menu-missing", {});
    std::filesystem::remove(g.root / "GADDATA" / "HOST.GAD");
    MainMenu menu(*g.dir, &g.texts);
    CHECK(!menu.usable());
}

TEST_CASE("main menu: the screen \"Mods\" turns packages on and off and takes in received ones") {
    Game g("o1602-test-menu-mods", {});
    MainMenu menu(*g.dir, &g.texts);
    REQUIRE(menu.usable());
    const auto press = [&](std::int32_t x, std::int32_t y) {
        auto events = menu.mouse(x, y, true);
        for (MenuEvent& e : menu.mouse(x, y, false)) {
            events.push_back(std::move(e));
        }
        return events;
    };
    // Open1602's own button below the original's.
    auto opened = press(104, 733);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0].kind == MenuEvent::Kind::Mods);
    CHECK(menu.screen() == MainMenu::Screen::Mods);

    app::ModsModel model;
    app::ModsModel::Entry own;
    own.id = "leuchtturm";
    own.folder = "mods/leuchtturm";
    own.name = "Leuchtturm";
    own.version = "1.2";
    app::ModsModel::Entry received = own;
    received.id = "wetter";
    received.folder = "mods-cache/wetter-0123456789abcdef";
    received.name = "Wetter";
    received.state = app::ModsModel::Entry::State::Received;
    received.adoptable = true;
    model.entries = {own, received};
    menu.set_mods(model);
    CHECK(shows(menu, "Leuchtturm"));
    CHECK(shows(menu, app::ModsMenu::state_text(app::ModsModel::Entry::State::Received)));

    auto toggled = press(84, 203);  // the check box of the first row
    REQUIRE(toggled.size() == 1);
    CHECK(toggled[0].kind == MenuEvent::Kind::ToggleMod);
    CHECK(toggled[0].name == "leuchtturm");

    CHECK(press(300, 237).empty());  // chooses the received package
    auto adopted = press(200, 686);
    REQUIRE(adopted.size() == 1);
    CHECK(adopted[0].kind == MenuEvent::Kind::AdoptMod);
    CHECK(adopted[0].path == received.folder);

    CHECK(menu.back());
    CHECK(menu.screen() == MainMenu::Screen::Base);
}
