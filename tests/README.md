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

ctest knows two tests:

| ctest test | Runs | Command |
|---|---|---|
| `unit` | every test whose name does not start with `data:` | `o1602_tests --no-data` |
| `data` | only the `data:` tests (time limit one hour: some play whole games) | `o1602_tests --only-data` |

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

## Tests against the original data

Set `ANNO1602_DIR` to the folder of an installation of *Anno 1602* (see the
[README](../README.md#original-game-data)). The tests read it and never write into it. Without the
variable every data test skips. With it, a test still skips (and says why) when what it needs is not
there, for example:

- scenarios such as `Szenes/Das Atoll.szs`, `Szenes/Endlosspiel00.szs` or `Szenes/Am Ende allein.szm`;
- savegames of the original in `SAVEGAME/` (`game00.gam` and others), and `Game.dat`, which only
  exists after the game has been started once;
- the videos in `VideoSmk/` and the interface files.

Several tests were written against specific savegames of the maintainers' installation; on another
installation they skip or test what the savegames there contain.

## Environment variables

| Variable | Effect |
|---|---|
| `ANNO1602_DIR` | Folder of the original installation; enables the data tests |
| `O1602_REPLAY` | `earlier.gam,later.gam` (paths in the installation, for example `SAVEGAME/game08.gam,SAVEGAME/game09.gam`): the data test "a savegame played on in the original replays tile for tile" plays the first savegame for the game time between the two and compares the result with the second. Without it that test skips. |
| `O1602_FUZZ_ITERATIONS` | Number of mutated savegames the save fuzz test tries (default 400); raise it for longer runs |
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
