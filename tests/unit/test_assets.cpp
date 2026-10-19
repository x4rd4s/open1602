// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the asset loaders using small synthetic files (no original data needed).

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

using namespace o1602;

namespace {

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void put_chunk(std::vector<std::uint8_t>& out, const char* name, const std::vector<std::uint8_t>& payload) {
    std::uint8_t header[16] = {};
    std::memcpy(header, name, std::strlen(name));
    out.insert(out.end(), header, header + 16);
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace

TEST_CASE("chunk file: parses names, offsets and sizes") {
    std::vector<std::uint8_t> file;
    put_chunk(file, "FIRST", {1, 2, 3});
    put_chunk(file, "SECOND_LONG_NAME", {});
    put_chunk(file, "THIRD", {9});

    auto parsed = assets::ChunkFile::from_bytes(file);
    REQUIRE(parsed.ok());
    REQUIRE(parsed->chunks().size() == 3);
    CHECK(parsed->chunks()[0].name == "FIRST");
    CHECK(parsed->chunks()[0].offset == 20);
    CHECK(parsed->chunks()[0].size == 3);
    CHECK(parsed->chunks()[1].name == "SECOND_LONG_NAME");  // all 16 bytes used, no terminator
    CHECK(parsed->chunks()[1].size == 0);
    REQUIRE(parsed->find("THIRD") != nullptr);
    CHECK(parsed->payload(*parsed->find("THIRD"))[0] == 9);
    CHECK(parsed->find("MISSING") == nullptr);
}

TEST_CASE("chunk file: rejects truncated data") {
    std::vector<std::uint8_t> file;
    put_chunk(file, "DATA", {1, 2, 3, 4});
    file.resize(file.size() - 1);
    CHECK(!assets::ChunkFile::from_bytes(file).ok());

    std::vector<std::uint8_t> header_only(10, 'A');
    CHECK(!assets::ChunkFile::from_bytes(header_only).ok());
}

TEST_CASE("cod: decode is byte-wise negation and roundtrips") {
    const std::string plain = "  IDBODEN =   20101\r\n";
    const auto encoded = assets::encode_cod(plain);
    CHECK(encoded[0] == 0xE0);  // -0x20
    CHECK(assets::decode_cod(encoded) == plain);
    // Real-world sample: first bytes of haeuser.cod (f3 f6 c5 c3) decode to "\r\n;="
    const std::vector<std::uint8_t> sample = {0xF3, 0xF6, 0xC5, 0xC3};
    CHECK(assets::decode_cod(sample) == "\r\n;=");
}

TEST_CASE("text: cp1252 to utf8") {
    CHECK(core::cp1252_to_utf8("T\xfcrme") == "T\xc3\xbcrme");  // ue
    CHECK(core::cp1252_to_utf8("\x80") == "\xe2\x82\xac");      // euro sign
    CHECK(core::cp1252_to_utf8("plain") == "plain");
}

TEST_CASE("palette: reads 256 RGBX entries") {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 256; ++i) {
        payload.insert(payload.end(), {static_cast<std::uint8_t>(i), 10, 20, 0});
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "COL", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto palette = assets::Palette::from_chunk_file(*chunks);
    REQUIRE(palette.ok());
    CHECK((*palette)[7] == (assets::Rgba{7, 10, 20, 255}));
    CHECK((*palette)[255].r == 255);
}

namespace {

/// Builds a BSH file with one 4x2 sprite:
///   row 0: ..AB   (skip 2, run 2)
///   row 1: C...   (skip 0, run 1)
std::vector<std::uint8_t> make_test_bsh() {
    std::vector<std::uint8_t> rle = {2, 2, 0xA, 0xB, 0xFE, 0, 1, 0xC, 0xFF};
    std::vector<std::uint8_t> payload;
    put_u32(payload, 4);  // offset table: one entry, record starts at 4
    put_u32(payload, 4);  // width
    put_u32(payload, 2);  // height
    put_u32(payload, 1);  // type
    const auto length = static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3});
    put_u32(payload, length);
    payload.insert(payload.end(), rle.begin(), rle.end());
    payload.resize(4 + length, 0);
    std::vector<std::uint8_t> file;
    put_chunk(file, "BSH", payload);
    return file;
}

}  // namespace

TEST_CASE("sprite sheet: decodes RLE rows with transparency") {
    auto chunks = assets::ChunkFile::from_bytes(make_test_bsh());
    REQUIRE(chunks.ok());
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    REQUIRE(sheet.ok());
    REQUIRE(sheet->size() == 1);
    CHECK(sheet->info(0).width == 4);
    CHECK(sheet->info(0).height == 2);

    auto image = sheet->decode(0);
    REQUIRE(image.ok());
    const std::vector<std::uint8_t> expected_mask = {0, 0, 1, 1, 1, 0, 0, 0};
    CHECK(image->opaque == expected_mask);
    CHECK(image->indices[2] == 0xA);
    CHECK(image->indices[3] == 0xB);
    CHECK(image->indices[4] == 0xC);
    CHECK(image->clipped_pixels == 0);
    CHECK(!sheet->decode(1).ok());
}

