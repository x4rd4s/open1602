// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own settings files (engine/app/src/app_settings.hpp): settings.cfg and slots.cfg are
// UTF-8, the names in the game Windows-1252.

#include <string>

#include "../test_framework.hpp"
#include "app_settings.hpp"

using namespace o1602;

TEST_CASE("app settings: names are written as UTF-8 and read back") {
    app::AppSettings s;
    s.player_name = "J\xfcrgen";  // Windows-1252, as typed in the menu
    s.remembered_hosts = {"example.org:2601"};
    const std::string text = app::write_settings(s);
    CHECK(text.find("player_name = J\xc3\xbcrgen\n") != std::string::npos);
    CHECK(text.find("UTF-8") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.player_name == "J\xfcrgen");
    CHECK(back.remembered_hosts.size() == 1);
    CHECK(back.remembered_hosts[0] == "example.org:2601");
}

TEST_CASE("app settings: a byte order mark and files of older versions") {
    // Saved by an editor with a byte order mark: the first key still counts.
    const app::AppSettings bom =
        app::parse_settings("\xef\xbb\xbfmusic = off\nplayer_name = \xc3\x84gir\n", app::AppSettings{});
    CHECK(!bom.audio.music);
    CHECK(bom.player_name == "\xc4gir");
    // Older versions wrote the Windows-1252 bytes as they were: not UTF-8, so taken as they are.
    const app::AppSettings old = app::parse_settings("player_name = J\xfcrgen\n", app::AppSettings{});
    CHECK(old.player_name == "J\xfcrgen");
    // What Windows-1252 cannot show is left out.
    const app::AppSettings wide = app::parse_settings("player_name = A\xe2\x86\x92Z\n", app::AppSettings{});
    CHECK(wide.player_name == "AZ");
}

TEST_CASE("app settings: savegame names are written as UTF-8 and read back") {
    app::SaveSlots slots;
    slots.set(0, "Fu\xdf", 1);  // sharp s
    slots.set(2, "plain", 2);
    const std::string text = app::write_slots(slots);
    CHECK(text.find("0 1 Fu\xc3\x9f\n") != std::string::npos);
    const app::SaveSlots back = app::parse_slots("\xef\xbb\xbf" + text);
    CHECK(back.names[0] == "Fu\xdf");
    CHECK(back.humans[0] == 1);
    CHECK(back.names[2] == "plain");
    // A file of an older version.
    CHECK(app::parse_slots("1 1 M\xfchle\n").names[1] == "M\xfchle");
}
