# Tests

All tests are in one executable, `o1602_tests`, built with every preset (no SDL needed). It uses a
small framework of its own ([test_framework.hpp](test_framework.hpp)).

- `unit/`: unit tests. They need no original game data and run everywhere, including CI.
- `data/`: tests against the original game data and real savegames. Their names start with `data:`.
  They skip themselves when the data is missing.

## Running the tests

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

ctest knows these tests:

| ctest test | Runs | Command |
|---|---|---|
| `unit` | every test whose name does not start with `data:` | `o1602_tests --no-data` |
| `data` | only the `data:` tests (time limit one hour: some play whole games) | `o1602_tests --only-data` |
| `tool:*` | smoke tests of `o1602-tool` (built with the tools): its commands on small synthetic files | see below |

The executable returns 0 when everything selected passed, 1 when a test failed or nothing matched the
filter, and 77 when every selected test skipped. ctest reports exit code 77 as "Skipped", so without
the original data the `data` test shows as skipped rather than passed.

The executable can also be started directly. Any argument that is not `--no-data` or `--only-data`
is a filter: only tests whose name contains that text run.

```sh
./build/debug/tests/o1602_tests                       # everything
./build/debug/tests/o1602_tests --no-data             # unit tests only
./build/debug/tests/o1602_tests "save game"           # tests with "save game" in their name
./build/debug/tests/o1602_tests --only-data savegame   # data tests about savegames
```

Each test prints `[  OK  ]`, `[ FAIL ]` or `[ SKIP ]` with the reason for a skip, and a summary at the end.

## Smoke tests of o1602-tool

`tool:files` runs `o1602_tool_files` ([tool/write_tool_files.cpp](tool/write_tool_files.cpp)), which
writes small synthetic files into `tests/tool-files/` of the build folder: a savegame-like chunk file
with one island and one player, a `.cod` script, a sprite sheet, a palette, a truncated chunk file
and a folder that is no installation. The other `tool:` tests run `o1602-tool` on them (`chunks`,
`islands`, `state`, `cod`, `cod-dump`, `bsh-info`, `bsh-export`, the usage text, and the errors for
a missing file, a truncated file and a folder that is no installation) through
[tool/expect.cmake](tool/expect.cmake), which checks the exit code and the output together. They
need no original data.

```sh
ctest --preset debug -R tool:
```

## Tests against the original data

Set `ANNO1602_DIR` to the folder of an installation of *Anno 1602* (see the
[README](../README.md#original-game-data)). The tests read it and never write into it. Without the
variable every data test skips. With it, a test still skips (and says why) when what it needs is not
there, for example:

- scenarios such as `Szenes/Das Atoll.szs`, `Szenes/Endlosspiel00.szs` or `Szenes/Am Ende allein.szm`;
- savegames of the original in `SAVEGAME/` (`game00.gam` and others), and `Game.dat`, which only
  exists after the game has been started once;
- the videos in `VideoSmk/` and the interface files.

Several tests check what particular savegames contain. Those savegames come from a maintainer's
installation and are listed with size and CRC-32 in [data/fixtures.hpp](data/fixtures.hpp); a test
uses one only when the file in that slot matches, and otherwise skips it (or the check that needs
it). Tests that only load, save and compare work with whatever savegames an installation has.

| Fixture | What the tests expect of it |
|---|---|
| `SAVEGAME/game00.gam` | Late game: seven free traders, trade routes, soldiers, towers and castles |
| `SAVEGAME/game03.gam` | Game with busy trade at sea |
| `SAVEGAME/game06.gam`, `game07.gam` | Endless games saved about 8 s after the start ("Durchschnittlich", player Merlin, yellow) |
| `SAVEGAME/game10.gam` | Endless game "Endlos_Laufend" with explored islands and known deposits |

To add a fixture, put its path, size and CRC-32 (for example `python3 -c 'import zlib,sys; print(hex(zlib.crc32(open(sys.argv[1],"rb").read())))' file`)
into `kFixtures` and load it with `test::fixture()` or `test::fixture_or_skip()`.

## Environment variables

| Variable | Effect |
|---|---|
| `ANNO1602_DIR` | Folder of the original installation; enables the data tests |
| `O1602_REPLAY` | `earlier.gam,later.gam` (paths in the installation, for example `SAVEGAME/game08.gam,SAVEGAME/game09.gam`): the data test "a savegame played on in the original replays tile for tile" plays the first savegame for the game time between the two and compares the result with the second. Without it that test skips. |
| `O1602_FUZZ_ITERATIONS` | Number of mutated savegames the save fuzz test tries (default 400); raise it for longer runs |
| `O1602_FUZZ_SEED` | First seed of the save fuzz test (default 1); a failure prints the seed that caused it |
| `O1602_AI_LOG` | When set, the computer players log their decisions to stderr (also in the game) |

## Before a pull request

CI checks the formatting with clang-format 18 (the version of Ubuntu 24.04) and fails on any
difference. Format the files you changed before you push:

```sh
clang-format -i <changed .cpp and .hpp files>
# or check everything as CI does:
find engine tools tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 | xargs -0 clang-format --dry-run --Werror
```

Then build and run the tests as above; a build with the `ci` preset treats warnings as errors, as CI does.
