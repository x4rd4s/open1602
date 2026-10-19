# 0005 – Module structure and dependency rules

Status: accepted · 2026-10-16

```
            core            Files, errors (Result), bytes, text, compression
              ▲
            assets          Loaders for original formats (no platform/graphics dependency)
              ▲
             sim            Deterministic simulation (world, islands, economy, AI, random numbers …)
              ▲
   net, render, audio, ui   Lockstep multiplayer, draw lists, sound, interface (each on sim)
              ▲
   video (on audio), render_sdl (on render, ui, platform), platform (on core and SDL3)
              ▲
             app            The game executable, links all of them
```

The exact links of every target are listed at the top of `engine/CMakeLists.txt`.

Rules:
- Arrows point to dependencies; cycles are forbidden.
- `sim` **never** depends on `platform`, `render`, `audio`, `ui` (headless capability, determinism).
- Every module is its own CMake library `o1602::<module>` with `include/o1602/<module>/` as its
  public interface; internals live in `src/`.
- Namespace `o1602::<module>`.

All modules exist except `script` (Lua, M8). Each is described in `engine/<module>/README.md`.
