// SPDX-License-Identifier: GPL-3.0-or-later
//
// Review tools (part SYNC): what connections that have not even said hello can make the host hold
// in memory. ENet takes packets up to the transport's limit, which is the size of a whole game
// (kMaxMessageBytes, 32 MiB) in both directions, and reserves a packet's whole size when its first
// fragment arrives. HostSession refuses messages above kMaxMessageToHost (1 MiB) only once they are
// complete. Linux only (glibc's mallinfo2 counts the heap).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/net/enet_transport.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/net/transport.hpp"

#if defined(__GLIBC__)
#include <enet/enet.h>
#include <malloc.h>

using namespace o1602;

namespace {

std::size_t heap_bytes() {
    const struct mallinfo2 m = mallinfo2();
    return m.uordblks + m.hblkhd;
}

}  // namespace

TEST_CASE("review: connections that send the first fragment of a large message do not make the host hold it") {
    constexpr std::uint16_t kPort = 46031;
    constexpr int kClients = 16;
    constexpr std::size_t kSize = 32U << 20;
    auto listening = net::enet_listen(kPort, 32);
    if (!listening) {
        SKIP("UDP port 46031 is not free here");
    }
    std::unique_ptr<net::Transport> host = std::move(listening).value();
    std::vector<net::TransportEvent> events;
    std::vector<ENetHost*> clients;
    std::vector<ENetPeer*> peers;
    ENetAddress to{};
    enet_address_set_host(&to, "127.0.0.1");
    to.port = kPort;
    for (int i = 0; i < kClients; ++i) {
        ENetHost* c = enet_host_create(nullptr, 1, 1, 0, 0);
        REQUIRE(c != nullptr);
        clients.push_back(c);
        peers.push_back(enet_host_connect(c, &to, 1, 0));
    }
    int connected = 0;
    for (int round = 0; round < 400 && connected < kClients; ++round) {
        host->poll(events);
        for (ENetHost* c : clients) {
            ENetEvent e;
            while (enet_host_service(c, &e, 1) > 0) {
                connected += e.type == ENET_EVENT_TYPE_CONNECT ? 1 : 0;
            }
        }
    }
    REQUIRE(connected == kClients);
    host->poll(events);
    const std::size_t before = heap_bytes();
    // One buffer for all: the clients' side allocates nothing of the size (ENET_PACKET_FLAG_NO_ALLOCATE).
    static std::vector<std::uint8_t> data(kSize, 7);
    for (ENetPeer* p : peers) {
        ENetPacket* packet =
            enet_packet_create(data.data(), kSize, ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_NO_ALLOCATE);
        REQUIRE(enet_peer_send(p, 0, packet) == 0);
    }
    const std::size_t queued = heap_bytes();
    // A few rounds only: the first fragments go out, then the clients fall silent.
    for (int round = 0; round < 20; ++round) {
        for (ENetHost* c : clients) {
            enet_host_flush(c);
        }
        host->poll(events);
    }
    const std::size_t after = heap_bytes();
    const double host_mb = static_cast<double>(after - queued) / (1024.0 * 1024.0);
    std::printf("  %d connections without a Hello: the host holds %.0f MB more (the clients' queues took %.0f MB)\n",
                kClients, host_mb, static_cast<double>(queued - before) / (1024.0 * 1024.0));
    CHECK(host_mb < kClients * static_cast<double>(net::kMaxMessageToHost) / (1024.0 * 1024.0) * 2.0);
    for (ENetHost* c : clients) {
        enet_host_destroy(c);
    }
}
#endif
