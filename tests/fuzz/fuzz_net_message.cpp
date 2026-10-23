// SPDX-License-Identifier: GPL-3.0-or-later
// The network wire (engine/net/protocol): every message a host or computer may receive, decoded
// from hostile bytes. A message that decodes must encode and decode again to the same encoding.
// The same bytes also go through the LAN discovery packets (lan.hpp) and the lobby's setup blob
// (app::read_lobby_setup, what the multiplayer menu reads out of Lobby::setup).
#include <cstdint>
#include <vector>

#include "fuzz_support.hpp"
#include "multiplayer_menu.hpp"

#include "o1602/net/lan.hpp"
#include "o1602/net/protocol.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    if (const auto message = net::decode(bytes)) {
        const std::vector<std::uint8_t> encoded = net::encode(*message);
        const auto again = net::decode(encoded);
        fuzz::require(again.has_value(), "an encoded message decodes");
        fuzz::require(net::encode(*again) == encoded, "decode(encode(m)) encodes alike");
        if (const auto* lobby = std::get_if<net::Lobby>(&*message)) {
            (void)app::read_lobby_setup(lobby->setup);
        }
        if (const auto* batch = std::get_if<net::CommandBatch>(&*message)) {
            for (const auto& c : batch->commands) {
                (void)net::command_player(c);
            }
        }
    }
    (void)net::decode_search(bytes);
    if (const auto answer = net::decode_answer(bytes)) {
        const auto encoded = net::encode_answer(answer->first, answer->second);
        fuzz::require(net::decode_answer(encoded).has_value(), "an encoded LAN answer decodes");
    }
    (void)app::read_lobby_setup(bytes);
    return 0;
}
