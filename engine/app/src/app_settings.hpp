// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's own settings and savegame names in the user's data folder (never Game.dat, never the
// installation): settings.cfg with the sounds of the panels MUSIK and ANNO, and saves/slots.cfg with
// the names of the savegames of the panel DISK (the original has twelve, Open1602 any number).
// Both files are UTF-8 (a byte order mark is passed over); the names are converted to and from the
// Windows-1252 the game keeps them in. Files of older versions with Windows-1252 bytes still read.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/audio/game_audio.hpp"
#include "o1602/core/text.hpp"

namespace o1602::app {

struct AppSettings {
    audio::AudioSettings audio;
    bool mission_window = false;  ///< "Auftrag ansehen"
    std::string player_name;      ///< the name typed in the menu (Windows-1252)
    /// The language of Open1602's own texts ("de", "en", ...); "auto": the installation's.
    std::string language = "auto";
    /// Whales, dolphins and krakens: on average one per screen of sea every this many seconds
    /// (the original: 5); 0 turns them off. Only the picture, the game does not depend on it.
    std::int32_t sea_creatures_every = 60;
    /// Multiplayer hosts joined by address, the latest first ("name[:port]"); at most kMaxHosts.
    std::vector<std::string> remembered_hosts;
    static constexpr std::size_t kMaxHosts = 8;
};

/// "key = value" lines; unknown keys are passed over, missing ones keep `defaults`.
[[nodiscard]] AppSettings parse_settings(std::string_view text, const AppSettings& defaults);
[[nodiscard]] std::string write_settings(const AppSettings& settings);

/// The savegames of the panel DISK: a name and the number of human players per slot. The original
/// has twelve slots; Open1602 keeps as many as are used plus one free slot at the end, at least
/// twelve, so a new savegame always has a place.
struct SaveSlots {
    static constexpr std::size_t kMinCount = 12;
    static constexpr std::size_t kMaxCount = 10'000;                       ///< a damaged file cannot ask for more
    std::vector<std::string> names = std::vector<std::string>(kMinCount);  ///< Windows-1252, empty = free
    std::vector<std::uint8_t> humans = std::vector<std::uint8_t>(kMinCount);

    [[nodiscard]] std::size_t size() const noexcept { return names.size(); }
    /// Names a slot (beyond the list: the list grows) and keeps one free slot after the last used.
    void set(std::size_t slot, std::string name, std::uint8_t human_players);
};
/// Lines "slot humans name" (slot from 0).
[[nodiscard]] SaveSlots parse_slots(std::string_view text);
[[nodiscard]] std::string write_slots(const SaveSlots& slots);
/// File of a slot in the savegame folder: spiel00.o1602, spiel01.o1602, ..., spiel100.o1602, ...
[[nodiscard]] std::filesystem::path slot_file(const std::filesystem::path& folder, std::size_t slot);

using core::utf8_to_cp1252;

}  // namespace o1602::app
