// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/enet_transport.hpp"

#include <cstdint>
#include <enet/enet.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "enet_library.hpp"

namespace o1602::net {

namespace {

using detail::EnetLibrary;

constexpr std::size_t kChannels = 1;
/// ENet applies its packet limit to what a host sends and receives alike, and the host sends whole
/// games. HostSession refuses large messages from computers itself. (protocol.hpp is not included
/// here: on Windows ENet brings windows.h, whose macros near and far break the game's headers.)
constexpr std::size_t kMaxPacket = kMaxMessageBytes;
constexpr enet_uint32 kTimeoutMinMs = 5000;
constexpr enet_uint32 kTimeoutMaxMs = 15000;

class EnetTransport final : public Transport {
public:
    /// `host_link`: a computer's end, whose one link (to the host) is 0.
    EnetTransport(ENetHost* host, bool host_link) : m_host(host), m_next(host_link ? 0 : 1) {}
    ~EnetTransport() override {
        for (const auto& [link, peer] : m_links) {
            enet_peer_disconnect_now(peer, 0);
        }
        enet_host_flush(m_host);
        enet_host_destroy(m_host);
        EnetLibrary::release();
    }
    EnetTransport(const EnetTransport&) = delete;
    EnetTransport& operator=(const EnetTransport&) = delete;

    /// The link of a connection being made (a computer's end).
    void expect(ENetPeer* peer) { add(peer); }

    void poll(std::vector<TransportEvent>& events) override {
        for (const LinkId link : m_broken) {
            events.push_back({TransportEvent::Kind::Disconnected, link, {}});
        }
        m_broken.clear();
        ENetEvent e;
        while (enet_host_service(m_host, &e, 0) > 0) {
            switch (e.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    const LinkId link = e.peer->data != nullptr ? id(e.peer) : add(e.peer);
                    enet_peer_timeout(e.peer, 0, kTimeoutMinMs, kTimeoutMaxMs);
                    events.push_back({TransportEvent::Kind::Connected, link, {}});
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    if (e.peer->data != nullptr) {
                        const LinkId link = id(e.peer);
                        m_links.erase(link);
                        e.peer->data = nullptr;
                        events.push_back({TransportEvent::Kind::Disconnected, link, {}});
                    }
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    if (e.peer->data != nullptr) {
                        events.push_back({TransportEvent::Kind::Received,
                                          id(e.peer),
                                          {e.packet->data, e.packet->data + e.packet->dataLength}});
                    }
                    enet_packet_destroy(e.packet);
                    break;
                case ENET_EVENT_TYPE_NONE:
                    break;
            }
        }
    }

    void send(LinkId link, std::span<const std::uint8_t> message) override {
        const auto it = m_links.find(link);
        if (it == m_links.end()) {
            return;
        }
        ENetPacket* packet = enet_packet_create(message.data(), message.size(), ENET_PACKET_FLAG_RELIABLE);
        if (packet == nullptr || enet_peer_send(it->second, 0, packet) != 0) {
            if (packet != nullptr) {
                enet_packet_destroy(packet);
            }
            // Delivered whole or the link breaks (Transport::send): a message lost here would
            // leave the other side waiting for it forever.
            const LinkId broken = it->first;
            it->second->data = nullptr;
            enet_peer_disconnect_later(it->second, 0);
            m_links.erase(it);
            m_broken.push_back(broken);
        }
    }

    void flush() override { enet_host_flush(m_host); }

    void disconnect(LinkId link) override {
        const auto it = m_links.find(link);
        if (it == m_links.end()) {
            return;
        }
        // After what is queued; ENet's own DISCONNECT for it is no longer reported.
        it->second->data = nullptr;
        enet_peer_disconnect_later(it->second, 0);
        m_links.erase(it);
    }

    [[nodiscard]] std::uint32_t round_trip_ms(LinkId link) const override {
        const auto it = m_links.find(link);
        return it == m_links.end() ? 0 : it->second->roundTripTime;
    }

    [[nodiscard]] std::string address(LinkId link) const override {
        const auto it = m_links.find(link);
        char text[64] = {};
        if (it == m_links.end() || enet_address_get_host_ip(&it->second->address, text, sizeof text) != 0) {
            return {};
        }
        return text;
    }

private:
    // The link id lives in the peer's data pointer (ids start at 1 there, so null means none).
    static LinkId id(const ENetPeer* peer) {
        return static_cast<LinkId>(reinterpret_cast<std::uintptr_t>(peer->data) - 1);
    }
    LinkId add(ENetPeer* peer) {
        const LinkId link = m_next++;
        peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link) + 1);
        m_links.emplace(link, peer);
        return link;
    }

    ENetHost* m_host;
    LinkId m_next;
    std::unordered_map<LinkId, ENetPeer*> m_links;  // lookups only, never walked for the game
    std::vector<LinkId> m_broken;                   ///< failed to send: reported at the next poll
};

}  // namespace

core::Result<std::unique_ptr<Transport>> enet_listen(std::uint16_t port, std::size_t max_computers) {
    if (!EnetLibrary::acquire()) {
        return core::make_error(core::ErrorCode::PlatformError, "ENet could not be initialised");
    }
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    ENetHost* host = enet_host_create(&address, max_computers, kChannels, 0, 0);
    if (host == nullptr) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError,
                                "cannot listen on UDP port " + std::to_string(port) + " (in use?)");
    }
    host->maximumPacketSize = kMaxPacket;
    host->maximumWaitingData = kMaxPacket;
    return std::unique_ptr<Transport>(std::make_unique<EnetTransport>(host, false));
}

core::Result<std::unique_ptr<Transport>> enet_connect(const std::string& address, std::uint16_t port) {
    if (!EnetLibrary::acquire()) {
        return core::make_error(core::ErrorCode::PlatformError, "ENet could not be initialised");
    }
    ENetAddress to{};
    if (enet_address_set_host(&to, address.c_str()) != 0) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::NotFound, "unknown host: " + address);
    }
    to.port = port;
    ENetHost* host = enet_host_create(nullptr, 1, kChannels, 0, 0);
    if (host == nullptr) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError, "cannot open a UDP socket");
    }
    ENetPeer* peer = enet_host_connect(host, &to, kChannels, 0);
    if (peer == nullptr) {
        enet_host_destroy(host);
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError, "cannot connect to " + address);
    }
    auto transport = std::make_unique<EnetTransport>(host, true);
    transport->expect(peer);
    return std::unique_ptr<Transport>(std::move(transport));
}

}  // namespace o1602::net
