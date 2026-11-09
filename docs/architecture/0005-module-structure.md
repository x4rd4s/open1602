# 0005 – Module structure and dependency rules

Status: accepted · 2026-10-16

```
   annokit::core            Files, errors (Result), bytes, text, compression, PCG32 (annokit)
              ▲
            assets          Loaders for original formats (no platform/graphics dependency)
              ▲
             sim            Deterministic simulation (world, islands, economy, AI, random numbers …)
              ▲
   net, render, audio, ui   Lockstep multiplayer, draw lists, sound, interface (each on sim)
              ▲
   annokit::video           Smacker, Bink 2 and WebM films (annokit)
   editor (on sim, assets)  Scenario documents: editing steps with undo and redo, checks
   menus (on audio, ui,     The game's parts that need no platform: menus, settings files, build
          net, render)      tool, command sink, event presenter, video queue, map gestures
                            (sources in engine/app/src, tested without SDL)
   annokit::platform        Window, renderer, audio output on SDL3 (annokit)
   render_sdl (on render,   Draws sprites and panels with SDL3
          ui, platform)
              ▲
             app            The game executable, links all of them
```

The exact links of every target are listed at the top of `engine/CMakeLists.txt`. Only the game build
(`O1602_BUILD_GAME`) adds `annokit::platform`, `render_sdl` and `app`.

**Update 2026-11-09: annokit.** The game-independent modules are in their own library,
[annokit](https://github.com/x4rd4s/annokit), which the top-level `CMakeLists.txt` fetches at a
fixed commit (FetchContent): `annokit::core` (all of the former `core`), `annokit::audio` (WAVE, Ogg
Vorbis and FLAC decoding, the mixer), `annokit::video` (all of the former `video`), `annokit::net`
(the transport interface, ENet, the loopback network), `annokit::script` (the Lua sandbox) and
`annokit::platform` (all of the former `platform`), with the third-party libraries they build.
Open1602's `audio`, `net` and `script` keep what belongs to the game and build on them; the
simulation's generators (`sim::Random`) are annokit's PCG32. annokit names no game and holds no
game data, so that other projects can share it. To change annokit and Open1602 together, point
`FETCHCONTENT_SOURCE_DIR_ANNOKIT` at a local clone; a change of annokit reaches Open1602 by moving
the pinned commit.

Rules:
- Arrows point to dependencies; cycles are forbidden.
- `sim` **never** depends on `platform`, `render`, `audio`, `ui` (headless capability, determinism).
- A module includes only headers of modules it links (directly or through them), and only
  `annokit::platform`, `render_sdl` and `app` include SDL. The ctest `layering` (`cmake/CheckLayering.cmake`)
  checks this on the sources (for `menus` on the sources `engine/app/menus/CMakeLists.txt` lists), since the include directories of all modules are visible to the tests
  and tools through `PUBLIC` links and a stray include would otherwise compile.
- Every module is its own CMake library `o1602::<module>` with `include/o1602/<module>/` as its
  public interface; internals live in `src/`. annokit's are `annokit::<module>` with
  `annokit/<module>/`.
- Namespace `o1602::<module>` (annokit: `annokit::<module>`).

All modules exist except `script` (Lua, M8). Each is described in `engine/<module>/README.md`.
