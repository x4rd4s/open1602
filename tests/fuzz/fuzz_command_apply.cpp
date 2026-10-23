// SPDX-License-Identifier: GPL-3.0-or-later
// Commands as hostile input to the simulation (D9): the bytes are read as a stream of commands as
// the network delivers them, each one submitted to a running game of the small test world as a
// player's order (Simulation::submit), with ticks in between. Whatever the commands, the game
// must not crash and the world must still validate.
//
// The first byte of the input: low 4 bits = ticks between two commands (0..15), bit 4 = carry them
// out with Simulation::execute (the tools' way in, which also takes kNoPlayer) instead of submit.
#include <cstdint>

#include "fuzz_support.hpp"

#include "o1602/core/byte_reader.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) {
        return 0;
    }
    const std::uint32_t ticks = data[0] & 0x0FU;
    const bool execute = (data[0] & 0x10U) != 0;
    sim::World world = fuzz::make_world();
    sim::Simulation simulation(world, fuzz::buildings(), fuzz::figures());
    core::ByteReader in(std::span<const std::uint8_t>(data + 1, size - 1));
    for (int n = 0; n < 32; ++n) {
        auto command = sim::read_command(in);
        if (!command) {
            break;
        }
        if (execute) {
            (void)simulation.execute(*command);
        } else {
            (void)simulation.submit(std::move(*command));
        }
        for (std::uint32_t t = 0; t < ticks + 1; ++t) {
            simulation.tick();
        }
    }
    for (int t = 0; t < 8; ++t) {
        simulation.tick();
    }
    if (const auto status = sim::validate(world); !status) {
        std::fprintf(stderr, "%s\n", status.error().message.c_str());
        fuzz::fail("the world does not validate after the commands");
    }
    return 0;
}
