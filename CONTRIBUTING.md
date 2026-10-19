# Contributing to Open1602

## Basic legal rules

1. **No original data** in the repository – neither game files nor exports generated from them
   (PNGs, decoded `.cod` texts, memory dumps with game content).
2. **No decompiled code** in `engine/`. Findings are described as a specification in `docs/`
   and reimplemented from it (see ADR 0003).
3. Contributions are licensed under GPL-3.0-or-later. Third-party code only with a compatible licence,
   documented in `third_party/README.md`.

## Code

- C++20, formatting via `.clang-format` (`clang-format -i <files>`), 120 characters. CI checks it with
  clang-format 18 and fails on any difference, so format your changes with that version before a pull
  request ([tests/README.md](tests/README.md#before-a-pull-request)).
- Everything in the repository is in **English**: documents, identifiers and code comments, log and
  error messages, commit messages, PR titles and descriptions. Terms from the original game data
  (`Insel`, `Haus`, `Figur`) stay recognisable. In-game texts follow the language of the installed game;
  new texts go through the localisation ([docs/modding/languages.md](docs/modding/languages.md)).
- Names: types `PascalCase`, functions/variables `snake_case`, members `m_name`, constants `kName`.
- Error handling when loading data via `core::Result<T>` – never crash on broken files.
- No owning raw pointers, no `new`/`delete`; containers and RAII.
- Simulation (`engine/sim`): deterministic, no floating point in the state (ADR 0004).
- Every file starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.

## Reverse engineering

- Enter new function/variable names in `re/symbols/*.csv` with confidence and evidence.
- Document confirmed formats/structures in `docs/formats/` or `docs/re/`.
- Collect bugs of the original you find in `docs/re/bugs.md`.

The symbol tables (`re/symbols/`) and the analysis of the original (`docs/re/`) are not in
this repository (https://github.com/x4rd4s/open1602) but in the private companion repository
`open1602-re`. Links into `docs/re/` from the public documents only work with it in place. Maintainers
set it up next to this folder:

```sh
git clone <url-of-open1602-re> ../open1602-re
ln -s ../../open1602-re/docs/re docs/re
ln -s ../../open1602-re/re/symbols re/symbols
```

Both paths are in `.gitignore`; changes to them are committed in `open1602-re`.

## Commits

- Small, self-contained commits; prefix by area, e.g. `assets: decode ZEI glyph overhang`,
  `re: name Cod_LoadHaeuser`, `docs: describe INSELHAUS bits`.
- Before committing: `cmake --build --preset debug && ctest --preset debug`, and clang-format 18 over
  the changed files. How to run single tests and the tests against the original data:
  [tests/README.md](tests/README.md).
