// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's players (ADR 0013): the humans' and computers' slots of a scenario, what each is and
// starts with, and slots added and taken away, up to sim::kMaxPlayers. Each change is made on a
// copy of the world and given back as a WorldEdit for undo, like the cities'.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "o1602/core/result.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The longest name a player takes, as a city's.
inline constexpr std::size_t kMaxPlayerName = 40;
/// The most a player can start with, and the least (in debt).
inline constexpr std::int32_t kMaxStartMoney = 9'999'000;
inline constexpr std::int32_t kMinStartMoney = -99'000;
/// The most monuments or triumphal arches a scenario allows a player.
inline constexpr std::uint16_t kMaxAllowed = 99;

/// What a scenario sets of a player's slot.
struct PlayerValues {
    sim::PlayerRole role = sim::PlayerRole::Computer;  ///< Human or Computer
    sim::BannerId banner = 0;
    std::int32_t money = 0;
    std::string name;  ///< Windows-1252; empty: the name of its colour
    std::uint16_t monuments_allowed = 0;
    std::uint16_t arches_allowed = 0;
    friend bool operator==(const PlayerValues&, const PlayerValues&) = default;
};

/// The slots the editor lists: the humans and computers, in the order of the slots (0 to 3, then 7
/// on); the trader, the pirates and the natives not.
[[nodiscard]] std::vector<sim::PlayerId> player_slots(const sim::World& world);

/// The banners a player can have: the four of the original and the further players' (as the
/// colour screen offers them), in that order.
[[nodiscard]] std::vector<sim::BannerId> player_banners();

/// The values of `player`'s slot.
[[nodiscard]] PlayerValues player_values(const sim::Player& player);

/// The slot `player` with `values`. A banner another player has goes to it in exchange for the old
/// one, as on the colour screen. Fails (NotFound) for no human or computer, (InvalidFormat) for a
/// name a player could not give, a role that is neither, a banner no player can have or money out
/// of range.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_player(const sim::World& world, sim::PlayerId player,
                                                                  const PlayerValues& values);

/// A new computer player in the next slot (sim::player_slot), with the first free banner, the money
/// of the first computer and nothing else: without a city or a ship it sits the game out until the
/// scenario gives it one. The diplomacy starts over as in a new game. Fails (LimitExceeded) with
/// sim::kMaxPlayers players.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_player(const sim::World& world);

/// Takes the last player slot away (the highest of player_slots()), and the empty slots before it
/// that only held its place. Fails (Unsupported) for the first four slots, which every scenario
/// has, and (Unsupported) while the player owns a city, ship or soldier.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_last_player(const sim::World& world);

}  // namespace o1602::editor