TEST_CASE("sprite sheet: unterminated RLE is an error, not a crash") {
    auto data = make_test_bsh();
    // Replace the end marker (0xFF) and padding with row-continuation bytes.
    for (std::size_t i = 20 + 4 + 16 + 8; i < data.size(); ++i) {
        data[i] = 0xFE;
    }
    auto chunks = assets::ChunkFile::from_bytes(data);
    REQUIRE(chunks.ok());
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    REQUIRE(sheet.ok());
    CHECK(!sheet->decode(0).ok());
}

TEST_CASE("islands: header and INSELHAUS tiles") {
    std::vector<std::uint8_t> header = {19, 70, 60, 0};
    put_u16(header, 90);
    put_u16(header, 50);
    header.resize(116, 0);

    std::vector<std::uint8_t> tiles;
    put_u16(tiles, 1201);
    tiles.push_back(3);
    tiles.push_back(4);
    put_u32(tiles, 0xDEADBEEF);

    std::vector<std::uint8_t> file;
    put_chunk(file, "INSEL5", header);
    put_chunk(file, "INSELHAUS", tiles);
    put_chunk(file, "HIRSCH2", {});

    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto islands = assets::load_islands(*chunks);
    REQUIRE(islands.ok());
    REQUIRE(islands->size() == 1);
    const auto& island = (*islands)[0];
    CHECK(island.header.number == 19);
    CHECK(island.header.width == 70);
    CHECK(island.header.height == 60);
    CHECK(island.header.pos_x == 90);
    CHECK(island.header.pos_y == 50);
    REQUIRE(island.layers.size() == 1);
    REQUIRE(island.layers[0].size() == 1);
    CHECK(island.layers[0][0].cod_id() == 21201);
    CHECK(island.layers[0][0].x == 3);
    CHECK(island.layers[0][0].y == 4);
    CHECK(island.layers[0][0].bits == 0xDEADBEEF);
}

TEST_CASE("file: a full disk is an error, and an atomic write keeps the old file then") {
    namespace fs = std::filesystem;
    if (fs::exists("/dev/full")) {
        // Every write to /dev/full fails with "no space left"; before the file is closed the data
        // may still sit in a buffer.
        const std::vector<std::uint8_t> data(100, 7);
        CHECK(!core::write_file("/dev/full", data).ok());
    }
    const fs::path dir = test::temp_path("o1602_test_atomic");
    std::error_code ec;
    fs::remove_all(dir, ec);
    REQUIRE(fs::create_directories(dir));
    const fs::path file = dir / "game.o1602";
    const std::vector<std::uint8_t> first = {1, 2, 3};
    const std::vector<std::uint8_t> second = {4, 5, 6, 7};
    REQUIRE(core::write_file_atomic(file, first).ok());
    REQUIRE(core::write_file_atomic(file, second).ok());
    CHECK(*core::read_file(file) == second);
    CHECK(!fs::exists(dir / "game.o1602.tmp"));
    // A target that cannot be replaced (a folder of that name): nothing left behind.
    REQUIRE(fs::create_directories(dir / "folder.o1602" / "inside"));
    CHECK(!core::write_file_atomic(dir / "folder.o1602", first).ok());
    CHECK(!fs::exists(dir / "folder.o1602.tmp"));
    fs::remove_all(dir, ec);
}

TEST_CASE("file: paths go to and from UTF-8 unchanged (a user named Juergen)") {
    namespace fs = std::filesystem;
    const std::string name = "J\xC3\xBCrgen";  // "Jürgen" in UTF-8
    CHECK(core::path_to_utf8(core::path_from_utf8(name)) == name);
    const fs::path dir = test::temp_path("o1602_test_utf8");
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path folder = dir / core::path_from_utf8(name);
    REQUIRE(fs::create_directories(folder));
    const std::vector<std::uint8_t> data = {1, 2, 3};
    REQUIRE(core::write_file(folder / "keys.cfg", data).ok());
    CHECK(*core::read_file(folder / "keys.cfg") == data);
    CHECK(core::path_to_utf8(folder.filename()) == name);
    // The case-insensitive lookup compares the names as UTF-8 as well.
    const auto found = core::find_case_insensitive(dir, "j\xC3\xBCrgen/KEYS.CFG");
    REQUIRE(found.has_value());
    CHECK(*core::read_file(*found) == data);
    // Nothing outside the root: no "..", no absolute path.
    CHECK(!core::find_case_insensitive(folder, "../j\xC3\xBCrgen/keys.cfg").has_value());
    CHECK(!core::find_case_insensitive(folder, "..\\j\xC3\xBCrgen\\keys.cfg").has_value());
    CHECK(!core::find_case_insensitive(dir, core::path_to_utf8(folder / "keys.cfg")).has_value());
    // A folder is no file.
    const auto folder_read = core::read_file(folder);
    CHECK(!folder_read.ok());
    fs::remove_all(dir, ec);

    CHECK(core::environment_variable("PATH").has_value());
    CHECK(!core::environment_variable("O1602_TEST_VARIABLE_THAT_IS_NOT_SET").has_value());
}
