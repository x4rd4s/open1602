// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers for the tests of the SDL layer: SDL's dummy drivers, and a software renderer drawing into
// a surface whose pixels the tests read back.
#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/palette.hpp"

namespace o1602::test {

/// No window on screen and no sound: SDL's dummy drivers, unless SDL_VIDEO_DRIVER or
/// SDL_AUDIO_DRIVER say otherwise (SDL prefers the environment). SDL_Quit forgets hints, so every
/// test calls this before it starts SDL.
inline void use_dummy_drivers() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
}

/// Pixels read back from a renderer (SDL_RenderReadPixels), as the game's screenshots do.
class Pixels {
public:
    void read(SDL_Renderer* renderer) {
        SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
        REQUIRE(shot != nullptr);
        SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(shot);
        REQUIRE(rgba != nullptr);
        m_width = rgba->w;
        m_pixels.assign(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4, 0);
        for (int y = 0; y < rgba->h; ++y) {
            const auto* row =
                static_cast<const std::uint8_t*>(rgba->pixels) + static_cast<std::ptrdiff_t>(y) * rgba->pitch;
            std::copy_n(row, rgba->w * 4, m_pixels.begin() + static_cast<std::ptrdiff_t>(y) * rgba->w * 4);
        }
        SDL_DestroySurface(rgba);
    }

    /// A pixel of the last read(); its alpha is left out.
    [[nodiscard]] assets::Rgba at(int x, int y) const {
        const auto i =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)) * 4;
        return assets::Rgba{m_pixels[i], m_pixels[i + 1], m_pixels[i + 2], 255};
    }

private:
    int m_width = 0;
    std::vector<std::uint8_t> m_pixels;
};

/// Fills the renderer's whole output with one colour.
inline void clear(SDL_Renderer* renderer, assets::Rgba c) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderClear(renderer);
}

/// An SDL software renderer drawing into an RGBA surface of its own; needs no video driver.
class Canvas {
public:
    Canvas(int width, int height) {
        m_surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(m_surface != nullptr);
        m_renderer = SDL_CreateSoftwareRenderer(m_surface);
        REQUIRE(m_renderer != nullptr);
    }
    ~Canvas() {
        SDL_DestroyRenderer(m_renderer);
        SDL_DestroySurface(m_surface);
    }
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return m_renderer; }

    void clear(assets::Rgba c) { test::clear(m_renderer, c); }
    void read() { m_pixels.read(m_renderer); }
    /// A pixel of the last read(); its alpha is left out.
    [[nodiscard]] assets::Rgba at(int x, int y) const { return m_pixels.at(x, y); }

private:
    SDL_Surface* m_surface = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    Pixels m_pixels;
};

/// Whether two colours differ by at most `tolerance` in every channel (blending rounds).
inline bool near(assets::Rgba a, assets::Rgba b, int tolerance = 2) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}

}  // namespace o1602::test
