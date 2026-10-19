// SPDX-License-Identifier: GPL-3.0-or-later
//
// The viewer's side of a multiplayer game (ADR 0011): the session, its menu (multiplayer_menu.hpp),
// the search on the local network, and the games the host makes from its lobby.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>

#include "map_viewer.hpp"

#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/platform/window.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// How often the menu asks the network for games while it shows the search.
constexpr std::uint64_t kSearchEveryMs = 3000;
/// Lines the chat keeps.
constexpr std::size_t kChatKept = 100;

}  // namespace

std::uint64_t MapViewer::content_fingerprint() const {
    // The tables the simulation computes with. The names for new cities and ships travel with the
    // game (Simulation::offer_names), so installations in other languages play together.
    std::uint64_t h = net::fingerprint({});
    for (const char* file : {"haeuser.cod", "figuren.cod"}) {
        if (const auto path = m_game.resolve(file)) {
            if (const auto bytes = core::read_file(*path)) {
                h = net::fingerprint(*bytes, h);
            }
        }
    }
    return h;
}

net::HostSession* MapViewer::host_session() const noexcept {
    return dynamic_cast<net::HostSession*>(m_session.get());
}

net::ClientSession* MapViewer::client_session() const noexcept {
    return dynamic_cast<net::ClientSession*>(m_session.get());
}

core::Status MapViewer::start_multiplayer() {
    const ViewerOptions::Multiplayer& mp = *m_options.multiplayer;
    if (mp.role == ViewerOptions::Multiplayer::Role::Host) {
        auto transport = net::enet_listen(mp.port);
        if (!transport) {
            return transport.error();
        }
        if (m_local_player == sim::kNoPlayer) {
            return core::make_error(core::ErrorCode::Unsupported, "the game has no human player to host with");
        }
        m_session = std::make_unique<net::HostSession>(std::move(transport).value(), m_simulation, m_buildings,
                                                       m_local_player, mp.name, content_fingerprint());
        m_session->submit(sim::PlayerRename{m_local_player, mp.name});
        std::fprintf(stderr, "multiplayer: hosting on UDP port %u as player %u\n", static_cast<unsigned>(mp.port),
                     static_cast<unsigned>(m_local_player));
        return core::Ok{};
    }
    std::fprintf(stderr, "multiplayer: joining %s:%u\n", mp.address.c_str(), static_cast<unsigned>(mp.port));
    return join_game(mp.address, mp.port, mp.password);
}

sim::Simulation* MapViewer::load_from_host(std::span<const std::uint8_t> save) {
    std::vector<std::string> warnings;
    auto world = sim::read_game(save, m_buildings, &warnings, &m_figures);
    if (!world) {
        std::fprintf(stderr, "multiplayer: the host's game cannot be loaded: %s\n", world.error().message.c_str());
        return nullptr;
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    replace_world(std::move(world).value());
    return &m_simulation;
}

void MapViewer::chat_line(std::string name, std::string text, sim::PlayerId seat) {
    std::fprintf(stderr, "multiplayer: %s%s%s\n", name.c_str(), name.empty() ? "" : ": ", text.c_str());
    if (!m_hud) {
        return;
    }
    std::deque<ChatLine>& chat = m_hud->multiplayer_model().chat;
    chat.push_back(ChatLine{std::move(name), std::move(text), seat, platform::ticks_ms()});
    while (chat.size() > kChatKept) {
        chat.pop_front();
    }
}

void MapViewer::apply_session_events() {
    const auto who = [](const net::SessionEvent& e) { return e.name.empty() ? std::to_string(e.seat) : e.name; };
    std::optional<std::string> ended;
    for (const net::SessionEvent& e : m_session->take_events()) {
        switch (e.kind) {
            case net::SessionEvent::Kind::Welcomed:
                std::fprintf(stderr, "multiplayer: in as %s\n",
                             e.seat == sim::kNoPlayer ? "spectator" : ("player " + std::to_string(e.seat)).c_str());
                if (m_hud && m_hud->multiplayer_open() &&
                    m_hud->multiplayer()->screen() == MultiplayerMenu::Screen::Connecting) {
                    m_hud->multiplayer()->show_lobby();
                }
                break;
            case net::SessionEvent::Kind::Loaded:
                std::fprintf(stderr, "multiplayer: game from the host loaded at frame %llu\n",
                             static_cast<unsigned long long>(e.frame));
                if (m_hud && m_hud->multiplayer_open()) {
                    m_hud->close_menu();
                    if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
                        apply_hud(*a);
                    }
                }
                break;
            case net::SessionEvent::Kind::Joined:
                chat_line({}, mp_text(MpText::PlayerJoined, who(e)), e.seat);
                break;
            case net::SessionEvent::Kind::Left:
                chat_line({}, mp_text(MpText::PlayerLeft, who(e)), e.seat);
                break;
            case net::SessionEvent::Kind::Lagging:
                chat_line({}, mp_text(MpText::Lagging, who(e)), e.seat);
                break;
            case net::SessionEvent::Kind::Desync:
                std::fprintf(stderr, "multiplayer: player %u diverged after frame %llu\n",
                             static_cast<unsigned>(e.seat), static_cast<unsigned long long>(e.frame));
                chat_line({}, mp_text(MpText::Desync, who(e)), e.seat);
                break;
            case net::SessionEvent::Kind::Ended:
                std::fprintf(stderr, "multiplayer: %s\n",
                             e.reason == net::EndReason::Refused ? net::describe(e.refused) : net::describe(e.reason));
                ended = e.reason == net::EndReason::Refused ? mp_text(e.refused) : mp_text(e.reason);
                break;
            case net::SessionEvent::Kind::LobbyChanged:
                break;  // the menu shows the lobby as it is
            case net::SessionEvent::Kind::Chat:
                chat_line(e.name, e.text, e.seat);
                break;
        }
    }
    // A session that ends before its game ran: back to the search with the reason. One that ends
    // in the game keeps the game standing as it is (the title says so).
    if (ended && m_hud && m_hud->multiplayer_open()) {
        leave_session(*ended);
    }
}

