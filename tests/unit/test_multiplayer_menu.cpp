// SPDX-License-Identifier: GPL-3.0-or-later
// The multiplayer menu (ADR 0011): addresses, its screens at work, its texts in both languages.

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"
#include "multiplayer_menu.hpp"
#include "multiplayer_texts.hpp"

#include "o1602/net/loopback.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::MpText;
using app::MultiplayerEvent;
using app::MultiplayerMenu;

namespace {

constexpr std::array<const ui::Font*, 3> kNoFonts = {nullptr, nullptr, nullptr};

/// Where the text is drawn (its anchor in design pixels), if the screen shows it.
std::optional<std::pair<std::int32_t, std::int32_t>> find_text(MultiplayerMenu& menu,
                                                               const app::MultiplayerModel& model,
                                                               const std::string& text) {
    for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text == text) {
            return std::pair{d.x, d.y};
        }
    }
    return std::nullopt;
}

/// A click on the widget that shows `text` (just above its text's base line).
std::vector<MultiplayerEvent> click(MultiplayerMenu& menu, const app::MultiplayerModel& model,
                                    const std::string& text) {
    const auto at = find_text(menu, model, text);
    REQUIRE(at.has_value());
    (void)menu.mouse(model, at->first, at->second - 4, true);
    return menu.mouse(model, at->first, at->second - 4, false);
}

/// A click at a point of the design.
std::vector<MultiplayerEvent> click_at(MultiplayerMenu& menu, const app::MultiplayerModel& model, std::int32_t x,
                                       std::int32_t y) {
    (void)menu.mouse(model, x, y, true);
    return menu.mouse(model, x, y, false);
}

/// Puts English back for the tests that follow.
struct LanguageGuard {
    explicit LanguageGuard(std::string_view code) { ui::set_language(code); }
    ~LanguageGuard() { ui::set_language(ui::kFallbackLanguage); }
    LanguageGuard(const LanguageGuard&) = delete;
    LanguageGuard& operator=(const LanguageGuard&) = delete;
};

}  // namespace

TEST_CASE("multiplayer menu: addresses with and without a port") {
    const auto plain = app::parse_address(" 192.168.1.20 ");
    REQUIRE(plain.has_value());
    CHECK(plain->first == "192.168.1.20");
    CHECK(plain->second == net::kDefaultPort);
    const auto named = app::parse_address("anno.example.org:17000");
    REQUIRE(named.has_value());
    CHECK(named->first == "anno.example.org");
    CHECK(named->second == 17000);
    CHECK(!app::parse_address("").has_value());
    CHECK(!app::parse_address("host:").has_value());
    CHECK(!app::parse_address("host:0").has_value());
    CHECK(!app::parse_address("host:70000").has_value());
    CHECK(!app::parse_address("two words").has_value());
    CHECK(!app::parse_address(":16020").has_value());
}

TEST_CASE("multiplayer menu: every text in both languages, with its value") {
    const ui::Catalogue english = ui::builtin_catalogue("en");
    const ui::Catalogue german = ui::builtin_catalogue("de");
    for (std::size_t i = 0; i <= static_cast<std::size_t>(MpText::HostLeftSaved); ++i) {
        const std::string_view key = app::mp_key(static_cast<MpText>(i));
        CHECK(english.find(key) != nullptr);
        CHECK(german.find(key) != nullptr);
    }
    CHECK(app::mp_text(MpText::PlayerJoined, "Anna") == "Anna joined.");
    CHECK(app::mp_text(MpText::HostLeftSaved, "hostleft.o1602").find("hostleft.o1602") != std::string::npos);
    CHECK(!app::mp_text(net::RefuseReason::Kicked).empty());
    CHECK(!app::mp_text(net::EndReason::HostLeft).empty());
    const LanguageGuard german_game("de");
    CHECK(app::mp_text(MpText::PlayerJoined, "Anna") == "Anna ist beigetreten.");
    // The fonts' Windows-1252: "Zurück" with 0xFC.
    CHECK(app::mp_text(MpText::Back) ==
          "Zur\xFC"
          "ck");
    CHECK(app::mp_text(net::RefuseReason::Password) == "Falsches Kennwort.");
}

TEST_CASE("multiplayer menu: searching, joining by address and back") {
    const LanguageGuard german_game("de");
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    menu.open();
    CHECK(menu.screen() == MultiplayerMenu::Screen::Browse);
    CHECK(find_text(menu, model, app::mp_text(MpText::NoGamesFound)).has_value());
    CHECK(menu.name() == "Anna");

    // A game that answered the search: one click chooses it, the button joins it.
    net::FoundGame found;
    found.address = "192.168.1.20";
    found.info.title = "Annas Spiel";
    found.info.host = "Anna";
    found.info.players = 1;
    found.info.slots = 4;
    found.ping_ms = 12;
    model.found.push_back(found);
    CHECK(click(menu, model, "Annas Spiel").empty());
    auto events = click(menu, model, app::mp_text(MpText::Join));
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Join);
    CHECK(events[0].address == "192.168.1.20");
    CHECK(events[0].port == net::kDefaultPort);

    // An address typed in, with a password; Enter connects.
    CHECK(click_at(menu, model, 800, 166).empty());  // the address field
    CHECK(menu.typing());
    menu.type("10.0.0.7:17000");
    (void)menu.key(model, 9);  // Tab: the password
    menu.type("geheim");
    events = menu.key(model, 13);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Join);
    CHECK(events[0].address == "10.0.0.7");
    CHECK(events[0].port == 17000);
    CHECK(events[0].password == "geheim");
    CHECK(!menu.typing());

    // Escape: on the search it closes the menu.
    const auto back = menu.back();
    REQUIRE(back.has_value());
    CHECK(back->kind == MultiplayerEvent::Kind::Close);
}

