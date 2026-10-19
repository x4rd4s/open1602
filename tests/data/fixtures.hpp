// SPDX-License-Identifier: GPL-3.0-or-later
// The savegames of the original that data tests expect by content. They come from one maintainer's
// installation (tests/README.md lists them); a savegame slot of another installation holds another
// game, so a test checks size and CRC-32 before it relies on what the file contains.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "../test_framework.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/core/file.hpp"

namespace o1602::test {

struct Fixture {
    std::string_view path;  ///< relative to the installation
    std::uintmax_t size;
    std::uint32_t crc;  ///< CRC-32 of the whole file (zlib's)
};

inline constexpr std::array kFixtures{
    Fixture{"SAVEGAME/game00.gam", 727133, 0xe3b36e1bU}, Fixture{"SAVEGAME/game03.gam", 823120, 0xa37e25dcU},
    Fixture{"SAVEGAME/game06.gam", 364243, 0xf1fe6574U}, Fixture{"SAVEGAME/game07.gam", 362351, 0x7c108894U},
    Fixture{"SAVEGAME/game10.gam", 467219, 0x5ca54d09U},
};

/// The file at `path` in the installation when it is the fixture of that name; nothing when it is
/// missing or another file lies there.
inline std::optional<std::filesystem::path> fixture(const assets::GameDirectory& game, std::string_view path) {
    const Fixture* known = nullptr;
    for (const Fixture& f : kFixtures) {
        if (f.path == path) {
            known = &f;
        }
    }
    if (known == nullptr) {
        report(__FILE__, __LINE__, "path is in kFixtures");
        throw Failure{};
    }
    auto file = game.resolve(path);
    if (!file) {
        return std::nullopt;
    }
    std::error_code error;
    if (std::filesystem::file_size(*file, error) != known->size || error) {
        return std::nullopt;
    }
    auto bytes = core::read_file(*file);
    if (!bytes.ok() || core::crc32(*bytes) != known->crc) {
        return std::nullopt;
    }
    return file;
}

/// fixture(), skipping the test when the installation does not have that fixture.
inline std::filesystem::path fixture_or_skip(const assets::GameDirectory& game, std::string_view path) {
    auto file = fixture(game, path);
    if (!file) {
        SKIP(std::string(path) + " missing or another savegame (see tests/README.md)");
    }
    return std::move(file).value();
}

}  // namespace o1602::test
