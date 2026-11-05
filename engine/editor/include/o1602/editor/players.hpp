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
    /// Its faction (sim::World::factions()): the settlers' or a package's of the same behaviour.
    sim::FactionId faction = sim::faction::kSettlers;
    friend bool operator==(const PlayerValues&, const PlayerValues&) = default;
};

/// The slots the editor lists: the humans and computers, in the order of the slots (0 to 3, then 7
/// on); the trader, the pirates and the natives not.
[[nodiscard]] std::vector<sim::PlayerId> player_slots(const sim::World& world);

/// The banners a player can have: the four of the original and the further players' (as the
/// colour screen offers them), in that order.
[[nodiscard]] std::vector<sim::BannerId> player_banners();

/// The factions a human's or computer's slot can belong to: those of the world that act as settlers
/// (the base game's settlers and packages' factions of that behaviour), in the world's order.
[[nodiscard]] std::vector<sim::FactionId> slot_factions(const sim::World& world);

/// The values of `player`'s slot.
[[nodiscard]] PlayerValues player_values(const sim::Player& player);

/// The slot `player` with `values`. A banner another player has goes to it in exchange for the old
/// one, as on the colour screen. Fails (NotFound) for no human or computer, (InvalidFormat) for a
/// name a player could not give, a role that is neither, a banner no player can have, money out
/// of range or a faction not of slot_factions() (other than the one it has).
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_player(const sim::World& world, sim::PlayerId player,
                                                                  const PlayerValues& values);

/// A new computer player in the next slot (sim::player_slot), with the first free banner, the money
/// of the first computer and nothing else: without a city or a ship it is out of the game (defeated)
/// as soon as it starts, unless the scenario gives it one; at peace with everyone. The relations
/// of the others stay. Fails (LimitExceeded) with sim::kMaxPlayers players.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_player(const sim::World& world);

/// Takes the last player slot away (the highest of player_slots()), and the empty slots before it
/// that only held its place. Fails (Unsupported) for the first four slots, which every scenario
/// has, and (Unsupported) while the player owns a city, ship or soldier.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> remove_last_player(const sim::World& world);

/// The slot the neutral buildings' player takes (sim::FactionBehaviour::Neutral): behind the slots
/// of every human and computer, so that adding and taking them away leaves it where it is.
inline constexpr sim::PlayerId kNeutralSlot = static_cast<sim::PlayerId>(sim::kMaxPlayerSlots);
/// The banner the neutral buildings show: the natives', without a flag.
inline constexpr sim::BannerId kNeutralBanner = 5;

/// The neutral buildings' player of the world, kNoPlayer if it has none yet.
[[nodiscard]] sim::PlayerId neutral_player(const sim::World& world) noexcept;

/// The neutral buildings' player added to `world` if it has none (in kNeutralSlot, the slots before
/// it held by absent players; the faction anno1602:neutral added if the world's factions lack it),
/// at war with the humans and computers, whose soldiers take its buildings, and at peace with the
/// rest. The relations of the others stay. Its id.
sim::PlayerId add_neutral_player(sim::World& world);

/// add_neutral_player() as a step of its own. Fails (Unsupported) if the world has one already.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> add_neutral(const sim::World& world);

/// How two players stand at the start: at peace or at war, with a trade treaty or without.
struct Relations {
    bool peace = true;
    bool trade = false;
    friend bool operator==(const Relations&, const Relations&) = default;
};

/// How `a` and `b` stand (a's side; the editor keeps both sides alike).
[[nodiscard]] Relations relations(const sim::World& world, sim::PlayerId a, sim::PlayerId b);

/// `a` and `b`, both humans or computers, standing as `r` towards each other when the game starts,
/// both sides alike, offers and memories (tribute, trade, damage) cleared. Fails (NotFound) for no
/// human or computer and (InvalidFormat) for a player with himself or a trade treaty at war.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_relations(const sim::World& world, sim::PlayerId a,
                                                                     sim::PlayerId b, Relations r);

/// The parties of their own behaviour a scenario sets the start towards: the natives, the pirates
/// and the free trader.
enum class Neutral : std::uint8_t { Natives, Pirates, Trader };

/// The neutral parties the world has (a slot of that behaviour), in the order of Neutral.
[[nodiscard]] std::vector<Neutral> neutral_parties(const sim::World& world);

/// Whether the world has villages of the natives (cities of a slot of their behaviour): only they
/// hold a goodwill.
[[nodiscard]] bool has_native_villages(const sim::World& world);

/// How a human or computer stands at the start towards a neutral party.
struct NeutralRelations {
    /// At peace or at war (the natives, the pirates); the trader is always at peace.
    bool peace = true;
    /// The natives: the player's ships trade at their villages (it needs peace, as in the game: an
    /// attacked village never trades with the attacker again). The pirates: at their nests, at war
    /// too. The trader: he calls at the player's Kontors (sim::scenario_flag::kNoTrader cleared).
    bool trade = true;
    /// The natives: the goodwill of their villages towards the player (sim::City::goodwill), the
    /// same in each; the first village's is given.
    std::uint32_t goodwill = sim::kNativeGoodwillStart;
    /// The pirates: the peace lasts whatever their mood, until the player attacks them
    /// (sim::scenario_flag::kPiratePeace, which the original's files do not hold).
    bool lasting = false;
    friend bool operator==(const NeutralRelations&, const NeutralRelations&) = default;
};

/// How `player` stands towards `party` (towards its first slot, when there are more).
[[nodiscard]] NeutralRelations neutral_relations(const sim::World& world, sim::PlayerId player, Neutral party);

/// `player`, a human or computer, standing as `r` towards every slot of `party` when the game
/// starts, memories (tribute, trade, damage) cleared:
/// - natives: peace and trade on both sides, the goodwill in every village;
/// - pirates: peace on both sides; a peace holds as one the player bought at the start (it fades
///   with time, as in the game), or for good if it is `lasting`; a war forgets the pirates' good
///   memories of the player. The trade is the player's side only: the pirates' ships trade with
///   nobody;
/// - trader: whether he calls at the player's Kontors (a scenario switch of the original).
/// Fails (NotFound) for no human or computer or a party the world does not have, (InvalidFormat)
/// for a trade with the natives at war, a war with the trader, a lasting peace other than with the
/// pirates or at war, or a goodwill above sim::kMaxNativeGoodwill.
[[nodiscard]] core::Result<std::unique_ptr<Operation>> set_neutral_relations(const sim::World& world,
                                                                             sim::PlayerId player, Neutral party,
                                                                             const NeutralRelations& r);

}  // namespace o1602::editor
