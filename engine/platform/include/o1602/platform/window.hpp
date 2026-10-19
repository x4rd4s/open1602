// SPDX-License-Identifier: GPL-3.0-or-later
//
// Platform layer on SDL3: the only module that talks to the operating system directly
// (ADR 0005). Everything here is RAII; SDL handles never leak into sim or assets.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "o1602/core/result.hpp"

struct SDL_Window;
struct SDL_Renderer;

namespace o1602::platform {

/// Error from the last failed SDL call (SDL_GetError) with some context.
[[nodiscard]] core::Error sdl_error(const std::string& what);

/// SDL_Init / SDL_Quit for the video subsystem. Create exactly one, before any window.
class SdlSystem {
public:
    static core::Result<std::unique_ptr<SdlSystem>> init();
    ~SdlSystem();
    SdlSystem(const SdlSystem&) = delete;
    SdlSystem& operator=(const SdlSystem&) = delete;

private:
    SdlSystem() = default;
};

struct WindowConfig {
    std::string title = "Open1602";
    std::int32_t width = 1280;  ///< in window coordinates (may be scaled on HiDPI screens)
    std::int32_t height = 800;
    bool vsync = true;
};

/// A resizable, HiDPI-aware window with an SDL_Renderer (GPU accelerated where available).
class Window {
public:
    static core::Result<std::unique_ptr<Window>> create(const WindowConfig& config);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] SDL_Window* sdl_window() const noexcept { return m_window; }
    [[nodiscard]] SDL_Renderer* sdl_renderer() const noexcept { return m_renderer; }

    /// Drawable size in physical pixels.
    void output_size(std::int32_t& width, std::int32_t& height) const;
    /// Physical pixels per window coordinate (2 on typical HiDPI screens).
    [[nodiscard]] float pixel_density() const;
    /// The size the desktop wants things drawn at, per window coordinate times the pixel density:
    /// 2 on a HiDPI screen, 1.5 on Windows at 150 % (SDL_GetWindowDisplayScale).
    [[nodiscard]] float display_scale() const;
    void set_title(const std::string& title);
    /// Name of the active render backend (e.g. "vulkan", "opengl", "direct3d11", "software").
    [[nodiscard]] std::string renderer_name() const;
    /// Largest texture the renderer accepts (0 = unknown).
    [[nodiscard]] std::int32_t max_texture_size() const;

private:
    Window() = default;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
};

/// Milliseconds since SDL_Init (monotonic).
[[nodiscard]] std::uint64_t ticks_ms();

}  // namespace o1602::platform
