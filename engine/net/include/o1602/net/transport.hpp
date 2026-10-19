// SPDX-License-Identifier: GPL-3.0-or-later
//
// How messages travel (ADR 0011): reliable and in order, between the host and each other computer.
// The sessions know nothing else of the network, so that ENet (enet_transport.hpp), the in-memory
// network of the tests (loopback.hpp) or later another library can carry them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace o1602::net {

/// The largest message a transport carries: a whole game (protocol.hpp kMaxSnapshotBytes) and the
/// header of its message.
inline constexpr std::size_t kMaxMessageBytes = (32U << 20) + (64U << 10);

/// One connection of a transport; the host has one per computer, a computer one to the host.
using LinkId = std::uint32_t;

struct TransportEvent {
    enum class Kind : std::uint8_t {
        Connected,
        Disconnected,  ///< closed by the other side, timed out, or a connection attempt failed
        Received,
    };
    Kind kind = Kind::Received;
    LinkId link = 0;
    std::vector<std::uint8_t> data;  ///< Received: one whole message
};

class Transport {
public:
    virtual ~Transport() = default;

    /// Appends what happened since the last call; never blocks.
    virtual void poll(std::vector<TransportEvent>& events) = 0;
    /// Queues a message on a link: delivered whole, once, in the order sent, or the link breaks.
    virtual void send(LinkId link, std::span<const std::uint8_t> message) = 0;
    /// Sends what is queued now instead of at the next poll.
    virtual void flush() = 0;
    /// Closes a link after what is queued on it; no Disconnected event follows for it.
    virtual void disconnect(LinkId link) = 0;
    /// Time for a message there and back, as far as the transport knows (0 if not).
    [[nodiscard]] virtual std::uint32_t round_trip_ms(LinkId link) const = 0;
};

}  // namespace o1602::net
