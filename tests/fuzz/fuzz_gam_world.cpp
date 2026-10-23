// SPDX-License-Identifier: GPL-3.0-or-later
// A .GAM savegame or .SZS scenario loaded as a world (sim::load_world): islands on the map,
// players, cities, ships, soldiers, the game state. What loads must validate, run a few ticks, save
// as .o1602 and load from that again. The game directory is an empty stand-in (no island
// templates): islands that need one are loaded without it, as with a damaged installation.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

const assets::GameDirectory& game_directory() {
    static const assets::GameDirectory game = [] {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "o1602-fuzz-game";
        fs::create_directories(root / "toolgfx");
        fs::create_directories(root / "gfx");
        for (const char* name : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            std::ofstream(root / name).put('\n');
        }
        auto dir = assets::GameDirectory::open(root);
        fuzz::require(dir.ok(), "stand-in game directory");
        return std::move(dir).value();
    }();
    return game;
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    (void)game_directory();
    (void)fuzz::buildings();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto file = assets::ChunkFile::from_bytes(core::Bytes(data, data + size));
    if (!file) {
        return 0;
    }
    std::vector<std::string> warnings;
    auto world = sim::load_world(*file, fuzz::buildings(), game_directory(), &warnings);
    if (!world) {
        return 0;
    }
    if (!sim::validate(*world).ok()) {
        fuzz::fail("a loaded .GAM does not validate");
    }
    {
        sim::Simulation simulation(*world, fuzz::buildings(), fuzz::figures());
        for (int t = 0; t < 4; ++t) {
            simulation.tick();
        }
        if (!sim::validate(*world).ok()) {
            fuzz::fail("a loaded .GAM does not validate after four ticks");
        }
        auto saved = sim::write_game(*world, fuzz::buildings(), &simulation, sim::SaveOptions{false});
        fuzz::require(saved.ok(), "a loaded .GAM saves");
        auto again = sim::read_game(*saved, fuzz::buildings(), nullptr, &fuzz::figures());
        if (!again) {
            std::fprintf(stderr, "%s\nworld %ux%u, %zu islands:\n", again.error().message.c_str(), world->width(),
                         world->height(), world->islands().size());
            for (const auto& i : world->islands()) {
                std::fprintf(stderr, "  #%u %ux%u at %u,%u template %u size %u climate %u\n", i.header.number,
                             i.map.width(), i.map.height(), i.x, i.y, i.header.template_number, i.header.template_size,
                             i.header.climate);
            }
            fuzz::fail("a loaded .GAM saved as .o1602 loads again");
        }
    }
    return 0;
}