// --- The menu ---

void MapViewer::open_multiplayer_menu() {
    if (!m_hud || m_hud->multiplayer() == nullptr) {
        return;
    }
    MultiplayerModel& model = m_hud->multiplayer_model();
    if (auto texts = assets::TextTable::load(m_game)) {
        model.level_names = texts->section("ENDLESSKIND");
        std::array<std::string, 4> headings;
        for (std::size_t g = 0; g < headings.size(); ++g) {
            const auto& lines = texts->section("GAME");
            headings[g] = 85 + g < lines.size() ? lines[85 + g] : std::string{};
        }
        if (const auto folder = m_game.resolve("Szenes")) {
            model.scenarios = scenario_list(*folder, headings);
        }
    }
    m_hud->open_multiplayer();
    m_lan_search_ms = 0;  // search at once
}

void MapViewer::apply_multiplayer_menu() {
    MultiplayerMenu* menu = m_hud->multiplayer();
    if (menu == nullptr) {
        return;
    }
    const std::uint64_t now = platform::ticks_ms();
    const auto delta = static_cast<std::uint32_t>(std::min<std::uint64_t>(now - m_lan_update_ms, 1000));
    m_lan_update_ms = now;

    // The search runs while the search screen shows.
    if (menu->screen() == MultiplayerMenu::Screen::Browse) {
        if (!m_lan_browser) {
            if (auto browser = net::LanBrowser::open()) {
                m_lan_browser = std::move(browser).value();
            } else {
                std::fprintf(stderr, "multiplayer: no search on the local network: %s\n",
                             browser.error().message.c_str());
                m_lan_search_ms = now;  // try again with the next search
            }
        }
        if (m_lan_browser) {
            if (m_lan_search_ms == 0 || now - m_lan_search_ms >= kSearchEveryMs) {
                m_lan_browser->search(m_app_settings.remembered_hosts);
                m_lan_search_ms = now;
            }
            m_lan_browser->update(delta);
        }
    } else {
        m_lan_browser.reset();
        m_lan_search_ms = 0;
    }

    MultiplayerModel& model = m_hud->multiplayer_model();
    model.name = m_app_settings.player_name.empty() ? ui::tr("menu.default_name") : m_app_settings.player_name;
    model.found = m_lan_browser ? m_lan_browser->games() : std::vector<net::FoundGame>{};
    model.savegames.clear();
    for (std::size_t slot = 0; slot < m_slots.size(); ++slot) {
        if (!m_slots.names[slot].empty() && std::filesystem::exists(slot_file(save_folder(), slot))) {
            model.savegames.emplace_back(slot, m_slots.names[slot]);
        }
    }
    const auto session_to_model = [&] {
        model.session = m_session.get();
        const net::HostSession* host = host_session();
        model.hosting = host != nullptr;
        model.everyone_ready = host != nullptr && host->everyone_ready();
        model.port = m_hosted ? m_hosted->port : net::kDefaultPort;
    };
    session_to_model();
    for (const MultiplayerEvent& e : m_hud->take_multiplayer_events()) {
        handle_multiplayer(e);
        if (!m_hud->multiplayer_open()) {
            break;  // the rest belongs to the menu that is gone
        }
    }
    session_to_model();  // the events may have begun or ended a session
}