TEST_CASE("multiplayer menu: creating a game: endless, scenario, settings") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.level_names = {"Easy", "Medium", "Hard", "Very hard"};
    app::MenuScenario scenario;
    scenario.name = "Die Insel";
    scenario.files = {{0xFF, "Szenes/Insel.szs"}};
    model.scenarios.push_back(scenario);
    menu.open();
    CHECK(click(menu, model, app::mp_text(MpText::CreateGame)).empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Create);
    // Escape goes back to the search by itself: nothing for the viewer.
    CHECK(!menu.back().has_value());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Browse);
    CHECK(click(menu, model, "Create game").empty());

    CHECK(click(menu, model, "Hard").empty());
    CHECK(click(menu, model, "Host without a player").empty());
    auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Host);
    const app::HostSetup& endless = events[0].setup;
    CHECK(endless.kind == app::HostSetup::Kind::Endless);
    CHECK(endless.level == 2);
    CHECK(endless.dedicated);
    CHECK(endless.title == "Bernd's game");
    CHECK(endless.port == net::kDefaultPort);
    CHECK(endless.file_name.find("Hard") != std::string::npos);

    // A scenario must be chosen before the lobby opens.
    CHECK(click(menu, model, "Scenario").empty());
    CHECK(click(menu, model, "Open lobby").empty());  // not yet: nothing chosen
    CHECK(click(menu, model, "Die Insel").empty());
    events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.kind == app::HostSetup::Kind::Scenario);
    CHECK(events[0].setup.file == std::filesystem::path("Szenes/Insel.szs"));
    CHECK(events[0].setup.file_name == "Die Insel");
}

TEST_CASE("multiplayer menu: the host's lobby: slots, chat and the start") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(4);
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    model.everyone_ready = host.everyone_ready();
    menu.show_lobby();
    CHECK(find_text(menu, model, "Abend").has_value());
    CHECK(find_text(menu, model, "Anna (you)").has_value());
    CHECK(menu.typing());  // the chat line takes the keys

    // The host clicks an open slot through the kinds.
    auto events = click(menu, model, "Open");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::SetSeat);
    CHECK(events[0].seat == 1);
    CHECK(events[0].seat_kind == net::LobbySeat::Kind::Computer);

    menu.type("Hallo");
    events = menu.key(model, 13);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Say);
    CHECK(events[0].text == "Hallo");
    CHECK(menu.typing());  // the chat line stays open

    events = click(menu, model, "Start game");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Start);

    // Escape twice: out of the chat line, then out of the session.
    (void)menu.key(model, 27);
    const auto back = menu.back();
    REQUIRE(back.has_value());
    CHECK(back->kind == MultiplayerEvent::Kind::Leave);
}

TEST_CASE("multiplayer menu: the host sends a spectator away") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(1);  // only the host's: whoever comes watches
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    net::Hello hello;
    hello.content = 1;
    hello.name = "Wilma";
    net::ClientSession wilma(network.connect(), hello, [](std::span<const std::uint8_t>) { return nullptr; });
    for (int i = 0; i < 20; ++i) {
        host.update(50);
        wilma.update(50);
    }
    REQUIRE(host.roster().size() == 2);
    REQUIRE(host.roster()[1].seat == sim::kNoPlayer);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    menu.show_lobby();
    CHECK(find_text(menu, model, "Wilma").has_value());
    const auto events = click(menu, model, "Remove");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::KickMember);
    CHECK(events[0].member == 1);
}

TEST_CASE("multiplayer menu: four to twelve players, the factions' slots left out of the lobby") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.level_names = {"Easy", "Medium", "Hard", "Very hard"};
    menu.open();
    CHECK(click(menu, model, "Create game").empty());
    CHECK(find_text(menu, model, "4").has_value());
    CHECK(click(menu, model, "-").empty());  // never fewer than four
    CHECK(find_text(menu, model, "4").has_value());
    CHECK(click(menu, model, "+").empty());
    CHECK(click(menu, model, "+").empty());
    auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.players == 6);
    CHECK(events[0].setup.file_name.find("6 players") != std::string::npos);

    // Six players: slots 0-3 and 7-8; the free trader's, pirates' and natives' are not shown.
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.seats.resize(9);
    for (std::size_t s = 4; s < 7; ++s) {
        lobby.seats[s].kind = net::LobbySeat::Kind::Reserved;
    }
    net::HostSession host(network.listen(), lobby, 0, "Bernd", 1);
    host.set_seat(5, net::LobbySeat::Kind::Open);  // the pirates' slot stays out of the game
    CHECK(host.lobby().seats[5].kind == net::LobbySeat::Kind::Reserved);
    model.session = &host;
    model.hosting = true;
    menu.show_lobby();
    std::size_t open = 0;
    for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
        open += d.kind == ui::DrawItem::Kind::Text && d.text == "Open" ? 1U : 0U;
    }
    CHECK(open == 5);
    // The host clicks a slot between computer and open: every game has all its players.
    events = click(menu, model, "Open");
    REQUIRE(events.size() == 1);
    CHECK(events[0].seat_kind == net::LobbySeat::Kind::Computer);
}
