# engine/platform

Platform layer on SDL3: window, renderer, input events and timers (`window.hpp`) and the sound
device (`audio_output.hpp`). The only place with operating system dependencies
([ADR 0005](../../docs/architecture/0005-module-structure.md)). Everything is RAII; SDL types never
reach `sim` or `assets`. Built only with `-DO1602_BUILD_GAME=ON`.

The SDL3 drawing backend lives in `engine/render/sdl` (its own target `o1602::render_sdl`), so that
tools and tests work without SDL.
