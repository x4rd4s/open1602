// SPDX-License-Identifier: GPL-3.0-or-later
//
// All sprites of a sheet decoded once and packed into a few large RGBA pages. The GPU backend
// uploads the pages as textures; the software rasterizer blits from them. Packing is done on the
// CPU without any platform dependency, so it is covered by the tests.
#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/core/result.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/render/sprite_source.hpp"

namespace o1602::render {

struct AtlasRect {
    std::uint16_t page = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;  ///< 0 = sprite missing or empty
    std::uint16_t height = 0;

    [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
};

class SpriteAtlas {
public:
    /// Decodes every sprite and packs them with a shelf packer (tallest first) into square pages
    /// of `page_size` pixels. One pixel of padding keeps linear filtering from bleeding.
    static core::Result<SpriteAtlas> build(const assets::SpriteSheet& sheet, const assets::Palette& palette,
                                           std::uint32_t page_size = 2048);

    /// Several sheets in one atlas (e.g. STADTFLD.BSH and SHIP.BSH), so sprites of all of them can
    /// be drawn in one batch. Sprite i of sheet k has the index sheet_base(k) + i.
    static core::Result<SpriteAtlas> build(std::span<const assets::SpriteSheet* const> sheets,
                                           const assets::Palette& palette, std::uint32_t page_size = 2048);

    /// General form: sprites from any sources (original sheets, reduced sheets, mod images). Stops
    /// with an error once `cancel` is set.
    static core::Result<SpriteAtlas> build(std::span<const SpriteSource* const> sources, std::uint32_t page_size = 2048,
                                           const std::atomic<bool>* cancel = nullptr);

    /// First sprite index of a sheet given to build().
    [[nodiscard]] std::uint32_t sheet_base(std::size_t sheet) const noexcept {
        return sheet < m_sheet_bases.size() ? m_sheet_bases[sheet] : 0;
    }

    [[nodiscard]] std::size_t sprite_count() const noexcept { return m_rects.size(); }
    [[nodiscard]] const AtlasRect& rect(std::uint32_t sprite) const noexcept {
        return sprite < m_rects.size() ? m_rects[sprite] : m_missing;
    }
    [[nodiscard]] const std::vector<RgbaImage>& pages() const noexcept { return m_pages; }
    [[nodiscard]] std::uint32_t page_size() const noexcept { return m_page_size; }
    [[nodiscard]] std::int32_t max_sprite_width() const noexcept { return m_max_width; }
    [[nodiscard]] std::int32_t max_sprite_height() const noexcept { return m_max_height; }

    /// Frees the page pixels (e.g. after uploading them to the GPU); rects and sizes stay valid.
    void drop_pixels() noexcept;

private:
    std::vector<AtlasRect> m_rects;
    std::vector<RgbaImage> m_pages;
    std::vector<std::uint32_t> m_sheet_bases;
    AtlasRect m_missing{};
    std::uint32_t m_page_size = 0;
    std::int32_t m_max_width = 0;
    std::int32_t m_max_height = 0;
};

/// Draws the commands into `target` (RGBA8, already sized), with the target's top left corner at
/// (origin_x, origin_y) in command coordinates. Fully transparent atlas pixels are skipped.
void blit_draw_list(const std::vector<DrawCommand>& commands, const SpriteAtlas& atlas, RgbaImage& target,
                    std::int32_t origin_x, std::int32_t origin_y);

}  // namespace o1602::render
