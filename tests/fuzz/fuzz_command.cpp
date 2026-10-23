// SPDX-License-Identifier: GPL-3.0-or-later
// The command codec (sim/command_io, ADR 0004): what a computer of a multiplayer game or a replay
// hands in. A command that decodes must encode to the same bytes it was read from (every field is
// written, so there is one encoding per command), and decode again to the same encoding. The
// input is also read as a stream of scheduled commands, as a replay holds them.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/command_io.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    if (const auto command = sim::decode_command(bytes)) {
        const std::vector<std::uint8_t> encoded = sim::encode_command(*command);
        const auto again = sim::decode_command(encoded);
        fuzz::require(again.has_value(), "an encoded command decodes");
        fuzz::require(sim::encode_command(*again) == encoded, "decode(encode(c)) encodes alike");
        if (encoded.size() != size || !std::equal(encoded.begin(), encoded.end(), data)) {
            fuzz::fail("a decoded command encodes to other bytes than it was read from");
        }
    }
    core::ByteReader in(bytes);
    for (int n = 0; n < 64; ++n) {
        const auto scheduled = sim::read_scheduled(in);
        if (!scheduled) {
            break;
        }
        core::ByteWriter out;
        sim::write_scheduled(out, *scheduled);
    }
    return 0;
}
