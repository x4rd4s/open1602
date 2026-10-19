// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/net/lan.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <enet/enet.h>
#include <map>
#include <set>
#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>  // SIO_GET_INTERFACE_LIST, INTERFACE_INFO
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "enet_library.hpp"
#include "wire.hpp"

#include "o1602/core/byte_writer.hpp"

namespace o1602::net {

namespace {

using detail::EnetLibrary;
using detail::In;

// Searches and answers start with these; anything else on the port is ignored.
constexpr std::array<std::uint8_t, 6> kSearchMagic = {'O', '1', '6', '0', '2', '?'};
constexpr std::array<std::uint8_t, 6> kAnswerMagic = {'O', '1', '6', '0', '2', '!'};
constexpr std::size_t kMaxDatagram = 1024;
/// Datagrams read in one update, games kept in the list, searches whose answers are taken.
constexpr std::size_t kMaxDatagramsPerUpdate = 64;
constexpr std::size_t kMaxGames = 64;
constexpr std::size_t kKeptSearches = 16;

bool starts_with(std::span<const std::uint8_t> bytes, const std::array<std::uint8_t, 6>& magic) {
    return bytes.size() >= magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin());
}

/// A non-blocking UDP socket on `port` (0: any), with broadcasts allowed; ENET_SOCKET_NULL if not.
ENetSocket open_socket(std::uint16_t port) {
    ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL) {
        return socket;
    }
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    if (enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1) != 0 ||
        enet_socket_set_option(socket, ENET_SOCKOPT_BROADCAST, 1) != 0 || enet_socket_bind(socket, &address) != 0) {
        enet_socket_destroy(socket);
        return ENET_SOCKET_NULL;
    }
    return socket;
}

void send_to(ENetSocket socket, const ENetAddress& to, const std::vector<std::uint8_t>& bytes) {
    ENetBuffer buffer;
    buffer.data = const_cast<std::uint8_t*>(bytes.data());  // ENet's buffers are not const; it only reads
    buffer.dataLength = bytes.size();
    enet_socket_send(socket, &to, &buffer, 1);
}

/// The next datagram waiting on the socket, nothing when there is none.
std::optional<std::vector<std::uint8_t>> receive_from(ENetSocket socket, ENetAddress& from) {
    std::array<std::uint8_t, kMaxDatagram> data{};
    ENetBuffer buffer;
    buffer.data = data.data();
    buffer.dataLength = data.size();
    const int n = enet_socket_receive(socket, &from, &buffer, 1);
    if (n <= 0) {
        return std::nullopt;  // nothing more (0), or an error such as a refused earlier send (< 0)
    }
    return std::vector<std::uint8_t>(data.begin(), data.begin() + n);
}

/// This computer's IPv4 networks: its own addresses and their broadcast addresses (both as ENet
/// keeps hosts, in network byte order). The broadcast to 255.255.255.255 leaves through one
/// network only (Windows with a second adapter, a VPN), so each network gets its own.
struct LocalNetworks {
    std::set<enet_uint32> own;
    std::set<enet_uint32> broadcasts;
};

LocalNetworks local_networks([[maybe_unused]] ENetSocket socket) {
    LocalNetworks n;
    const auto add = [&n](enet_uint32 address, enet_uint32 mask) {
        n.own.insert(address);
        if (mask != 0 && mask != 0xFFFFFFFFU) {
            n.broadcasts.insert(address | ~mask);
        }
    };
#ifdef _WIN32
    std::array<INTERFACE_INFO, 32> list{};
    DWORD bytes = 0;
    if (WSAIoctl(socket, SIO_GET_INTERFACE_LIST, nullptr, 0, list.data(), sizeof(list), &bytes, nullptr, nullptr) ==
        0) {
        for (std::size_t i = 0; i < bytes / sizeof(INTERFACE_INFO); ++i) {
            const INTERFACE_INFO& f = list[i];
            if ((f.iiFlags & IFF_UP) != 0 && (f.iiFlags & IFF_LOOPBACK) == 0 &&
                f.iiAddress.Address.sa_family == AF_INET) {
                add(f.iiAddress.AddressIn.sin_addr.s_addr,
                    (f.iiFlags & IFF_BROADCAST) != 0 ? f.iiNetmask.AddressIn.sin_addr.s_addr : 0);
            }
        }
    }
#else
    ifaddrs* all = nullptr;
    if (getifaddrs(&all) == 0) {
        for (const ifaddrs* f = all; f != nullptr; f = f->ifa_next) {
            if (f->ifa_addr == nullptr || f->ifa_addr->sa_family != AF_INET || (f->ifa_flags & IFF_UP) == 0 ||
                (f->ifa_flags & IFF_LOOPBACK) != 0) {
                continue;
            }
            const auto ipv4 = [](const sockaddr* a) {
                sockaddr_in in{};
                std::memcpy(&in, a, sizeof(in));  // a sockaddr is not aligned as a sockaddr_in
                return static_cast<enet_uint32>(in.sin_addr.s_addr);
            };
            const bool broadcast = (f->ifa_flags & IFF_BROADCAST) != 0 && f->ifa_netmask != nullptr;
            add(ipv4(f->ifa_addr), broadcast ? ipv4(f->ifa_netmask) : 0);
        }
        freeifaddrs(all);
    }
#endif
    return n;
}

