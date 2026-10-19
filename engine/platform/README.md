# engine/platform

Platform layer on SDL3: window, renderer, input events and timers (`window.hpp`) and the sound
device (`audio_output.hpp`). The only place with operating system dependencies
([ADR 0005](../../docs/architecture/0005-module-structure.md)). Everything is RAII; SDL types never
reach `sim` or `assets`. Built only with `-DO1602_BUILD_GAME=ON`.

When the render device is lost (`SDL_EVENT_RENDER_DEVICE_LOST`, `Window::render_reset`), the window
makes its renderer again (`Window::recreate_renderer`). Everything that holds textures of the old
renderer destroys them first and then takes the new one: the game (`MapViewer::recreate_renderer`)
drops the world's sprite renderers and loads them again, and gives the new renderer to the HUD (its
`SdlUiRenderer`) and the video player. A device that was only reset (`SDL_EVENT_RENDER_DEVICE_RESET`)
keeps the renderer; the textures are made again.

The SDL3 drawing backend lives in `engine/render/sdl` (its own target `o1602::render_sdl`), so that
tools and tests work without SDL.
