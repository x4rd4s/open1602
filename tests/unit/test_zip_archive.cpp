// SPDX-License-Identifier: GPL-3.0-or-later
// Reading zip archives (engine/core/include/o1602/core/zip_archive.hpp).

#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "zip_support.hpp"

#include "o1602/core/compress.hpp"
#include "o1602/core/zip_archive.hpp"

using namespace o1602;

namespace {

std::string text_of(const core::ZipEntry& e) {
    return std::string(e.bytes.begin(), e.bytes.end());
}

}  // namespace

TEST_CASE("zip archive: stored and deflated files, without folders") {
    const bool deflate = core::has_zlib();
    const std::string long_text(5000, 'a');
    const auto archive = test::make_zip({{"mod.toml", "version = \"1\"", false},
                                         {"gfx/", "", false},
                                         {"gfx/tower.txt", long_text, deflate},
                                         {"empty.txt", "", false}});
    const auto entries = core::read_zip(archive);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 3);
    CHECK((*entries)[0].path == "mod.toml");
    CHECK(text_of((*entries)[0]) == "version = \"1\"");
    CHECK((*entries)[1].path == "gfx/tower.txt");
    CHECK(text_of((*entries)[1]) == long_text);
    CHECK((*entries)[2].path == "empty.txt");
    CHECK((*entries)[2].bytes.empty());
    CHECK(core::read_zip(test::make_zip({}))->empty());
}

TEST_CASE("zip archive: damaged, cut off and too large archives are refused") {
    const auto archive = test::make_zip({{"a.txt", "hello", false}, {"b.txt", "world", false}});
    // Cut off: the end of the central directory is gone.
    CHECK(!core::read_zip(std::span(archive).first(archive.size() - 5)).ok());
    CHECK(!core::read_zip(std::vector<std::uint8_t>{1, 2, 3}).ok());
    // A changed byte of a file: its checksum no longer matches.
    auto changed = archive;
    changed[30 + 5] ^= 0x01;  // the first byte of a.txt's data
    const auto damaged = core::read_zip(changed);
    REQUIRE(!damaged.ok());
    CHECK(damaged.error().message.find("checksum") != std::string::npos);
    // Encrypted.
    auto encrypted = archive;
    const std::size_t central = archive.size() - 22 - 2 * (46 + 5);
    encrypted[central + 8] |= 0x01;
    CHECK(core::read_zip(encrypted).error().code == core::ErrorCode::Unsupported);
    // Beyond the limits.
    CHECK(core::read_zip(archive, core::ZipLimits{1, 1000}).error().code == core::ErrorCode::LimitExceeded);
    CHECK(core::read_zip(archive, core::ZipLimits{10, 9}).error().code == core::ErrorCode::LimitExceeded);
    CHECK(core::read_zip(archive, core::ZipLimits{2, 10}).ok());
}

TEST_CASE("zip archive: a comment after the central directory") {
    auto archive = test::make_zip({{"a.txt", "hello", false}});
    const std::string comment = "made by a zip tool PK\x05\x06 with a signature inside";
    archive[archive.size() - 2] = static_cast<std::uint8_t>(comment.size());
    archive.insert(archive.end(), comment.begin(), comment.end());
    const auto entries = core::read_zip(archive);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 1);
    CHECK(text_of((*entries)[0]) == "hello");
}
