// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool - inspect and convert original Anno 1602 data files.
//
//   o1602-tool chunks     <file>                         list chunks of any chunk container
//   o1602-tool cod        <file.cod> [out.txt]           decode a .cod script to UTF-8 text
//   o1602-tool cod-dump   <file.cod> [--predefined haeuser|figuren|none] [--type NAME]
//                                                        interpret a .cod script and print its records
//   o1602-tool bsh-info   <file.bsh|.zei>                list sprites
//   o1602-tool bsh-export <file.bsh|.zei> <outdir> [--palette FILE] [--sheet] [--first N] [--count N]
//   o1602-tool islands    <file.scp|.szs|.szm|.gam>      list islands and tile statistics
//   o1602-tool savegames                                 list savegames (names from Game.dat)
//   o1602-tool render-island <file.scp|.szs|.gam> <out.png> [--island N] [--zoom 0|1|2]
//                            [--rotation 0-3] [--anim N] [--no-template] [--base FILE.scp]
//                                                        render an island (needs ANNO1602_DIR)
//   o1602-tool render-world <file.szs|.gam> <out.png> [--zoom 0|1|2] [--rotation 0-3] [--time MS]
//                           [--tile X,Y] [--size WxH] [--owners]
//                                                        render the world map or a part of it
//
// The palette defaults to $ANNO1602_DIR/TOOLGFX/STADTFLD.COL.
//
// The commands live in formats.cpp, savegames.cpp, render.cpp and video.cpp (declared in tool.hpp).

#include <cstdio>
#include <string_view>

#include "tool.hpp"

#include "o1602/assets/game_directory.hpp"

namespace o1602::tool {

int fail(const core::Error& error) {
    std::fprintf(stderr, "error: %s\n", error.message.c_str());
    return 1;
}

int usage() {
    std::fputs(
        "usage:\n"
        "  o1602-tool chunks     <file>\n"
        "  o1602-tool cod        <file.cod> [out.txt]\n"
        "  o1602-tool cod-dump   <file.cod> [--predefined haeuser|figuren|none] [--type NAME]\n"
        "  o1602-tool bsh-info   <file.bsh|.zei>\n"
        "  o1602-tool bsh-export <file.bsh|.zei> <outdir> [--palette FILE] [--sheet] [--first N] [--count N]\n"
        "  o1602-tool islands    <file.scp|.szs|.szm|.gam>\n"
        "  o1602-tool savegames                               (names from Game.dat, needs ANNO1602_DIR)\n"
        "  o1602-tool export    <in.gam|in.o1602> <out.gam> [--via-own]  (needs ANNO1602_DIR)\n"
        "  o1602-tool new-world <out.gam> [--level 0-3] [--seed N] [--world WxH] [--players N]\n"
        "                       [--island-count C] [--villages C] [--volcano-islands C] [--island-sizes S]\n"
        "                       [--together]   (an own world as a new game; C none|few|normal|many,\n"
        "                                       S small|mixed|large; written as a savegame)\n"
        "  o1602-tool smk        <file.smk> [--png DIR] [--every N] [--wav TRACK OUT.wav]\n"
        "                        (Smacker video: header, decode all frames; frames as PNG, a track as WAVE)\n"
        "  o1602-tool state      <file.gam|.szs> [--wares] [--production]\n"
        "                        (players, cities, warehouses, production, ships, deer)\n"
        "  o1602-tool render-island <file.scp|.szs|.gam> <out.png> [--island N] [--zoom 0|1|2] [--rotation 0-3]\n"
        "                           [--anim N] [--no-template] [--base FILE.scp]\n"
        "  o1602-tool render-world <file.szs|.gam> <out.png> [--zoom 0|1|2] [--rotation 0-3] [--time MS]\n"
        "                          [--tile X,Y] [--size WxH] [--owners] [--original-sheets] [--original-flags]\n"
        "                          [--banner P:RRGGBB] [--simulate SECONDS] [--walkers]\n"
        "                          [--build \"ID@X,Y[,R];...\"]  (world tiles, for the first human player)\n"
        "                          (default: whole world at zoom 2; --simulate: run the economy first; "
        "--original-sheets: no figures where the\n"
        "                          original has no graphics for them; --original-flags: the original flag\n"
        "                          images instead of the tinted generic flag)\n",
        stderr);
    return 2;
}

core::Result<assets::Palette> load_palette(const Args& args) {
    if (const auto it = args.options.find("--palette"); it != args.options.end()) {
        return assets::Palette::load(fs::path(it->second));
    }
    auto dir = assets::GameDirectory::from_environment();
    if (!dir) {
        return core::make_error(core::ErrorCode::NotFound, "no --palette given and " + dir.error().message);
    }
    const auto path = dir->resolve("toolgfx/stadtfld.col");
    if (!path) {
        return core::make_error(core::ErrorCode::NotFound, "TOOLGFX/STADTFLD.COL not found");
    }
    return assets::Palette::load(*path);
}

}  // namespace o1602::tool

int main(int argc, char** argv) {
    using namespace o1602::tool;
    if (argc < 2) {
        return usage();
    }
    const std::string_view command = argv[1];
    const Args args(argc, argv);
    if (command == "chunks") {
        return cmd_chunks(args);
    }
    if (command == "cod") {
        return cmd_cod(args);
    }
    if (command == "cod-dump") {
        return cmd_cod_dump(args);
    }
    if (command == "bsh-info") {
        return cmd_bsh_info(args);
    }
    if (command == "bsh-export") {
        return cmd_bsh_export(args);
    }
    if (command == "render-island") {
        return cmd_render_island(args);
    }
    if (command == "render-world") {
        return cmd_render_world(args);
    }
    if (command == "savegames") {
        return cmd_savegames(args);
    }
    if (command == "islands") {
        return cmd_islands(args);
    }
    if (command == "smk") {
        return cmd_smk(args);
    }
    if (command == "state") {
        return cmd_state(args);
    }
    if (command == "new-world") {
        return cmd_new_world(args);
    }
    if (command == "export") {
        return cmd_export(args);
    }
    return usage();
}