void MapViewer::handle_multiplayer(const MultiplayerEvent& e) {
    MultiplayerMenu* menu = m_hud->multiplayer();
    const auto keep_name = [&] {
        const std::string& name = menu->name();
        if (!name.empty() && name != m_app_settings.player_name) {
            m_app_settings.player_name = name;
            save_settings();
        }
    };
    net::HostSession* host = host_session();
    net::ClientSession* client = client_session();
    switch (e.kind) {
        case MultiplayerEvent::Kind::Close:
            m_lan_browser.reset();
            m_hud->close_multiplayer();
            break;
        case MultiplayerEvent::Kind::Search:
            m_lan_search_ms = 0;
            break;
        case MultiplayerEvent::Kind::Name:
            keep_name();
            break;
        case MultiplayerEvent::Kind::Join:
            keep_name();
            if (auto status = join_game(e.address, e.port, e.password); !status) {
                menu->open(mp_text(MpText::CannotConnect, status.error().message));
            }
            break;
        case MultiplayerEvent::Kind::Host:
            keep_name();
            if (auto status = host_lobby(e.setup); !status) {
                leave_session(mp_text(MpText::CannotHost, status.error().message));
            }
            break;
        case MultiplayerEvent::Kind::Leave:
            leave_session();
            break;
        case MultiplayerEvent::Kind::Start:
            if (auto status = start_hosted_game(); !status) {
                chat_line({}, mp_text(MpText::StartFailed, status.error().message));
            }
            break;
        case MultiplayerEvent::Kind::TakeSeat:
            if (host != nullptr) {
                (void)host->take_seat(e.seat);
            } else if (client != nullptr) {
                client->take_seat(e.seat);
            }
            break;
        case MultiplayerEvent::Kind::SetSeat:
            if (host != nullptr) {
                host->set_seat(e.seat, e.seat_kind);
            }
            break;
        case MultiplayerEvent::Kind::Kick:
            if (host != nullptr) {
                host->kick(e.seat);
            }
            break;
        case MultiplayerEvent::Kind::Ready:
            if (client != nullptr) {
                client->set_ready(e.ready);
            }
            break;
        case MultiplayerEvent::Kind::Say:
            if (m_session) {
                m_session->say(e.text);
            }
            break;
    }
}

void MapViewer::update_lan_beacon() {
    if (m_lan_beacon && m_hosted && m_session) {
        m_lan_beacon->set_info(net::game_info(m_session->lobby(), m_session->roster(), m_hosted->port));
        m_lan_beacon->update();
    }
}

void MapViewer::end_session() {
    m_session.reset();
    m_lan_beacon.reset();
    m_hosted.reset();
    m_hosted_world.reset();
    m_lan_search_ms = 0;
    if (m_hud) {
        m_hud->multiplayer_model().session = nullptr;
        m_hud->multiplayer_model().hosting = false;
        m_hud->multiplayer_model().chat.clear();
    }
}

void MapViewer::leave_session(std::string notice) {
    end_session();
    if (m_hud) {
        m_hud->open_multiplayer(std::move(notice));
    }
}

// --- Hosting ---

