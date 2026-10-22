// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings of a new game (ADR 0008): the rules of the game and, for an endless game, if wanted,
// a world of its own instead of the original's maps. The start dialog shows them as items with a few
// values each, a left click choosing the next, a right click the previous; settings.cfg keeps the last choice
// ("endless.*" lines for the endless game, "scenario.*" for scenarios, "multiplayer.*" for the games a host sets up).
// Which items a game offers depends on what is played (SetupScope).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "o1602/sim/rules.hpp"
#include "o1602/sim/world_plan.hpp"

namespace o1602::app {

/// What the settings are for: which items the dialog shows and which it lets the player change.
enum class SetupScope : std::uint8_t {
    Endless,             ///< a new endless game alone: every item
    MultiplayerEndless,  ///< a new endless game a host sets up: the lobby decides the players
    Scenario,            ///< a scenario: its world is given, only the rules
    Savegame,            ///< a saved game: its rules, to look at only (the game keeps its own)
};

struct EndlessSetup {
    sim::Rules rules = sim::Rules::open1602();
    bool own_world = false;      ///< a world of its own (`world`) instead of the level's map
    sim::WorldSettings world{};  ///< size, players, islands, villages, volcanoes, gold, starts (seed and level aside)
    /// The size follows the players as in a multiplayer game (sim::settings_for_players): the
    /// original's 500 x 350 for four or fewer, more sea and land for more; `world`'s size is then
    /// not used. The islands follow the area.
    bool auto_size = true;

    /// Open1602's rules on the original's maps. A multiplayer game's own world, if it gets one,
    /// has all starts at one place, as the original's multiplayer maps.
    [[nodiscard]] static EndlessSetup defaults(SetupScope scope = SetupScope::Endless);
    /// The original's rules on the original's maps (starts as for defaults()).
    [[nodiscard]] static EndlessSetup original(SetupScope scope = SetupScope::Endless);
    /// The world to make: `world`, with the size for its players if `auto_size`.
    [[nodiscard]] sim::WorldSettings world_settings() const;

    friend bool operator==(const EndlessSetup& a, const EndlessSetup& b) noexcept;
};

/// The items of the start dialog, in its order.
enum class SetupItem : std::uint8_t {
    // Economy
    Refund,
    TraderPrices,
    // Limits
    Ships,
    Units,
    GameSpeed,
    // Disasters
    Droughts,
    Volcanoes,
    // World
    Map,
    WorldSize,
    Players,
    Islands,
    IslandSizes,
    Villages,
    VolcanoIslands,
    GoldIslands,
    Starts,
    Resources,
    // Opponents
    ClassicAi,
    Pirates,
    // Victory conditions (a page of their own, endless games only; sim::VictoryConditions)
    WinInhabitants,
    WinClass,
    WinClassInhabitants,
    WinMoney,
    WinBalance,
    WinIslands,
    WinEliminate,
    WinScore,
    WinMonument,
    WinTimeLimit,
    WinAll,
    WinComputers,
    Count,
};

/// Whether an item is one of the victory conditions (their page of the dialog).
[[nodiscard]] constexpr bool victory_item(SetupItem item) noexcept {
    return item >= SetupItem::WinInhabitants && item < SetupItem::Count;
}
/// Whether what is played offers victory conditions: endless games (a saved one to look at).
[[nodiscard]] bool victory_offered(SetupScope scope) noexcept;
/// How many victory conditions are on (the time limit counts as one).
[[nodiscard]] std::size_t victory_count(const sim::VictoryConditions& v) noexcept;
/// The victory conditions in a few words: "none", or how many are on.
[[nodiscard]] std::string victory_summary(const sim::VictoryConditions& v);
/// The victory conditions as a sentence for the mission window ("Victory conditions: ..."), with
/// the values as they are (settings.cfg may give others than the dialog); empty with none.
[[nodiscard]] std::string victory_text(const sim::VictoryConditions& v);

inline constexpr std::size_t kSetupItems = static_cast<std::size_t>(SetupItem::Count);

/// Whether the dialog shows the item for what is played: the world's items and the victory
/// conditions only for endless games.
[[nodiscard]] bool setup_item_offered(SetupScope scope, SetupItem item) noexcept;
/// Whether the player can change the item: offered, not a saved game, and not decided elsewhere
/// (the players of a multiplayer game by its lobby; with more than four of them the world is
/// always one of its own).
[[nodiscard]] bool setup_item_open(const EndlessSetup& setup, SetupScope scope, SetupItem item) noexcept;
/// Whether two setups give the same game for what is played (a scenario compares the rules only).
[[nodiscard]] bool same_setup(const EndlessSetup& a, const EndlessSetup& b, SetupScope scope) noexcept;
/// The setup a multiplayer endless game for `players` really gets: more than four always play on a
/// world of their own, with the players of the lobby.
[[nodiscard]] EndlessSetup multiplayer_setup(EndlessSetup setup, std::uint8_t players);

/// How many values an item offers.
[[nodiscard]] std::size_t setup_choices(SetupItem item) noexcept;
/// The value the setup has, or the nearest one an item offers (a setting from settings.cfg or the
/// command line may lie between them).
[[nodiscard]] std::size_t setup_choice(const EndlessSetup& setup, SetupItem item) noexcept;
/// Sets an item to one of its values (an index beyond them is ignored).
void choose(EndlessSetup& setup, SetupItem item, std::size_t choice);
/// The next value, after the last the first.
void choose_next(EndlessSetup& setup, SetupItem item);
/// The previous value, before the first the last.
void choose_previous(EndlessSetup& setup, SetupItem item);
/// Whether the item counts: the world's items only for a world of its own, the level of the
/// residents only with a number of them, how the conditions are combined only with two or more.
[[nodiscard]] bool setup_item_used(const EndlessSetup& setup, SetupItem item) noexcept;

/// The item's name and a value's text in the language of the interface (Windows-1252).
[[nodiscard]] std::string setup_label(SetupItem item);
[[nodiscard]] std::string setup_value(SetupItem item, std::size_t choice);
/// The text of the value the setup has (the automatic size with the size it gives).
[[nodiscard]] std::string setup_value(const EndlessSetup& setup, SetupItem item);

/// One "endless.<key> = value" line of settings.cfg (key without the prefix); false if the key is
/// not one of them. A value it does not know keeps the setting.
bool read_setup_line(EndlessSetup& setup, std::string_view key, std::string_view value);
/// The lines for settings.cfg, each key after `prefix`.
[[nodiscard]] std::string write_setup(const EndlessSetup& setup, std::string_view prefix = "endless.");
/// Reads "<prefix><key> = value" lines, as write_setup writes them, into `setup`; other lines are
/// left out. For the lobby, which passes the host's setup on as such text.
void read_setup(EndlessSetup& setup, std::string_view text, std::string_view prefix = "endless.");

}  // namespace o1602::app
