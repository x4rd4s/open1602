// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws the interface's draw lists (ui::Panel) with an SDL_Renderer: sprites of the interface's
// sheets, their shapes in one colour, text in the original fonts and colours, filled and
// checkered rectangles. Textures are made when first needed and kept (texts: the 512 most recently
// used, and all of the current frame).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/render/text_cache.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/panel.hpp"
#include "o1602/ui/resources.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace o1602::render {

class SdlUiRenderer {
public:
    /// Loads the interface's sheets and fonts of one art size from the game directory (missing ones
    /// draw nothing).
    SdlUiRenderer(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
                  ui::ArtSize size);
    ~SdlUiRenderer();
    SdlUiRenderer(const SdlUiRenderer&) = delete;
    SdlUiRenderer& operator=(const SdlUiRenderer&) = delete;

    [[nodiscard]] ui::ArtSize art_size() const noexcept { return m_size; }
    /// The three font slots, for panels that wrap text.
    [[nodiscard]] std::array<const ui::Font*, 3> fonts() const noexcept;
    /// Size of a sprite of a sheet at the art's own scale, nothing if there is none.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> sprite_size(std::int32_t block,
                                                                                   std::int32_t gfx) const;

    /// Draws the items with their region's top left at (x, y); `scale` is the region's scale
    /// (screen width / 1024), the art is stretched from its own size to it.
    void draw(const std::vector<ui::DrawItem>& items, std::int32_t x, std::int32_t y, double scale);
    /// A window's art made as wide as `rect`: its left two thirds, copies of its middle third and its
    /// right third at the right edge, so the frame closes there (status bar, mission window).
    void draw_widened(std::int32_t block, std::int32_t gfx, const ui::Rect& rect, double scale);
    /// A rectangle of a palette colour (backgrounds).
    void fill(const ui::Rect& rect, std::uint8_t colour);
    /// An RGBA image as a texture at (x, y), size (w, h) on screen (the minimap). The texture is
    /// kept under `key` and replaced when the image changes size or `update` is set.
    void image(int key, const std::vector<std::uint8_t>& rgba, std::int32_t width, std::int32_t height,
               const ui::Rect& at, bool update);
    /// After a frame is shown: its texts' textures may be given up again when the cache is full.
    void next_frame() noexcept { m_texts.next_frame(); }
    /// The render device was reset and the textures lost their contents: all go and are made
    /// again when next drawn (an image from the pixels given then).
    void release_textures();

private:
    struct Texture {
        SDL_Texture* texture = nullptr;
        std::int32_t w = 0;
        std::int32_t h = 0;
    };
    Texture sprite(std::int32_t block, std::int32_t gfx, int mode, std::int32_t colour);
    Texture text(const std::string& s, std::int32_t font, std::int32_t colour);
    Texture make(const std::vector<std::uint8_t>& rgba, std::int32_t w, std::int32_t h);
    [[nodiscard]] double art_scale() const noexcept;

    SDL_Renderer* m_renderer;
    assets::Palette m_palette;
    ui::ArtSize m_size;
    std::array<std::optional<assets::SpriteSheet>, 5> m_sheets;
    std::array<std::optional<ui::Font>, 3> m_fonts;
    std::array<ui::Recolour, ui::kTextColours> m_colours{};
    std::map<std::tuple<std::int32_t, std::int32_t, int, std::int32_t>, Texture> m_sprites;
    TextCache<Texture> m_texts;
    std::map<int, Texture> m_images;
    Texture m_checker;
};

}  // namespace o1602::render
