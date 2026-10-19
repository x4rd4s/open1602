// SPDX-License-Identifier: GPL-3.0-or-later
//
// Players of a game. Unlike the original (7 fixed slots, colours 0..6, owner 7 = nobody) the number
// of players and banners is open (ADR 0007); the original values are translated when loading.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "o1602/assets/scenario.hpp"

namespace o1602::sim {

/// Index into World::players(). 0xFF means "nobody", so a game can have up to 254 players.
using PlayerId = std::uint8_t;
inline constexpr PlayerId kNoPlayer = 0xFF;
/// Most players a world can hold (every PlayerId but kNoPlayer).
inline constexpr std::size_t kMaxPlayerCount = kNoPlayer;

/// Banner of a player: display colour and flag sprites (render::BannerTable). The base game
/// provides the original colours 0..6 as banners 0..6; mods add more.
using BannerId = std::uint16_t;

/// Role of a player slot. Stands for the controller (human, computer) and the fixed factions of
/// the original until factions become data (ADR 0007, section 3).
enum class PlayerRole : std::uint8_t {
    Human,
    Computer,
    Trader,  ///< free trader
    Pirates,
    Natives,
};

/// Whether a player is still in the game (the original's controller 9 and 10).
enum class PlayerStatus : std::uint8_t {
    Playing,
    Defeated,  ///< lost: no city and no ship, bankrupt, or a mission's loss condition
    Absent,    ///< nobody plays the slot: it had no city and no ship when the game started
};

/// Index into World::missions().
using MissionId = std::uint8_t;
inline constexpr MissionId kNoMission = 0xFF;

struct Player {
    PlayerRole role = PlayerRole::Human;
    BannerId banner = 0;
    std::int32_t money = 0;
    std::string name;
    /// Unlocked build levels: bit n - 1 = INFRA number n (BuildingTable::infra_levels()). A level
    /// stays unlocked once one city of the player reached it (0x0047F8A0).
    std::uint32_t infra_mask = 0;
    /// Buildings the player may build only once or by allowance (record Bauinfra SCHLOSS,
    /// KATHETRALE, TRIUMPH, DENKMAL).
    std::uint8_t castles_built = 0;
    std::uint8_t cathedrals_built = 0;
    std::uint16_t arches_allowed = 0;
    std::uint16_t arches_built = 0;
    std::uint16_t monuments_allowed = 0;
    std::uint16_t monuments_built = 0;

    /// The player's mission (scenarios; docs/re/missions.md), kNoMission for none.
    MissionId mission = kNoMission;
    PlayerStatus status = PlayerStatus::Playing;
    /// Accomplished the mission (or met it when someone else did); doubles the score.
    bool won = false;
    /// Opponents this player defeated (goal 0x200). The original counts the triumphal arch
    /// allowances (player + 0x8E) instead, which a scenario may preset; loading such a file takes
    /// that value.
    std::uint16_t opponents_defeated = 0;
    /// Fights for the score (PLAYER4 +0x14 .. +0x1A, runtime +0x92 ..): soldiers this player killed
    /// and lost, ships it sank and lost. A sunk ship counts its soldiers aboard as killed and lost
    /// too. Kills count only for humans and computers (the pirates earn nothing).
    std::uint16_t soldiers_killed = 0;
    std::uint16_t soldiers_lost = 0;
    std::uint16_t ships_sunk = 0;
    std::uint16_t ships_lost = 0;
    /// 5-s checks in a row with money below -1000 and no positive balance (40 = bankrupt) and
    /// with no city and no ship in a multiplayer game (40 = defeated). The original keeps one
    /// counter for both and never resets it (S-107).
    std::uint8_t bankrupt_checks = 0;
    std::uint8_t empty_checks = 0;
    /// Trade statistics (player + 0x74, + 0x78): every purchase and sale with a Kontor adds 32
    /// times its value, every 5 s they shrink by 1/180. Divided by 480 they are a moving
    /// average per minute (the trade balance of missions and of the status bar).
    std::int64_t purchases = 0;
    std::int64_t sales = 0;

    [[nodiscard]] bool active() const noexcept { return status == PlayerStatus::Playing; }

