# engine/render

Isometric rendering following the original's method ([docs/re/rendering.md](../../docs/re/rendering.md)).
Builds draw lists from the simulation state and only reads it. Depends on `sim` and `assets`, not on
SDL, so the tools and tests draw with the software rasteriser.

| Header | Contents |
|---|---|
| `island_renderer.hpp`, `world_renderer.hpp` | Draw lists for an island or a viewport of the world (painter's algorithm), software rasteriser |
| `sprite_atlas.hpp`, `sprite_source.hpp`, `world_graphics.hpp` | Sprite sheets of a zoom level decoded into atlas pages |
| `sprite_hit_mask.hpp` | Visible pixels of a sprite, for pixel-exact selection |
| `camera.hpp` | Which part of the world is on screen |
| `animation_clock.hpp`, `figure_animation.hpp` | Animation timing as in the original |
| `land_figures.hpp`, `walker_figures.hpp`, `ship_trail.hpp` | Figures, carriers and ships as drawn between ticks |
| `ambient.hpp` | Decorative life that does not affect the game (walkers, whales, dolphins, krakens) |
| `banner.hpp`, `flag_tint.hpp`, `unit_tint.hpp` | Player banners, tinted flags and units in any colour |

`sdl/` is the separate target `o1602::render_sdl` (game build only): it draws these draw lists and
the interface's draw lists with an `SDL_Renderer` (`sdl_sprite_renderer.hpp`, `sdl_ui_renderer.hpp`).
