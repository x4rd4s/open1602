// SPDX-License-Identifier: GPL-3.0-or-later
// The text files of the user's folder and the game: settings.cfg (with its endless.*, display.*
// lines), slots.cfg, keys.cfg, a .lang catalogue, the high score lists (.hss) and Game.dat's
// savegame names, plus the address field of the multiplayer menu. A settings file read and written
// again must read back to the same file (what the game writes, it reads alike).
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "app_settings.hpp"
#include "endless_setup.hpp"
#include "fuzz_support.hpp"
#include "multiplayer_menu.hpp"

#include "o1602/assets/scenario.hpp"
#include "o1602/ui/key_bindings.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text = fuzz::as_text({data, size});

    static const app::AppSettings defaults;
    const app::AppSettings settings = app::parse_settings(text, defaults);
    // One round may still normalise (bytes Windows-1252 leaves undefined become U+FFFD when
    // written and are dropped when read back); from then on the file must stay as it is.
    const std::string first = app::write_settings(settings);
    const std::string written = app::write_settings(app::parse_settings(first, defaults));
    const std::string again = app::write_settings(app::parse_settings(written, defaults));
    if (again != written) {
        std::fputs("--- first\n", stderr);
        std::fwrite(written.data(), 1, written.size(), stderr);
        std::fputs("--- second\n", stderr);
        std::fwrite(again.data(), 1, again.size(), stderr);
        fuzz::fail("settings.cfg written, read and written again differ");
    }

    const app::SaveSlots slots = app::parse_slots(text);
    // As for settings.cfg: one round may normalise the names, after that the file is stable.
    const std::string slots_text = app::write_slots(app::parse_slots(app::write_slots(slots)));
    if (app::write_slots(app::parse_slots(slots_text)) != slots_text) {
        std::fwrite(slots_text.data(), 1, slots_text.size(), stderr);
        fuzz::fail("slots.cfg round trip");
    }

    app::EndlessSetup setup = app::EndlessSetup::defaults();
    app::read_setup(setup, text);

    ui::KeyBindings keys;
    const ui::KeyBindings::KeyFromName key_from_name = [](std::string_view name, bool) -> std::optional<std::int32_t> {
        if (name.size() == 1) {
            return static_cast<unsigned char>(name[0]);
        }
        return std::nullopt;
    };
    (void)keys.read_text(text, key_from_name);
    (void)ui::KeyBindings::parse_chord(text, key_from_name);

    (void)ui::Catalogue::parse(text);
    (void)assets::parse_high_scores(text);
    (void)assets::parse_savegame_slots(text);
    (void)app::parse_address(text);
    return 0;
}
