// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own savegame (.o1602, docs/formats/o1602-save.md), coverage-guided: the header of an
// uncompressed file is resealed (sizes, CRC-32) before loading, so that changes reach the section
// readers. What loads must validate, save and load again, and run (as test_save_fuzz.cpp checks
// for its random mutations). save_info, the list view of a savegame, reads the same bytes.
#include <cstdint>
#include <string>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::vector<std::uint8_t> file(data, data + size);
    fuzz::reseal_save(file);
    (void)sim::save_info(file);
    std::vector<std::string> warnings;
    auto world = sim::read_game(file, fuzz::buildings(), &warnings, &fuzz::figures());
    if (!world) {
        return 0;
    }
    if (!sim::validate(*world).ok()) {
        fuzz::fail("a loaded .o1602 does not validate");
    }
    auto again = sim::write_game(*world, fuzz::buildings(), nullptr, sim::SaveOptions{false});
    fuzz::require(again.ok(), "a loaded .o1602 saves");
    auto reloaded = sim::read_game(*again, fuzz::buildings(), nullptr, &fuzz::figures());
    if (!reloaded) {
        std::fprintf(stderr, "%s\n", reloaded.error().message.c_str());
        fuzz::fail("a loaded .o1602 saves and loads again");
    }
    sim::Simulation simulation(*world, fuzz::buildings(), fuzz::figures());
    for (int t = 0; t < 4; ++t) {
        simulation.tick();
    }
    if (!sim::validate(*world).ok()) {
        fuzz::fail("a loaded .o1602 does not validate after four ticks");
    }
    return 0;
}