/// 127.0.0.1, where a host on this computer always hears.
const enet_uint32 kLoopback = ENET_HOST_TO_NET_32(0x7F000001U);

/// Owns a socket and its share of the ENet library.
class Socket {
public:
    explicit Socket(ENetSocket socket) noexcept : m_socket(socket) {}
    ~Socket() {
        enet_socket_destroy(m_socket);
        EnetLibrary::release();
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    [[nodiscard]] ENetSocket get() const noexcept { return m_socket; }

private:
    ENetSocket m_socket;
};

core::Result<ENetSocket> acquire_socket(std::uint16_t port) {
    if (!EnetLibrary::acquire()) {
        return core::make_error(core::ErrorCode::PlatformError, "ENet could not be initialised");
    }
    const ENetSocket socket = open_socket(port);
    if (socket == ENET_SOCKET_NULL) {
        EnetLibrary::release();
        return core::make_error(core::ErrorCode::PlatformError,
                                "cannot open UDP port " + std::to_string(port) + " (in use?)");
    }
    return socket;
}

class EnetBeacon final : public LanBeacon {
public:
    explicit EnetBeacon(ENetSocket socket) : m_socket(socket) {}

    void set_info(GameInfo info) override { m_info = std::move(info); }

    void update() override {
        ENetAddress from{};
        // A bounded number each frame: a flood of searches must not stop the host's game. The rest
        // waits in the socket (and is dropped by the system when it is full).
        for (std::size_t n = 0; n < kMaxDatagramsPerUpdate; ++n) {
            const auto bytes = receive_from(m_socket.get(), from);
            if (!bytes) {
                break;
            }
            if (const auto nonce = decode_search(*bytes)) {
                send_to(m_socket.get(), from, encode_answer(*nonce, m_info));
            }
        }
    }

private:
    Socket m_socket;
    GameInfo m_info;
};

class EnetBrowser final : public LanBrowser {
public:
    EnetBrowser(ENetSocket socket, std::uint16_t port) : m_socket(socket), m_port(port) {}

    void search(const std::vector<std::string>& addresses) override {
        // The nonce is the time of the search: answers to other nonces are not to this computer.
        if (m_searches.empty() || m_searches.back() != m_clock_ms) {
            m_searches.push_back(m_clock_ms);
            if (m_searches.size() > kKeptSearches) {
                m_searches.pop_front();
            }
        }
        const std::vector<std::uint8_t> bytes = encode_search(m_clock_ms);
        // Everyone on every network of this computer, and this computer itself.
        m_networks = local_networks(m_socket.get());
        std::set<enet_uint32> targets = m_networks.broadcasts;
        targets.insert(ENET_HOST_BROADCAST);
        targets.insert(kLoopback);
        for (const enet_uint32 host : targets) {
            ENetAddress to{};
            to.host = host;
            to.port = m_port;
            send_to(m_socket.get(), to, bytes);
        }
        for (const std::string& name : addresses) {
            if (const auto to = resolve(name)) {
                send_to(m_socket.get(), *to, bytes);
            }
        }
    }

    void update(std::uint32_t real_ms, std::uint32_t forget_ms) override {
        m_clock_ms += real_ms;
        for (FoundGame& g : m_games) {
            g.age_ms += real_ms;
        }
        ENetAddress from{};
        for (std::size_t n = 0; n < kMaxDatagramsPerUpdate; ++n) {
            const auto bytes = receive_from(m_socket.get(), from);
            if (!bytes) {
                break;
            }
            const auto answer = decode_answer(*bytes);
            if (!answer || std::find(m_searches.begin(), m_searches.end(), answer->first) == m_searches.end()) {
                continue;
            }
            // A host on this computer answers on each of its addresses: one game, at 127.0.0.1.
            if (m_networks.own.contains(from.host)) {
                from.host = kLoopback;
            }
            std::array<char, 64> text{};
            if (enet_address_get_host_ip(&from, text.data(), text.size()) != 0) {
                continue;
            }
            const std::string address(text.data());
            auto it = std::find_if(m_games.begin(), m_games.end(), [&](const FoundGame& g) {
                return g.address == address && g.info.port == answer->second.port;
            });
            if (it == m_games.end()) {
                if (m_games.size() >= kMaxGames) {
                    continue;  // a list nobody could read; and someone flooding answers
                }
                m_games.push_back(FoundGame{address, {}, 0, 0});
                it = m_games.end() - 1;
            }
            it->info = answer->second;
            it->ping_ms = m_clock_ms - answer->first;  // the nonce is the time of the search
            it->age_ms = 0;
        }
        std::erase_if(m_games, [&](const FoundGame& g) { return g.age_ms > forget_ms; });
    }

