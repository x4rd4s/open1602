// SPDX-License-Identifier: GPL-3.0-or-later
// Which installation the game plays with (engine/app/src/game_folder.hpp) and the line of the
// folder chosen in settings.cfg.

#include <filesystem>
#include <fstream>
#include <string>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "game_folder.hpp"

#include "annokit/core/file.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;

namespace {

/// A folder with the files GameDirectory::open() asks for.
void make_installation(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "TOOLGFX");
    std::filesystem::create_directories(root / "GFX");
    for (const char* file : {"Haeuser.cod", "Figuren.cod", "TOOLGFX/STADTFLD.COL", "GFX/STADTFLD.BSH"}) {
        std::ofstream(root / file) << "";
    }
}

}  // namespace

TEST_CASE("game folder: --game-dir, then ANNO1602_DIR, then the saved folder") {
    const auto base = test::temp_path("o1602-test-game-folder-order");
    std::filesystem::remove_all(base);
    const auto a = base / "A 1602";
    const auto b = base / "B 1602";
    const auto c = base / "C 1602";
    make_installation(a);
    make_installation(b);
    make_installation(c);
    const std::string sa = annokit::core::path_to_utf8(a);
    const std::string sb = annokit::core::path_to_utf8(b);
    const std::string sc = annokit::core::path_to_utf8(c);

    auto found = app::find_game_folder(sa, sc, sb);
    REQUIRE(found.game);
    CHECK(found.game->root() == a);
    found = app::find_game_folder("", sc, "\"" + sb + "\"");  // quotes around the variable's value
    REQUIRE(found.game);
    CHECK(found.game->root() == b);
    found = app::find_game_folder("", sc, std::nullopt);
    REQUIRE(found.game);
    CHECK(found.game->root() == c);
    found = app::find_game_folder("", sc, std::string(" "));  // set, but empty: as if not set
    REQUIRE(found.game);
    CHECK(found.game->root() == c);

    // A folder named on the command line or in the variable is never replaced, nor asked for.
    found = app::find_game_folder(annokit::core::path_to_utf8(base / "typo"), sc, std::nullopt);
    CHECK(!found.game);
    CHECK(!found.ask);
    CHECK(found.error.message.find("typo") != std::string::npos);
    found = app::find_game_folder("", sc, annokit::core::path_to_utf8(base / "typo"));
    CHECK(!found.game);
    CHECK(!found.ask);

    // A saved folder that is gone: an installation in the usual folders, else the question.
    found = app::find_game_folder("", annokit::core::path_to_utf8(base / "moved"), std::nullopt);
    CHECK(found.saved_invalid == base / "moved");
    CHECK((found.game.has_value() || found.ask));
    std::filesystem::remove_all(base);
}

TEST_CASE("game folder: the folder chosen, or the one installation in it") {
    const auto base = test::temp_path("o1602-test-game-folder-chosen");
    std::filesystem::remove_all(base);
    make_installation(base / "GOG Games" / "Anno 1602 Koenigsedition");
    std::filesystem::create_directories(base / "GOG Games" / "Anno 1503");

    auto chosen = app::open_chosen_folder(base / "GOG Games" / "Anno 1602 Koenigsedition");
    REQUIRE(chosen);
    CHECK(chosen->root() == base / "GOG Games" / "Anno 1602 Koenigsedition");
    chosen = app::open_chosen_folder(base / "GOG Games");  // one level too high
    REQUIRE(chosen);
    CHECK(chosen->root() == base / "GOG Games" / "Anno 1602 Koenigsedition");
    CHECK(!app::open_chosen_folder(base / "GOG Games" / "Anno 1503"));

    // Two installations inside: the player has to say which.
    make_installation(base / "GOG Games" / "Anno 1602 Copy");
    CHECK(!app::open_chosen_folder(base / "GOG Games"));
    std::filesystem::remove_all(base);
}

TEST_CASE("game folder: why a folder is no installation") {
    ui::set_language("en");
    const auto base = test::temp_path("o1602-test-game-folder-problem");
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base / "Anno");
    std::ofstream(base / "Anno" / "haeuser.cod") << "";

    const std::string gone = app::folder_problem(base / "gone");
    CHECK(gone.find("does not exist") != std::string::npos);
    CHECK(gone.find(annokit::core::path_to_utf8(base / "gone")) != std::string::npos);
    const std::string partial = app::folder_problem(base / "Anno");
    CHECK(partial.find("figuren.cod") != std::string::npos);
    CHECK(assets::GameDirectory::missing_file(base / "Anno") == std::optional<std::string_view>("figuren.cod"));

    make_installation(base / "Anno");
    CHECK(app::folder_problem(base / "Anno").empty());
    CHECK(!assets::GameDirectory::missing_file(base / "Anno"));

    // Several installations inside: the text names them (FB-28).
    make_installation(base / "GOG" / "Anno 1602 A");
    make_installation(base / "GOG" / "Anno 1602 B");
    const std::string several = app::folder_problem(base / "GOG");
    CHECK(several.find("several") != std::string::npos);
    CHECK(several.find("\nAnno 1602 A") != std::string::npos);
    CHECK(several.find("\nAnno 1602 B") != std::string::npos);
    std::filesystem::remove_all(base);
}

TEST_CASE("game folder: texts in UTF-8 keep every letter of a path") {
    ui::set_language("de");
    // "Jürgen" in Windows-1252 would be one byte; the dialogs of the system want UTF-8.
    const std::string text = app::tr_utf8("game_folder.missing", {"/home/J\xc3\xbcrgen/\xd0\x90nno"});
    CHECK(text.find("/home/J\xc3\xbcrgen/\xd0\x90nno") != std::string::npos);
    CHECK(text.find("gibt es nicht") != std::string::npos);
    CHECK(app::tr_utf8("game_folder.saved_invalid").find("gew\xc3\xa4hlte") != std::string::npos);  // UTF-8
    ui::set_language("en");
}

TEST_CASE("game folder: the chosen folder goes into settings.cfg, the other lines stay") {
    const std::string before = "# Open1602\nmusic = off\ngame_folder = /old\nlanguage = de";
    const std::string after = app::set_setting_line(before, "game_folder", "/games/Anno 1602");
    CHECK(after == "# Open1602\nmusic = off\ngame_folder = /games/Anno 1602\nlanguage = de\n");
    CHECK(app::set_setting_line("", "game_folder", "/a") == "game_folder = /a\n");
    CHECK(app::set_setting_line("music = on\r\n# game_folder = /x\n", "game_folder", "/a") ==
          "music = on\r\n# game_folder = /x\ngame_folder = /a\n");

    const app::AppSettings read = app::parse_settings(after, app::AppSettings{});
    CHECK(read.game_folder == "/games/Anno 1602");
    CHECK(!read.audio.music);
    // The game writes its whole settings file later: the folder stays in it.
    const std::string written = app::write_settings(read);
    CHECK(app::parse_settings(written, app::AppSettings{}).game_folder == "/games/Anno 1602");
    CHECK(app::write_settings(app::AppSettings{}).find("game_folder") == std::string::npos);
}
