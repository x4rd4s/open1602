// SPDX-License-Identifier: GPL-3.0-or-later
// Sprite sheets (.BSH graphics, .ZEI font glyphs) and palettes (.COL): the chunk container, the
// sprite table, the RLE decoder of every sprite (also widened as font glyphs are), RGBA expansion
// and the font built from a ZEI sheet.
#include <algorithm>
#include <cstdint>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/ui/font.hpp"

using namespace o1602;

namespace {

const assets::Palette& grey_palette() {
    static const assets::Palette palette = [] {
        core::Bytes file(16, 0);
        file[0] = 'C';
        file[1] = 'O';
        file[2] = 'L';
        const std::uint32_t size = 1024;
        for (int i = 0; i < 4; ++i) {
            file.push_back(static_cast<std::uint8_t>(size >> (8 * i)));
        }
        for (int i = 0; i < 256; ++i) {
            file.insert(file.end(),
                        {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i), 0});
        }
        auto chunks = assets::ChunkFile::from_bytes(std::move(file));
        fuzz::require(chunks.ok(), "palette chunk");
        auto p = assets::Palette::from_chunk_file(*chunks);
        fuzz::require(p.ok(), "palette");
        return std::move(p).value();
    }();
    return palette;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto file = assets::ChunkFile::from_bytes(core::Bytes(data, data + size));
    if (!file) {
        return 0;
    }
    for (const auto& chunk : file->chunks()) {
        fuzz::require(chunk.offset <= size && chunk.size <= size - chunk.offset, "chunk inside the file");
    }
    (void)assets::Palette::from_chunk_file(*file);
    auto sheet = assets::SpriteSheet::from_chunk_file(*file);
    if (!sheet) {
        return 0;
    }
    const std::size_t count = std::min<std::size_t>(sheet->size(), 256);
    for (std::size_t i = 0; i < count; ++i) {
        // Sizes come from the file; the decoder must refuse what it cannot hold.
        auto image = sheet->decode(i, (i & 1U) != 0 ? 8 : 0);
        if (image) {
            fuzz::require(image->indices.size() == std::size_t{image->width} * image->height, "indices fit the size");
            fuzz::require(image->opaque.size() == image->indices.size(), "mask fits the size");
            if (image->indices.size() <= (1U << 20)) {
                (void)image->to_rgba(grey_palette());
            }
        }
    }
    auto font = ui::Font::from_sheet(*sheet);
    if (font) {
        (void)font->fix_accents();
    }
    return 0;
}