    [[nodiscard]] const std::vector<FoundGame>& games() const override { return m_games; }

private:
    /// A name looked up once (lookups may wait on the network); nothing if it is unknown.
    std::optional<ENetAddress> resolve(const std::string& name) {
        auto it = m_resolved.find(name);
        if (it == m_resolved.end()) {
            ENetAddress address{};
            std::optional<ENetAddress> found;
            if (enet_address_set_host(&address, name.c_str()) == 0) {
                address.port = m_port;
                found = address;
            }
            it = m_resolved.emplace(name, found).first;
        }
        return it->second;
    }

    Socket m_socket;
    std::uint16_t m_port;
    std::uint32_t m_clock_ms = 0;
    std::deque<std::uint32_t> m_searches;  ///< the nonces of the last searches
    std::vector<FoundGame> m_games;
    std::map<std::string, std::optional<ENetAddress>> m_resolved;
    LocalNetworks m_networks;
};

}  // namespace

GameInfo game_info(const Lobby& lobby, const std::vector<Member>& roster, std::uint16_t port) {
    GameInfo info;
    info.port = port;
    info.title = lobby.title;
    info.game = lobby.game;
    for (const Member& m : roster) {
        if (m.host) {
            info.host = m.name;
        }
    }
    for (const LobbySeat& seat : lobby.seats) {
        if (seat.kind == LobbySeat::Kind::Human || seat.kind == LobbySeat::Kind::Open) {
            ++info.slots;
            info.players = static_cast<std::uint8_t>(info.players + (seat.kind == LobbySeat::Kind::Human ? 1 : 0));
        }
    }
    info.started = lobby.started;
    info.password = lobby.password;
    info.join_running = lobby.join_running;
    info.spectators = lobby.spectators;
    return info;
}

std::vector<std::uint8_t> encode_search(std::uint32_t nonce) {
    core::ByteWriter out;
    out.bytes(kSearchMagic);
    out.u16(kProtocolVersion);
    out.u32(nonce);
    return out.take();
}

std::optional<std::uint32_t> decode_search(std::span<const std::uint8_t> bytes) {
    if (!starts_with(bytes, kSearchMagic)) {
        return std::nullopt;
    }
    In in(bytes.subspan(kSearchMagic.size()));
    in.u16();  // the searcher's version: every version gets an answer, to show why it cannot join
    const std::uint32_t nonce = in.u32();
    return in.ok() ? std::optional(nonce) : std::nullopt;
}

std::vector<std::uint8_t> encode_answer(std::uint32_t nonce, const GameInfo& info) {
    core::ByteWriter out;
    out.bytes(kAnswerMagic);
    out.u32(nonce);
    out.u16(info.protocol);
    out.u16(info.port);
    out.string(info.title.substr(0, kMaxTitleLength));
    out.string(info.host.substr(0, kMaxNameLength));
    out.string(info.game.substr(0, kMaxTitleLength));
    out.u8(info.players);
    out.u8(info.slots);
    out.boolean(info.started);
    out.boolean(info.password);
    out.boolean(info.join_running);
    out.boolean(info.spectators);
    return out.take();
}

std::optional<std::pair<std::uint32_t, GameInfo>> decode_answer(std::span<const std::uint8_t> bytes) {
    if (!starts_with(bytes, kAnswerMagic)) {
        return std::nullopt;
    }
    In in(bytes.subspan(kAnswerMagic.size()));
    const std::uint32_t nonce = in.u32();
    GameInfo info;
    info.protocol = in.u16();
    info.port = in.u16();
    info.title = in.text(kMaxTitleLength);
    info.host = in.text(kMaxNameLength);
    info.game = in.text(kMaxTitleLength);
    info.players = in.u8();
    info.slots = in.u8();
    info.started = in.boolean();
    info.password = in.boolean();
    info.join_running = in.boolean();
    info.spectators = in.boolean();
    if (!in.ok() || !in.at_end()) {
        return std::nullopt;
    }
    return std::pair{nonce, std::move(info)};
}

core::Result<std::unique_ptr<LanBeacon>> LanBeacon::open(std::uint16_t port) {
    auto socket = acquire_socket(port);
    if (!socket) {
        return socket.error();
    }
    return std::unique_ptr<LanBeacon>(std::make_unique<EnetBeacon>(socket.value()));
}

core::Result<std::unique_ptr<LanBrowser>> LanBrowser::open(std::uint16_t port) {
    auto socket = acquire_socket(0);
    if (!socket) {
        return socket.error();
    }
    return std::unique_ptr<LanBrowser>(std::make_unique<EnetBrowser>(socket.value(), port));
}

}  // namespace o1602::net
