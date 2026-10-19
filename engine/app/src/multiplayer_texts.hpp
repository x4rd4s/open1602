// SPDX-License-Identifier: GPL-3.0-or-later
//
// The texts of the multiplayer menu and messages, which the original's Text.cod does not have: keys
// "net.*" of Open1602's catalogues (data/locale), in the language of the installation
// (ui/localization.hpp). mp_text() gives them in the fonts' Windows-1252.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "o1602/net/protocol.hpp"
#include "o1602/net/session.hpp"

namespace o1602::app {

enum class MpText : std::uint16_t {
    Multiplayer,
    YourName,
    Games,
    ColGame,
    ColHost,
    ColPlayers,
    ColState,
    ColPing,
    StateLobby,
    StateRunning,
    StateRunningJoin,
    StateRunningWatch,
    StateOtherVersion,
    NoGamesFound,
    SearchHint,
    ConnectDirectly,
    HostAddress,
    Password,
    Connect,
    CreateGame,
    HostHint,
    Back,
    Refresh,
    Join,
    CreateTitle,
    NewEndless,
    Scenario,
    Savegame,
    Level,
    WorldNumber,
    Roll,
    Players,
    PlayerCount,
    EndlessHint,
    ScenarioHint,
    SavegameHint,
    NoSavegames,
    Session,
    GameTitle,
    PasswordOptional,
    ShowOnLan,
    AllowJoinRunning,
    AllowSpectators,
    HostOnly,
    Port,
    OpenLobby,
    DefaultTitle,
    EndlessGame,
    LevelN,
    Watchers,
    Nobody,
    SeatOpen,
    SeatComputer,
    SeatClosed,
    Ready,
    Choosing,
    Free,
    Take,
    Kick,
    You,
    Host,
    Chat,
    ChatHint,
    Leave,
    Start,
    StartHint,
    WaitHostStart,
    Watch,
    Yes,
    No,
    Connecting,
    PlayerJoined,
    PlayerLeft,
    Lagging,
    Desync,
    Waiting,
    HostPort,
    CannotHost,
    CannotConnect,
    StartFailed,
    HostLeftSaved,
};

/// The catalogue key of a text ("net.player_joined").
[[nodiscard]] std::string_view mp_key(MpText id) noexcept;
[[nodiscard]] std::string mp_text(MpText id);
/// The text with its "{1}" replaced by `value` (a name, a number).
[[nodiscard]] std::string mp_text(MpText id, std::string_view value);
/// Why a session ended or a computer was refused, for the player.
[[nodiscard]] std::string mp_text(net::EndReason reason);
[[nodiscard]] std::string mp_text(net::RefuseReason reason);

}  // namespace o1602::app