core::Status MapViewer::host_lobby(const HostSetup& setup) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "a multiplayer game runs already");
    }
    net::Lobby lobby;
    lobby.title = setup.title;
    lobby.game = setup.file_name;
    std::optional<sim::World> world;
    if (setup.kind == HostSetup::Kind::Endless) {
        // A slot per player, the world is made at the start. Players five on sit behind the
        // free trader, the pirates and the natives (sim::player_slot), whose slots are left out.
        const std::size_t players = std::clamp<std::size_t>(setup.players, HostSetup::kMinPlayers, sim::kMaxPlayers);
        lobby.seats.resize(std::max<std::size_t>(sim::player_slot(players - 1) + 1U, sim::kOriginalPlayers));
        for (std::size_t s = 0; s < lobby.seats.size(); ++s) {
            const bool used = s < players || s >= sim::kFirstExtraPlayer;
            if (!used) {
                lobby.seats[s].kind = net::LobbySeat::Kind::Reserved;
            }
        }
    } else {
        const std::filesystem::path path =
            setup.kind == HostSetup::Kind::Scenario ? setup.file : slot_file(save_folder(), setup.slot);
        auto loaded = read_world(path);
        if (!loaded) {
            return loaded.error();
        }
        // The slots of the humans and computers of the game; human ones are open for whoever comes.
        const std::vector<sim::Player>& players = loaded->players();
        std::size_t count = 0;
        const auto playable = [&](std::size_t s) {
            const sim::Player& p = players[s];
            return p.status != sim::PlayerStatus::Absent &&
                   (p.role == sim::PlayerRole::Human || p.role == sim::PlayerRole::Computer);
        };
        for (std::size_t s = 0; s < players.size() && s < net::kMaxLobbySeats; ++s) {
            count = playable(s) ? s + 1 : count;
        }
        for (std::size_t s = 0; s < count; ++s) {
            net::LobbySeat seat;
            seat.kind = !playable(s)                                   ? net::LobbySeat::Kind::Reserved
                        : players[s].role == sim::PlayerRole::Computer ? net::LobbySeat::Kind::Computer
                                                                       : net::LobbySeat::Kind::Open;
            lobby.seats.push_back(std::move(seat));
        }
        if (lobby.seats.empty()) {
            return core::make_error(core::ErrorCode::Unsupported, "the game has no player slots");
        }
        world = std::move(loaded).value();
    }
    // The host plays the first open slot (the first slot if all are computers), unless it only hosts.
    sim::PlayerId seat = sim::kNoPlayer;
    if (!setup.dedicated) {
        const auto open = std::find_if(lobby.seats.begin(), lobby.seats.end(),
                                       [](const net::LobbySeat& s) { return s.kind == net::LobbySeat::Kind::Open; });
        seat = static_cast<sim::PlayerId>(open != lobby.seats.end() ? open - lobby.seats.begin() : 0);
    }
    net::SessionConfig config;
    config.password = setup.password;
    config.join_running = setup.join_running;
    config.spectators = setup.spectators;
    auto transport = net::enet_listen(setup.port);
    if (!transport) {
        return transport.error();
    }
    const std::string name = m_hud ? m_hud->multiplayer()->name() : m_app_settings.player_name;
    m_session = std::make_unique<net::HostSession>(std::move(transport).value(), std::move(lobby), seat, name,
                                                   content_fingerprint(), config);
    m_hosted = setup;
    m_hosted_world = std::move(world);
    if (setup.lan) {
        if (auto beacon = net::LanBeacon::open()) {
            m_lan_beacon = std::move(beacon).value();
        } else {
            // Another host on this computer has the port; the game can still be joined by address.
            std::fprintf(stderr, "multiplayer: not shown on the local network: %s\n", beacon.error().message.c_str());
        }
    }
    std::fprintf(stderr, "multiplayer: lobby on UDP port %u\n", static_cast<unsigned>(setup.port));
    if (m_hud) {
        m_hud->multiplayer_model().chat.clear();
        m_hud->multiplayer()->show_lobby();
    }
    return core::Ok{};
}

