# Fuzz targets

Coverage-guided fuzzing with libFuzzer (Clang only) of every reader of hostile input: data files
and mods, savegames, network messages, commands, the user's settings files and media.

| Target | Reads |
|---|---|
| `fuzz_cod` | `.cod` files into `BuildingTable` / `FigureTable` (first byte: bit 0 figuren, bit 1 encoded file as shipped) |
| `fuzz_gad` | `GADDATA/*.GAD` with includes (layout, NUL, the text every include returns) |
| `fuzz_sprites` | chunk files, `.BSH`/`.ZEI` sprite sheets, every sprite decoded, `.COL`, fonts |
| `fuzz_chunks` | chunk files: islands and their maps, missions, traders, players, ships, world plan, `.GAM` game state |
| `fuzz_gam_world` | `.GAM`/`.SZS` loaded as a world, validated, run four ticks, saved and loaded as `.o1602` |
| `fuzz_o1602_save` | `.o1602` savegames (the header is resealed so changes reach the sections), validated, saved, loaded, run |
| `fuzz_command` | the command codec: decode, and the encoding of a decoded command is the input |
| `fuzz_command_apply` | a stream of decoded commands given to a running game (submit or execute), then validated |
| `fuzz_net_message` | network messages (round trip), LAN discovery packets, the lobby's setup blob |
| `fuzz_settings` | `settings.cfg` (write/read round trip), `slots.cfg`, `keys.cfg`, `.lang`, `.hss`, `Game.dat`, addresses |
| `fuzz_media` | Smacker videos (all frames, RGBA, audio) and WAVE files (first byte even / odd) |

`o1602_mod_limits` is no fuzzer: it prints how the loaders treat the values of
`docs/modding/data-limits.md` at and beyond their limits.

## Build and run

    cmake -S . -B build/fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g" \
          -DCMAKE_CXX_FLAGS=-D_GLIBCXX_ASSERTIONS "-DO1602_SANITIZE=address;undefined" -DO1602_FUZZ=ON \
          -DO1602_BUILD_TESTS=OFF -DO1602_BUILD_TOOLS=OFF
    cmake --build build/fuzz
    tools/fuzz/run_fuzzers.sh build/fuzz 60 fuzz-work            # every target for 60 s
    FUZZ_KEEP_GOING=1 tools/fuzz/run_fuzzers.sh build/fuzz 1800 fuzz-work cod   # one, long

Findings land in `fuzz-work/artifacts/<target>/`; reproduce one with
`build/fuzz/tests/fuzz/fuzz_<target> <file>`.

## Seeds from the original data

The seeds are synthetic. With an installation, add its files to the corpus before a run (the
first byte of `fuzz_cod` and `fuzz_media` picks the mode):

    tools/fuzz/add_original_seeds.sh "$ANNO1602_DIR" fuzz-work

copies `haeuser.cod`/`figuren.cod` (with the mode byte), the `.GAD` layouts, a selection of
`.BSH`/`.ZEI`/`.COL`, island templates, scenarios, savegames, `.SMK` and `.WAV` files into the
corpus. Do not share that corpus: it holds the game's data.