    [[nodiscard]] bool unlocked(std::uint32_t infra) const noexcept {
        return infra == 0 || (infra <= 32 && (infra_mask >> (infra - 1) & 1U) != 0);
    }
};

/// Adds to one of a player's counters, which stop at their largest value.
constexpr void add_count(std::uint16_t& counter, std::uint32_t n) noexcept {
    counter = static_cast<std::uint16_t>(std::min<std::uint32_t>(std::uint32_t{counter} + n, 0xFFFF));
}

/// Owner number of the original files (island tile attribute, SHIP4, PLAYER4 slot): 0..6 are the
/// slots of PLAYER4, everything else (7 = nobody) has no owner.
[[nodiscard]] constexpr PlayerId player_from_original(std::uint8_t owner) noexcept {
    return owner < assets::kOriginalPlayerSlots ? owner : kNoPlayer;
}

[[nodiscard]] constexpr PlayerRole role_from_original(assets::PlayerKind kind) noexcept {
    switch (kind) {
        case assets::PlayerKind::Defeated:
        case assets::PlayerKind::Absent:
            break;  // the role is lost; status_from_original() tells the rest
        case assets::PlayerKind::Computer:
            return PlayerRole::Computer;
        case assets::PlayerKind::Trader:
            return PlayerRole::Trader;
        case assets::PlayerKind::Pirate:
            return PlayerRole::Pirates;
        case assets::PlayerKind::Natives:
            return PlayerRole::Natives;
        case assets::PlayerKind::Human:
            break;
    }
    return PlayerRole::Human;
}

[[nodiscard]] constexpr PlayerStatus status_from_original(assets::PlayerKind kind) noexcept {
    return kind == assets::PlayerKind::Defeated ? PlayerStatus::Defeated
           : kind == assets::PlayerKind::Absent ? PlayerStatus::Absent
                                                : PlayerStatus::Playing;
}

/// Money a player gets from or pays for a trade with a Kontor (0x00475AB0, 0x00475A20): it
/// counts in the trade statistics, unlike taxes, costs or tributes.
/// Money after adding `value`, held within the range of the original's 32-bit field.
[[nodiscard]] inline std::int32_t money_plus(std::int32_t money, std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(std::int64_t{money} + value,
                                                              std::numeric_limits<std::int32_t>::min(),
                                                              std::numeric_limits<std::int32_t>::max()));
}
inline void earn_from_trade(Player& p, std::int64_t value) noexcept {
    p.money = money_plus(p.money, value);
    p.sales += 32 * value;
}
inline void pay_for_trade(Player& p, std::int64_t value) noexcept {
    p.money = money_plus(p.money, -value);
    p.purchases += 32 * value;
}

/// The original has seven slots: four players (red, blue, yellow, white), then the free trader,
/// the pirates and the natives. A new world with more players puts the fifth and those after it
/// behind them, with banners of their own from kFirstExtraBanner on (render::BannerTable).
inline constexpr PlayerId kOriginalPlayers = 4;
inline constexpr PlayerId kFirstExtraPlayer = 7;
inline constexpr BannerId kFirstExtraBanner = 7;
/// Players with a start in a new world: as many as there are banner colours that stay apart
/// (render::BannerTable::kDefaultColors).
inline constexpr std::size_t kMaxPlayers = 12;

/// Of the four player colours the original draws some pictures in (coats of arms, unit figures,
/// message icons), the one for a banner: the colours of players five on and of the other
/// banners look like the fourth.
[[nodiscard]] constexpr std::int32_t four_colour_index(BannerId banner) noexcept {
    return banner < kOriginalPlayers ? static_cast<std::int32_t>(banner) : kOriginalPlayers - 1;
}

/// The slot of the n-th player with a start (0-based): 0-3, then 7 on.
[[nodiscard]] constexpr PlayerId player_slot(std::size_t n) noexcept {
    return static_cast<PlayerId>(n < kOriginalPlayers ? n : kFirstExtraPlayer + (n - kOriginalPlayers));
}

/// Original colours map 1:1 to the banners of the base game.
[[nodiscard]] constexpr BannerId banner_from_original(assets::PlayerColor color) noexcept {
    return static_cast<BannerId>(color);
}

[[nodiscard]] const char* role_name(PlayerRole role) noexcept;

}  // namespace o1602::sim
