// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/sound_bank.hpp"

#include <algorithm>
#include <cctype>

#include "annokit/audio/sound_file.hpp"
#include "annokit/core/file.hpp"

namespace o1602::audio {

std::shared_ptr<const annokit::audio::Pcm> SoundBank::get(std::string_view relative) {
    std::string key(relative);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return c == '\\' ? '/' : static_cast<char>(std::tolower(c)); });
    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second;
    }
    auto sound = load(relative);
    m_cache.emplace(std::move(key), sound);  // a missing file is remembered too
    return sound;
}

std::optional<std::filesystem::path> SoundBank::find(std::string_view relative) const {
    auto path = m_game->resolve(relative);
    if (!path) {
        m_error = std::string(relative) + ": not found";
    }
    return path;
}

annokit::core::Result<annokit::audio::Pcm> SoundBank::decode_file(const std::filesystem::path& path) {
    auto bytes = annokit::core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    auto pcm = annokit::audio::decode_sound(*bytes);
    if (!pcm) {
        return annokit::core::make_error(pcm.error().code,
                                         annokit::core::path_to_utf8(path) + ": " + pcm.error().message);
    }
    return pcm;
}

std::shared_ptr<const annokit::audio::Pcm> SoundBank::load(std::string_view relative) const {
    const auto path = find(relative);
    if (!path) {
        return nullptr;
    }
    auto pcm = decode_file(*path);
    if (!pcm) {
        m_error = pcm.error().message;
        return nullptr;
    }
    return std::make_shared<const annokit::audio::Pcm>(std::move(pcm).value());
}

}  // namespace o1602::audio
