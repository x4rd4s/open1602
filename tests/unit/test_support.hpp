// SPDX-License-Identifier: GPL-3.0-or-later
// Small builders for synthetic original-format data used by several unit tests.
#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"

namespace o1602::test {

inline void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

inline void put_chunk(std::vector<std::uint8_t>& file, const char* name, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> header(16, 0);
    std::memcpy(header.data(), name, std::strlen(name));
    file.insert(file.end(), header.begin(), header.end());
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}

/// The bytes of a BSH file with solid sprites of the given sizes, all pixels palette index `index`.
inline std::vector<std::uint8_t> sheet_bytes(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes,
                                             std::uint8_t index = 1) {
    std::vector<std::vector<std::uint8_t>> records;
    for (const auto& [w, h] : sizes) {
        std::vector<std::uint8_t> rle;
        for (std::uint32_t y = 0; y < h; ++y) {
            rle.push_back(0);
            rle.push_back(static_cast<std::uint8_t>(w));
            rle.insert(rle.end(), w, index);
            rle.push_back(y + 1 == h ? 0xFF : 0xFE);
        }
        std::vector<std::uint8_t> rec;
        put_u32(rec, w);
        put_u32(rec, h);
        put_u32(rec, 1);
        put_u32(rec, static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3}));
        rec.insert(rec.end(), rle.begin(), rle.end());
        rec.resize((rec.size() + 3) & ~std::size_t{3}, 0);
        records.push_back(std::move(rec));
    }
    std::vector<std::uint8_t> payload;
    auto offset = static_cast<std::uint32_t>(4 * records.size());
    for (const auto& rec : records) {
        put_u32(payload, offset);
        offset += static_cast<std::uint32_t>(rec.size());
    }
    for (const auto& rec : records) {
        payload.insert(payload.end(), rec.begin(), rec.end());
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "BSH", payload);
    return file;
}

/// BSH with solid sprites of the given sizes, all pixels palette index `index`.
inline assets::SpriteSheet make_sheet(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes,
                                      std::uint8_t index = 1) {
    auto chunks = assets::ChunkFile::from_bytes(sheet_bytes(sizes, index));
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    return std::move(sheet).value();
}

/// Palette with colour i = (i, 0, 0).
inline assets::Palette make_palette() {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 256; ++i) {
        payload.insert(payload.end(), {static_cast<std::uint8_t>(i), 0, 0, 0});
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "COL", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    return std::move(assets::Palette::from_chunk_file(*chunks)).value();
}

}  // namespace o1602::test
