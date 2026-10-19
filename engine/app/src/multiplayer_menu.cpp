// SPDX-License-Identifier: GPL-3.0-or-later
#include "multiplayer_menu.hpp"

#include <algorithm>
#include <charconv>
#include <random>

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

// The search
constexpr Id kName = 1;
constexpr Id kAddress = 2;
constexpr Id kJoinPassword = 3;
constexpr Id kConnect = 4;
constexpr Id kCreate = 5;
constexpr Id kBack = 6;
constexpr Id kRefresh = 7;
constexpr Id kJoin = 8;
constexpr Id kBrowseUp = 9;
constexpr Id kBrowseDown = 10;
constexpr Id kBrowseRow = 100;  ///< + row
constexpr std::size_t kBrowseRows = 14;
// Creating
constexpr Id kKindEndless = 20;
constexpr Id kKindScenario = 21;
constexpr Id kKindSavegame = 22;
constexpr Id kLevel = 30;  ///< + level
constexpr Id kSeed = 40;
constexpr Id kRoll = 41;
constexpr Id kTitle = 42;
constexpr Id kPassword = 43;
constexpr Id kLan = 44;
constexpr Id kJoinRunning = 45;
constexpr Id kSpectators = 46;
constexpr Id kDedicated = 47;
constexpr Id kPort = 48;
constexpr Id kOpenLobby = 49;
constexpr Id kCreateBack = 50;
constexpr Id kListUp = 51;
constexpr Id kListDown = 52;
constexpr Id kFewerPlayers = 53;
constexpr Id kMorePlayers = 54;
constexpr Id kListRow = 200;  ///< + row
constexpr std::size_t kListRows = 11;
// The lobby
constexpr Id kSeatKind = 300;           ///< + slot
constexpr Id kSeatTake = 400;           ///< + slot
constexpr Id kSeatKick = 500;           ///< + slot
constexpr std::size_t kMaxSeats = 100;  ///< slot numbers the ids above leave room for
constexpr Id kWatcherKick = 700;        ///< + row of the spectators the host can send away
constexpr std::size_t kWatcherRows = 5;
constexpr Id kChat = 60;
constexpr Id kLeave = 61;
constexpr Id kReady = 62;
constexpr Id kStart = 63;
constexpr Id kWatch = 64;
constexpr std::size_t kChatLines = 12;
// Connecting
constexpr Id kCancel = 70;

constexpr std::size_t kMaxAddress = 64;
constexpr std::size_t kMaxNumber = 10;

// The frame of every screen, its heading and the buttons at the bottom.
constexpr Rect kScreen{16, 16, 992, 736};
constexpr Rect kHeading{40, 28, 560, 40};
constexpr std::int32_t kBottomY = 690;
constexpr std::int32_t kRowH = 32;

std::uint32_t random_seed() {
    std::random_device device;
    return device() % 100000;
}

std::optional<std::uint32_t> number(std::string_view text) {
    std::uint32_t v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    return ec == std::errc() && end == text.data() + text.size() && !text.empty() ? std::optional(v) : std::nullopt;
}

}  // namespace

