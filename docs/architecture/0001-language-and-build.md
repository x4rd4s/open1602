# 0001 – C++20, CMake, SDL3

Status: accepted · 2026-10-16

## Context
The engine should run cross-platform (Linux, Windows, macOS), fast and stable over long
sessions. The original is an MSVC 6 C++ program.

## Decision
- **C++20** for the engine and tools; **Python 3** for research/RE scripts.
- **CMake ≥ 3.25** with presets; dependencies from the system on Linux, from Homebrew on macOS and via **vcpkg** on Windows.
- **SDL3** as the only platform layer (window, input, audio, timers, GPU API).
- `assets` and `core` stay free of mandatory external dependencies (zlib optional).

## Rationale
The structures and algorithms of the original map directly onto C++; comparable projects
(OpenRCT2, OpenTTD, OpenMW, Julius/Augustus) prove that the approach is viable. Rust was considered
(memory safety), but rejected because it maps the original structures less well and has a smaller
game development ecosystem.

## Consequences
Memory safety has to be enforced by rules: RAII, no owning raw pointers,
bounds-checked parsing (`ByteReader`, `Result<T>`), warnings as errors in CI, ASan/UBSan builds.
