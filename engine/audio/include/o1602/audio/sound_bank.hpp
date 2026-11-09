// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound files of the game directory, decoded once and kept (SAMPLES are small; music is decoded
// when its track starts and not kept).
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "annokit/audio/wav.hpp"

#include "o1602/assets/game_directory.hpp"

namespace o1602::audio {

class SoundBank {
public:
    explicit SoundBank(const assets::GameDirectory& game) : m_game(&game) {}

    /// The sound at a path relative to the game directory (case does not matter), decoded and kept;
    /// nullptr if it is missing or cannot be decoded (once reported in `last_error`).
    std::shared_ptr<const annokit::audio::Pcm> get(std::string_view relative);
    /// Decodes a file without keeping it (music).
    [[nodiscard]] std::shared_ptr<const annokit::audio::Pcm> load(std::string_view relative) const;
    /// Where a sound is (case does not matter); nothing if it is missing (reported in `last_error`).
    [[nodiscard]] std::optional<std::filesystem::path> find(std::string_view relative) const;
    /// Reads and decodes a file; touches no bank, so another thread may call it.
    [[nodiscard]] static annokit::core::Result<annokit::audio::Pcm> decode_file(const std::filesystem::path& path);
    [[nodiscard]] const std::string& last_error() const noexcept { return m_error; }

private:
    const assets::GameDirectory* m_game;
    std::unordered_map<std::string, std::shared_ptr<const annokit::audio::Pcm>> m_cache;
    mutable std::string m_error;
};

}  // namespace o1602::audio