std::optional<std::pair<std::string, std::uint16_t>> parse_address(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    std::uint16_t port = net::kDefaultPort;
    if (const std::size_t colon = text.rfind(':'); colon != std::string_view::npos) {
        const auto p = number(text.substr(colon + 1));
        if (!p || *p == 0 || *p > 65535) {
            return std::nullopt;
        }
        port = static_cast<std::uint16_t>(*p);
        text = text.substr(0, colon);
    }
    if (text.empty() || text.find_first_of(" :") != std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{std::string(text), port};
}

MultiplayerMenu::MultiplayerMenu(ui::FormStyle style) : m_form(style) {
    m_setup.seed = random_seed();
    m_seed_text = std::to_string(m_setup.seed);
    m_port_text = std::to_string(m_setup.port);
}

void MultiplayerMenu::open(std::string notice) {
    m_screen = Screen::Browse;
    m_notice = std::move(notice);
    m_form.set_focus(std::nullopt);
    m_name_known = false;  // the name from the settings again
    m_ready = false;
}

void MultiplayerMenu::show_connecting(std::string address) {
    m_screen = Screen::Connecting;
    m_connecting = std::move(address);
    m_form.set_focus(std::nullopt);
}

void MultiplayerMenu::show_lobby() {
    m_screen = Screen::Lobby;
    m_ready = false;
    m_chat.clear();
    m_form.set_focus(kChat);  // the keys go to the chat
}

std::vector<ui::DrawItem> MultiplayerMenu::draw(const MultiplayerModel& model,
                                                const std::array<const ui::Font*, 3>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::vector<MultiplayerEvent> MultiplayerMenu::mouse(const MultiplayerModel& model, std::int32_t x, std::int32_t y,
                                                     bool down) {
    std::vector<MultiplayerEvent> out;
    layout(model);
    if (const auto id = m_form.mouse(x, y, down)) {
        click(model, *id, out);
    } else if (!down) {
        // A click beside the fields ends typing (the chat line keeps its text).
        if (const auto pressed = m_form.hovered(); !pressed || field_text(*pressed) == nullptr) {
            if (m_screen != Screen::Lobby) {
                m_form.set_focus(std::nullopt);
            }
        }
    }
    return out;
}

void MultiplayerMenu::layout(const MultiplayerModel& model) {
    if (!m_name_known) {
        m_name = model.name;
        m_name_known = true;
    }
    m_form.clear();
    m_form.panel(kScreen);
    switch (m_screen) {
        case Screen::Browse:
            layout_browse(model);
            break;
        case Screen::Create:
            layout_create(model);
            break;
        case Screen::Lobby:
            layout_lobby(model);
            break;
        case Screen::Connecting:
            layout_connecting();
            break;
    }
}

// --- Finding a game ---

std::vector<MultiplayerMenu::BrowseRow> MultiplayerMenu::browse_rows(const MultiplayerModel& model) const {
    std::vector<BrowseRow> rows;
    for (const net::FoundGame& g : model.found) {
        rows.push_back(BrowseRow{g.address + ":" + std::to_string(g.info.port), &g});
    }
    return rows;
}

std::string MultiplayerMenu::state_text(const net::GameInfo& info) const {
    if (info.protocol != net::kProtocolVersion) {
        return tr(MpText::StateOtherVersion);
    }
    if (!info.started) {
        return tr(MpText::StateLobby);
    }
    if (info.join_running && info.players < info.slots) {
        return tr(MpText::StateRunningJoin);
    }
    return tr(info.spectators ? MpText::StateRunningWatch : MpText::StateRunning);
}

void MultiplayerMenu::layout_browse(const MultiplayerModel& model) {
    ui::Form& f = m_form;
    f.label(kHeading, tr(MpText::Multiplayer), Align::Left, Tone::Heading);
    f.label(Rect{560, 32, 170, 32}, tr(MpText::YourName), Align::Right);
    f.field(kName, Rect{740, 32, 244, 32}, m_name);

    // The games that answered the search (hosts joined before are asked too, but only listed when
    // they answer).
    f.panel(Rect{40, 84, 644, 586});
    const std::array<std::pair<std::int32_t, std::int32_t>, 5> cols = {
        {{48, 210}, {258, 140}, {398, 80}, {478, 130}, {608, 70}}};
    const std::array<MpText, 5> heads = {MpText::ColGame, MpText::ColHost, MpText::ColPlayers, MpText::ColState,
                                         MpText::ColPing};
    for (std::size_t c = 0; c < cols.size(); ++c) {
        f.label(Rect{cols[c].first, 92, cols[c].second, 28}, tr(heads[c]), Align::Left, Tone::Heading);
    }
    const std::vector<BrowseRow> rows = browse_rows(model);
    m_browse_first = std::min(m_browse_first, rows.size() > kBrowseRows ? rows.size() - kBrowseRows : 0);
    if (rows.empty()) {
        f.label(Rect{48, 140, 620, 30}, tr(MpText::NoGamesFound));
    }
    for (std::size_t r = 0; r < kBrowseRows && m_browse_first + r < rows.size(); ++r) {
        const BrowseRow& row = rows[m_browse_first + r];
        const std::int32_t y = 124 + static_cast<std::int32_t>(r) * kRowH;
        f.row(kBrowseRow + static_cast<Id>(r), Rect{44, y, 636, kRowH}, m_selected == row.address);
        const net::GameInfo& info = row.game->info;
        f.label(Rect{cols[0].first, y, cols[0].second, kRowH}, info.password ? "* " + info.title : info.title);
        f.label(Rect{cols[1].first, y, cols[1].second, kRowH}, info.host);
        f.label(Rect{cols[2].first, y, cols[2].second, kRowH},
                std::to_string(info.players) + " / " + std::to_string(info.slots));
        f.label(Rect{cols[3].first, y, cols[3].second, kRowH}, state_text(info), Align::Left,
                info.protocol == net::kProtocolVersion ? Tone::Normal : Tone::Warning);
        f.label(Rect{cols[4].first, y, cols[4].second, kRowH}, std::to_string(row.game->ping_ms) + " ms");
    }
    if (m_browse_first > 0) {
        f.button(kBrowseUp, Rect{604, 588, 36, 30}, "-");
    }
    if (m_browse_first + kBrowseRows < rows.size()) {
        f.button(kBrowseDown, Rect{644, 588, 36, 30}, "+");
    }
    f.label(Rect{48, 626, 620, 30}, m_notice.empty() ? tr(MpText::SearchHint) : m_notice, Align::Left,
            m_notice.empty() ? Tone::Dim : Tone::Warning);

    // Joining by address, creating.
    f.panel(Rect{700, 84, 284, 236});
    f.label(Rect{708, 92, 268, 30}, tr(MpText::ConnectDirectly), Align::Left, Tone::Heading);
    f.label(Rect{708, 124, 268, 24}, tr(MpText::HostAddress), Align::Left, Tone::Dim);
    f.field(kAddress, Rect{712, 150, 260, 32}, m_address);
    f.label(Rect{708, 186, 268, 24}, tr(MpText::Password), Align::Left, Tone::Dim);
    f.field(kJoinPassword, Rect{712, 212, 260, 32}, m_join_password, true);
    f.button(kConnect, Rect{712, 262, 260, 40}, tr(MpText::Connect), parse_address(m_address).has_value());
    f.button(kCreate, Rect{700, 340, 284, 56}, tr(MpText::CreateGame), true, true);
    f.paragraph(Rect{700, 410, 284, 120}, tr(MpText::HostHint, std::to_string(net::kDefaultPort)), Tone::Dim);

    f.button(kBack, Rect{40, kBottomY, 180, 44}, tr(MpText::Back));
    f.button(kRefresh, Rect{596, kBottomY, 180, 44}, tr(MpText::Refresh));
    f.button(kJoin, Rect{804, kBottomY, 180, 44}, tr(MpText::Join), m_selected.has_value());
}

// --- Creating a game ---

void MultiplayerMenu::layout_create(const MultiplayerModel& model) {
    ui::Form& f = m_form;
    f.label(kHeading, tr(MpText::CreateTitle), Align::Left, Tone::Heading);
    using Kind = HostSetup::Kind;
    f.button(kKindEndless, Rect{40, 84, 200, 40}, tr(MpText::NewEndless), true, m_setup.kind == Kind::Endless);
    f.button(kKindScenario, Rect{244, 84, 180, 40}, tr(MpText::Scenario), true, m_setup.kind == Kind::Scenario);
    f.button(kKindSavegame, Rect{428, 84, 180, 40}, tr(MpText::Savegame), true, m_setup.kind == Kind::Savegame);
    f.panel(Rect{40, 130, 600, 540});
    switch (m_setup.kind) {
        case Kind::Endless: {
            f.label(Rect{52, 142, 300, 30}, tr(MpText::Level), Align::Left, Tone::Heading);
            for (std::size_t i = 0; i < sim::NewGameOptions::kDifficulties; ++i) {
                std::string name = i < model.level_names.size() && !model.level_names[i].empty()
                                       ? model.level_names[i]
                                       : tr(MpText::LevelN, std::to_string(i + 1));
                const auto x = static_cast<std::int32_t>(56 + (i % 2) * 288);
                const auto y = static_cast<std::int32_t>(180 + (i / 2) * 50);
                f.button(kLevel + static_cast<Id>(i), Rect{x, y, 280, 42}, std::move(name), true, m_setup.level == i);
            }
            f.label(Rect{52, 300, 300, 30}, tr(MpText::WorldNumber), Align::Left, Tone::Heading);
            f.field(kSeed, Rect{56, 336, 200, 36}, m_seed_text);
            f.button(kRoll, Rect{264, 336, 140, 36}, tr(MpText::Roll));
            f.label(Rect{52, 392, 300, 30}, tr(MpText::Players), Align::Left, Tone::Heading);
            f.button(kFewerPlayers, Rect{56, 428, 44, 36}, "-", m_setup.players > HostSetup::kMinPlayers);
            f.label(Rect{100, 428, 80, 36}, std::to_string(m_setup.players), Align::Centre);
            f.button(kMorePlayers, Rect{180, 428, 44, 36}, "+", m_setup.players < sim::kMaxPlayers);
            f.paragraph(Rect{52, 484, 576, 80}, tr(MpText::EndlessHint), Tone::Dim);
            break;
        }
        case Kind::Scenario:
        case Kind::Savegame: {
            const bool scenarios = m_setup.kind == Kind::Scenario;
            const std::size_t count = scenarios ? model.scenarios.size() : model.savegames.size();
            m_list_first = std::min(m_list_first, count > kListRows ? count - kListRows : 0);
            if (count == 0 && !scenarios) {
                f.label(Rect{52, 142, 560, 30}, tr(MpText::NoSavegames));
            }
            for (std::size_t r = 0; r < kListRows && m_list_first + r < count; ++r) {
                const std::size_t i = m_list_first + r;
                const std::int32_t y = 140 + static_cast<std::int32_t>(r) * kRowH;
                if (scenarios) {
                    const MenuScenario& s = model.scenarios[i];
                    if (s.separator) {
                        f.label(Rect{52, y, 560, kRowH}, s.name, Align::Left, Tone::Heading);
                        continue;
                    }
                    f.row(kListRow + static_cast<Id>(r), Rect{44, y, 592, kRowH}, m_chosen == i);
                    f.label(Rect{52, y, 576, kRowH}, s.name);
                } else {
                    f.row(kListRow + static_cast<Id>(r), Rect{44, y, 592, kRowH}, m_chosen == model.savegames[i].first);
                    f.label(Rect{52, y, 576, kRowH}, model.savegames[i].second);
                }
            }
            if (m_list_first > 0) {
                f.button(kListUp, Rect{556, 498, 36, 30}, "-");
            }
            if (m_list_first + kListRows < count) {
                f.button(kListDown, Rect{596, 498, 36, 30}, "+");
            }
            f.paragraph(Rect{52, 540, 576, 120}, tr(scenarios ? MpText::ScenarioHint : MpText::SavegameHint),
                        Tone::Dim);
            break;
        }
    }

    // The session.
    f.panel(Rect{656, 84, 328, 586});
    f.label(Rect{668, 92, 300, 30}, tr(MpText::Session), Align::Left, Tone::Heading);
    f.label(Rect{664, 126, 312, 24}, tr(MpText::GameTitle), Align::Left, Tone::Dim);
    f.field(kTitle, Rect{668, 152, 304, 34}, m_setup.title);
    f.label(Rect{664, 194, 312, 24}, tr(MpText::PasswordOptional), Align::Left, Tone::Dim);
    f.field(kPassword, Rect{668, 220, 304, 34}, m_setup.password, true);
    f.check(kLan, Rect{668, 274, 304, 32}, tr(MpText::ShowOnLan), m_setup.lan);
    f.check(kJoinRunning, Rect{668, 310, 304, 32}, tr(MpText::AllowJoinRunning), m_setup.join_running);
    f.check(kSpectators, Rect{668, 346, 304, 32}, tr(MpText::AllowSpectators), m_setup.spectators);
    f.check(kDedicated, Rect{668, 382, 304, 32}, tr(MpText::HostOnly), m_setup.dedicated);
    f.label(Rect{664, 620, 150, 34}, tr(MpText::Port), Align::Left, Tone::Dim);
    f.field(kPort, Rect{820, 620, 152, 34}, m_port_text);

    const bool chosen = m_setup.kind == HostSetup::Kind::Endless || m_chosen.has_value();
    const auto port = number(m_port_text);
    const bool valid = chosen && port && *port > 0 && *port <= 65535 &&
                       (m_setup.kind != HostSetup::Kind::Endless || number(m_seed_text).has_value());
    f.button(kCreateBack, Rect{40, kBottomY, 180, 44}, tr(MpText::Back));
    f.button(kOpenLobby, Rect{764, kBottomY, 220, 44}, tr(MpText::OpenLobby), valid, true);
}

// --- The lobby ---

void MultiplayerMenu::layout_lobby(const MultiplayerModel& model) {
    ui::Form& f = m_form;
    const net::Session* session = model.session;
    if (session == nullptr) {
        return;
    }
    const net::Lobby& lobby = session->lobby();
    const sim::PlayerId me = session->local_seat();
    f.label(kHeading, lobby.title, Align::Left, Tone::Heading);
    f.label(Rect{560, 32, 424, 32}, model.hosting ? tr(MpText::HostPort, std::to_string(model.port)) : lobby.game,
            Align::Right, Tone::Dim);

    // The slots of the players (the factions' are left out), closer together when there are many;
    // the chat below gets what they leave.
    std::vector<std::size_t> shown;
    for (std::size_t s = 0; s < lobby.seats.size() && s < kMaxSeats; ++s) {
        if (lobby.seats[s].kind != net::LobbySeat::Kind::Reserved) {
            shown.push_back(s);
        }
    }
    const std::int32_t row_h = shown.size() <= 8 ? 36 : 30;
    const auto rows_end = static_cast<std::int32_t>(96 + shown.size() * static_cast<std::size_t>(row_h));
    const std::int32_t chat_top = rows_end + 50;
    f.panel(Rect{40, 84, 620, chat_top - 96});
    for (std::size_t r = 0; r < shown.size(); ++r) {
        const std::size_t s = shown[r];
        const net::LobbySeat& seat = lobby.seats[s];
        const auto y = static_cast<std::int32_t>(96 + r * static_cast<std::size_t>(row_h));
        const std::int32_t h = row_h - 2;
        const Id k = static_cast<Id>(s);
        f.swatch(Rect{52, y + 4, 22, h - 8}, banner_colour(s));
        std::string who;
        std::string state;
        Tone tone = Tone::Normal;
        switch (seat.kind) {
            case net::LobbySeat::Kind::Human: {
                who = seat.name;
                const bool host = std::any_of(session->roster().begin(), session->roster().end(),
                                              [&](const net::Member& m) { return m.host && m.seat == s; });
                if (s == me) {
                    who += " " + tr(MpText::You);
                } else if (host) {
                    who += " " + tr(MpText::Host);
                }
                state = seat.ready ? tr(MpText::Ready) : tr(MpText::Choosing);
                tone = seat.ready ? Tone::Normal : Tone::Dim;
                break;
            }
            case net::LobbySeat::Kind::Open:
                who = tr(MpText::SeatOpen);
                state = tr(MpText::Free);
                tone = Tone::Dim;
                break;
            case net::LobbySeat::Kind::Computer:
                who = tr(MpText::SeatComputer);
                state = tr(MpText::Ready);
                break;
            case net::LobbySeat::Kind::Closed:
            case net::LobbySeat::Kind::Reserved:
                who = tr(MpText::SeatClosed);
                tone = Tone::Dim;
                break;
        }
        const bool human = seat.kind == net::LobbySeat::Kind::Human;
        if (model.hosting && !human) {
            // The host sets who plays a slot by clicking through the kinds.
            f.button(kSeatKind + k, Rect{84, y, 250, h}, who);
        } else {
            f.label(Rect{80, y, 260, h}, who, Align::Left, human ? Tone::Normal : Tone::Dim);
        }
        f.label(Rect{344, y, 150, h}, state, Align::Left, tone);
        if (model.hosting && human && s != me) {
            f.button(kSeatKick + k, Rect{500, y, 150, h}, tr(MpText::Kick));
        } else if (seat.kind == net::LobbySeat::Kind::Open && s != me && !lobby.started) {
            f.button(kSeatTake + k, Rect{500, y, 150, h}, tr(MpText::Take));
        }
    }
    std::string watchers;
    for (const net::Member& m : session->roster()) {
        if (m.seat == sim::kNoPlayer) {
            watchers += (watchers.empty() ? "" : ", ") + m.name;
        }
    }
    f.label(Rect{48, rows_end + 4, 600, 30}, tr(MpText::Watchers, watchers.empty() ? tr(MpText::Nobody) : watchers),
            Align::Left, Tone::Dim);

    // The chat.
    f.panel(Rect{40, chat_top, 620, 670 - chat_top});
    const auto chat_lines = static_cast<std::size_t>(std::max(0, (622 - 8 - (chat_top + 8)) / 28));
    const std::size_t lines = std::min({model.chat.size(), kChatLines, chat_lines});
    for (std::size_t i = 0; i < lines; ++i) {
        const ChatLine& line = model.chat[model.chat.size() - lines + i];
        const std::int32_t y = chat_top + 8 + static_cast<std::int32_t>(i) * 28;
        if (line.name.empty()) {
            f.label(Rect{48, y, 604, 28}, line.text, Align::Left, Tone::Dim);
        } else {
            f.label(Rect{48, y, 604, 28}, line.name + ": " + line.text);
        }
    }
    f.field(kChat, Rect{52, 622, 596, 36}, m_chat);
    if (m_chat.empty() && m_form.focus() != kChat) {
        f.label(Rect{52, 622, 596, 36}, tr(MpText::ChatHint), Align::Left, Tone::Dim);
    }

    // What is played, and the hint.
    f.panel(Rect{676, 84, 308, 586});
    f.paragraph(Rect{684, 96, 292, 70}, lobby.game, Tone::Heading, 26);
    const auto setting = [&](std::int32_t y, MpText name, bool on) {
        f.label(Rect{684, y, 200, 30}, tr(name), Align::Left, Tone::Dim);
        f.label(Rect{876, y, 100, 30}, tr(on ? MpText::Yes : MpText::No), Align::Right);
    };
    setting(176, MpText::Password, lobby.password);
    setting(208, MpText::AllowJoinRunning, lobby.join_running);
    setting(240, MpText::AllowSpectators, lobby.spectators);
    if (lobby.spectators && me != sim::kNoPlayer) {
        f.button(kWatch, Rect{688, 290, 284, 38}, tr(MpText::Watch));
    }
    if (model.hosting) {
        // The host can send spectators away too (they hold no slot to kick).
        m_watchers.clear();
        const auto& roster = session->roster();
        for (std::size_t i = 1; i < roster.size() && m_watchers.size() < kWatcherRows; ++i) {
            if (roster[i].seat == sim::kNoPlayer) {
                const auto y = static_cast<std::int32_t>(340 + m_watchers.size() * 40);
                f.label(Rect{688, y, 130, 38}, roster[i].name, Align::Left, Tone::Dim);
                f.button(kWatcherKick + static_cast<Id>(m_watchers.size()), Rect{822, y, 150, 38}, tr(MpText::Kick));
                m_watchers.push_back(i);
            }
        }
    }
    f.paragraph(Rect{684, 560, 292, 100}, tr(model.hosting ? MpText::StartHint : MpText::WaitHostStart), Tone::Dim);

    f.button(kLeave, Rect{40, kBottomY, 180, 44}, tr(MpText::Leave));
    if (!model.hosting && me != sim::kNoPlayer) {
        f.button(kReady, Rect{560, kBottomY, 200, 44}, tr(MpText::Ready), true, m_ready);
    }
    if (model.hosting) {
        f.button(kStart, Rect{764, kBottomY, 220, 44}, tr(MpText::Start), model.everyone_ready, true);
    }
}

void MultiplayerMenu::layout_connecting() {
    m_form.label(kHeading, tr(MpText::Multiplayer), Align::Left, Tone::Heading);
    m_form.label(Rect{40, 340, 944, 40}, tr(MpText::Connecting, m_connecting), Align::Centre);
    m_form.button(kCancel, Rect{40, kBottomY, 180, 44}, tr(MpText::Back));
}

// --- Clicks and keys ---

std::optional<MultiplayerEvent> MultiplayerMenu::join_event(const std::string& address) const {
    const auto parsed = parse_address(address);
    if (!parsed) {
        return std::nullopt;
    }
    MultiplayerEvent e;
    e.kind = MultiplayerEvent::Kind::Join;
    e.address = parsed->first;
    e.port = parsed->second;
    e.password = m_join_password;
    return e;
}

void MultiplayerMenu::click(const MultiplayerModel& model, Id id, std::vector<MultiplayerEvent>& out) {
    if (field_text(id) != nullptr) {
        m_form.set_focus(id);
        return;
    }
    m_form.set_focus(m_screen == Screen::Lobby ? std::optional<Id>(kChat) : std::nullopt);
    const auto event = [&](MultiplayerEvent::Kind kind) {
        MultiplayerEvent e;
        e.kind = kind;
        out.push_back(std::move(e));
        return &out.back();
    };
    switch (m_screen) {
        case Screen::Browse: {
            const std::vector<BrowseRow> rows = browse_rows(model);
            if (id == kBack) {
                event(MultiplayerEvent::Kind::Close);
            } else if (id == kRefresh) {
                event(MultiplayerEvent::Kind::Search);
            } else if (id == kConnect) {
                if (auto e = join_event(m_address)) {
                    out.push_back(std::move(*e));
                }
            } else if (id == kJoin && m_selected) {
                if (auto e = join_event(*m_selected)) {
                    out.push_back(std::move(*e));
                }
            } else if (id == kCreate) {
                m_screen = Screen::Create;
                if (m_setup.title.empty()) {
                    m_setup.title = tr(MpText::DefaultTitle, m_name);
                }
            } else if (id == kBrowseUp) {
                m_browse_first -= std::min(m_browse_first, kBrowseRows);
            } else if (id == kBrowseDown) {
                m_browse_first += kBrowseRows;
            } else if (id >= kBrowseRow && id < kBrowseRow + static_cast<Id>(kBrowseRows)) {
                const std::size_t i = m_browse_first + static_cast<std::size_t>(id - kBrowseRow);
                if (i < rows.size()) {
                    if (m_selected == rows[i].address) {
                        if (auto e = join_event(rows[i].address)) {  // a second click joins
                            out.push_back(std::move(*e));
                        }
                    }
                    m_selected = rows[i].address;
                }
            }
            break;
        }
        case Screen::Create: {
            using Kind = HostSetup::Kind;
            if (id == kCreateBack) {
                m_screen = Screen::Browse;
            } else if (id == kKindEndless || id == kKindScenario || id == kKindSavegame) {
                const Kind kind = id == kKindEndless    ? Kind::Endless
                                  : id == kKindScenario ? Kind::Scenario
                                                        : Kind::Savegame;
                if (kind != m_setup.kind) {
                    m_setup.kind = kind;
                    m_chosen.reset();
                    m_list_first = 0;
                }
            } else if (id >= kLevel && id < kLevel + static_cast<Id>(sim::NewGameOptions::kDifficulties)) {
                m_setup.level = static_cast<std::uint8_t>(id - kLevel);
            } else if (id == kFewerPlayers && m_setup.players > HostSetup::kMinPlayers) {
                --m_setup.players;
            } else if (id == kMorePlayers && m_setup.players < sim::kMaxPlayers) {
                ++m_setup.players;
            } else if (id == kRoll) {
                m_setup.seed = random_seed();
                m_seed_text = std::to_string(m_setup.seed);
            } else if (id == kLan) {
                m_setup.lan = !m_setup.lan;
            } else if (id == kJoinRunning) {
                m_setup.join_running = !m_setup.join_running;
            } else if (id == kSpectators) {
                m_setup.spectators = !m_setup.spectators;
            } else if (id == kDedicated) {
                m_setup.dedicated = !m_setup.dedicated;
            } else if (id == kListUp) {
                m_list_first -= std::min(m_list_first, kListRows);
            } else if (id == kListDown) {
                m_list_first += kListRows;
            } else if (id >= kListRow && id < kListRow + static_cast<Id>(kListRows)) {
                const std::size_t i = m_list_first + static_cast<std::size_t>(id - kListRow);
                if (m_setup.kind == Kind::Scenario && i < model.scenarios.size() && !model.scenarios[i].separator) {
                    m_chosen = i;
                } else if (m_setup.kind == Kind::Savegame && i < model.savegames.size()) {
                    m_chosen = model.savegames[i].first;
                }
            } else if (id == kOpenLobby) {
                HostSetup setup = m_setup;
                setup.seed = number(m_seed_text).value_or(m_setup.seed);
                setup.port = static_cast<std::uint16_t>(number(m_port_text).value_or(net::kDefaultPort));
                if (setup.title.empty()) {
                    setup.title = tr(MpText::DefaultTitle, m_name);
                }
                if (m_setup.kind == Kind::Scenario && m_chosen && *m_chosen < model.scenarios.size()) {
                    const MenuScenario& s = model.scenarios[*m_chosen];
                    setup.file = s.files.empty() ? std::filesystem::path{} : s.files.front().second;
                    setup.file_name = s.name;
                } else if (m_setup.kind == Kind::Savegame && m_chosen) {
                    setup.slot = *m_chosen;
                    for (const auto& [slot, name] : model.savegames) {
                        if (slot == *m_chosen) {
                            setup.file_name = name;
                        }
                    }
                } else if (m_setup.kind == Kind::Endless) {
                    std::string level = setup.level < model.level_names.size()
                                            ? model.level_names[setup.level]
                                            : tr(MpText::LevelN, std::to_string(setup.level + 1));
                    setup.file_name = tr(MpText::EndlessGame) + ", " + level + ", " +
                                      tr(MpText::PlayerCount, std::to_string(setup.players)) + ", " +
                                      tr(MpText::WorldNumber) + " " + std::to_string(setup.seed);
                }
                event(MultiplayerEvent::Kind::Host)->setup = std::move(setup);
            }
            break;
        }
        case Screen::Lobby: {
            const net::Session* session = model.session;
            if (id == kLeave) {
                event(MultiplayerEvent::Kind::Leave);
            } else if (id == kReady) {
                m_ready = !m_ready;
                event(MultiplayerEvent::Kind::Ready)->ready = m_ready;
            } else if (id == kStart) {
                event(MultiplayerEvent::Kind::Start);
            } else if (id == kWatch) {
                event(MultiplayerEvent::Kind::TakeSeat)->seat = sim::kNoPlayer;
                m_ready = false;
            } else if (id >= kSeatTake && id < kSeatTake + static_cast<Id>(kMaxSeats)) {
                event(MultiplayerEvent::Kind::TakeSeat)->seat = static_cast<sim::PlayerId>(id - kSeatTake);
                m_ready = false;
            } else if (id >= kWatcherKick && id < kWatcherKick + static_cast<Id>(m_watchers.size())) {
                event(MultiplayerEvent::Kind::KickMember)->member =
                    m_watchers[static_cast<std::size_t>(id - kWatcherKick)];
            } else if (id >= kSeatKick && id < kSeatKick + static_cast<Id>(kMaxSeats)) {
                event(MultiplayerEvent::Kind::Kick)->seat = static_cast<sim::PlayerId>(id - kSeatKick);
            } else if (id >= kSeatKind && id < kSeatKind + static_cast<Id>(kMaxSeats) && session != nullptr) {
                const auto s = static_cast<std::size_t>(id - kSeatKind);
                if (s < session->lobby().seats.size()) {
                    using SeatKind = net::LobbySeat::Kind;
                    const SeatKind now = session->lobby().seats[s].kind;
                    MultiplayerEvent* e = event(MultiplayerEvent::Kind::SetSeat);
                    e->seat = static_cast<sim::PlayerId>(s);
                    // Every game has all its players: a slot is a person's or the computer's.
                    e->seat_kind = now == SeatKind::Open ? SeatKind::Computer : SeatKind::Open;
                }
            }
            break;
        }
        case Screen::Connecting:
            if (id == kCancel) {
                event(MultiplayerEvent::Kind::Leave);
            }
            break;
    }
}

std::optional<MultiplayerEvent> MultiplayerMenu::back() {
    m_form.set_focus(std::nullopt);
    MultiplayerEvent e;
    switch (m_screen) {
        case Screen::Browse:
            e.kind = MultiplayerEvent::Kind::Close;
            return e;
        case Screen::Create:
            m_screen = Screen::Browse;
            return std::nullopt;
        case Screen::Lobby:
        case Screen::Connecting:
            e.kind = MultiplayerEvent::Kind::Leave;
            return e;
    }
    return std::nullopt;
}

std::string* MultiplayerMenu::field_text(Id id) {
    switch (id) {
        case kName:
            return m_screen == Screen::Browse ? &m_name : nullptr;
        case kAddress:
            return m_screen == Screen::Browse ? &m_address : nullptr;
        case kJoinPassword:
            return m_screen == Screen::Browse ? &m_join_password : nullptr;
        case kSeed:
            return m_screen == Screen::Create ? &m_seed_text : nullptr;
        case kTitle:
            return m_screen == Screen::Create ? &m_setup.title : nullptr;
        case kPassword:
            return m_screen == Screen::Create ? &m_setup.password : nullptr;
        case kPort:
            return m_screen == Screen::Create ? &m_port_text : nullptr;
        case kChat:
            return m_screen == Screen::Lobby ? &m_chat : nullptr;
        default:
            return nullptr;
    }
}

std::size_t MultiplayerMenu::field_limit(Id id) const {
    switch (id) {
        case kName:
            return net::kMaxNameLength;
        case kAddress:
            return kMaxAddress;
        case kJoinPassword:
        case kPassword:
            return net::kMaxPasswordLength;
        case kTitle:
            return net::kMaxTitleLength;
        case kSeed:
        case kPort:
            return kMaxNumber;
        case kChat:
            return net::kMaxChatLength;
        default:
            return 0;
    }
}

void MultiplayerMenu::type(std::string_view text) {
    const auto focus = m_form.focus();
    std::string* s = focus ? field_text(*focus) : nullptr;
    if (s == nullptr) {
        return;
    }
    const bool digits = *focus == kSeed || *focus == kPort;
    for (const char c : text) {
        const bool ok = digits ? (c >= '0' && c <= '9') : static_cast<unsigned char>(c) >= 0x20;
        if (ok && s->size() < field_limit(*focus)) {
            s->push_back(c);
        }
    }
}

std::vector<MultiplayerEvent> MultiplayerMenu::key(const MultiplayerModel& model, int key) {
    std::vector<MultiplayerEvent> out;
    const auto focus = m_form.focus();
    std::string* s = focus ? field_text(*focus) : nullptr;
    if (s == nullptr) {
        m_form.set_focus(std::nullopt);
        return out;
    }
    switch (key) {
        case 8:
            if (!s->empty()) {
                s->pop_back();
            }
            break;
        case 9: {
            // The next field of the screen.
            constexpr std::array<Id, 8> kOrder = {kName,     kAddress, kJoinPassword, kTitle,
                                                  kPassword, kSeed,    kPort,         kChat};
            const auto it = std::find(kOrder.begin(), kOrder.end(), *focus);
            for (std::size_t n = 1; n <= kOrder.size(); ++n) {
                const Id next = kOrder[(static_cast<std::size_t>(it - kOrder.begin()) + n) % kOrder.size()];
                if (field_text(next) != nullptr) {
                    m_form.set_focus(next);
                    break;
                }
            }
            break;
        }
        case 13:
            if (*focus == kChat) {
                if (!m_chat.empty()) {
                    MultiplayerEvent e;
                    e.kind = MultiplayerEvent::Kind::Say;
                    e.text = std::exchange(m_chat, {});
                    out.push_back(std::move(e));
                }
                break;  // the chat line stays open
            }
            if (*focus == kName) {
                MultiplayerEvent e;
                e.kind = MultiplayerEvent::Kind::Name;
                e.text = m_name;
                out.push_back(std::move(e));
            } else if (*focus == kAddress || *focus == kJoinPassword) {
                if (auto e = join_event(m_address)) {
                    out.push_back(std::move(*e));
                }
            }
            m_form.set_focus(std::nullopt);
            break;
        case 27:
            m_form.set_focus(std::nullopt);
            break;
        default:
            break;
    }
    if (focus == kName && key != 13 && m_form.focus() != kName) {
        // Left the name field another way: the name counts all the same.
        MultiplayerEvent e;
        e.kind = MultiplayerEvent::Kind::Name;
        e.text = m_name;
        out.push_back(std::move(e));
    }
    (void)model;
    return out;
}

}  // namespace o1602::app