core::Status MapViewer::start_hosted_game() {
    net::HostSession* host = host_session();
    if (host == nullptr || host->started() || !m_hosted) {
        return core::make_error(core::ErrorCode::Unsupported, "no lobby to start");
    }
    const HostSetup& setup = *m_hosted;
    const net::Lobby lobby = host->lobby();
    const sim::PlayerId own = host->local_seat();
    std::optional<sim::World> world;
    if (setup.kind == HostSetup::Kind::Endless) {
        auto texts = assets::TextTable::load(m_game);
        if (!texts) {
            return texts.error();
        }
        // The world of the number the host chose: the same number, the same islands.
        sim::NewGameOptions o;
        o.difficulty = std::min<std::uint8_t>(setup.level, sim::NewGameOptions::kDifficulties - 1);
        o.layout = static_cast<std::uint8_t>(setup.seed % sim::NewGameOptions::kLayouts);
        o.colour = own < sim::kOriginalPlayers ? own : 0;
        o.name = own < lobby.seats.size() ? lobby.seats[own].name : std::string{};
        o.world = m_options.world;
        o.world.seed = setup.seed;
        if (setup.players > sim::kOriginalPlayers) {
            // More players than the original's maps have starts for: an own world, as much sea
            // and land per player as the original's 500 x 350 for four.
            sim::WorldSettings custom;
            custom.players = static_cast<std::uint8_t>(std::min<std::size_t>(setup.players, sim::kMaxPlayers));
            const double scale = std::sqrt(static_cast<double>(custom.players) / sim::kOriginalPlayers);
            custom.width = static_cast<std::uint32_t>(std::lround(custom.width * scale));
            custom.height = static_cast<std::uint32_t>(std::lround(custom.height * scale));
            custom.starts = sim::WorldSettings::Starts::Together;  // all ships at one place, as the original
            o.custom = custom;
        }
        std::random_device device;
        const std::time_t now = std::time(nullptr);
        if (const std::tm* t = std::localtime(&now)) {
            o.game_id = sim::new_game_id(static_cast<std::uint32_t>(t->tm_year + 1900),
                                         static_cast<std::uint32_t>(t->tm_mon + 1),
                                         static_cast<std::uint32_t>(t->tm_mday), static_cast<std::uint32_t>(t->tm_hour),
                                         static_cast<std::uint32_t>(t->tm_min), device());
        }
        std::vector<std::string> warnings;
        auto made = sim::start_new_game(m_game, m_buildings, *texts, o, &warnings);
        if (!made) {
            return made.error();
        }
        for (const auto& w : warnings) {
            std::fprintf(stderr, "warning: %s\n", w.c_str());
        }
        world = std::move(made).value();
        name_more_players(*world);
    } else if (m_hosted_world) {
        world = std::move(*m_hosted_world);
        m_hosted_world.reset();
    } else {
        return core::make_error(core::ErrorCode::NotFound, "the game of the lobby is gone");
    }
    // The slots as the lobby has them. Nobody plays an open slot yet: the computer plays it, and
    // when people may come into the running game, the host hands it to whoever comes.
    for (std::size_t s = 0; s < lobby.seats.size(); ++s) {
        sim::Player* p = world->player(static_cast<sim::PlayerId>(s));
        if (p == nullptr) {
            continue;
        }
        const net::LobbySeat& seat = lobby.seats[s];
        net::LobbySeat::Kind kind = seat.kind;
        if (kind == net::LobbySeat::Kind::Reserved) {
            // Not a player's slot (the factions), or one beyond the number of players.
            if (p->role != sim::PlayerRole::Human && p->role != sim::PlayerRole::Computer) {
                continue;
            }
            kind = net::LobbySeat::Kind::Closed;
        }
        switch (kind) {
            case net::LobbySeat::Kind::Human:
                p->role = sim::PlayerRole::Human;
                p->name = seat.name;
                break;
            case net::LobbySeat::Kind::Open:
            case net::LobbySeat::Kind::Computer:
                p->role = sim::PlayerRole::Computer;
                break;
            case net::LobbySeat::Kind::Closed: {
                // Nobody plays it: no ship of it sails (a slot with no city and no ship is absent).
                p->status = sim::PlayerStatus::Absent;
                std::erase_if(world->ships(), [s](const sim::Ship& ship) { return ship.owner == s; });
                break;
            }
            case net::LobbySeat::Kind::Reserved:
                break;  // made closed above
        }
    }
    replace_world(std::move(*world));
    host->start(m_simulation, m_buildings);
    if (own != sim::kNoPlayer && own < lobby.seats.size()) {
        m_session->submit(sim::PlayerRename{own, lobby.seats[own].name});
    }
    std::fprintf(stderr, "multiplayer: the game starts\n");
    if (m_hud) {
        m_hud->close_menu();
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
    }
    return core::Ok{};
}

// --- Joining ---

core::Status MapViewer::join_game(const std::string& address, std::uint16_t port, const std::string& password) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "a multiplayer game runs already");
    }
    auto transport = net::enet_connect(address, port);
    if (!transport) {
        return transport.error();
    }
    // The host joined by address comes first in the search next time.
    const std::string remembered = port == net::kDefaultPort ? address : address + ":" + std::to_string(port);
    std::vector<std::string>& hosts = m_app_settings.remembered_hosts;
    std::erase(hosts, remembered);
    hosts.insert(hosts.begin(), remembered);
    hosts.resize(std::min(hosts.size(), AppSettings::kMaxHosts));
    save_settings();

    net::Hello hello;
    hello.content = content_fingerprint();
    // The name typed in the menu; joining from the command line, the one given there.
    std::string name = m_hud && m_hud->multiplayer() != nullptr ? m_hud->multiplayer()->name() : std::string{};
    if (name.empty()) {
        name = m_options.multiplayer ? m_options.multiplayer->name : m_app_settings.player_name;
    }
    hello.name = name.empty() ? ui::tr("menu.default_name") : name;
    hello.password = password;
    m_local_player = sim::kNoPlayer;  // until the host sends the game
    m_session = std::make_unique<net::ClientSession>(
        std::move(transport).value(), hello,
        [this](std::span<const std::uint8_t> save) { return load_from_host(save); });
    if (m_hud && m_hud->multiplayer() != nullptr) {
        m_hud->multiplayer_model().chat.clear();
        m_hud->multiplayer()->show_connecting(address + ":" + std::to_string(port));
    }
    return core::Ok{};
}

}  // namespace o1602::app
