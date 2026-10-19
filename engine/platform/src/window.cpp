// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/platform/window.hpp"

#include <SDL3/SDL.h>

namespace o1602::platform {

core::Error sdl_error(const std::string& what) {
    return core::make_error(core::ErrorCode::PlatformError, what + ": " + SDL_GetError());
}

core::Result<std::unique_ptr<SdlSystem>> SdlSystem::init() {
    SDL_SetAppMetadata("Open1602", "0.1.0", "org.open1602.Open1602");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return sdl_error("SDL_Init");
    }
    return std::unique_ptr<SdlSystem>(new SdlSystem());
}

SdlSystem::~SdlSystem() {
    SDL_Quit();
}

core::Result<std::unique_ptr<Window>> Window::create(const WindowConfig& config) {
    std::unique_ptr<Window> window(new Window());
    window->m_window = SDL_CreateWindow(config.title.c_str(), config.width, config.height,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window->m_window == nullptr) {
        return sdl_error("SDL_CreateWindow");
    }
    window->m_vsync = config.vsync;
    if (auto status = window->make_renderer(); !status) {
        return status.error();
    }
    return window;
}

core::Status Window::make_renderer() {
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (m_renderer == nullptr) {
        return sdl_error("SDL_CreateRenderer");
    }
    if (m_vsync) {
        SDL_SetRenderVSync(m_renderer, 1);  // not fatal if unsupported
    }
    return core::Ok{};
}

core::Status Window::recreate_renderer() {
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    return make_renderer();
}

RenderReset Window::render_reset(const SDL_Event& event) const {
    if (event.type != SDL_EVENT_RENDER_DEVICE_RESET && event.type != SDL_EVENT_RENDER_DEVICE_LOST) {
        return RenderReset::None;
    }
    if (event.render.windowID != SDL_GetWindowID(m_window)) {
        return RenderReset::None;
    }
    return event.type == SDL_EVENT_RENDER_DEVICE_LOST ? RenderReset::Renderer : RenderReset::Textures;
}

Window::~Window() {
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
}

void Window::output_size(std::int32_t& width, std::int32_t& height) const {
    int w = 0;
    int h = 0;
    SDL_GetRenderOutputSize(m_renderer, &w, &h);
    width = w;
    height = h;
}

float Window::pixel_density() const {
    const float density = SDL_GetWindowPixelDensity(m_window);
    return density > 0.0F ? density : 1.0F;
}

float Window::display_scale() const {
    const float scale = SDL_GetWindowDisplayScale(m_window);
    return scale > 0.0F ? scale : pixel_density();
}

void Window::set_title(const std::string& title) {
    SDL_SetWindowTitle(m_window, title.c_str());
}

std::string Window::renderer_name() const {
    const char* name = SDL_GetRendererName(m_renderer);
    return name != nullptr ? name : "unknown";
}

std::int32_t Window::max_texture_size() const {
    const SDL_PropertiesID props = SDL_GetRendererProperties(m_renderer);
    return static_cast<std::int32_t>(SDL_GetNumberProperty(props, SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0));
}

std::uint64_t ticks_ms() {
    return SDL_GetTicks();
}

}  // namespace o1602::platform
