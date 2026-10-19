// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own texts in the language of the installation (ui/localization.hpp,
// docs/modding/languages.md).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "../test_framework.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

namespace {

/// A folder of its own under the system's temporary folder, removed again at the end.
struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(std::string_view name) : path(test::temp_path(std::string(name))) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    void write(std::string_view name, std::string_view text) const {
        std::ofstream out(path / name, std::ios::binary);
        out << text;
    }
};

/// Puts English back for the tests that follow, whatever a test chose.
struct LanguageGuard {
    LanguageGuard() = default;
    ~LanguageGuard() { ui::set_language(ui::kFallbackLanguage); }
    LanguageGuard(const LanguageGuard&) = delete;
    LanguageGuard& operator=(const LanguageGuard&) = delete;
};

}  // namespace

TEST_CASE("localization: a catalogue keeps its texts in Windows-1252 with line breaks") {
    const ui::Catalogue c = ui::Catalogue::parse(
        "\xEF\xBB\xBF# a comment\n"
        "\n"
        "dialog.yes = Ja\r\n"
        "dialog.quit = Spiel beenden?\\nDer Stand wird als \xE2\x80\x9Elastgame\xE2\x80\x9C gesichert.\n"
        "no equals sign\n"
        "  spaced.key   =   Gr\xC3\xB6\xC3\x9F"
        "e  \n");
    CHECK(c.size() == 3);
    REQUIRE(c.find("dialog.yes") != nullptr);
    CHECK(*c.find("dialog.yes") == "Ja");
    CHECK(*c.find("dialog.quit") == "Spiel beenden?\nDer Stand wird als \x84lastgame\x93 gesichert.");
    CHECK(*c.find("spaced.key") ==
          "Gr\xF6\xDF"
          "e");
    CHECK(c.find("missing") == nullptr);
}

TEST_CASE("localization: English and German are built in, and German has no key English lacks") {
    const auto codes = ui::builtin_languages();
    CHECK(std::find(codes.begin(), codes.end(), "en") != codes.end());
    CHECK(std::find(codes.begin(), codes.end(), "de") != codes.end());
    const ui::Catalogue en = ui::builtin_catalogue("en");
    CHECK(en.size() > 50);
    for (const std::string& code : codes) {
        const ui::Catalogue c = ui::builtin_catalogue(code);
        CHECK(c.find("language.name") != nullptr);
        CHECK(c.find("language.detect") != nullptr);
        for (const std::string& key : c.keys()) {
            CHECK(en.find(key) != nullptr);  // English is the fallback for every key
        }
    }
    CHECK(ui::builtin_catalogue("xx").size() == 0);
}

TEST_CASE("localization: the language of an installation comes from Text.cod's player colours") {
    const auto german = assets::TextTable::parse(
        "[PLAYCOL]\nRoter Spieler\nBlauer Spieler\nGelber Spieler\nWei\xDF"
        "er Spieler\n[END]\n");
    CHECK(ui::detect_language(german) == "de");
    const auto english = assets::TextTable::parse("[PLAYCOL]\nRed Player\nBlue Player\n[END]\n");
    CHECK(ui::detect_language(english) == "en");
    const auto unknown = assets::TextTable::parse("[PLAYCOL]\nJoueur rouge\n[END]\n");
    CHECK(ui::detect_language(unknown).empty());
    CHECK(ui::detect_language(assets::TextTable{}).empty());
}

TEST_CASE("localization: GOG's info file names the language when Text.cod does not") {
    const TempFolder folder("o1602-test-gog-language");
    for (const char* file : {"haeuser.cod", "figuren.cod"}) {
        folder.write(file, "");
    }
    std::filesystem::create_directories(folder.path / "toolgfx");
    std::filesystem::create_directories(folder.path / "gfx");
    folder.write("toolgfx/stadtfld.col", "");
    folder.write("gfx/stadtfld.bsh", "");
    auto game = assets::GameDirectory::open(folder.path);
    REQUIRE(game);
    CHECK(ui::gog_language(*game).empty());
    CHECK(ui::installation_language(*game, nullptr) == "en");  // nothing tells: English

    folder.write("goggame-1438168222.info",
                 "{\n  \"language\": \"German\",\n  \"languages\": [\n    \"de-DE\"\n  ]\n}\n");
    CHECK(ui::gog_language(*game) == "de");
    CHECK(ui::installation_language(*game, nullptr) == "de");
    const auto english = assets::TextTable::parse("[PLAYCOL]\nRed Player\n[END]\n");
    CHECK(ui::installation_language(*game, &english) == "en");  // Text.cod comes first
}

TEST_CASE("localization: tr answers in the chosen language, else in English, else with the key") {
    const LanguageGuard guard;
    CHECK(ui::tr("dialog.yes") == "Yes");  // English before a language is chosen

    ui::set_language("de");
    CHECK(ui::current_language() == "de");
    CHECK(ui::tr("dialog.yes") == "Ja");
    CHECK(ui::tr("keys.action.white_flag") ==
          "Wei\xDF"
          "e Flagge");
    CHECK(ui::tr("keys.group_digits") == "1 .. 0");  // only in English
    CHECK(ui::tr("no.such.key") == "no.such.key");
    CHECK(ui::tr("dialog.break_peace", {"Rot"}) == "Friedensvertrag mit Rot brechen?\nDas bedeutet Krieg.");

    ui::set_language("FR");  // no catalogue: English
    CHECK(ui::current_language() == "fr");
    CHECK(ui::tr("dialog.yes") == "Yes");
}

TEST_CASE("localization: arguments go where the translation puts them") {
    const LanguageGuard guard;
    const TempFolder folder("o1602-test-locale");
    folder.write("xx.lang", "test.order = {2} before {1}, {3} missing, {x} and {0} stay\n");
    ui::set_language("xx", folder.path);
    CHECK(ui::tr("test.order", {"one", "two"}) == "two before one,  missing, {x} and {0} stay");
}

TEST_CASE("localization: a catalogue in the user's folder extends and overrides the built-in one") {
    const LanguageGuard guard;
    const TempFolder folder("o1602-test-locale-user");
    folder.write("de.lang", "dialog.yes = Jawohl\nmod.text = Neu\n");
    ui::set_language("de", folder.path);
    CHECK(ui::tr("dialog.yes") == "Jawohl");
    CHECK(ui::tr("dialog.no") == "Nein");
    CHECK(ui::tr("mod.text") == "Neu");
}
