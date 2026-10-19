// SPDX-License-Identifier: GPL-3.0-or-later
//
// open1602 - map viewer (milestone M2). Usage:
//
//   open1602 [--game-dir DIR] [--scenario FILE | --savegame NAME | --load FILE] [--size WxH] [--zoom 0|1|2]
//            [--owners] [--original-sheets] [--frames N] [--screenshot out.png]
//
// The game directory defaults to $ANNO1602_DIR. Without a game the program starts in the main menu
// (--menu forces it); with --no-panel the scenario defaults to "Szenes/Das Atoll.szs".
// A relative scenario path is looked up in the game directory first. --savegame NAME opens a
// savegame by the name shown in the game (looked up in Game.dat, e.g. "Merlin01" ->
// SAVEGAME/game00.gam) or by file name. Scenarios and savegames share one format. --load FILE opens
// a savegame of Open1602 (.o1602, docs/formats/o1602-save.md) or any file of the original by its path.

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_main.h>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

#include "diagnostics.hpp"
#include "map_viewer.hpp"

#include "o1602/assets/scenario.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

using namespace o1602;

namespace {

int usage() {
    std::fputs(
        "usage: open1602 [--game-dir DIR] [--scenario FILE | --savegame NAME | --load FILE] [--size WxH]\n"
        "                [--zoom 0|1|2]\n"
        "                [--owners] [--original-sheets] [--original-flags] [--refund MODE] [--droughts MODE]\n"
        "                [--volcanoes MODE] [--frames N]\n"
        "                [--new-game DIFFICULTY [--layout 0-2] [--name NAME] [--colour 0-3]] [--seed N]\n"
        "                [--islands original|seed]\n"
        "                [--world WxH] [--players 1-12] [--island-count C] [--island-sizes S] [--villages C]\n"
        "                [--volcano-islands C] [--starts spread|together]\n"
        "                [--screenshot out.png] [--no-sound] [--no-music] [--no-panel] [--no-videos] [--intro]\n"
        "                [--video N] [--input EVENTS] [--select ISLAND,X,Y|ship:N]\n"
        "                [--host [--port N] | --join HOST[:PORT]] [--name NAME]\n"
        "  --new-game D       new endless game, difficulty 0 (easy) .. 3 (hard); layout: random map of the three\n"
        "  --seed N           where the islands of an endless game are drawn from (default: random)\n"
        "  --islands MODE     original: as the original (512 worlds per file), seed: from the whole seed\n"
        "  --world WxH ...    an own world for the new game (any of these options): size in tiles (200-2000),\n"
        "                     players with a start, island count, villages and volcano islands none|few|normal|\n"
        "                     many, island sizes small|mixed|large, starts spread (default) or together\n"
        "  --original-sheets  no figures at the smallest zoom level, as in the original\n"
        "  --original-flags   original flag images instead of the tinted generic flag\n"
        "  --refund MODE      what demolishing gives back: none (original), partial[:PERCENT] (default 50), full\n"
        "  --droughts MODE    game (as the game's difficulty says, original), off, rare, normal, frequent\n"
        "  --volcanoes MODE   game (as the game's settings say, original), off, on\n"
        "  --no-panel         no panel and status bar around the map (the original's frame)\n"
        "  --no-sound         no effects and voice messages; --no-music: no music (the original's Game.dat\n"
        "                     is read for the volumes, never written)\n"
        "  --no-videos        no videos of messages, victory, defeat and pause\n"
        "  --original-mouse   the original's mouse: orders with the left button, the right one cancels\n"
        "  --no-confirm       no questions before scuttling, disbanding and declaring war\n"
        "  --keys FILE        key settings (default: keys.cfg in the user's data folder; F1 shows the keys)\n"
        "  --language CODE    language of Open1602's own texts, e.g. en, de (default: the installation's)\n"
        "  --intro            the two logos and the intro first, as the original's start (Escape skips them)\n"
        "  --video N          the video VideoSmk/N.smk first (e.g. 58 the intro, 10 the credits)\n"
        "  --input EVENTS     mouse and keys at frames, for tests: \"move X,Y@F;down X,Y@F;up X,Y@F;key K@F\";\n"
        "                     rdown/rup: the right button\n"
        "  --select I,X,Y     select the building at tile X,Y of island I (index) and look at it\n"
        "  --select ship:N    select the N-th ship of the game (index) and look at it; unit:N a land unit\n"
        "  --load FILE        open a savegame of Open1602 (.o1602) or a file of the original by its path\n"
        "  --menu             start in the main menu (the default when no game is named)\n"
        "  --host             host a multiplayer game of the game named (UDP port 16020, --port N); others join\n"
        "                     into the free human slots, or watch\n"
        "  --join HOST[:PORT] join the multiplayer game of a host (name or IPv4 address); the game comes from it\n"
        "  --password TEXT    the password of the game to join (--join)\n"
        "  --name NAME        the player's name (multiplayer, new endless game)\n"
        "  --save FILE        save the game when the viewer ends (never into the installation): Open1602's own\n"
        "                     savegame, or with the ending .gam a savegame the original loads (if the game fits)\n"
        "\n"
        "Keys (docs/architecture/0010-controls-and-interface.md):\n"
        "  WASD, arrows   scroll (Shift: faster)      right/middle mouse drag  scroll\n"
        "  Q / E          rotate view                 wheel, PageUp/PageDn     zoom\n"
        "  Space          to the last message         Home                     to the selection\n"
        "  F3, Pause      pause                       Num+ / Num-              game speed\n"
        "  F5 / F9        quick save / quick load     F12, Print               screenshot\n"
        "  F1             these keys over the map\n"
        "  B K I O        build, military, info, options\n"
        "  left mouse     select (drag: rectangle, Shift: add, Ctrl/double: all alike on screen)\n"
        "  right mouse    order (also on the minimap; Shift: queued)\n"
        "  Ctrl+1..0      store a group  Shift+1..0 add to it  1..0 select it (twice: go there)\n"
        "  . / ,          next idle ship / unit\n"
        "  units:         F attack, H stop, G hold, P patrol, C formation, Shift+C scatter, Tab one kind,\n"
        "                 Y white flag, X explore, T trade, R route, Del disband/scuttle\n"
        "  building:      R rotate, C pick the building under the mouse, Del demolish, Tab next\n"
        "  Esc            back, out of a tool, deselect   Ctrl+Q quit   Ctrl+O ownership view\n",
        stderr);
    return 2;
}

bool parse_number(std::string_view text, std::int32_t& value) {
    return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
}

/// "move X,Y@F;down X,Y@F;up X,Y@F;key K@F" (K: SDL key code, "+ctrl" / "+shift" after it).
bool parse_input(std::string_view text, std::vector<o1602::app::ViewerOptions::ScriptedInput>& out) {
    using Input = o1602::app::ViewerOptions::ScriptedInput;
    while (!text.empty()) {
        const std::size_t end = text.find(';');
        std::string_view item = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const std::size_t space = item.find(' ');
        const std::size_t at = item.find('@');
        if (space == std::string_view::npos || at == std::string_view::npos || at < space) {
            return false;
        }
        const std::string_view verb = item.substr(0, space);
        const std::string_view args = item.substr(space + 1, at - space - 1);
        Input in;
        std::int32_t frame = 0;
        if (!parse_number(item.substr(at + 1), frame) || frame < 0) {
            return false;
        }
        in.frame = static_cast<std::uint32_t>(frame);
        if (verb == "key") {
            in.kind = Input::Kind::Key;
            std::string_view key = args;
            struct Suffix {
                std::string_view text;
                std::uint16_t mod;
            };
            for (const auto& [suffix, mod] : {Suffix{"+ctrl", SDL_KMOD_LCTRL}, Suffix{"+shift", SDL_KMOD_LSHIFT}}) {
                if (const std::size_t p = key.find(suffix); p != std::string_view::npos) {
                    in.mod = static_cast<std::uint16_t>(in.mod | mod);
                    key = key.substr(0, p);
                }
            }
            if (!parse_number(key, in.key)) {
                return false;
            }
        } else {
            in.right = verb == "rdown" || verb == "rup";
            in.kind = verb == "down" || verb == "rdown" ? Input::Kind::Down
                      : verb == "up" || verb == "rup"   ? Input::Kind::Up
                                                        : Input::Kind::Move;
            if (verb != "down" && verb != "up" && verb != "move" && !in.right) {
                return false;
            }
            const std::size_t comma = args.find(',');
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (comma == std::string_view::npos || !parse_number(args.substr(0, comma), x) ||
                !parse_number(args.substr(comma + 1), y)) {
                return false;
            }
            in.x = static_cast<float>(x);
            in.y = static_cast<float>(y);
        }
        out.push_back(in);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    app::setup_diagnostics();
    std::string_view game_dir;
    std::string_view scenario = "Szenes/Das Atoll.szs";
    std::string_view savegame;
    std::string_view load;
    app::ViewerOptions options;
    bool new_game = false;
    bool game_named = false;  ///< without a game the program starts in the main menu
    bool menu = false;
    sim::NewGameOptions new_game_options;  // no name given: the menu's, or "Nameless" in the language set
    std::optional<std::uint8_t> layout_given;
    std::optional<std::uint64_t> seed_given;
    sim::WorldSettings custom;
    bool custom_world = false;
    app::ViewerOptions::Multiplayer multiplayer;
    bool multiplayer_given = false;
    bool name_given = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--game-dir" && has_value) {
            game_dir = argv[++i];
        } else if (arg == "--scenario" && has_value) {
            scenario = argv[++i];
            game_named = true;
        } else if (arg == "--savegame" && has_value) {
            savegame = argv[++i];
            game_named = true;
        } else if (arg == "--load" && has_value) {
            load = argv[++i];
            game_named = true;
        } else if (arg == "--menu") {
            menu = true;
        } else if (arg == "--no-panel") {
            options.frame = false;
        } else if (arg == "--no-sound") {
            options.effects = false;
        } else if (arg == "--no-music") {
            options.music = false;
        } else if (arg == "--input" && has_value) {
            if (!parse_input(argv[++i], options.input)) {
                return usage();
            }
        } else if (arg == "--select" && has_value && std::string_view(argv[i + 1]).starts_with("ship:")) {
            std::int32_t n = 0;
            if (!parse_number(std::string_view(argv[++i]).substr(5), n) || n < 0) {
                return usage();
            }
            options.select_ship = static_cast<std::uint32_t>(n);
        } else if (arg == "--select" && has_value && std::string_view(argv[i + 1]).starts_with("unit:")) {
            std::int32_t n = 0;
            if (!parse_number(std::string_view(argv[++i]).substr(5), n) || n < 0) {
                return usage();
            }
            options.select_soldier = static_cast<std::uint32_t>(n);
        } else if (arg == "--select" && has_value) {
            std::array<std::int32_t, 3> v{};
            std::string_view rest = argv[++i];
            bool ok = true;
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t comma = rest.find(',');
                ok = ok && parse_number(rest.substr(0, comma), v[k]) && v[k] >= 0 && v[k] <= 0xFFFF;
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            }
            if (!ok) {
                return usage();
            }
            options.select = std::array<std::uint16_t, 3>{
                static_cast<std::uint16_t>(v[0]), static_cast<std::uint16_t>(v[1]), static_cast<std::uint16_t>(v[2])};
        } else if (arg == "--original-mouse") {
            options.original_mouse = true;
        } else if (arg == "--no-confirm") {
            options.confirm = false;
        } else if (arg == "--keys" && has_value) {
            options.keys_file = core::path_from_utf8(argv[++i]);
        } else if (arg == "--language" && has_value) {
            options.language = argv[++i];
        } else if (arg == "--no-videos") {
            options.videos = false;
        } else if (arg == "--intro") {
            options.intro = true;
        } else if (arg == "--video" && has_value) {
            options.video = static_cast<std::uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--owners") {
            options.show_owners = true;
        } else if (arg == "--original-sheets") {
            options.derive_small_figures = false;
        } else if (arg == "--original-flags") {
            options.original_flags = true;
        } else if (arg == "--frames" && has_value) {
            std::int32_t frames = 0;
            if (!parse_number(argv[++i], frames) || frames < 0) {
                return usage();
            }
            options.max_frames = static_cast<std::uint32_t>(frames);
        } else if (arg == "--zoom" && has_value) {
            std::int32_t zoom = 0;
            if (!parse_number(argv[++i], zoom) || zoom < 0 || zoom > 2) {
                return usage();
            }
            options.zoom = static_cast<render::Zoom>(zoom);
        } else if (arg == "--screenshot" && has_value) {
            options.screenshot = core::path_from_utf8(argv[++i]);
        } else if (arg == "--refund" && has_value) {
            // Game setting: what demolishing gives back (none as in the original, partial[:N], full).
            const std::string_view mode = argv[++i];
            if (mode == "none") {
                options.rules.demolish_refund = sim::Refund::None;
            } else if (mode == "full") {
                options.rules.demolish_refund = sim::Refund::Full;
            } else if (mode.starts_with("partial")) {
                options.rules.demolish_refund = sim::Refund::Partial;
                if (mode.size() > 8 && mode[7] == ':') {
                    std::int32_t percent = 0;
                    if (!parse_number(mode.substr(8), percent) || percent < 1 || percent > 100) {
                        return usage();
                    }
                    options.rules.refund_percent = static_cast<std::uint8_t>(percent);
                }
            } else {
                return usage();
            }
        } else if (arg == "--droughts" && has_value) {
            // Game setting: how often droughts come.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.droughts = sim::Droughts::Game;
            } else if (mode == "off") {
                options.rules.droughts = sim::Droughts::Off;
            } else if (mode == "rare") {
                options.rules.droughts = sim::Droughts::Rare;
            } else if (mode == "normal") {
                options.rules.droughts = sim::Droughts::Normal;
            } else if (mode == "frequent") {
                options.rules.droughts = sim::Droughts::Frequent;
            } else {
                return usage();
            }
        } else if (arg == "--save" && has_value) {
            options.save = core::path_from_utf8(argv[++i]);
        } else if (arg == "--volcanoes" && has_value) {
            // Game setting: whether volcanoes erupt.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.volcanoes = sim::Volcanoes::Game;
            } else if (mode == "off") {
                options.rules.volcanoes = sim::Volcanoes::Off;
            } else if (mode == "on") {
                options.rules.volcanoes = sim::Volcanoes::On;
            } else {
                return usage();
            }
        } else if (arg == "--new-game" && has_value) {
            // A new endless game: difficulty 0 (easy) .. 3 (hard).
            std::int32_t difficulty = 0;
            if (!parse_number(argv[++i], difficulty) || difficulty < 0 ||
                difficulty >= sim::NewGameOptions::kDifficulties) {
                return usage();
            }
            new_game = true;
            new_game_options.difficulty = static_cast<std::uint8_t>(difficulty);
        } else if (arg == "--layout" && has_value) {
            std::int32_t layout = 0;
            if (!parse_number(argv[++i], layout) || layout < 0 || layout >= sim::NewGameOptions::kLayouts) {
                return usage();
            }
            layout_given = static_cast<std::uint8_t>(layout);
        } else if (arg == "--seed" && has_value) {
            std::uint64_t seed = 0;
            const std::string_view text = argv[++i];
            if (std::from_chars(text.data(), text.data() + text.size(), seed).ec != std::errc{}) {
                return usage();
            }
            seed_given = seed;
        } else if (arg == "--name" && has_value) {
            new_game_options.name = core::utf8_to_cp1252(argv[++i]);  // the game keeps names in Windows-1252
            name_given = true;
        } else if (arg == "--host") {
            multiplayer.role = app::ViewerOptions::Multiplayer::Role::Host;
            multiplayer_given = true;
        } else if (arg == "--join" && has_value) {
            // HOST or HOST:PORT (IPv4 or a name).
            const std::string_view to = argv[++i];
            const std::size_t colon = to.rfind(':');
            multiplayer.role = app::ViewerOptions::Multiplayer::Role::Join;
            multiplayer.address = std::string(to.substr(0, colon));
            if (colon != std::string_view::npos) {
                std::int32_t port = 0;
                if (!parse_number(to.substr(colon + 1), port) || port <= 0 || port > 65535) {
                    return usage();
                }
                multiplayer.port = static_cast<std::uint16_t>(port);
            }
            multiplayer_given = !multiplayer.address.empty();
            if (!multiplayer_given) {
                return usage();
            }
        } else if (arg == "--password" && has_value) {
            multiplayer.password = argv[++i];
        } else if (arg == "--port" && has_value) {
            std::int32_t port = 0;
            if (!parse_number(argv[++i], port) || port <= 0 || port > 65535) {
                return usage();
            }
            multiplayer.port = static_cast<std::uint16_t>(port);
        } else if (arg == "--colour" && has_value) {
            std::int32_t colour = 0;
            if (!parse_number(argv[++i], colour) || colour < 0 || colour > 3) {
                return usage();
            }
            new_game_options.colour = static_cast<std::uint8_t>(colour);
        } else if (arg == "--islands" && has_value) {
            // How the islands of an endless game are drawn: as the original (512 worlds per file)
            // or from the whole seed.
            const std::string_view mode = argv[++i];
            if (mode == "original") {
                options.world.draws = sim::WorldStart::Draws::Table;
            } else if (mode == "seed") {
                options.world.draws = sim::WorldStart::Draws::Seeded;
            } else {
                return usage();
            }
        } else if ((arg == "--island-count" || arg == "--villages" || arg == "--volcano-islands") && has_value) {
            const std::string_view mode = argv[++i];
            sim::WorldSettings::Amount amount = sim::WorldSettings::Amount::Normal;
            if (mode == "none") {
                amount = sim::WorldSettings::Amount::None;
            } else if (mode == "few") {
                amount = sim::WorldSettings::Amount::Few;
            } else if (mode == "normal") {
                amount = sim::WorldSettings::Amount::Normal;
            } else if (mode == "many") {
                amount = sim::WorldSettings::Amount::Many;
            } else {
                return usage();
            }
            (arg == "--island-count" ? custom.islands
             : arg == "--villages"   ? custom.natives
                                     : custom.volcanoes) = amount;
            custom_world = true;
        } else if (arg == "--island-sizes" && has_value) {
            const std::string_view mode = argv[++i];
            if (mode == "small") {
                custom.sizes = sim::WorldSettings::Sizes::Small;
            } else if (mode == "mixed") {
                custom.sizes = sim::WorldSettings::Sizes::Mixed;
            } else if (mode == "large") {
                custom.sizes = sim::WorldSettings::Sizes::Large;
            } else {
                return usage();
            }
            custom_world = true;
        } else if (arg == "--starts" && has_value) {
            const std::string_view mode = argv[++i];
            if (mode == "spread") {
                custom.starts = sim::WorldSettings::Starts::Spread;
            } else if (mode == "together") {
                custom.starts = sim::WorldSettings::Starts::Together;
            } else {
                return usage();
            }
            custom_world = true;
        } else if (arg == "--players" && has_value) {
            std::int32_t players = 0;
            if (!parse_number(argv[++i], players) || players < 1 ||
                players > static_cast<std::int32_t>(sim::kMaxPlayers)) {
                return usage();
            }
            custom.players = static_cast<std::uint8_t>(players);
            custom_world = true;
        } else if (arg == "--world" && has_value) {
            const std::string_view size = argv[++i];
            const auto x = size.find('x');
            std::int32_t w = 0;
            std::int32_t h = 0;
            if (x == std::string_view::npos || !parse_number(size.substr(0, x), w) ||
                !parse_number(size.substr(x + 1), h) || w < static_cast<std::int32_t>(sim::WorldSettings::kMinSide) ||
                h < static_cast<std::int32_t>(sim::WorldSettings::kMinSide) ||
                w > static_cast<std::int32_t>(sim::WorldSettings::kMaxSide) ||
                h > static_cast<std::int32_t>(sim::WorldSettings::kMaxSide)) {
                return usage();
            }
            custom.width = static_cast<std::uint32_t>(w);
            custom.height = static_cast<std::uint32_t>(h);
            custom_world = true;
        } else if (arg == "--size" && has_value) {
            const std::string_view size = argv[++i];
            const auto x = size.find('x');
            if (x == std::string_view::npos || !parse_number(size.substr(0, x), options.width) ||
                !parse_number(size.substr(x + 1), options.height)) {
                return usage();
            }
        } else {
            return usage();
        }
    }

    auto game = game_dir.empty() ? assets::GameDirectory::from_environment()
                                 : assets::GameDirectory::open(core::path_from_utf8(game_dir));
    if (!game) {
        app::report_fatal(game.error().message + "\n(set ANNO1602_DIR or pass --game-dir)");
        return 1;
    }
    std::string savegame_path;
    if (!savegame.empty()) {
        // The name shown in the game (Game.dat, "SPIELNAME") or a file name in SAVEGAME/.
        if (const auto dat = game->resolve("Game.dat")) {
            if (auto bytes = core::read_file(*dat)) {
                const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
                for (const auto& slot : assets::parse_savegame_slots(text)) {
                    if (core::equals_ignore_case(slot.name, savegame)) {
                        savegame_path = slot.file;
                    }
                }
            }
        }
        if (savegame_path.empty()) {
            savegame_path = "Savegame/" + std::string(savegame);
            if (!savegame_path.ends_with(".gam") && !savegame_path.ends_with(".GAM")) {
                savegame_path += ".gam";
            }
        }
        scenario = savegame_path;
    }
    if (!load.empty()) {
        options.scenario = core::path_from_utf8(load);
    } else if (const auto resolved = game->resolve(scenario)) {
        options.scenario = *resolved;
    } else {
        options.scenario = core::path_from_utf8(scenario);
    }

    // What the original rolls with rand(): the map layout and where the island table starts; the
    // game id from the clock.
    std::random_device device;
    options.world.seed = seed_given.value_or((std::uint64_t{device()} << 32) | device());
    options.world.guarantee_resources = options.rules.guarantee_resources;
    if (new_game) {
        new_game_options.layout =
            layout_given.value_or(static_cast<std::uint8_t>(device() % sim::NewGameOptions::kLayouts));
        const std::time_t now = std::time(nullptr);
        const std::tm* local = std::localtime(&now);
        new_game_options.game_id = local != nullptr
                                       ? sim::new_game_id(static_cast<std::uint32_t>(local->tm_year + 1900),
                                                          static_cast<std::uint32_t>(local->tm_mon + 1),
                                                          static_cast<std::uint32_t>(local->tm_mday),
                                                          static_cast<std::uint32_t>(local->tm_hour),
                                                          static_cast<std::uint32_t>(local->tm_min), device())
                                       : device();
        new_game_options.world = options.world;
        if (custom_world) {
            new_game_options.custom = custom;
            std::fprintf(stderr, "new endless game: own world %ux%u, %u players, level %u, seed %llu\n", custom.width,
                         custom.height, static_cast<unsigned>(custom.players),
                         static_cast<unsigned>(new_game_options.difficulty),
                         static_cast<unsigned long long>(options.world.seed));
        } else {
            std::fprintf(stderr, "new endless game: %s, islands from seed %llu\n",
                         sim::endless_file(new_game_options.difficulty, new_game_options.layout).c_str(),
                         static_cast<unsigned long long>(options.world.seed));
        }
        options.new_game = new_game_options;
    } else if (custom_world) {
        return usage();  // an own world is a new game
    }

    if (multiplayer_given) {
        if (name_given) {
            multiplayer.name = new_game_options.name;
        }
        options.multiplayer = multiplayer;
    }
    // No game named: the main menu, as the original starts (with --frame off there is none). A
    // computer joining a host gets the game from it.
    const bool joining = multiplayer_given && multiplayer.role == app::ViewerOptions::Multiplayer::Role::Join;
    options.menu = options.frame && !joining && !multiplayer_given && (menu || (!game_named && !new_game));
    auto viewer = app::MapViewer::create(*game, options);
    if (!viewer) {
        app::report_fatal(viewer.error().message);
        return 1;
    }
    return (*viewer)->run();
}
