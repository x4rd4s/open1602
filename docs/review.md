# Code review, f5a46dd

This review covers the whole repository at commit `f5a46dd` (`origin/main` after PR #107). It replaces the earlier review of `468c58a`, whose 99 findings were all fixed or left on purpose; the earlier items that regressed are listed under "Earlier review items that regressed". The review did not change engine code. Measurement tools were added on separate branches (see "Scope and method").

**Status.** All parts are verified and merged, including part DET (fuzzing, sanitizers, static analysis, mod data) and the device runs with the original game data on the user's machine. Its five fuzz inputs are root-caused. What remains open is listed per finding (unclear marks, runs not done).

The finding list is also available as `findings.csv` (one row per merged finding). Paths starting with `review-2026-10/` refer to the review's working files (part reports, verifications, reproducers and benchmarks), which are kept outside the repository with the maintainer.

## Summary

After verification and merging there are **187 findings**: 3 Critical, 16 High, 45 Medium, 95 Low and 28 Info (4 of them marked unclear). The biggest risks first:

1. **Multiplayer sessions can be taken over or split (3 Critical).** The successor receives every player's reconnect token and can take any player's empire on the running host (MS-1, a regression of MP-1). A successor whose own link breaks starts a second game while the host plays on (MS-2). Any player can stall every computer for 0.1-3 s per frame with legal rectangle demolish orders (MS-3).
2. **Further High session faults:** a rejoining player lands in a wrong slot or none (MS-4, MS-5, MS-6), unauthenticated peers make the host hold 32-64 MiB each (MS-7), and a stale `Standby` seat writes out of bounds on the new host (MS-8).
3. **One false desync, otherwise determinism holds.** A sinking ship comes back at full health after a load or resync, so a resync during a naval fight can mismatch again (DS-1). Eight game hours across GCC, Clang, -O0, sanitizers and FMA gave identical hashes, and with the original data all 19 savegames, long 8- and 12-player replays and a loopback multiplayer game showed no divergence.
4. **The stress targets are not reached in the tick.** Path planning has no budget (TB-1): with the original data on a fast desktop, 1 000 ships give single ticks of 0.64 s and 20 000 ships 12.7 s, and the default rules with 10 000 soldiers and 2 000 ships one tick of 1.25 s. 50 000-100 000 soldiers cost 34-40 ms per tick on that machine (TB-5, 50-90 ms in the synthetic benchmarks); the AI's whole-world scans add 8-16 ms at 100 000 soldiers (TB-9), and `unit_in_the_way` and `Diplomacy::mood` add seconds-long spikes (TB-7, TB-8). Normal 8- and 12-player games stay within budget.
5. **Large worlds cost multiplayer time, but less than the synthetic worlds suggested.** On a real 12-player 2000² late game each fingerprint takes 8-9 ms and each join or resync save about 0.11 s on a fast desktop, roughly 2-3× that on the minimum machine (TB-26, re-judged from High to Medium); the snapshot is 2.2 MiB, far below the 32 MiB limit (MS-11, now Low). The sea scenery adds a 46-218 ms tick every 5 s with 20 000 ships (TB-27).
6. **The frame budget is missed at the targets:** the status line walks every unit twice per frame (FB-1), and soldier figures cost about 50 ms per frame at 100 000 soldiers (FB-2).
7. **Natives' goodwill does not limit players 5-12** in games with more than four players (GB-1).
8. **Limits disagree across modules** (LC-1). Island numbers are 8 bits: the generator silently caps at 250 islands, and a 257-island world saves but does not load (LC-2). Loaded 4096² worlds with mostly land cannot be saved or resynced at all (DS-4).
9. **Hostile local data still crashes or exhausts memory:** `.cod`/`.gad` amplification, confirmed by fuzzing (HI-2), a Smacker audio bomb (HI-4), a crafted save that crashes the AI (HI-1), a `.GAM` with a zero-size island whose every save and multiplayer snapshot fails to load (HI-15), a 1 KB sprite file that asks for 8.5 GB (HI-18), and unchecked ship motion in saves and host snapshots that moves a ship off the map and makes the game unsaveable (HI-19).
10. **Otherwise the robustness checks came back clean:** about 4.6 h of ASan+UBSan fuzzing over 11 targets found nothing in `.o1602` saves, commands, network messages, chunks, media or sprites; all 645 unit tests pass under TSan and UBSan; cppcheck and clang-tidy found no real defect; the 24-game-hour soak with the original data passes with flat memory.
11. **Modding is blocked by structure:** about 20 id tables outside `game_data.hpp` (MD-1), compiled-in classes and ware ranges (MD-2, MD-3), and the AI and build menu (MD-4, MD-5).
12. **The safety net is thin where the code is hottest:** the AI has 36 % line coverage without data (BT-1), and one 90-second golden hash guards cross-platform determinism (DS-2).
13. Cloud measurements ran on a shared, heavily loaded VM and use thread CPU time and minimum-of-runs numbers. The device runs with the original data ran on a fast desktop (Ryzen 9 7900X3D), about 2-3× faster than the minimum machine; severities that depend on absolute time are scaled with that factor.

## Scope and method

**Commit.** `f5a46dd` of `open1602`; the private reverse-engineering notes (`open1602-re`, `c5069b3`) were used where a finding depends on the original game's behaviour. Paths are relative to the repository root.

**Parts.** Seven reader parts read their area in full (each part report lists every file with its line count and says what was not read in full). Two measurement parts built tools in their own worktrees. One cross-cutting pass collected hard-coded content (D5) and consistency (D7) over all reports. A third measurement part (DET) fuzzed the readers, ran the unit tests under sanitizers and ran static analysis.

| Part | Area | Kind | Second review |
|---|---|---|---|
| SIM | Simulation core (`engine/sim` without AI and save) | reader | verified |
| AI | Computer players (`engine/sim/src/ai`) | reader | verified |
| IO | Data loaders and savegames (`engine/assets`, `engine/core`, `engine/sim/src/save`) | reader | verified |
| NET | Network and multiplayer (`engine/net`, multiplayer app code) | reader | verified |
| APP | Game application and interface (`engine/app`, `engine/ui`) | reader | verified |
| RND | Rendering, audio, video, platform | reader | verified |
| BLD | Build, CI, tests, tools, documentation | reader | verified |
| LST | Measurement: stress, limits, long sessions (`tools/stress`) | measurement | verified |
| SYNC | Measurement: determinism and network edge cases (`tools/o1602-replay`, `tests/review`) | measurement | verified |
| DET | Measurement: fuzzing, sanitizers, static analysis, mod data (`tests/fuzz`, `tools/fuzz`) | measurement | verified |
| MODCON | Cross-cutting pass: hard-coded content (D5) and consistency (D7) | cross-cutting | not separately verified; built on the verified reports |

**Verification.** Every part was checked by an independent second reviewer, who re-read each cited code site, followed callers to the UI, AI and network entry points, re-ran the author's benchmarks and reproducers, and wrote own reproducers where a claim was doubtful. The verifier's verdict and severity win in this report. Corrected line numbers and measurements from the verifications are used. Findings reported by more than one part are merged into one, with the part IDs kept as aliases.

**Tools.** GCC 13.3 Release (the baseline build, preset `release`), GCC 13 Debug with coverage and extra warnings, Clang 18 (`-Weverything` syntax pass, ThreadSanitizer), ASan/UBSan, valgrind (callgrind, memcheck), heaptrack, gcovr 8, gdb stack sampling, and small benchmark and reproducer programs per part in the review's scratch folders (not in the repository). Part DET: Clang 18 with libFuzzer, cppcheck 2.13, clang-tidy 18 (results below).

**Review tools added to the repository** (no engine code changed; each on its own branch, merged into the working branch `claude/project-thread-kdui2s`):

| Branch | Content |
|---|---|
| `review-tools-lst` | `tools/stress`: `o1602-stress` (full `Simulation` on synthetic content or the original data; per-tick wall and thread CPU time, steps, memory, allocations, entity counts, state hash, save/load time, snapshot size, round-trip hash compare), `run-synthetic.sh`, `run-on-device.sh` |
| `review-tools-sync` | `tools/o1602-replay` (`o1602-replay`: deterministic replays with recorded or random commands and save/load round trips; `compare_builds.sh`), `tests/review` (17 lockstep edge-case tests over a fault-injecting loopback transport, plus an ENet memory test; ctest label `review`), `.github/workflows/determinism.yml` (proposed cross-compiler hash comparison) |
| `review-tools-det` | `cmake/Fuzzing.cmake` (`O1602_FUZZ`, Clang only), `tests/fuzz` (11 libFuzzer targets, the seed writer `o1602_fuzz_seeds` with 94 synthetic seeds, the mod-limit probe `o1602_mod_limits`, `.cod`/`.gad` dictionaries), `tools/fuzz` (`run_fuzzers.sh`, `add_original_seeds.sh`, `make_dicts.sh`, `clang-tidy-review.yaml`), `.github/workflows/fuzz.yml` (proposed: 60 s per target on reader PRs, 600 s nightly) |

Commit `b2c6600` on the working branch added the meadow record 22722 to the stress tool's synthetic content (see LST-V1 under "Refuted or withdrawn") and applied clang-format to the tool code.

**Where things were measured.** Everything ran in one Linux container: a 4-core VM shared with up to ten review agents (load average 5-13), with no GPU, no SDL3 for the BLD builds (the game, `render_sdl`, `platform` and the SDL tests were not built there), no original game data and no Windows or macOS machine. Consequences for the numbers:

- Single-run wall-clock p99 and maximum tick times on this machine are mostly scheduler preemption. They are **not** used as evidence. The report prefers thread CPU time of the simulation thread (`o1602-stress`) and the minimum per tick over several runs of the same deterministic scenario (AI verification), and says which one a number is.
- Micro-benchmarks that average over repetitions may be inflated up to about 2×. Where author and verifier both measured, both values are given ("author / verifier").
- Scaling and ratios hold. Absolute numbers with real content come from the device runs (section "Measurements with the original data"), on a machine about 2-3× faster than the minimum machine.

### Robustness results (part DET)

Engine code at `f5a46dd`; tools on `review-tools-det`. No original data on the review machine, so all runs used synthetic data and seeds (device runs DET-1 to DET-4 repeat them with the real data).

**Fuzzing.** ASan + UBSan with `-fno-sanitize-recover=all` and `_GLIBCXX_ASSERTIONS`, libFuzzer fork mode, `-rss_limit_mb=2048 -malloc_limit_mb=1024 -timeout=10`. Each target first ran 45 s on the synthetic seeds; the long runs built on that corpus. In total about 4.6 hours (16 556 s plus 11 × 45 s smoke runs). The verifier reproduced every crash class.

| Target | Time (s) | Runs | Runs/s | Corpus | Coverage | Crash / OOM / timeout | Classification |
|---|---|---|---|---|---|---|---|
| `fuzz_o1602_save` | 2,120 | 1,633,731 | 770 | 728 | 17,396 | 0 / 0 / 0 | – |
| `fuzz_command_apply` | 2,145 | 49,976 | 23 | 621 | 12,911 | 0 / 0 / 0 | – |
| `fuzz_gam_world` | 1,500 | 265,262 | 177 | 1,141 | 27,000 | 8 / 0 / 0 | all HI-15 |
| `fuzz_gad` | 1,873 | 82,063 | 44 | 982 | 9,145 | 3 / 6 / 2 (+1 slow) | HI-2 |
| `fuzz_cod` | 1,512 | 602,280 | 398 | 1,310 | 9,842 | 0 / 0 / 1 (+1 slow) | HI-2 (HI-17 came from the probe) |
| `fuzz_settings` | 1,202 | 947,962 | 789 | 1,398 | 7,862 | 90 / 0 / 0 | FB-36 (89 slots, 1 settings) |
| `fuzz_chunks` | 1,244 | 4,096,783 | 3,293 | 398 | 7,097 | 0 / 0 / 0 | – |
| `fuzz_net_message` | 1,239 | 31,603,007 | 25,507 | 1,227 | 6,792 | 0 / 0 / 0 | – |
| `fuzz_command` | 1,243 | 49,508,509 | 39,830 | 517 | 3,814 | 0 / 0 / 0 | – |
| `fuzz_media` (Smacker + WAV) | 1,248 | 11,195,025 | 8,970 | 551 | 2,799 | 0 / 0 / 0 | – |
| `fuzz_sprites` (chunks, BSH/ZEI, COL, fonts) | 1,230 | 11,320,838 | 9,204 | 210 | 2,398 | 0 / 0 / 0 | – |

Earlier fixes held under fuzzing: the PLAY player-count allocation (earlier IO-1: 1.63 M runs, no OOM), the Smacker quadratic audio (earlier IO-2 per chunk: 11.2 M runs, no OOM or timeout), ship and unit ids (earlier IO-4: validated after load and after 4 ticks).

**Sanitizers on the unit tests** (645 tests, synthetic data):

| Run | Configuration | Result |
|---|---|---|
| TSan | `-DO1602_SANITIZE=thread`, RelWithDebInfo -O1 | 645 passed, 0 reports. The LAN DNS thread and network threads run only as far as the tests use them. |
| UBSan | `-DO1602_SANITIZE=undefined` + float and integer divide-by-zero, `-fno-sanitize-recover=all`, `_GLIBCXX_ASSERTIONS` | 645 passed, 0 engine reports. ENet had to be built without UBSan (BT-24). |
| `_GLIBCXX_ASSERTIONS` | in the UBSan run and the fuzz build | 0 assertion failures |

**cppcheck 2.13** (`--enable=warning,performance,portability,style`, every `.cpp`/`.hpp` under `engine/`): 485 messages. The 6 errors and warnings are all false positives (`app_settings.cpp:275` returnDanglingLifetime, `cursor_resource.cpp:29` containerOutOfBounds ×3, `world_graphics.cpp:94` invalidContainer, `host_session.cpp:809` arrayIndexThenCheck). The rest is style: useStlAlgorithm 218, noExplicitConstructor 99, shadowFunction 59, constParameterReference 33, constVariablePointer 28, constVariableReference 12, knownConditionTrueFalse 9, passedByValue 7, and 20 more in 15 other checks (LC-23 for the ones worth a glance).

**clang-tidy 18** (`tools/fuzz/clang-tidy-review.yaml`: bugprone-\*, performance-\*, concurrency-\*, cert-err33/34/52/58/60; 147 engine translation units; unique warnings): 176 in all, none a real defect. bugprone-unchecked-optional-access 80 (guarded or invariant-backed), cert-err33-c 38 (unchecked `fprintf`/`snprintf` with bounded formats), bugprone-too-small-loop-variable 15 and bugprone-misplaced-widening-cast 8 (both LC-23), performance-enum-size 6, performance-unnecessary-value-param 5, performance-inefficient-vector-operation 5, performance-no-automatic-move 4, and 15 more in 10 other checks (one theoretical `noexcept` escape at `protocol.cpp:391`, one `getenv` at start-up in `ai_core.cpp:68`). Not covered: the generated `o1602_ui_embedded.cpp` and 23 platform files outside the Linux build. The verifier spot-checked the triage but did not re-run cppcheck or clang-tidy.

**How to read a finding.** IDs are per theme (MS, DS, TB, FB, GB, HI, LC, MD, BT). Severity: Critical (crash, data loss, desync, remote-exploitable in multiplayer), High (wrong game behaviour, a stress target not reached, a likely crash under realistic conditions), Medium (performance or maintainability with foreseeable consequences, limit reached only in extreme cases), Low, Info. Dimensions: D1 bugs, D2 bottlenecks, D3 stability, D4 performance, D5 hard-coded content, D6 limits, D7 consistency, D8 determinism, D9 hostile input, D10 testability, docs and platforms. Effort: S (hours), M (a day), L (several days). "Fix bundle" names the pull request in "Proposed fix order". The tick budget is 6.25 ms at speed 8; stress targets are 12 players, worlds up to 4096², as many islands as fit, 100 000 soldiers and 20 000 ships.

## Counts

### Per part, after verification

Each part's own IDs (including the verifiers' new findings) with the verified severity, before merging. "Shared" counts IDs that were merged with a finding of another part, so the same problem appears in more than one row. "Unclear" findings are also counted under their severity. The device runs re-judged two merged findings (TB-26 High → Medium, MS-11 Medium → Low) and added LC-24, HI-18 and HI-19; this table shows the parts' verified severities before that, the theme table below the final ones.

| Part | Critical | High | Medium | Low | Info | Refuted | Tool or errata | Unclear | Shared | IDs |
|---|---|---|---|---|---|---|---|---|---|---|
| SIM | 0 | 5 | 7 | 12 | 3 | 1 | 0 | 1 | 12 | 28 |
| AI | 0 | 2 | 8 | 9 | 1 | 0 | 0 | 0 | 5 | 20 |
| IO | 0 | 1 | 3 | 10 | 7 | 0 | 0 | 0 | 6 | 21 |
| NET | 2 | 5 | 5 | 9 | 1 | 0 | 0 | 0 | 8 | 22 |
| APP | 0 | 1 | 7 | 20 | 3 | 0 | 0 | 1 | 4 | 31 |
| RND | 0 | 2 | 4 | 16 | 7 | 0 | 0 | 2 | 4 | 29 |
| BLD | 0 | 0 | 2 | 13 | 2 | 0 | 1 | 0 | 3 | 18 |
| LST | 0 | 5 | 6 | 4 | 2 | 1 | 1 | 0 | 11 | 19 |
| SYNC | 3 | 2 | 4 | 3 | 2 | 0 | 0 | 0 | 8 | 14 |
| DET | 0 | 0 | 3 | 4 | 3 | 0 | 2 | 0 | 3 | 12 |
| MODCON | 0 | 0 | 5 | 11 | 4 | 0 | 0 | 0 | 7 | 20 |
| **All parts** | 5 | 23 | 54 | 111 | 35 | 2 | 4 | 4 | 71 | 234 |

### After merging, per theme

| Theme | Critical | High | Medium | Low | Info | Total |
|---|---|---|---|---|---|---|
| Multiplayer security and session integrity (MS) | 3 | 5 | 6 | 9 | 3 | 26 |
| Determinism and save/resync (DS) | 0 | 1 | 3 | 2 | 1 | 7 |
| Tick budget (TB) | 0 | 7 | 12 | 8 | 2 | 29 |
| Frame budget, interface and rendering (FB) | 0 | 2 | 5 | 26 | 3 | 36 |
| Game behaviour (GB) | 0 | 1 | 1 | 4 | 2 | 8 |
| Hostile input and loaders (HI) | 0 | 0 | 7 | 10 | 2 | 19 |
| Limits and consistency (LC) | 0 | 0 | 4 | 14 | 6 | 24 |
| Modding (MD) | 0 | 0 | 5 | 6 | 3 | 14 |
| Build, tests, docs (BT) | 0 | 0 | 2 | 16 | 6 | 24 |
| **Total** | 3 | 16 | 45 | 95 | 28 | 187 |

## Findings

Within each theme the findings are sorted by severity. Each one keeps the IDs of the part reports as aliases; `findings.csv` maps them as well.

### Multiplayer security and session integrity (MS)

Lockstep sessions, seats, reconnect tokens, host migration and the host's admission of commands and connections. Every finding here can be triggered over the network.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| MS-1 | Critical | The successor's `Standby` tokens let it take any player's slot on the running host (regression of earlier MP-1) | P1 |
| MS-2 | Critical | A client that loses only its own link acts as if the host left: the successor forks the game, the others never try the host again | P4 |
| MS-3 | Critical | Rectangle demolish orders let any player stall every computer for 0.1–3 s per frame | P2 |
| MS-4 | High | Rejoining from the program never sends the reconnect token: a returning player lands in another empire | P3 |
| MS-5 | High | A just-vacated slot goes to the next newcomer, together with its empire | P3 |
| MS-6 | High | With "join running game" off, a player whose link broke can never return | P3 |
| MS-7 | High | Unauthenticated peers and spectators can make the host hold 32–64 MiB each, on up to 32 connections | P5 |
| MS-8 | High | A `Standby` seat from the previous host is used unchecked: heap out-of-bounds write on the new host | P1 |
| MS-9 | Medium | The host's command queue is quadratic under a flood | P2 |
| MS-10 | Medium | The host's own commands are not checked against the decode limits: one oversized order disconnects every client | P2 |
| MS-12 | Medium | Connection-slot squatting: 32 ENet connections in all, spectators unlimited | P5 |
| MS-13 | Medium | After any `HostLeft`, a client follows the successor's address and hands it its token | P4 |
| MS-14 | Medium | During a host migration a newcomer can take a returning player's slot | P3 |
| MS-15 | Medium | Stall detection counts slow-but-progressing time as a stall, which starts a resync loop (Medium on large worlds, Low on small ones) | P8 |
| MS-11 | Low | The 32 MiB snapshot limit has no fallback, and the client's ENet limit is 22 bytes short of it (reached only by dense synthetic or hand-made 4096² worlds) | P5 |
| MS-16 | Low | Lobby messages have no rate limit, and each change rebroadcasts the whole lobby | P2 |
| MS-17 | Low | Commands beyond four frames' share are dropped without a word; a host-inserted `PlayerControl` can be lost | P2 |
| MS-18 | Low | The lobby's "kick spectator" button can remove a different member | P2 |
| MS-19 | Low | `resync_behind` counts frames, not time: at speed 8 a lagging computer gets the whole game after 3.75 s | P8 |
| MS-20 | Low | Pause can be used to grief (design decision for the owner) | — |
| MS-21 | Low | The LAN beacon answers any source address (about 16× reflection) | P5 |
| MS-22 | Low | A client of another protocol version is refused as "sent nonsense", not as "another version" | P4 |
| MS-23 | Low | The host holds about three copies of every snapshot per receiving peer | P5 |
| MS-24 | Info | The detached DNS thread of `LanBrowser` can outlive ENet | P5 |
| MS-25 | Info | The successor's IP address goes to every member, spectators included | P4 |
| MS-26 | Info | Network decoders reserve the announced element count before checking that the bytes are there | P5 |

#### MS-1 [Critical] The successor's `Standby` tokens let it take any player's slot on the running host (regression of earlier MP-1)

- **Aliases:** NET-1, SYNC-4 · **Dimensions:** D9, D8, D1 · **Effort:** M · **Depends on:** — · **Fix bundle:** P1
- **Where:** `engine/net/src/host_session.cpp:854-877` (`hand_over` sends every entry of `m_tokens`); `:384-398` (`hello` accepts any known token once the game runs and drops the holder's connection); `:732-740` (`drop` sends `Refused` only when `tell` is set); `:839-851` (the successor is the lowest playing seat); `engine/app/src/multiplayer.cpp:209-225` (the victim then follows the successor's address)
- **Failure scenario:** A player with a modified client takes the lowest seat after the host's in the lobby (first come, first served). The host names it successor and sends it `Standby` with every slot's token. It opens a second connection with `Hello{token = victim's}`. The host drops the victim's live connection without `Refused` and gives the slot, with its city, money and army, to the second connection. The token path decides the seat before the `join_running` check, so this also works with "join running game" off. The victim's client sees `HostLeft`, migrates to the attacker's address and hands it its own token (MS-13).
- **Evidence:** `repro_net` R2 (`review-2026-10/NET`), re-run by the verifier: `Standby token: seat 2 -> 1bc5bcfbebeb8465 (Bernd's)` ... `second connection got seat 2; Bernd: ended=1 reason='the connection to the host ended'` ... `Mallory now plays seats 1 and 2`. The SYNC review test "the successor cannot use the tokens it holds for the migration to take another's slot" fails the same way. ADR 0011 point 15 justifies sending the tokens with "it becomes the host, who knows them anyway" and does not consider that the old host is still running.
- **Solution:** (1) Send the successor a verifier instead of the token, for example `xxh64(token ‖ per-session salt)` or a keyed hash; the new host checks `H(Hello::token)`. This alone closes the hijack. (2) On the running host, never let a token replace a live, welcomed connection that reported `Progress` within the ENet timeout (about 15 s); when a connection is replaced, send it `Refused{Kicked}` or a new `Replaced` reason. (3) A host made by migration accepts tokens only with `returning`. Test: a raw client that becomes successor presents the tokens to the running host (the review test on branch `review-tools-sync` can be adopted).

#### MS-2 [Critical] A client that loses only its own link acts as if the host left: the successor forks the game, the others never try the host again

- **Aliases:** NET-2, SYNC-5 · **Dimensions:** D8, D3, D1 · **Effort:** M · **Depends on:** MS-4 (rejoin path), MS-1 (token verifiers) · **Fix bundle:** P4
- **Where:** `engine/net/src/client_session.cpp:113-120` (every `Disconnected` after `Welcome` becomes `HostLeft`); `:30-41` (`hosts_next`, `can_migrate`: only `Standby` held and `successor == seat`); `engine/app/src/multiplayer.cpp:188-231` (`migrate()` listens and builds a `HostSession` at once); `engine/net/src/host_session.cpp:202-207` (a quitting host only disconnects; there is no leaving message); `engine/net/src/enet_transport.cpp:24-25,55` (ENet timeouts 5–15 s)
- **Failure scenario:** The successor's Wi-Fi drops for 5–15 s while the host keeps running. The successor's client ends with `HostLeft`, finds `hosts_next()` true, listens on its port and hosts its own copy of the game, in which the computer plays every other slot. The real host gives the successor's empire to the computer after `take_over_ms` and names a new successor. Two games now diverge for good, and nothing detects or reports the split. A follower that loses its own link retries only `successor_address` for `migration_ms` (30 s), then saves `hostleft.o1602`; it never tries the host that is still running, which is the case the reconnect token exists for.
- **Evidence:** `repro_net` R1 (re-run): `Anna: ended=1 reason='the connection to the host ended' can_migrate=1 hosts_next=1` / `Host: ended=0, still running at frame 90` / `Forked HostSession made from Anna's ClientSession: started=1 frames=79 -> two games now`. R1 builds the `HostSession` exactly as `migrate()` does. SYNC review test "the successor whose own link breaks does not host a second game" fails the same way. `tests/unit/test_net.cpp:485` asserts `HostLeft` as the expected result of a cut client link. **Device:** the review test "the successor whose own link breaks does not host a second game" fails on the user's machine as well.
- **Solution:** A host that quits sends an explicit `HostLeaving` message (protocol version 8) before it disconnects; clients migrate at once only on that message. On a bare `Disconnected`, first rejoin the old host with the token for a few seconds (needs MS-4). The successor starts listening only after its own reconnect attempts have failed, or does not consider itself host until another member arrives with a valid token. Tests: cut only the successor's link and assert that no second host appears; cut a follower's link and assert that it returns to its own slot.

#### MS-3 [Critical] Rectangle demolish orders let any player stall every computer for 0.1–3 s per frame

- **Aliases:** SYNC-1 · **Dimensions:** D2, D9 · **Effort:** S (constant) + M (budget) · **Depends on:** — (shares the admission code with MS-9, MS-10) · **Fix bundle:** P2
- **Where:** `engine/sim/src/construction.cpp:1977-2045` (`plan_demolish`: `building_at` for every tile, `std::set` insert before the owner check); `:2092-2095` (`execute_demolish`); `engine/sim/src/command_io.cpp:72-79` (any `from`/`to`, clamped to the island later); `engine/net/src/host_session.cpp:249-256` (`frame_share`: up to 256 commands per player and frame); `:445-468` (no cost check)
- **Failure scenario:** A modified client sends up to 256 `DemolishOrder`s over a whole island per frame (128 with 8 players, 85 with 12; several attackers together up to 1024), about 100 KB/s of upload. Every computer runs `plan_demolish` for each. The commands are legal, the stall detector sees everyone equally slow, and nobody can tell who causes it; the host must guess whom to kick through an interface that answers every 1–3 s. With the defaults (`join_running` on, no password) a stranger from the LAN list gets a player slot. The interface itself cannot trigger this: it makes one order per drag release.
- **Evidence:** Verifier, minimum of 3 runs, GCC -O3, 1×1 forest (one origin per tile):

  | Island | One order over the island | 256 orders in one frame |
  |---|---|---|
  | 50 × 50 | 0.33 ms | 90 ms |
  | 120 × 120 (largest generated) | 2.15 ms | 580 ms |
  | 200 × 200 | 6.4 ms | 1.74 s |
  | 255 × 255 (hand-made) | 12.8 ms | 3.26 s |

  Review test "the heaviest commands a player may put into one frame" (`tests/review/sync_net_edge.cpp`, branch `review-tools-sync`). Its second half ("18 build orders of 4096 roads") measures refused orders and bounds nothing. **Device (review test, original data build):** 256 demolish orders on a 200×200 island took 612.6 ms on a Ryzen 9 7900X3D, about 1.2-1.8 s on the minimum machine. Confirms Critical.
- **Solution:** The real fix is admission by work, not by count: the host gives each player a per-frame work budget and weights commands by cost (`DemolishOrder` by the clamped rectangle area, `BuildOrder` by item count, `SoldierOrder` by unit count), and refuses rectangles larger than the island's bounding box intersected with the player's territory. Cheap constant cut on top: test `b->city`/owner before touching `seen`, and use a per-island scratch bitmap instead of `std::set` (alone it does not fix the area cost). Test: 256 maximal orders of each kind cost less than 6.25 ms, with a player that has a Kontor and territory.

#### MS-4 [High] Rejoining from the program never sends the reconnect token: a returning player lands in another empire

- **Aliases:** NET-3 · **Dimensions:** D1, D8 · **Effort:** S–M · **Depends on:** — · **Fix bundle:** P3
- **Where:** `engine/app/src/multiplayer.cpp:689-729` (`join_game` sends `token = 0`, `seat = kNoPlayer`; the token is never stored); `engine/net/src/host_session.cpp:403-415`, `:798-818` (`free_seat(kNoPlayer)` gives the lowest free slot); `multiplayer.cpp:653-658` and `host_session.cpp:1112-1120` (lobby slots left open become computer players and stay `Open`)
- **Failure scenario:** A single drop is enough. Lobby: seats 1 and 2 open, seat 3 a computer; Anna takes seat 2, the game starts, her link breaks, she rejoins through the menu or `--join`. She gets seat 1, a computer's empire, under her name, while her own empire stays human with nobody in it until the computer takes it over after 60 s. With two drops at once, whoever returns first takes the other's empire. ADR 0011 points 6 and 8 promise rejoining by token.
- **Evidence:** `repro_net` R3: `Bernd back: seat 1 = 'Bernd' (his own empire is seat 2)`. Verifier V1 (one drop): `Anna back: seat 1, world name 'Anna', role of seat 1=0 seat 2=0`. The unit tests pass the token by hand (`Computer(network, "Bernd", token)`), so the program's path is untested.
- **Solution:** Keep `{address, port, seat, token}` from `Welcomed` (in memory for the program's run at least, optionally in the settings) and send them from `join_game` when rejoining the same host. On the host, `free_seat` skips slots in `m_vacant` that are still inside their waiting time (MS-5). Add an app-level rejoin test.

#### MS-5 [High] A just-vacated slot goes to the next newcomer, together with its empire

- **Aliases:** SYNC-2 · **Dimensions:** D1, D9 · **Effort:** S · **Depends on:** MS-4 · **Fix bundle:** P3
- **Where:** `engine/net/src/host_session.cpp:798-821` (`free_seat` treats a slot in `m_vacant` as free); `:403-415` (`hello` then calls `forget_token(seat)`, line 415); `:742-754` (`drop` makes the slot `Open` at once)
- **Failure scenario:** Bernd's connection drops. Within `take_over_ms` (60 s, documented as "time to come back after a broken connection") any newcomer joins. An unmodified client is enough: `join_game` asks for `kNoPlayer`, and the lowest free slot is Bernd's. The newcomer gets Bernd's city, money and army; Bernd's token is forgotten, and he comes back into the computer's slot or as a spectator.
- **Evidence:** SYNC review test "a stranger cannot take the slot of a player whose connection just broke": `the stranger got slot 2 (Bernd had 2)` / `Bernd is back in slot 3` (the computer player's slot). **Device:** the review test fails on the user's machine as well (newcomer takes a dropped player's slot).
- **Solution:** Reserve a slot in `m_vacant` for its token until `take_over_ms` has passed; do not forget the token of a vacated slot; a newcomer gets another slot or watches. Fix together with MS-4, which makes the token usable by the returning player.

#### MS-6 [High] With "join running game" off, a player whose link broke can never return

- **Aliases:** NET-V1 · **Dimensions:** D1, D3 · **Effort:** S (with MS-4) · **Depends on:** MS-4 · **Fix bundle:** P3
- **Where:** `engine/net/src/host_session.cpp:405-411`; `engine/app/src/multiplayer.cpp:710-719`
- **Failure scenario:** The host unticks "allow joining the running game". A player's link breaks for 5–15 s, or his program crashes. He joins again through the menu; `join_game` sends no token, so `hello` reaches `started() && !join_running` and answers `Refused{Closed}`. He is out for good, and his empire goes to the computer after 60 s.
- **Evidence:** Code reading: the token branch (lines 389-401) would set the seat before this check, but the program's `Hello` carries token 0.
- **Solution:** The MS-4 fix (remember and send the token). Optionally let a returning name into its `m_vacant` slot within `take_over_ms` even with joining off.

#### MS-7 [High] Unauthenticated peers and spectators can make the host hold 32–64 MiB each, on up to 32 connections

- **Aliases:** NET-4, SYNC-8 · **Dimensions:** D9, D3 · **Effort:** M · **Depends on:** — (enables MS-11) · **Fix bundle:** P5
- **Where:** `engine/net/src/enet_transport.cpp:22-23,161-162` (`maximumPacketSize` and `maximumWaitingData` = 32 MiB + 64 KiB on the listening host); `:67-72` (every packet copied into a vector); `engine/net/src/host_session.cpp:340-343` (the 1 MiB limit is checked only on complete messages); `third_party/enet/protocol.c:580,622`, `peer.c:956-992` (the reassembly buffer of `totalLength` is allocated at the first fragment); `duplicatePeers` left at 4095
- **Failure scenario:** Up to 32 connections, all from one address, each deliver most of a 32 MiB reliable packet. Peers that never say `Hello` hold it for the 10 s greeting window (filling 1 GiB needs about 1 Gbit/s: realistic on a LAN). A welcomed spectator (spectators are on by default, no password) that withholds the last fragment holds it for as long as it keeps the link alive: a one-time upload of 1–2 GiB keeps the 4 GB minimum machine near out-of-memory, and the game stops for everyone.
- **Evidence:** Verifier `enet_partial` against a real `HostSession` process with 8 unwelcomed peers: `went silent after the first service: server RSS 3 MiB, VmSize 264 MiB`; `after 2000 ms of sending: server RSS 64 MiB` (RSS equals the bytes delivered). SYNC test `sync_enet_memory` (16 connections): +160 MB `mallinfo2` (reserved, not resident). The claim that first fragments alone suffice ("hardly any bandwidth") is refuted for Linux and macOS; on Windows a large `HeapAlloc` commits at once (suspected, up to about 2 GiB of commit). **Device:** `sync_enet_memory` with 16 connections without `Hello`: +160 MB on the host, as in the cloud run.
- **Solution:** Split snapshots at the application level into chunks of a few hundred KiB (`SnapshotPart`, reassembled into a buffer whose size the host announced). Then set `maximumPacketSize` and `maximumWaitingData` to about 1–2 MiB on both ends (the host only ever needs to receive `kMaxMessageToHost` = 1 MiB) and `duplicatePeers` to about 4. Alternative: patch ENet for separate incoming and outgoing limits. There is no cheaper interim fix: ENet also checks `maximumPacketSize` when sending (`peer.c:115`), and the host sends whole games.

#### MS-8 [High] A `Standby` seat from the previous host is used unchecked: heap out-of-bounds write on the new host

- **Aliases:** NET-12 · **Dimensions:** D9, D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P1
- **Where:** `engine/net/src/protocol.cpp:304-319` (`get_standby` accepts any `u8` seat); `engine/net/src/host_session.cpp:165-170` (the migration constructor copies the seats); `:389-425` (`hello` indexes `m_lobby.seats[seat]` without a bounds check once started; `occupy` at `:48`)
- **Failure scenario:** A buggy or hostile old host sends `Standby{seat 200}` with a lobby of 2 seats and leaves. When a computer returns with that token, the new host writes outside `m_lobby.seats`. Memory corruption from a network message; not Critical only because the sender must have been the host.
- **Evidence:** Verifier `standby_oob` with ASan: `heap-buffer-overflow ... WRITE of size 1 #0 occupy host_session.cpp:48 #1 HostSession::hello host_session.cpp:425 #2 HostSession::receive host_session.cpp:350`. With `_GLIBCXX_ASSERTIONS` it stops at `__n < this->size()`.
- **Solution:** Drop `Standby` entries whose seat is `>= lobby.seats.size()` or whose slot is `Reserved`/`Closed`, in the migration constructor; check `seat < m_lobby.seats.size()` in `hello()`; validate `computer_held` the same way. Turn the reproducer into a unit test.

#### MS-9 [Medium] The host's command queue is quadratic under a flood

- **Aliases:** NET-6, SYNC-6 · **Dimensions:** D9, D2 · **Effort:** S · **Depends on:** — (same code as MS-3, MS-17) · **Fix bundle:** P2
- **Where:** `engine/net/src/host_session.cpp:236-243` (`count_if` over `m_overflow` for every command beyond the share); `:536-550` (`close_frame` queues the whole overflow again every frame); `:249-256` (`frame_share` walks all peers per command); `:445-468` (no limit per message or per second)
- **Failure scenario:** An admitted player sends `CommandBatch`es of 1024 cheap commands every update. The host's update time grows with the rate, and lockstep makes everyone wait. Spectators cannot send commands (line 446).
- **Evidence:** Wall clock on the loaded machine. `repro_net` R4 re-run: 4 batches per update (2.5 MB/s) mean 10.2 ms, worst 23.5 ms; 16 batches 40.8 ms; 64 batches 139 ms. Decoding a 1024-command batch costs 0.037 ms, so the time is the queueing. SYNC: 12 flooders 36.7–108 ms mean; callgrind puts 82 % of all instructions in `HostSession::queue`. **Device:** 12 players flooding commands cost the host 20.7 ms per update on average (25.1 ms max); of a 5 000-command burst 1 280 are carried out and the rest are dropped silently (MS-17).
- **Solution:** Per-seat counters of waiting commands and a per-peer overflow deque drained round-robin; compute `frame_share()` once per update; drop at `commands()` in O(1) once a peer's queue is full; a token bucket in bytes or commands per peer before decoding; kick a peer that keeps exceeding it.

#### MS-10 [Medium] The host's own commands are not checked against the decode limits: one oversized order disconnects every client

- **Aliases:** NET-7 · **Dimensions:** D9, D6, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P2
- **Where:** `engine/net/src/host_session.cpp:216-247` (`submit` → `queue`, no check); `engine/sim/include/o1602/sim/command_io.hpp:23-24` (`kMaxCommandItems = 4096`, `kMaxCommandText = 255`); `engine/sim/src/command_io.cpp:309-314,349-361,383-396` (the writer writes any length, the reader refuses more); `ClientSession::submit` has the same gap
- **Failure scenario:** The host's player gives a `SoldierOrder` with more than 4096 units or a `ShipOrder` queue with more than 4096 entries. Every client fails to decode the `Turn` and ends with `ProtocolError`: the session is over. The interface cannot reach this today (`UnitSelection::kMaxUnits` = 21, `build_tool` `kMaxItems` = 512, names bounded), but lifting the selection limit (LC-3) does.
- **Evidence:** `repro_net` R5 re-run, a host `SoldierOrder` with 5000 ids: `Anna ended=1 'a message made no sense', Bernd ended=1 'a message made no sense'`.
- **Solution:** A shared `sim::command_fits(const Command&)` in `HostSession::queue` and `ClientSession::submit`; split a `SoldierOrder` deterministically into chunks of 4096; a `static_assert` tying the interface maxima to `kMaxCommandItems`.

#### MS-12 [Medium] Connection-slot squatting: 32 ENet connections in all, spectators unlimited

- **Aliases:** NET-9, SYNC-12 · **Dimensions:** D9, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P5
- **Where:** `engine/net/include/o1602/net/enet_transport.hpp:22` (`max_computers = 32`); `engine/net/src/host_session.cpp:288-292,400-415` (peers without `Hello` are held 10 s, spectators forever); `engine/net/src/enet_transport.cpp:155` (`duplicatePeers` not set); `engine/app/src/multiplayer.cpp:51,200,540` (`enet_listen` with the default)
- **Failure scenario:** Anyone who knows the address (the LAN beacon announces it) connects 32 times as a spectator, or reconnects every 10 s without `Hello`. All ENet slots are taken and real players cannot connect; kicked spectators come straight back. Each spectator also receives the whole game.
- **Evidence:** Code reading; not run with 32 sockets.
- **Solution:** `SessionConfig::max_spectators` (4–8); ENet capacity = players + spectators + a margin for greeting connections; `duplicatePeers` about 4; a `Hello` window of 2–3 s; a session ban list for kicked addresses.

#### MS-13 [Medium] After any `HostLeft`, a client follows the successor's address and hands it its token

- **Aliases:** NET-V2 · **Dimensions:** D9 · **Effort:** S (with MS-2) · **Depends on:** MS-1, MS-2 · **Fix bundle:** P4
- **Where:** `engine/app/src/multiplayer.cpp:209-225`; `engine/net/src/client_session.cpp:23-28`
- **Failure scenario:** Under MS-1 or MS-2 the old host is still alive, and the "successor" is just another player's computer, possibly hostile. It receives `Hello{token, returning}` and can feed the client any snapshot of its choosing, which `read_game` must withstand (HI-11).
- **Evidence:** Code reading; follows from MS-1 and MS-2.
- **Solution:** Fixed together with MS-2 (migrate only on an explicit `HostLeaving`) and MS-1 (with verifiers the successor can check a returning player but learns nothing reusable).

#### MS-14 [Medium] During a host migration a newcomer can take a returning player's slot

- **Aliases:** SYNC-3 · **Dimensions:** D1, D9 · **Effort:** S · **Depends on:** MS-5 · **Fix bundle:** P3
- **Where:** `engine/net/src/host_session.cpp:176-183` (the migration constructor opens and vacates every human slot); `:403-415`; `:798-821`
- **Failure scenario:** The host leaves and the successor hosts on. Bernd notices a few seconds later (ENet timeout 5–15 s). In between a newcomer connects and gets Bernd's slot; Bernd comes back as a spectator. Rarer than MS-5, because the newcomer must arrive within those seconds.
- **Evidence:** SYNC review test: `newcomer in slot 2, Bernd (had 2) in slot 255`. **Device:** the review test fails on the user's machine as well (newcomer takes a slot during migration).
- **Solution:** As MS-5: slots with a token in `Standby` stay reserved for `migration_ms`.

#### MS-15 [Medium] Stall detection counts slow-but-progressing time as a stall, which starts a resync loop (Medium on large worlds, Low on small ones)

- **Aliases:** SYNC-7 · **Dimensions:** D2 · **Effort:** S–M · **Depends on:** MS-19, TB-26 · **Fix bundle:** P8
- **Where:** `engine/net/src/host_session.cpp:567-644` (`run` holds at `slowest() + ahead_frames()`; `watch_stalls` adds to `m_held_ms` in every update that hit the limit, even when frames ran); `:639-643` (resync after `resync_behind`)
- **Failure scenario:** A client manages two thirds of the needed frames (old laptop, speed 4, late game). The game correctly runs at its pace, but after `stall_ms` (10 s, then 2.5 s) the client is declared lagging, falls 600 frames behind, receives a full snapshot, catches up and starts again: 4 loads in 5 minutes. Each resync costs the host a `write_game` on its game thread (TB-26), so on a large world everyone gets a periodic hitch; on a small world the slow player's game is jumpy.
- **Evidence:** SYNC review test (5 min): `held 17888 ms in all, at most 10160 ms at once; slow computer loaded the game 4 times, lagging 4 times`. Verifier: during the longest "held" stretch the host still ran 149 frames (73 % of normal speed); the longest stretch without any frame was 96 ms. The report's "the game froze for 10.2 s" is withdrawn. **Device:** a client at 2/3 speed made the host hold for 10.2 s at once, as in the cloud run.
- **Solution:** Count `m_held_ms` only in updates in which no frame could run, or compare the peer's progress rate. Optionally lower the game's effective speed to what the slowest computer reports in `Progress`, with a message. Change the review test to assert "no stretch without frames above X" and "at most one load".

#### MS-11 [Low] The 32 MiB snapshot limit has no fallback, and the client's ENet limit is 22 bytes short of it (reached only by dense synthetic or hand-made 4096² worlds)

- **Aliases:** NET-8 · **Dimensions:** D6, D2, D7 · **Effort:** S (client limit) / M (chunks) · **Depends on:** MS-7, TB-26 · **Fix bundle:** P5
- **Where:** `engine/net/include/o1602/net/protocol.hpp:34-35` (`kMaxSnapshotBytes`); `engine/net/src/host_session.cpp:674-677` (too large: `drop(peer, Closed)`); `engine/net/src/enet_transport.cpp:176-189` (`enet_connect` keeps ENet's default `maximumPacketSize` of 32 MiB, while a snapshot message is the save plus 22 bytes, `protocol.cpp:60-67`)
- **Failure scenario:** A save between 32 MiB − 22 B and 32 MiB passes the host's check, but the client's ENet refuses the fragments (`protocol.c:580`) and the client times out. Above 32 MiB nobody can join or resync, and after a host migration every follower is refused with `Closed`. After a migration the game also goes whole to each of up to 11 clients over a home uplink, which takes minutes, and every `Turn` queues behind it on the single ENet channel.
- **Evidence:** Synthetic worlds (NET): 2000² compresses to 4.3 MiB; 4096² to 9.6–14.1 MiB; the largest LST snapshot was 1.24 MiB. That a real 4096² world reaches 32 MiB is an extrapolation and stays **suspected** until the device run. **Device run with the original data:** the resync snapshot of a real 12-player 2000×2000 late game (600 game minutes) is 2.21 MiB, 6.9 % of the 32 MiB limit; the largest stress snapshot was 2.09 MiB (6.5 %). With real data the concern does not appear; it stays only for dense synthetic or hand-made 4096² worlds. **Re-judged from Medium to Low.** The one-line `maximumPacketSize` fix stays worth doing, and the migration transfer of 2.2 MiB to up to 11 clients takes seconds, not minutes.
- **Solution:** Now: set the client's `maximumPacketSize` to the host's constant (one line). Then: chunked snapshots (MS-7) with a separate, configurable cap on the total size, the same constant as `save_game`'s cap (DS-4); a second ENet channel for snapshots so that `Turn`s for playing peers are not stuck behind them.

#### MS-16 [Low] Lobby messages have no rate limit, and each change rebroadcasts the whole lobby

- **Aliases:** NET-10 · **Dimensions:** D9, D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P2
- **Where:** `engine/net/src/host_session.cpp:357-364,909-956,1016-1043`
- **Failure scenario:** `SetReady`, `TakeSeat` and `SetBanner` toggled in a loop. Each change sets `m_lobby_changed`, so the lobby (up to 64 KiB of setup) goes to every peer on the next host update; `TakeSeat` also sends a `Welcome` and draws a token from a fresh `std::random_device`. The amplification is bounded by the host's update rate. Chat is limited to 5 lines per second; these messages are not.
- **Evidence:** Code reading, confirmed by the verifier.
- **Solution:** A per-peer budget for lobby messages (about 10 per second) and a lobby broadcast at most every 100 ms.

#### MS-17 [Low] Commands beyond four frames' share are dropped without a word; a host-inserted `PlayerControl` can be lost

- **Aliases:** NET-11, SYNC-9 · **Dimensions:** D1, D6, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P2
- **Where:** `engine/net/src/host_session.cpp:236-256` (silent drop at line 240); `:749` (`drop()` adds `PlayerControl` only while `m_open.size() < 1024`); `engine/net/src/client_session.cpp:137-154` (the client sends all of `m_outgoing` in one update)
- **Failure scenario:** A burst of more than share + 4 × share commands within one frame window (425 with 12 players) loses the rest silently. When every player floods, `drop()` can fail to hand a slot to the computer, and the slot stays human with nobody in it. No interface path makes that many commands today (21 units, 512 items, one command per route edit); scripts and mods will.
- **Evidence:** SYNC review test: `of 5000 commands 1280 were carried out; no event tells the player of the rest`.
- **Solution:** Client-side pacing (at most one frame's share per frame); a `Dropped{count}` notice from the host; reserve a few entries per frame for host-inserted commands.

#### MS-18 [Low] The lobby's "kick spectator" button can remove a different member

- **Aliases:** NET-13 · **Dimensions:** D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P2
- **Where:** `engine/app/src/multiplayer_menu.cpp:660-669,902-904` (`m_watchers` holds roster indices from the last layout); `engine/net/src/host_session.cpp:1084-1097` (`kick_member` takes any index)
- **Failure scenario:** A spectator leaves between layout and click; the indices shift and the host kicks the next member, who may be a player.
- **Evidence:** Code reading, confirmed.
- **Solution:** Pass a stable identity (link id, or name and seat as laid out); in `kick_member` check that the entry is still a spectator.

#### MS-19 [Low] `resync_behind` counts frames, not time: at speed 8 a lagging computer gets the whole game after 3.75 s

- **Aliases:** NET-18 · **Dimensions:** D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P8
- **Where:** `engine/net/include/o1602/net/session.hpp:60-62`; `engine/net/src/host_session.cpp:639-643`
- **Failure scenario:** Documented as "30 s at normal speed"; at speed 8 the 600 frames pass in 3.75 s, so a short hiccup at high speed triggers a full snapshot, which on large worlds stalls the host for seconds (TB-26).
- **Evidence:** Code reading, confirmed.
- **Solution:** Scale the threshold by speed, as `ahead_frames()` does, or express it in milliseconds.

#### MS-20 [Low] Pause can be used to grief (design decision for the owner)

- **Aliases:** NET-16 · **Dimensions:** D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** `engine/net/src/host_session.cpp:455-465`; `engine/sim/src/simulation.cpp:436-452`
- **Failure scenario:** Any player may pause and resume 4 times a second, without limit over time, and anyone may resume another player's pause, so one player can keep the game paused for everyone.
- **Evidence:** Code reading.
- **Solution:** A pause budget per player (N pauses per game, as in many RTS games), and resuming only by the player who paused or by the host.

#### MS-21 [Low] The LAN beacon answers any source address (about 16× reflection)

- **Aliases:** NET-17 · **Dimensions:** D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P5
- **Where:** `engine/net/src/lan.cpp:182-188,271-284,473-489`
- **Failure scenario:** A 12-byte search with a spoofed source gets an answer of up to about 190 bytes. Port 16021 binds to every address, so this works from the internet when the port is forwarded; answers are rate-limited to 64 datagrams per update.
- **Evidence:** Code reading.
- **Solution:** Answer only private or link-local sources plus explicit unicast; or pad the search to the answer's size; rate-limit per source.

#### MS-22 [Low] A client of another protocol version is refused as "sent nonsense", not as "another version"

- **Aliases:** SYNC-10 · **Dimensions:** D10, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P4
- **Where:** `engine/net/src/protocol.cpp:378-389` (`decode` rejects trailing bytes); `engine/net/src/host_session.cpp:344-348` (drop with `Invalid`) before `:370-374` (the version check)
- **Failure scenario:** A version-8 client with a longer `Hello` is refused with `RefuseReason::Invalid` ("this computer sent what makes no sense"); an older one with a shorter `Hello` likewise. The version check can only fire while `Hello` keeps its layout. This matters as soon as P4 bumps the protocol.
- **Evidence:** SYNC review test: `refused: this computer sent what makes no sense`.
- **Solution:** Peek the type and the `u16` protocol before the full decode and answer `Refused{Version}`; document that the first three bytes of `Hello` never change.

#### MS-23 [Low] The host holds about three copies of every snapshot per receiving peer

- **Aliases:** NET-V3 · **Dimensions:** D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P5
- **Where:** `engine/net/src/host_session.cpp:678-681`; `engine/net/src/enet_transport.cpp:86`
- **Failure scenario:** `send()` encodes the save into a new vector and `enet_packet_create` copies it again for each peer. After a migration with 11 followers and a 14 MiB game that is about 300 MiB transient.
- **Evidence:** Code reading.
- **Solution:** Encode once per update and send one reference-counted `ENetPacket` to every peer (`Transport::send_shared`).

#### MS-24 [Info] The detached DNS thread of `LanBrowser` can outlive ENet

- **Aliases:** NET-19 · **Dimensions:** D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P5
- **Where:** `engine/net/src/lan.cpp:396-410`
- **Failure scenario:** `enet_address_set_host` runs in a detached thread; on Windows `enet_deinitialize` calls `WSACleanup` while a lookup may still run. Harmless today, because the lookup's `shared_ptr` keeps its data alive.
- **Evidence:** Code reading.
- **Solution:** Acquire and release `EnetLibrary` inside the thread, or join the threads with a timeout on destruction.

#### MS-25 [Info] The successor's IP address goes to every member, spectators included

- **Aliases:** SYNC-14 · **Dimensions:** D9 · **Effort:** S · **Depends on:** MS-2 · **Fix bundle:** P4
- **Where:** `engine/net/src/host_session.cpp:839-851` (`Roster.successor_address`)
- **Failure scenario:** Needed for migration among players, but spectators do not need it.
- **Evidence:** Code reading.
- **Solution:** Send the address only to members with a slot, or only with `HostLeaving` (MS-2).

#### MS-26 [Info] Network decoders reserve the announced element count before checking that the bytes are there

- **Aliases:** DET-8 · **Dimensions:** D4, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P5
- **Where:** `engine/net/src/wire.hpp:41-47` (`count(limit)` checks only the limit), `:58-60` (`commands()`); `engine/net/src/protocol.cpp:219-220` (`m.frames.reserve(n)` with `n ≤ 65 536`), `:238, 260, 272, 296, 307, 315`
- **Failure scenario:** An 8-byte message makes the receiver allocate `limit × sizeof(T)`, about 2 MB for a Turn. Bounded amplification, not a crash.
- **Evidence:** 31.6 M runs of `fuzz_net_message` (20 min) found no OOM; verifier confirmed the code. Not reported by NET.
- **Solution:** In `count()`, also fail when `n × min_encoded_size > remaining()`, or reserve `min(n, remaining / min_size)`.

### Determinism and save/resync (DS)

Divergence between peers or between a game and its reloaded save, and what saves and resync snapshots can hold. Determinism itself held in every measurement: 8 game hours with 29 050 made-up commands gave identical hash sequences for GCC 13 -O3, Clang 18 -O3/-O2, GCC -O0, ASan/UBSan and an FMA build; save → load → continue matched at every checkpoint; valgrind found no uninitialised reads; no floating point reaches a game decision (SYNC).

| ID | Severity | Title | Bundle |
|---|---|---|---|
| DS-1 | High | A sinking ship comes back at full health after a load or resync: false desync | P6 |
| DS-2 | Medium | Cross-platform determinism is guarded by one 90-second golden hash; the replay matrix is not in CI | P27 |
| DS-3 | Medium | Four versioning schemes inside the SIMU state; the AI's refuses every other version | P10 |
| DS-4 | Medium | Large 4096² worlds cannot be saved, resynced or migrated, and the failure comes after seconds of compression | P9 |
| DS-5 | Low | The content fingerprint covers only the raw `.cod` files; data fixes and future mod data are outside it | P2 |
| DS-6 | Low | `write_file_atomic` does not flush the directory after the rename | P9 |
| DS-7 | Info | The volcano's sine table is computed in floating point at compile time | P10 |

#### DS-1 [High] A sinking ship comes back at full health after a load or resync: false desync

- **Aliases:** LST-1, LST-V3 · **Dimensions:** D8, D1, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P6
- **Where:** `engine/sim/src/ships.cpp:117-122` (`Fleet::Fleet` turns `health == 0` into `max_health`, "older files have no health"); `:411-413` (`Fleet::hit` sets health 0 and calls `sink`); `engine/sim/src/save_game.cpp:156` with `engine/sim/src/simulation.cpp:78-85` (`state_readable` builds a probe `Simulation`, and with it a `Fleet`, on the loaded world); `engine/net/src/host_session.cpp:674-679` (a snapshot is taken at the host's current frame); `:513` (a match resets `resyncs`); `engine/net/include/o1602/net/session.hpp:78` (`max_resyncs = 2`)
- **Failure scenario:** A late joiner, a resync or a host migration loads a snapshot taken while a ship is in its sinking phase (3–5 s per sinking ship). The ship loads at full health, so the next fingerprint inside its remaining sink time differs: an extra resync (a stall for everyone), and three mismatches in a row drop the client. Per snapshot taken during a naval fight the chance is about 50–80 % at speed 1 and 5–10 % at speed 8. Behaviour does not diverge (every reader treats 0 as full, `hit` ignores sinking ships) and the hashes agree again once the ship is gone. In single player a save during a battle silently changes the state, so replays recorded before and after differ.
- **Evidence:** LST round trip (`o1602-stress`, thread CPU): G5 with 1 000/5 000/20 000 ships `state hash DIFFERS` 3 of 3; the unpacked saves differ in 3 `u32` fields 0 → 0x0820 (2 080) in `FLEE`; with the default rules, 1 200 ships: `ships sinking 4 (health 0: 4)`, hash differs. Verifier `sink_rt` without tool code: `tick 506: ship 2 sinking, health 0` / `loaded world before Simulation: ship 2 health 1600 sinking 1` / `hash host 4cafbb54c3e916e5 loaded 6cea51681d33525d -> DIFFERS` / `hash after removal: EQUAL` / `hash 100 s later: EQUAL`. `pirates.cpp:164` shows the normalisation was known ("a loaded game would fill 0 to full"); sinking ships were missed. Not Critical: there is no real divergence.
- **Solution:** In `Fleet::Fleet`, normalise only when `!s.sinking`; better, mark old files by section version instead of reading health 0 as "missing". A fix only in the save reader is undone by the probe `Simulation`. Correct the doc comment of `state_readable` ("without running anything"). Unit test in `test_naval.cpp`: sink a ship by `hit`, write, read, compare `state_hash()`.

#### DS-2 [Medium] Cross-platform determinism is guarded by one 90-second golden hash; the replay matrix is not in CI

- **Aliases:** BLD-2, SYNC-13 · **Dimensions:** D8, D10 · **Effort:** M · **Depends on:** — (BT-1 helps) · **Fix bundle:** P27
- **Where:** `tests/unit/test_ai.cpp:357-371` (the only golden value for how a game develops; `tests/unit/test_save_format.cpp:709-911` pins the encoding of every section, not its evolution); `.github/workflows/ci.yml`; proposed `tools/o1602-replay` and `.github/workflows/determinism.yml` on branch `review-tools-sync`
- **Failure scenario:** A Linux player hosts and a Windows player joins. A compiler or standard-library difference in combat, ship movement, pirates, fires or a second computer's planning desyncs the game every few minutes, and each desync costs a full resync. CI is green on five compilers, because the golden scenario is one computer founding one city in 90 s.
- **Evidence:** SYNC (8 game hours, 29 050 random commands): identical hash sequences for GCC 13 -O3, Clang 18 -O3 and -O2, GCC -O0 (2 h), Clang ASan/UBSan (2 h) and an FMA build (`-march=x86-64-v3 -ffp-contract=fast`); save→load→continue identical at every checkpoint; valgrind memcheck clean. Scope (verifier): the synthetic game is small (7 ships, 0 soldiers, 2 cities, 3.2 KB save), so this covers sailing, founding, roads, trade and naval combat, not houses, production, population, land combat, disasters or AI city growth. MSVC and macOS arm64 were not run. **Device, with the original data: no divergence anywhere.** 8 players for 480 game minutes; `check-roundtrip` over 7 919 frames; a recorded multiplayer run replayed from its command log with round trips at 997 identical; all 19 savegames (15 original `.gam`, 4 own `.o1602`) load and replay cleanly; 12 players on 2000×2000 for 600 game minutes. Only Linux x86-64 GCC was run; the cross-platform part of this finding stays open.
- **Solution:** Adopt `determinism.yml` after the verifier's fixes (the `compare` job with `if: ${{ !cancelled() }}`, a current `download-artifact`, 20 game minutes on PRs for the -O0 and ASan jobs, 120 on push). Add 3–4 golden hashes of richer synthetic games, each under 1 s: a land battle between two computers and a human, ships with routes plus a pirate attack, a fire and plague city, a `WorldPlan` world with `settings_for_players(8)`. Add `-ffp-contract=off` to the `sim` target as a guard for arm64. Extend the synthetic world once data-free tests have houses and soldiers (BT-1).

#### DS-3 [Medium] Four versioning schemes inside the SIMU state; the AI's refuses every other version

- **Aliases:** CON-2, SIM-19 · **Dimensions:** D7, D3 · **Effort:** M · **Depends on:** — (do before GB-5 and LC-2) · **Fix bundle:** P10
- **Where:** `engine/sim/src/simulation.cpp:139` (`kVersion = 6`) and `:163` (a second literal 6); `:176` (`Logistics::load` uses the outer version); `engine/sim/src/ships.cpp:1512-1515` (`Fleet` has its own u16); `engine/sim/src/military.cpp:578-604` (no version: optional fields guessed with `at_end()`, which works only because the AI state follows from version 4 on, `simulation.cpp:182-190`); `engine/sim/src/ai/ai_core.cpp:24,620` (exact equality with `kStateVersion`); `bool load()` in `logistics.hpp:229`, `military.hpp:186`, `ai.hpp:53`, `ships.hpp:314`
- **Failure scenario:** The planned AI state version 2 (GB-5) would make every earlier save and resync snapshot fail with "the state of the running game is damaged (SIMU)" unless someone writes a v1 reader. Adding a field to `Military`, or moving the AI state, breaks old saves without a version to tell. The `bool` loaders cannot say what was wrong.
- **Evidence:** Code reading (cross-cutting pass; SIM-19 verified: the `at_end()` tests only work for version-3 files and are always "not at end" from version 4 on, correct by accident).
- **Solution:** Make every persisted subsystem its own section (`LOGI`, `FLET`, `MILI`, `AIST`, `SIMC` for clocks, names and speed) with the section mechanism's u16 version, `max_version`, required flag and "reader accepts 1..max"; `load(in, version)` returns `Result` with a message. Keep reading the old SIMU blob for one transitional version. Add a golden-save corpus test that loads a stored save of every earlier version without original data.

#### DS-4 [Medium] Large 4096² worlds cannot be saved, resynced or migrated, and the failure comes after seconds of compression

- **Aliases:** IO-2, LST-5 · **Dimensions:** D6, D3, D4, D10 · **Effort:** S (order, cap, doc) / M–L (compact MAPW) · **Depends on:** TB-26 (same encoding) · **Fix bundle:** P9
- **Where:** `engine/sim/src/save_game.cpp:27` (`kMaxSize = 1 << 28`, "a game on the largest map needs about 100 MB"); `:79-89` (checked after deflate); `:61` (read); `engine/sim/src/save/map.cpp:88-117` (MAPW: 20 bytes per island tile); `docs/formats/o1602-save.md` (size estimate)
- **Failure scenario:** A hand-made or externally produced 4096² world with more than about 80 % land (about 13.4 M island tiles) runs, but `write_game` fails with "the game is too large" after about 4 s of deflate: no save, no autosave, no resync snapshot, no migration, and the player sees "the file is damaged" (LC-14). The generator stops at 2000² (62 MB of sections without units), so only loaded worlds reach it.
- **Evidence:** Verifier (`exp_save 4096 16 230`): 256 islands, 13.5 M tiles, 258.3 MiB of sections → `the game is too large (after 4.52 s)`, peak RSS 1 084 MB. 9.0 M tiles: 198 MB of sections, 1.4 MB file, write 3.47 s, read 3.27 s, peak RSS 1 001 MB (about 5× the sections). LST G2 4096² with 256 islands of 240²: `SAVE FAILED`, peak RSS 1 197 MB. The "uncompressed copy" in the IO report happens only on the uncompressed path. **Device:** with real content the largest snapshot was 2.09-2.21 MiB and the largest save 2.39 MB; DS-4 concerns only loaded 4096² worlds with mostly land.
- **Solution:** Check the section size before deflate (S). Derive the cap from the header's map size (`w·h·20` plus an entity bound) or raise it to 1 GiB (the reader grows its buffer; a hostile 1 MB file could then make 1 GiB, which is memory pressure, not corruption). Shrink MAPW: run-length or palette-encode the tile layers, or store unchanged template tiles as `TileMode::Differences` does; this also cuts the hash cost (TB-26) and RAM. Decode while inflating instead of holding 3–5 copies. Fix the doc estimate. Test that the largest legal world writes and reads back.

#### DS-5 [Low] The content fingerprint covers only the raw `.cod` files; data fixes and future mod data are outside it

- **Aliases:** SYNC-11, MOD-9 · **Dimensions:** D8, D7, D5 · **Effort:** S / M · **Depends on:** — · **Fix bundle:** P2
- **Where:** `engine/app/src/multiplayer.cpp:26-38` (fingerprint of the raw `haeuser.cod`/`figuren.cod` bytes); `engine/app/src/map_viewer.cpp:208-219` (data fixes applied per computer from `settings.cfg` or `--original-data`); `engine/assets/include/o1602/assets/data_fixes.hpp:6-9` (assumes the simulation never reads the fixed formation); `engine/sim/src/military.cpp:111-113`; `map_viewer.cpp:169-189` and `engine/app/src/message_table.cpp:312-333` (two `mods/` enumerators, each sorting `std::filesystem::path`, which is `wchar_t` order on Windows)
- **Failure scenario:** A crafted `SoldierOrder` with `formation` set reads the fixed or the unfixed formation table depending on each computer's setting: different targets, a desync, resyncs, and after `max_resyncs` the computer is sent away (griefing, not a crash). The first mod that touches simulated data would be read in a different order on different systems and would not be fingerprinted. Today only factions (which travel in `FACT`) and messages (interface only) come from mods.
- **Evidence:** Code reading; the runtime effect is suspected (no data here).
- **Solution:** Now: the host clears `formation` in `commands()`, or the fix switch goes into the fingerprint. Later (P26): fingerprint the parsed tables as the simulation sees them (this would also let language editions with different comments play together, earlier MP-9); one `assets::ModPackages` enumerator sorting by the UTF-8 bytes of folder names; the package list with checksums in the save and the lobby (ADR 0007 §7).

#### DS-6 [Low] `write_file_atomic` does not flush the directory after the rename

- **Aliases:** IO-12 · **Dimensions:** D3, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P9
- **Where:** `engine/core/src/file.cpp:119` (the file is flushed); `:138` (`fs::rename`, no directory flush)
- **Failure scenario:** A power loss right after a save into a new slot can leave the new name absent while the `.tmp` file exists. An existing target keeps either the old or the new file. On Windows `fs::rename` maps to `MoveFileExW` without `MOVEFILE_WRITE_THROUGH`.
- **Evidence:** Code reading, confirmed with the narrower scenario.
- **Solution:** On POSIX `open(dir, O_RDONLY)` and `fsync` after the rename; on Windows `MoveFileEx(..., MOVEFILE_WRITE_THROUGH)`.

#### DS-7 [Info] The volcano's sine table is computed in floating point at compile time

- **Aliases:** SIM-25 · **Dimensions:** D8 · **Effort:** S · **Depends on:** — · **Fix bundle:** P10
- **Where:** `engine/sim/src/volcanoes.cpp:64-87`; render-only doubles in `lava_position`, `lava_ground`, `walker_position`; `std::sqrt` in `world_plan.cpp:859` at generation time (host only)
- **Failure scenario:** None found. Another compiler could in theory round the `constexpr` series differently.
- **Evidence:** Code reading; SYNC found no float in any game decision.
- **Solution:** Pin the table with a `static_assert` on a few entries (for example `kSine[128] == 65536`, `kSine[43]`), or use a literal integer table as `world_plan.cpp` does.

### Tick budget (TB)

The budget is 6.25 ms per tick at speed 8, including p99 and spikes. Numbers are thread CPU time (`o1602-stress`) or the minimum per tick over repeated runs where available; ranges are "author / verifier" when both measured.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| TB-1 | High | Ship and soldier path planning has no per-tick budget: group orders and battles stall the tick for tens of ms to seconds | P11 |
| TB-5 | High | `Combat::update` rebuilds and sorts every fighter every tick, even in peace: 100 000 idle soldiers cost 50-90 ms per tick | P12 |
| TB-7 | High | `unit_in_the_way` scans every soldier in the world for each placed item, up to three times per item, also in the per-frame build preview | P12 |
| TB-8 | High | `Diplomacy::mood` loops over all soldiers once per own small-island city: about 30 ms per call at 100 k soldiers | P12 |
| TB-9 | High | The AI scans all soldiers or all ships in per-tick and per-step paths: 100 000 soldiers cost 8-16 ms per tick in the computers alone (the earlier "about 1 ms" note no longer holds) | P16 |
| TB-10 | High | `plan_landing` walks all soldiers, towers and ships for every scored beach tile: 100-200 ms in one tick at the stress targets | P16 |
| TB-27 | High | Sea-creature scenery scans the whole sea, all ships and all islands inside the simulation tick, all at once every 5 s | P13 |
| TB-2 | Medium | The AI orders whole island armies in one `SoldierOrder`, so planning bursts are reachable without any UI limit | P11 |
| TB-3 | Medium | `in_circle` copies a vector on every call: 96 % of all heap allocations (contradicts earlier AU8) | P11 |
| TB-4 | Medium | O(ships²) scans in the fleet: `ship_ahead`, `plan`, `local_detour`, `launch` and `complain_of_ships` loop over all ships | P11 |
| TB-6 | Medium | `IdIndex` rebuilds with `stable_sort` after any add or removal: 2-6 ms at 100 k soldiers on every tick with a death or a recruit | P12 |
| TB-11 | Medium | Every AI city visit re-surveys the whole island and re-lays out its roads; `tile()` is out of line and costs 11-41 ns | P17 |
| TB-12 | Medium | AI site searches have no per-step bound: one step may walk the whole road list with four `plan_build` calls per position | P17 |
| TB-13 | Medium | AI settlement search: full-island scans per island tested, `plan_build` per beach candidate, and the search goes on without a ship | P17 |
| TB-14 | Medium | No deterministic AI work budget, and all computers' 6-s, 10-s and 30-s timers fire in the same tick | P13 (timer staggering), P17 (budget) |
| TB-19 | Medium | Every construction event rescans its whole island (towers, sea map, combat): large build orders and mass disasters give 30-110 ms ticks | P14 |
| TB-20 | Medium | `running_costs` is O(productions × cities) and runs every 10 s, in AI steps and on every frame of the Kontor city page | P14 |
| TB-26 | Medium | Fingerprints, snapshots and loads re-serialise the whole game on the game thread: on a real 12-player 2000² game 8-9 ms per fingerprint and a 0.11 s save per join or resync on a fast desktop (2-3× that on the minimum machine); seconds only on dense synthetic worlds | P8 |
| TB-28 | Medium | All islands enter their epoch in the same tick (Medium on very large worlds, Low otherwise) | P13 |
| TB-15 | Low | `search_mine_wide` scans the island and walks four directions from every road tile in one step | P17 |
| TB-16 | Low | Repeated `own_ships()` and a linear `note()` in the AI logistics; vectors allocated per step | P16 |
| TB-17 | Low | AI `init_player` evaluates `Diplomacy::mood` for every player slot every hour and on every city loss; `peaceful()` on every develop step | P12 |
| TB-21 | Low | `World::city(island, index)` is a linear scan, also inside the per-tick loop over all cities in city events (earlier SC-14 won't-fix no longer holds) | P14 |
| TB-22 | Low | Logistics, houses and demolish preview do world-wide scans per local event | P14 |
| TB-23 | Low | `Military::grid_build` clears a world-sized grid every tick, even with no soldiers; `natives_tick` scans all soldiers per native city | P12 |
| TB-24 | Low | `recount_disasters` and `advance_disasters` are quadratic; `houses_of` scans all houses | P14 |
| TB-25 | Low | `forget_growth(single)` is an `erase_if` over all growth entries; `wither_scan` scans the whole island every epoch during a drought | P14 |
| TB-18 | Info | AI quick wins: inline `by_sprite`; the hourly re-init causes a burst of surveys | P17 |
| TB-29 | Info | Long sessions are healthy: memory, event buffers and ids stay bounded over 24 game hours, also with the original data | — |

#### Path planning

##### TB-1 [High] Ship and soldier path planning has no per-tick budget: group orders and battles stall the tick for tens of ms to seconds

- **Aliases:** SIM-1, LST-2, LST-3 · **Dimensions:** D2, D6 · **Effort:** M (budget queue), L (A*, flow fields) · **Depends on:** TB-3 (goal test allocation) · **Fix bundle:** P11
- **Where:** Ships: `engine/sim/src/ships.cpp:565-620` (`Fleet::plan`), started from `decide` (`:890-915`) when a course ends; search in `engine/sim/src/sea_paths.cpp:66-251` (`SeaPlanner::plan`, window 161², Dijkstra, a `std::function` goal per settled cell); goal test `ships.cpp:340-370` (`reached()` calls the linear `World::city` and `building_in_reach` per node). Soldiers: `engine/sim/src/soldiers.cpp:385-440` (`Military::plan_to` → `LandPlanner::plan`, fixed 91² window) and `soldiers.cpp:620-729` (`decide`, up to seven chase plans per idle hunter; `kIdleMs = 1000` at `:15` makes idle soldiers re-decide in step).
- **Failure scenario:** Every ship or soldier with `path_done` plans in the same tick. A UI group order (21 units, `ui::UnitSelection::kMaxUnits`) costs about 35-60 ms in one tick; the AI's `attack_orders` sends a whole island army in one order (TB-2); soldiers that become idle together re-plan every second in step. In lockstep every peer stalls. Loading a save does not cause a burst (courses are saved, `save/ships.cpp:84-85`); that sub-claim of SIM-1 is withdrawn.
- **Evidence:** Thread CPU time (LST, `o1602-stress`), one land battle: 1 000 soldiers mean 2.9 ms, max 326 ms (verifier re-run: max 252-314 ms); 3 000 (the default rules' cap for 12 players) mean 6.7 ms, p99 22.8 ms, max 1.08 s; 10 000 mean 16 ms, max 4.9 s; 100 000 mean 136 ms, max 62 s. Ships: 1 000 mean 3.7 ms, p99 10.1 ms, max 0.89 s; 1 200 with default rules max 0.77 s; 5 000 mean 13.5 ms, max 3.4 s; 20 000 mean 129 ms, max 15.6 s. Verifier (`bench_military` on a 100×100 island, min of runs): two 21-unit orders 66.9 ms, 400 soldiers 766 ms, about 1.6-1.9 ms per plan. Callgrind: 98.6 % of instructions in `SeaPlanner::plan` (1 000 ships), 99 % in `LandPlanner::plan` (soldiers). Stack samples (G5 1 k): `SeaPlanner::plan` 20/40, `World::city` 8/40, `std::function` goal 10/40. **Device run with the original data (confirms High):** ships give single ticks of 0.64 s (1 000 ships), 3.1 s (5 000) and 12.7 s (20 000, mean 83 ms); the default rules with 10 000 soldiers and 2 000 ships give one tick of 1.25 s. A 10 000-soldier battle stayed at p99 6.3 ms and max about 7.7 ms, so the soldier spikes of the synthetic runs did not reproduce in that scenario; the High rests on the ship numbers and the 10 k + 2 k default-rules run. The device machine is about 2-3× faster than the minimum machine.
- **Solution:** (1) A deterministic per-tick planning budget: a queue keyed by unit id, drained in the sim tick, N plans or M settled cells per tick; units with `path_done` wait. A plain cap of N plans per tick already removes the spikes and is the first step. (2) Jitter the idle timers by unit id so decisions do not synchronise. (3) Resolve the goal once per plan into a precomputed goal mask or template predicate instead of a `std::function` that calls `World::city` per node. (4) Shrink the window to the bounding box of start and aim plus a margin; A* with an octile heuristic (same path cost, but tie-breaking may change paths, so replays and tests change). (5) Later: a shared flow field per group target, cached sea routes between Kontors, reuse of the last plan when the target moved less than a tile.

##### TB-2 [Medium] The AI orders whole island armies in one `SoldierOrder`, so planning bursts are reachable without any UI limit

- **Aliases:** SIM-V1 · **Dimensions:** D2, D6 · **Effort:** S · **Depends on:** TB-1 · **Fix bundle:** P11
- **Where:** `engine/sim/src/ai/ai_military.cpp:1039-1046` (`attack_orders`, branch with no hostile city: `need = enemy_count + enemy_count/4`, all of `mine` up to `need` in one order, not split into groups of 21 as the UI does)
- **Failure scenario:** In a large war a computer sends hundreds of soldiers in one tick; each needs a `LandPlanner` plan of about 1.6 ms (TB-1). The spike hits every peer in lockstep.
- **Evidence:** Code reading plus the TB-1 measurement: 400 soldiers gave a 766 ms tick (`bench_military 400 100`).
- **Solution:** Covered by the TB-1 planning budget. Stop-gap: the AI orders in chunks of 21 over successive steps.

##### TB-3 [Medium] `in_circle` copies a vector on every call: 96 % of all heap allocations (contradicts earlier AU8)

- **Aliases:** LST-7 · **Dimensions:** D4, D2, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P11
- **Where:** `engine/sim/src/ships.cpp:58-64`: `const std::vector<std::uint8_t>& circle = radius == kReach ? kCircle5 : circle_half_widths(radius);` The second operand is a prvalue, so the whole conditional is a prvalue and `kCircle5` is copied into a temporary on every call. Same pattern, per build plan only, at `construction.cpp:956`.
- **Failure scenario:** Called per node in ship goal tests and reach checks: millions of allocations per game hour, up to 54.7 M in one tick at 20 000 ships.
- **Evidence:** heaptrack, 60 game minutes of the LST T4 mix: 64.5 M allocations, 62.05 M from `in_circle`. G5 1 k ships: 175 k allocations per tick on average. The verifier's `cond.cpp` shows 1 000 allocations for 1 000 calls with `radius == kReach`.
- **Solution:** Use `circle_table(static_cast<std::uint8_t>(radius))`, which exists for exactly this purpose (`engine/sim/include/o1602/sim/pathfinding.hpp:44-45`, "no allocation in the tick"), or two branches. Add a micro-benchmark with an allocation counter.

##### TB-4 [Medium] O(ships²) scans in the fleet: `ship_ahead`, `plan`, `local_detour`, `launch` and `complain_of_ships` loop over all ships

- **Aliases:** SIM-5 · **Dimensions:** D2, D6 · **Effort:** M · **Depends on:** TB-1 · **Fix bundle:** P11
- **Where:** `engine/sim/src/ships.cpp:586` (obstacle list in `plan`), `:651` (`local_detour`), `:741` (`complain_of_ships`), `:846-863` (`ship_ahead`, per decide of a moving ship), `:254` (`fleet_size`), `engine/sim/src/shipyards.cpp:320` (`launch`); `ships.cpp:672` (`update_island` loops over all islands per decide)
- **Failure scenario:** At 20 000 ships every moving ship's decide costs O(20 k); with about a quarter deciding per tick that is about 1e8 comparisons per tick. Hidden under TB-1 today, it remains once TB-1 is fixed.
- **Evidence:** Suspected magnitude from the code; confirmed by the verifier from the code, not measured. At 1 000 ships callgrind shows it as negligible (`local_detour` 0.7 %).
- **Solution:** One spatial hash of ships per tile bucket (for example 8×8 tiles), updated on every tile change. Look up islands through an island-id map (the SeaMap could hold the island number per tile).

#### Combat and units

##### TB-5 [High] `Combat::update` rebuilds and sorts every fighter every tick, even in peace: 100 000 idle soldiers cost 50-90 ms per tick

- **Aliases:** SIM-2, LST-9 · **Dimensions:** D2, D6 · **Effort:** M · **Depends on:** TB-6 · **Fix bundle:** P12
- **Where:** `engine/sim/src/combat.cpp:227-300` (`Combat::update`: pushes every soldier and tower, `stable_sort` of an id index at `:241-246`, bucket sort at `:283-288`; the `m_enemies` check at `:262-272` comes after both sorts); `engine/sim/src/military.cpp:55` (`Military::tick`: per-soldier `World::soldier` lookups through `IdIndex`) and `soldiers.cpp:50` (`grid_build`)
- **Failure scenario:** A large endless game with 100 k soldiers in peace already uses 8-15× the 6.25 ms budget; war makes it worse.
- **Evidence:** `bench_military 100000 400 60 0` (no war, no orders): total p50 52 ms (Combat::update 29 ms, Military::tick 23 ms); verifier re-run on a busier box 89.8 ms (49.5 + 41.2). LST thread CPU: combat step mean 64 ms at 100 k soldiers in one battle, 55 ms spread, 22 ms at 20 k ships, 3.8 ms (43 % of the tick) at 3 000 soldiers. Callgrind at 20 k idle: the two sorts about 60 % of instructions, `IdIndex::find` about 20 %, `grid_build` 5 %. **Device run with the original data (confirms High):** 50 000 and 100 000 soldiers cost 34-40 ms per tick on average on a fast desktop (Ryzen 9 7900X3D), about 70-120 ms on the minimum machine.
- **Solution:** (1) Compute `m_hostile`/`m_enemies` first and return early when no player present has an enemy; clearing `m_entries`, `m_index` and `m_first` is enough (`fighter()` reads the world; `hostiles()` only needs `list_index` to miss). (2) Keep the buckets incrementally (a persistent grid like `Military`'s unit grid). (3) Pass indices through the tick instead of looking each id up again; or evaluate combat every N ticks per fighter (the original fires on timers).

##### TB-7 [High] `unit_in_the_way` scans every soldier in the world for each placed item, up to three times per item, also in the per-frame build preview

- **Aliases:** SIM-4 · **Dimensions:** D2, D6 · **Effort:** S · **Depends on:** TB-9 (census), or a persistent grid · **Fix bundle:** P12
- **Where:** `engine/sim/src/construction.cpp:259-273` (`unit_in_the_way`), called from `can_place` (`:321-324`) for every kind except road, ground, ruin, square, bridge, beach ruin and pier; `road_rotation` (`:398-399`) calls `can_place` twice more per building with a door; `edge_rotation` per coast candidate; `plan_build` is also the build preview, called on every frame in Build mode without a cache (`engine/app/src/map_viewer.cpp:1153-1156`)
- **Failure scenario:** A 512-item forest or field drag (the UI cap, `build_tool.cpp:14`) costs about 1 s per preview frame and per command at 100 k soldiers, about 55 ms at 10 k soldiers. A crafted 4096-item command costs about 20 s on every peer.
- **Evidence:** `bench_loops`, 12 288 calls: 10 000 soldiers 1.37 s (0.11 ms per call); 100 000 soldiers 20.8-25.3 s (1.7-2.1 ms per call). The soldier records are 256 B, so the loop is memory bound.
- **Solution:** Query a spatial index of soldiers limited to the footprint cells. `Military`'s 16-cell grid is only valid inside `Military::tick` (`m_grid_data` is reset after it), so either keep that grid valid between ticks or add a per-island soldier list (the census of TB-9 can serve). Cache the preview per (tool, origin, footprint) in APP.

##### TB-8 [High] `Diplomacy::mood` loops over all soldiers once per own small-island city: about 30 ms per call at 100 k soldiers

- **Aliases:** SIM-6 · **Dimensions:** D2, D6 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P12
- **Where:** `engine/sim/src/diplomacy.cpp:452-485` (loop over `world.cities()` × (`world.cities()` + `world.soldiers()`) when `minds_territory`). Callers: AI diplomacy `ai_military.cpp:1129` (once per human partner every 30 s, all computers in the same tick, TB-14), AI init `ai_core.cpp:484-486` (TB-17), `pirates_reconsider` (every 5 s), price functions, `pirates.cpp:288`, and the UI diplomacy panel on every refresh (`diplomacy_panel.cpp:132, 349-354`, FB-4).
- **Failure scenario:** 12 players, computers owning many small-island cities, 100 k soldiers: every 30 s all computers run diplomacy in one tick, 8 computers × 4 humans = 32 calls ≈ 1 s.
- **Evidence:** `bench_loops`, 121 islands, 241 cities: 26.6-31.8 ms per call (author), 32.4 ms (verifier) at 100 000 soldiers; 1.5 ms at 10 000.
- **Solution:** Per-island owner bitmasks of soldiers and of cities, rebuilt deterministically once per tick from the soldier list (or kept incrementally); `mood` becomes O(own cities). The same table serves TB-9.

##### TB-6 [Medium] `IdIndex` rebuilds with `stable_sort` after any add or removal: 2-6 ms at 100 k soldiers on every tick with a death or a recruit

- **Aliases:** SIM-11 · **Dimensions:** D2, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P12
- **Where:** `engine/sim/include/o1602/sim/id_index.hpp:37` (lazy rebuild), `engine/sim/src/world.cpp:110-117`; deaths erased in `engine/sim/src/military.cpp:71` every tick; `add_soldier` changes size and `next_id` as well
- **Failure scenario:** In any battle a soldier dies on most ticks, and castles and natives add soldiers regularly. The next `World::soldier()` rebuilds the whole index.
- **Evidence:** First lookup after an erase: 1.96-6.6 ms (author), 3.2 ms (verifier) at 100 000 soldiers.
- **Solution:** Keep a dense id → slot table and patch it on erase and add. A binary search over the vector itself only works if the vector is sorted by id, which does not hold after an original-format load (`load_soldiers` adds in record order with slot-derived ids); sorting at load would change iteration order and replays of old saves.

##### TB-17 [Low] AI `init_player` evaluates `Diplomacy::mood` for every player slot every hour and on every city loss; `peaceful()` on every develop step

- **Aliases:** AI-V1 · **Dimensions:** D2, D6 · **Effort:** S · **Depends on:** TB-8 · **Fix bundle:** P12
- **Where:** `engine/sim/src/ai/ai_core.cpp:484-486` (`previous_mood` loop over all players, including itself, computers and factions); `:391-393` (re-init every `kReinitMs` = 1 h, all computers within seconds of each other); `:336-341` (`check_city_loss` re-inits on every lost city); `ai_planner.cpp:61-79` (`peaceful()` per develop step of a `kPassive` scenario computer)
- **Failure scenario:** Each re-init makes 12 `mood` calls, about 0.4 s at 100 k soldiers; cities change hands often in a war. `previous_mood` is only read for human partners (`ai_military.cpp:1129-1131`).
- **Evidence:** Code reading; cost per call from TB-8.
- **Solution:** Compute `previous_mood` only for human partners; check the cheap treaty condition first in `peaceful()` and evaluate `mood` at most once per diplomacy round. TB-8 removes most of the cost.

##### TB-23 [Low] `Military::grid_build` clears a world-sized grid every tick, even with no soldiers; `natives_tick` scans all soldiers per native city

- **Aliases:** SIM-12, LST-14 · **Dimensions:** D2, D4 · **Effort:** S · **Depends on:** TB-8 (per-island counts) · **Fix bundle:** P12
- **Where:** `engine/sim/src/soldiers.cpp:50-67` (`grid_build`: one vector per 16-cell square, about 263 k at 4096²); `engine/sim/src/castles.cpp:302` (`natives_tick`, every 10 s from `cities_step`)
- **Failure scenario:** A 4096² world pays for the grid clear on every tick with few or no soldiers.
- **Evidence:** Verifier `grid.cpp`: 0.53 ms p50 per tick at 4096² with no soldiers (about 8 % of the budget); LST: soldiers step 0.38-0.5 ms with zero soldiers on 4096² (30 % of that tick), 0.1 ms at 2000².
- **Solution:** Skip when there are no soldiers; clear only the squares used last tick (keep a list). Per-island soldier lists for the natives.

#### Computer players

##### TB-9 [High] The AI scans all soldiers or all ships in per-tick and per-step paths: 100 000 soldiers cost 8-16 ms per tick in the computers alone (the earlier "about 1 ms" note no longer holds)

- **Aliases:** AI-2 · **Dimensions:** D2, D6 · **Effort:** M · **Depends on:** BT-1 (AI test arena) · **Fix bundle:** P16
- **Where:** Per tick: `island_controller` (`engine/sim/src/ai/ai_military.cpp:1059-1089`, `soldiers_on_island` per computer), `island_enemies` (`:888-907`), `peacetime_orders` (`:909-961`), `attack_orders` (`:963-1057`). Per step (50 ms per computer): `target_scan` (`:845-883`) → `attack_ok` (`:777-796`: `city_defenceless` full-island scan, world-wide `building_count`, two soldier passes). Every 6 s: `recruit` (`:324-467`) → `army_counts` (`:36-65`) once per human, `guard` (`:248-278`). Every 5.1 s: `threatened` (`ai_world.cpp:358-378`). Every 10 s: `own_ships` (`ai_settle.cpp:81-89`, 5-10 calls per pass), `update_fleet_target` (`ai_logistics.cpp:546`), `stronger` (`ai_military.cpp:121-133`).
- **Failure scenario:** A late game near the targets (100 k soldiers, 20 k ships) with 11 computers. With 160 or more islands every tick has a due island in the controller: 11 soldier passes ≈ 8 ms. During a war `target_scan` adds 2 × 11 passes ≈ 16 ms per tick.
- **Evidence:** `bench_engine 100 100000 20000` (verifier re-run): `soldiers_on_island` 0.71-0.79 ms, `weighted_army` 0.79-0.83 ms, `own_ships` 59-87 µs. `sizeof(Soldier)` 256 B, `sizeof(Ship)` 1 056 B, so every pass is memory bound. The author's end-to-end p99/max figures are single wall-clock runs and are not used. **Device run with the original data:** in normal 12-player games (no stress-target unit counts) the computers take 65.5 % of the tick but only 178 µs on average and at most 3.6 ms; the mean grows from 53 to 430 µs over 240 game minutes. This does not exercise 100 000 soldiers, so the High at the stress targets stands; in normal games the AI is within budget.
- **Solution:** (1) A soldier census once per tick at the start of `Engine::tick` (or kept by `Military`): `count[owner][island]`, weighted army per owner, per-island index lists; `soldiers_on_island` and `army_counts` become O(1), `soldiers_near` uses squares. TB-8 needs the same table. (2) An `own_ships` list per owner and an id → note index. (3) Cache `city_defenceless` and `building_count` per city from construction events, or test the cheap conditions first.

##### TB-10 [High] `plan_landing` walks all soldiers, towers and ships for every scored beach tile: 100-200 ms in one tick at the stress targets

- **Aliases:** AI-3 · **Dimensions:** D2, D6 · **Effort:** S · **Depends on:** TB-9 · **Fix bundle:** P16
- **Where:** `engine/sim/src/ai/ai_military.cpp:663-744`; loops at `:697-737` over `world.soldiers()` (`:714`), `world.towers()` (`:720`), `world.ships()` (`:727`) with `diplomacy().hostile()` each
- **Failure scenario:** A computer's troop ship reaches mission 6 (attack); the logistics pass evaluates the beach tiles within 24 tiles of a target market (typically 50-150, `:708`). Several computers attacking in the same tick add up. High only at the stress targets; at 2 000 soldiers and 500 ships it is about 0.5 ms per landing and harmless.
- **Evidence:** Verifier: 0.85-1.5 ms per beach tile at 100 k soldiers and 20 k ships; 4.7 µs per tile at 2 000 soldiers and 500 ships. The report's "a few hundred beach tiles" and "45 µs per tile" are withdrawn. The soldier loop already tests `o.island == island` before `hostile()`; the cost is the walk itself.
- **Solution:** Gather the hostile soldiers on the island, hostile towers on it and hostile ships within the island rectangle plus 16 tiles once (from the TB-9 census); score with a coarse danger grid so the cost is O(beach + units); optionally spread the evaluation over logistics passes.

##### TB-11 [Medium] Every AI city visit re-surveys the whole island and re-lays out its roads; `tile()` is out of line and costs 11-41 ns

- **Aliases:** AI-1 · **Dimensions:** D2, D4, D6 · **Effort:** M · **Depends on:** BT-1 · **Fix bundle:** P17
- **Where:** `engine/sim/src/ai/ai_core.cpp:421` (root clears `kSurveyed` on every city selection), `:449-457` (pushes kSurvey and kLayout); `ai_build.cpp:1064-1115` (`task_survey`); `ai_build.cpp:1053-1059` → `ai_sites.cpp:136-236` (`build_segments`, two whole-island passes); `ai_planner.cpp:419-424` (`nothing_to_do` zeroes the budget with more than one city); `ai_planner.cpp:959-961` (`free_land` at stage 0); `ai_world.cpp:25-60` (`tile()`)
- **Failure scenario:** A computer with several cities re-surveys and re-lays-out an island about every 4 steps (200 ms). Late-game estimate: about 3 island passes and 30 k `tile()` calls per visit, 0.3-0.5 ms; with 11 computers at most 2.75 visits per tick, so 0.8-1.4 ms per tick on average (13-22 % of the budget). Islands from the original templates are at most 100-120 tiles wide; the 200×200 figures apply to mods only. The original's root (0x00429070) does the same.
- **Evidence:** `bench_engine`: `tile()` over 100×100 233-409 µs; `build_segments` 0.5-1.1 ms at 100×100. Founding run, 11 computers, minimum per tick over 6 runs: whole computers step mean 80 µs, p99 0.49 ms. The callgrind share of `free_land` (50 %) is an artefact of the synthetic table (no houses, every city stays at stage 0) and is withdrawn; `by_sprite` (not inlined) is 24 % of the AI's instructions. **Device (original data, 12 players, 240 game minutes):** computers 65.5 % of the tick, mean 178 µs, max 3.6 ms on a fast desktop (about 7-11 ms on the minimum machine). No profile could be taken (no perf; valgrind fails with SIGILL), so the share of the survey is unknown. Medium stands.
- **Solution:** (1) Keep survey results incrementally from construction events; re-run task 8 only after init/load or as a periodic check. (2) Rebuild `segments` only when a per-city road-change counter moves. (3) Cache `r.land`. (4) Inline `BuildingTable::by_sprite` and skip `sub_tile` when only the kind is needed (cheapest first step). (5) Spread the remaining scans with a row cursor as `sweep()` does. Test: survey counts equal a full re-scan after random builds and demolitions.

##### TB-12 [Medium] AI site searches have no per-step bound: one step may walk the whole road list with four `plan_build` calls per position

- **Aliases:** AI-4 · **Dimensions:** D2, D4 · **Effort:** M · **Depends on:** BT-1 · **Fix bundle:** P17
- **Where:** The `for (;;)` loops in `engine/sim/src/ai/ai_sites.cpp`: `search_houses` :329, `search_workshop` :436, `search_stonemason` :505, `search_farm` :557, `search_grain` :664, `search_market` :794, `search_public` :897 (plus a loop over all houses of the world per candidate, `:911-920`), `search_military` :948, `search_denkmal` :1007, `search_pirate_houses` :1047, `search_shipyard` :1293; `can_place` (`:282-308`) builds a `BuildOrder` and `BuildPlan` per call; `rings(...)` with 16 rings (`ai_engine.hpp:439-479`)
- **Failure scenario:** In a built-up city with no free site, one step walks road tiles × 4 × `plan_build`, for example 1 000 × 4 × 1-2 µs ≈ 4-8 ms in one tick for one computer.
- **Evidence:** `bench_engine`: 16 rings around a 2×2 site 17-77 µs; `can_place` 0.3-6.3 µs. The worst case was not measured with real data (device run AI-2). The "1.3-3.3 ms per tick on average" is an upper bound with all computers searching. **Device:** the AI profile (device run AI-2) could not be run (no perf, valgrind SIGILL); the worst case is still not measured with real data. The whole AI stays at max 3.6 ms per tick with 12 players on the device.
- **Solution:** A deterministic work cap per step (N cursor positions or N `plan_build` calls); the cursor is already kept in `p.search`, so returning early is safe. Summed-area tables for ring scores (as `AreaCount` in `ai_settle.cpp:43-77`). Houses indexed per city for `search_public`. Reuse one `BuildOrder`/`BuildPlan`.

##### TB-13 [Medium] AI settlement search: full-island scans per island tested, `plan_build` per beach candidate, and the search goes on without a ship

- **Aliases:** AI-6 · **Dimensions:** D2, D1 · **Effort:** S-M · **Depends on:** BT-1 · **Fix bundle:** P17
- **Where:** `engine/sim/src/ai/ai_settle.cpp:203-296` (`task_settle_area`: `tile()` per tile, three width × height arrays); `:301-440` (`task_settle_site`: two `AreaCount` per region, up to 9 `plan_build` per beach tile; returns at `:425-428` without a ship and the search continues); `:122-198` with `ai_core.cpp:362-389` (`pick_island`: 30 passes with `need` 1 800 → 300, 30 more after dropping the crop)
- **Failure scenario:** A late-game colony search with all islands taken, or a computer whose settlement ship sank, burns full-island scans for minutes. Its cities are slowed, not stopped: one city visit per pass over all islands.
- **Evidence:** Verifier, minimum per tick over 5 runs, `bench_nosite 100 11 0 10` (no site ever fits): mean 0.56 ms, p99 6.9 ms, max 11.1 ms, 1 167 ticks over 2 ms; one computer: max 1.0 ms. `task_settle_area` alone 3.2 ms on 200×200. **Device:** with real data the whole AI stays at max 3.6 ms per tick (12 players, 240 game minutes); the no-site worst case was not reproduced there.
- **Solution:** Keep a per-island free-land counter and skip islands below `need` without scanning; stop the search when `settle_ship` and `own_ships` are empty; test beach-run and depth before `plan_build`; build the `AreaCount`s once per island test.

##### TB-14 [Medium] No deterministic AI work budget, and all computers' 6-s, 10-s and 30-s timers fire in the same tick

- **Aliases:** AI-7, SIM-V3 · **Dimensions:** D2, D8 · **Effort:** S (stagger), M (budget) · **Depends on:** TB-11, TB-12, TB-13 for the budget · **Fix bundle:** P13 (timer staggering), P17 (budget)
- **Where:** `engine/sim/src/ai/ai_core.cpp:154-160` (every computer steps every tick); `:161-196` (timers); `init_player` sets `t_6s = 3000` and `t_10s = 20000` for every computer and `diplomacy_ms` starts at 0 for all (`:190-195`), while `t_main` is randomised (`:479`); `step()` always appends `target_scan` and `sweep`
- **Failure scenario:** All computers run `recruit` in the same tick every 6 s, `logistics` every 10 s and diplomacy (`Diplomacy::mood` per human, TB-8) every 30 s, until a re-init shifts one. Heavy steps of different computers are never kept apart, so p99 and max grow with the number of computers.
- **Evidence:** Code reading. Diplomacy round at 100 k soldiers with 8 computers and 4 humans: 32 `mood` calls ≈ 1 s (TB-8). The no-site case (TB-13) shows p99 6.9 ms after removing preemption. The report's "p99 4.2-9.7 ms" is mostly preemption and is not used. **Device (original data):** computers 65.5 % of the tick with 12 players (60.2 % with 8), max 3.6 ms on a fast desktop; the mean grows from 53 to 430 µs over 240 game minutes. The timer alignment remains the main source of the maximum; Medium stands.
- **Solution:** (1) Stagger the timers deterministically per player (for example `t_10s = 20000 + 10000 * k / n`; `diplomacy_ms` from the AI random stream as `t_main`). Cheap, do first. (2) After TB-11, TB-12, TB-13: express heavy operations in work units and give the engine a fixed per-tick budget shared round-robin starting at `tick % n`. (3) `o1602-tool bench` prints the computers' p99 per step, measured as in BT-4.

##### TB-15 [Low] `search_mine_wide` scans the island and walks four directions from every road tile in one step

- **Aliases:** AI-5 · **Dimensions:** D2 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P17
- **Where:** `engine/sim/src/ai/ai_sites.cpp:1401-1451`, from `search_mine` (`:1360-1362`) when the 7-tile walk found nothing (non-classic rules); `mountain_walk` (`:1132-1185`); retried after `road_three` resets the attempts (`ai_build.cpp:290-296`)
- **Failure scenario:** A city without a reachable hill asks for a quarry or mine; the step that ends the road walk does the full scan. Rare and limited by the attempts.
- **Evidence:** Verifier `bench_v`, min of 5: 9.0 ms on a fully owned 100×100 island with empty blocks (the author's case), 4.6 ms built up, 0.63-0.90 ms for realistic 60×60-70×70 cities. 200×200 islands do not exist with the original templates.
- **Solution:** Spread the scan over steps with a cursor (simplest), or precompute per island the slope tiles with rock behind them and BFS back to the nearest own road.

##### TB-16 [Low] Repeated `own_ships()` and a linear `note()` in the AI logistics; vectors allocated per step

- **Aliases:** AI-15 · **Dimensions:** D4 · **Effort:** S · **Depends on:** TB-9 · **Fix bundle:** P16
- **Where:** `engine/sim/src/ai/ai_settle.cpp:81-89` (`own_ships` allocates and scans all ships per call); `ai_logistics.cpp:28-38` (`note()` linear inside loops over own ships, O(own²), and it appends to the list it is looked up in); per-call vectors in `buy_settings`, `sell_settings`, `production_switches`, `home_supply_route`, `fill_fields`, `search_shipyard`, `can_place`
- **Failure scenario:** A computer with hundreds of ships makes its logistics pass quadratic in its fleet.
- **Evidence:** Code reading, confirmed by the verifier.
- **Solution:** The per-owner ship list and id → note index of TB-9; a scratch arena in the engine.

##### TB-18 [Info] AI quick wins: inline `by_sprite`; the hourly re-init causes a burst of surveys

- **Aliases:** AI-18 · **Dimensions:** D4, D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P17
- **Where:** `BuildingTable::by_sprite` (out of line, 24 % of the AI's instructions); `engine/sim/src/ai/ai_core.cpp:391-393` (hourly re-initialisation, a kept quirk of the original, resets every city to stage 0 and empties `posts`, `segments` and counts); `ai_settle.cpp:446` (`free_colony_cargo` ignores `p`)
- **Failure scenario:** After the hourly re-init every computer surveys, lays out and runs stage-0 `free_land` for each city within the next visits.
- **Evidence:** Callgrind profile of the AI part; code reading.
- **Solution:** Make `by_sprite` inline in `building_table.hpp` (one line). Stagger re-inits with the timers of TB-14.

#### Construction and cities

##### TB-19 [Medium] Every construction event rescans its whole island (towers, sea map, combat): large build orders and mass disasters give 30-110 ms ticks

- **Aliases:** SIM-3, LST-8, LST-11 · **Dimensions:** D2, D6 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P14
- **Where:** `engine/sim/src/simulation.cpp:484-513` (`Simulation::handle`: for every event of any kind, including `StorageChanged` and `CityFounded`, `m_military.update_towers(island)`; for Placed/Removed also `m_fleet.island_changed` → `SeaMap::update_island` and `m_combat.island_changed`); `engine/sim/src/military.cpp:244-290` (`update_towers`: full island scan plus an `erase_if` over all towers of the world); `engine/sim/src/sea_paths.cpp:48-60`
- **Failure scenario:** A 512-item field order (UI cap) on a large original island gives about 512-1000 events: 30-70 ms in one tick on every peer. A crafted 4096-item command gives about 8 192 events. Fires and eruptions destroying hundreds of buildings in one tick go through the same path (LST G7: max 107 ms CPU).
- **Evidence:** Per event, `bench_loops`, min of 3: 0.05 ms (100×100), 0.07 ms (120×120), 0.55 ms (200×200), 1.93 ms (500×500, hand-made worlds only). LST per placed building: 0.12 ms (40²), 0.78 ms (120²), 2.6 ms (250²). G7 disasters: CPU mean 1.6 ms, p99 19.9 ms, max 107 ms. Part of LST's G1 stack samples came from the synthetic abandon loop (see Refuted) and is not used. The "seconds" scale in SIM-3 assumed 4096 items on 500×500 and is reduced accordingly.
- **Solution:** Collect the touched islands and a dirty rectangle per island over the whole event batch of a tick; run `update_towers` once per island and only when a tower or city border changed; run `SeaMap`/`LandMap` updates once per island over the dirty rectangle (plus the tower radius); ignore events that change neither coast nor towers.

##### TB-20 [Medium] `running_costs` is O(productions × cities) and runs every 10 s, in AI steps and on every frame of the Kontor city page

- **Aliases:** SIM-8, AI-9 · **Dimensions:** D2 · **Effort:** S · **Depends on:** TB-21 · **Fix bundle:** P14
- **Where:** `engine/sim/src/consumption.cpp:225-271` (linear `world.city()` per production and per cost building; a vector allocated per call). Callers: `advance_cities` (`consumption.cpp:388`, every `kCityTickMs` = 10 s); AI `net_income` (`ai_planner.cpp:426`, from `luxury` `:1851` and `ai_settle.cpp:795`) and `incomes_ok` (`:438`, from `:1028`); the HUD's `player_figures` once a second (`map_viewer.cpp:1293-1296`); the Kontor city page on every refresh (`selection_panels.cpp:621`, FB-4); `player_balance` when a human is at ≤ -1001.
- **Failure scenario:** 12 players with many cities: a periodic one-tick spike every 10 s, plus AI calls in develop steps, plus a per-frame cost while the city page is open.
- **Evidence:** Verifier `bench_rc` (250 islands, 500 cities): 60 000 productions p50 25 ms, p99 38 ms; 10 000 productions 5.9 ms.
- **Solution:** An (island, index) → slot table for cities (TB-21); compute per-city running costs once per tick or incrementally and let the AI and UI read the cache.

##### TB-21 [Low] `World::city(island, index)` is a linear scan, also inside the per-tick loop over all cities in city events (earlier SC-14 won't-fix no longer holds)

- **Aliases:** SIM-20, LST-13 · **Dimensions:** D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P14
- **Where:** `engine/sim/src/world.cpp:58-74` (`World::city`); callers `advance_houses` per due house, `update_towers` per tower, `running_costs` per building (TB-20), the ship goal test (TB-1), and `engine/sim/src/disasters.cpp:479-484` (`world.city(c.island, c.index)` inside `for i in cities`, every tick)
- **Failure scenario:** With hundreds of cities this multiplies several hot paths; the city-events step is quadratic in cities.
- **Evidence:** LST: city-events step mean 1.09 ms at 1 444 cities, 89 ms (97 % of the tick) at 10 000 cities (only reachable once more than 256 islands load, LC-2).
- **Solution:** A lookup table by (island, index). In `disasters.cpp:479-484` iterate `world.cities()` by reference.

##### TB-22 [Low] Logistics, houses and demolish preview do world-wide scans per local event

- **Aliases:** SIM-9 · **Dimensions:** D2, D6 · **Effort:** M · **Depends on:** — · **Fix bundle:** P14
- **Where:** `engine/sim/src/logistics.cpp:1009` (`building_removed`, all walkers per removed production), `:1094` (`deer_at`), `:1394` (`call_hunter`), `:1768` (`spawn_robber`), `:1792` (`find_victim`); `engine/sim/src/houses.cpp:527` (`update_services(island)`, once per build command in `Builder::finish`, `construction.cpp:1793-1795`); `construction.cpp:2015-2030` (`plan_demolish` Kontor check over all productions, houses and cost buildings); `construction.cpp:949-1004` (`territory_coverage`)
- **Failure scenario:** Large worlds: each local change costs O(world). Spikes for mass removals (volcano, city lost). Not quadratic in normal play.
- **Evidence:** Code reading; the triggers are rarer than first reported (per command, not per item), hence Low.
- **Solution:** Index walkers by production and island; let `update_services` take the dirty city or rectangle; index productions by island.

##### TB-24 [Low] `recount_disasters` and `advance_disasters` are quadratic; `houses_of` scans all houses

- **Aliases:** SIM-17 · **Dimensions:** D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P14
- **Where:** `engine/sim/src/disasters.cpp` (`recount_disasters` O(cities²) + O(disasters × cities) per start or end; `advance_disasters` finds by id in O(D²); `houses_of` at `:292`)
- **Failure scenario:** Many cities with simultaneous fires and plague give a small spike; the events are rare.
- **Evidence:** Code reading.
- **Solution:** Per-city counters; houses indexed by city.

##### TB-25 [Low] `forget_growth(single)` is an `erase_if` over all growth entries; `wither_scan` scans the whole island every epoch during a drought

- **Aliases:** SIM-18 · **Dimensions:** D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P14
- **Where:** `engine/sim/src/growth.cpp:221-224` (single-tile overload; the batch overload at `:228` is already linear with a binary search); `engine/sim/src/island_events.cpp` (`wither_scan`)
- **Failure scenario:** Large islands under drought give periodic spikes.
- **Evidence:** Code reading.
- **Solution:** Index growth entries by position; spread the scan over the epoch.

#### State hash, snapshots and periodic work

##### TB-27 [High] Sea-creature scenery scans the whole sea, all ships and all islands inside the simulation tick, all at once every 5 s

- **Aliases:** RND-1, RND-V1 · **Dimensions:** D2, D4, D6 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P13
- **Where:** `engine/render/src/ambient.cpp:198` (`every(kSeaPeriodMs, 0, ...)`: the whole world at phase 0), `:542-549` (`sea_creatures`, one RNG stream per 28×28 block), `:553ff` (`sea_block`: all ships `:567-582` and all islands `:586-597` per try), `:230` (`city_round` loops all soldiers per city); registered as tick observer at `engine/app/src/map_viewer.cpp:419-421` and `:2329`, called inside the tick at `engine/sim/src/simulation.cpp:586-587`
- **Failure scenario:** A big world at speed 8: every 5 s of game time one tick takes tens to hundreds of ms on every peer. A player setting `sea_creatures_every = 5` (allowed, `app_settings.cpp:128`) makes the scan about 12× larger.
- **Evidence:** `bench_render`, worst `Ambient::tick` over 10 s of game time (author / verifier): 4096² with 20 k ships 217.9 / 113.9 ms; 2000² with 20 k ships 45.8 / 58.8 ms; 4096² with 2 k ships 9.9 / 5.8 ms; 4096² with no ships 2.95 / 5.2-10.3 ms. The block and island loops alone exceed the budget on 4096², so a ship grid alone does not fix it. **Device:** not isolated; the 2000² network run's maximum tick was 18.6 ms, of which ships 16.6 ms, with real ship counts far below 20 000.
- **Solution:** Keep the design (the comment at `map_viewer.cpp:417` says the scenery is decided tick by tick, the same on every computer): give each block its own phase inside the 5 s period (`mix(seed, tag, bx, by) % kSeaPeriodMs`), as cities already have at `:194`, so the work spreads over the 100 ticks of the period; bucket ships into a coarse grid once per scan and island rectangles once per world; bucket soldiers by island for `city_round`. Together about 1/100 of today's cost per tick. Moving the scenery to frame time would change the stated design and needs the owner's consent.

##### TB-26 [Medium] Fingerprints, snapshots and loads re-serialise the whole game on the game thread: on a real 12-player 2000² game 8-9 ms per fingerprint and a 0.11 s save per join or resync on a fast desktop (2-3× that on the minimum machine); seconds only on dense synthetic worlds

- **Aliases:** NET-5, IO-1, LST-4 · **Dimensions:** D2, D4, D6, D8 · **Effort:** L · **Depends on:** MS-7 (chunked transfer) for the streaming part · **Fix bundle:** P8
- **Where:** `engine/net/src/session.cpp:91-109` (`run_frame` → `state_hash()` every `hash_interval (100) × bit_ceil(speed)` frames, about every 5 s of real time at any speed, multiplayer only); `engine/sim/src/simulation.cpp:87-97` (`state_hash`: `encode_sections` of every section including about 20 bytes per island tile, `save/map.cpp:88-127`, then `xxh64`; `m_hash_buffer` keeps its capacity for good); `engine/net/src/host_session.cpp:668-699` (`send_game` → `write_game` with zlib level 6 synchronously inside `update()`); `engine/net/src/client_session.cpp:363` (`load`)
- **Failure scenario:** 12 players on a 2000×2000 world (the generator's maximum), late game. Every fingerprint (every 5 s) serialises and hashes the whole game on the game thread, and in lockstep the slowest computer sets the pace. On a join, a resync or a client that falls `resync_behind` frames behind, the host writes and compresses the game inside `update()`, so its frames stop for the save time plus the client's load. After host migration every client is resynced at once. On worlds of the original's size the hash costs 1-3 ms and is harmless. Dense synthetic worlds (3.2 M land tiles on 2000², 8.1 M on 4096²) reach about 100 ms per hash and 2-12 s per join; real content at 2000² is about 10× smaller in state.
- **Evidence:** Warm `state_hash` (the real call reuses its buffer), NET `snapshot_size`, dense synthetic worlds calibrated to the original's snapshot size:

  | World | Land tiles | `state_hash` warm | `write_game` raw | `write_game` zlib (host) | `read_game` (client) |
  |---|---|---|---|---|---|
  | 2000², 100 islands | 3.2 M | 87-112 ms | 84 MiB, 0.8 s | 4.3 MiB, 2.2 s | 0.9 s |
  | 4096², 250 islands | 8.1 M | 291-341 ms | 177 MiB, 1.9-2.0 s | 9.6-14.1 MiB, 8.5-12.5 s | 2.9-3.4 s |

  LST in-loop on 2000² with 64 k houses (wall): 6 calls, mean 100.7 ms, max 257 ms; 866×606 soak: mean 10.5 ms, max 88 ms. IO's cold first-call figures (26 ms / 471 ms) overstate the warm cost 2-4× and are not used. The host briefly holds about 3× the compressed size per receiving peer (encode copy plus ENet copy). `docs/performance.md:57-59`, SC-14 and ADR 0011 point 5 quote 0.7-3 ms or 11-28 ms and are out of date. **Device run with the original data** (12 players, 2000×2000, 600 game minutes, Ryzen 9 7900X3D): state hash 8.2-8.6 ms, save 108.8-113 ms (2.39 MB at most), snapshot 2.21 MiB. **Re-judged from High to Medium.** The 6.25 ms budget is meant for the minimum machine of `docs/performance.md`, which is roughly 2-3× slower than the device machine. Scaled with that factor, a real 2000² game costs about 16-26 ms per fingerprint (3-4× the tick budget, once every 5 s) and freezes the host for about 0.2-0.35 s per join or resync (the save time; the client's load comes on top). That misses the budget with a periodic spike and a short freeze, which is Medium ("performance with foreseeable consequences"). The cloud figures of about 100 ms per hash and 2-12 s per join came from dense synthetic worlds and apply only to such worlds (hand-made or loaded dense 4096² maps, which the generator does not produce); there the finding would still be High.
- **Solution:** (1) Fingerprint without serialising: per-island tile hashes with a dirty flag set by `IslandMap::build`/`replace` (hash static layers once per island change), entity tables hashed directly with streaming XXH64, or cached encoded bytes per clean section. Keep the full hash for desync diagnosis. Test that the incremental hash equals the full one after random command sequences. (2) Write snapshots from a copy on a worker thread, or keep an incrementally maintained snapshot; zlib level 1 or zstd; send in chunks (MS-7). (3) Scale `hash_interval` with world size until (1) lands. (4) Release or cap `m_hash_buffer`. (5) A 2000² multiplayer benchmark; update `docs/performance.md`.

##### TB-28 [Medium] All islands enter their epoch in the same tick (Medium on very large worlds, Low otherwise)

- **Aliases:** LST-10 · **Dimensions:** D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P13
- **Where:** `engine/sim/src/simulation.cpp:681-712` (`islands_step`: one global phase; every island becomes due when it steps), `Logistics::kIslandEpochMs` = 30 s
- **Failure scenario:** Every 30 s of game time all islands run droughts, deer and `advance_islands` in one tick; the cost is the sum over islands.
- **Evidence:** Wall time from the LST step table only (no per-step CPU time): islands step max 181 ms on 4096² (256 islands of 240²), 48 ms on 2000², 10 ms on 1000², while the mean is 2-16 µs. Needs a device re-measurement. **Device:** the real 2000×2000 stress run had a maximum tick of 8.9 ms; the epoch step was not isolated, so the re-measurement remains open.
- **Solution:** Stagger islands over the epoch (island index modulo the ticks per epoch). The original uses the same phase scheme (`docs/re/newgame.md:105`, `growth.md:98`), so staggering changes the timing slightly against the original; keep the original phase under classic rules if that matters.

##### TB-29 [Info] Long sessions are healthy: memory, event buffers and ids stay bounded over 24 game hours, also with the original data

- **Aliases:** LST-16 · **Dimensions:** D3, D4 · **Effort:** — · **Depends on:** — · **Fix bundle:** —
- **Where:** Measured with `o1602-stress` (T4 soak, 1.73 M ticks, 12 computers, dense cities, 612 ships, 2 400 soldiers, disasters, eruptions, a hash every 800 ticks)
- **Failure scenario:** —
- **Evidence:** RSS flat at 38 MB (peak 54 MB only during hash or save); allocations 480 per tick on average; ids grow monotonically and are never reused; undrained event buffers stay bounded (`kMaxPending` 8 192); heaptrack: 1 leaked allocation (libc stdio); round trip equal after 24 h. CPU mean 0.77 ms, but max 760 ms (ships step, TB-1). **Device, 24 game hours with the original data, 12 players: PASS.** Mean 257 µs, p99 1 770 µs, max 5 983 µs per tick; memory 27 MB at the end, 38 MB peak, no growth. The synthetic soak's 760 ms ships maximum did not appear with real content.
- **Solution:** None needed. Keep the soak as a nightly or device run.

### Frame budget, interface and rendering (FB)

Per-frame work that grows with the world instead of the view, plus interface and platform robustness items of the app, render, audio and video modules.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| FB-1 | High | The status line walks every ship and soldier, up to twice, on every frame to find the unit under the mouse | P19 |
| FB-2 | High | Soldier figures are synchronised and animated for every soldier on every frame: 50 ms per frame at 100 000 soldiers before anything is drawn | P19 |
| FB-3 | Medium | The draw list builds sprites for every figure in the world and culls only by row afterwards, at every zoom level | P19 |
| FB-4 | Medium | The Kontor city page recomputes all cities' workers and running costs on every frame; the diplomacy panel calls `Diplomacy::mood` per partner on every refresh | P14 |
| FB-5 | Medium | Minimap: a full-world RGBA buffer re-uploaded whole every 500 ms after any change, refreshing one island per frame | P19 |
| FB-6 | Medium | Ship and city lists: 13 rows per page, page buttons only, no sort, filter or search (rebuilt every frame) | P22 |
| FB-7 | Medium (unclear) | Unclear: sprite batching flushes on every atlas page change; the draw-call count is unknown | P19 |
| FB-8 | Low | The resource bar counts workers of the whole world every frame while the mouse is over a city | P14 |
| FB-9 | Low | Animation sounds check every soldier instance in the world every frame | P19 |
| FB-10 | Low | Ambient creatures and juggler effects are produced world-wide every frame, not culled by the view | P19 |
| FB-11 | Low | All zoom atlases stay resident (documented); a device reset waits synchronously for the shown level | P29 |
| FB-12 | Low (unclear) | Unclear: at far zoom every sea tile is a separate quad (206 k quads per frame at 4K) | P29 |
| FB-13 | Low | Music tracks are loaded synchronously on the main thread | P29 |
| FB-14 | Low | Videos are read whole and opened on the main thread | P29 |
| FB-15 | Low | Save slots: every slot file is read whole at start, and the menu copies all slots every frame | P9 |
| FB-16 | Low | Smaller per-frame costs that grow with the world (panels, HUD, Kontor preview, text wrapping) | P19 |
| FB-17 | Low | `CombatEffects` rebuilds an `unordered_map` every frame; each tower shot searches towers linearly | P19 |
| FB-18 | Low | `shipyard_at` searches all cost buildings linearly for each shipyard status icon | P19 |
| FB-19 | Low | `AnimState.frame` is 8 bits: animations with more than 255 frames never complete | P29 |
| FB-20 | Low | A `bad_alloc` from the preloader worker's `m_ready.emplace_back` terminates the program | P28 |
| FB-21 | Low | The UI raster checker is stretched, not tiled, for rects wider than 64 px | P29 |
| FB-22 | Low | `SdlUiRenderer::make` and `image` do not check the RGBA size against w × h × 4 | P28 |
| FB-23 | Low | The UI sprite cache is unbounded: one texture per sprite, colour and tint key | P29 |
| FB-24 | Low | The mouse cursor is not scaled on HiDPI displays | P29 |
| FB-25 | Low | Unit tint templates are recomputed for each banner at load | P29 |
| FB-26 | Low | No exception barrier in `main()`; `replace_world` catches only `bad_alloc`; the save-path guard ignores its error code | P28 |
| FB-27 | Low (unclear) | Unclear: the "never write into the installation" check may be case-sensitive on macOS | P28 |
| FB-28 | Low | Game-folder detection has no macOS locations beyond GOG and `~/.wine`, and reports a missing file when several installations are found | P28 |
| FB-29 | Low | The game-folder dialog can wipe settings.cfg when reading it fails | P28 |
| FB-30 | Low | `EventPresenter` indexes islands without a bounds check; rival population messages flood the status bar with 11 rivals | P22 |
| FB-31 | Low | F10 "game menu" is bound but not implemented | P28 |
| FB-32 | Low | New-game dialog: the hint and the start error get one line when all world items are shown | P22 |
| FB-36 | Low | `settings.cfg` and `slots.cfg` need up to two save/load rounds before a name stays stable | P28 |
| FB-33 | Info | Banner flag frames: per-banner value used, atlas block sized from the template (latent) | P29 |
| FB-34 | Info | `Mixer::set_group_volume` does not affect playing voices (documented as intentional) | — |
| FB-35 | Info | Minor render items: `TextCache` orphan on duplicate insert, `SdlMapCanvas` only grows | P29 |

#### FB-1 [High] The status line walks every ship and soldier, up to twice, on every frame to find the unit under the mouse

- **Aliases:** APP-1 · **Dimensions:** D4, D6 · **Effort:** M · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/app/src/map_viewer.cpp:1288` (`tile_status()` every frame while no tool or notice is active) → `:904-912` (`m_input->unit_at_mouse()`) → `engine/app/src/map_input.cpp:1101-1165` (`unit_at`) → `:1033-1061` (`units_in` over all `world().ships()` and `world().soldiers()`, `unit_box` each, `:988`, which calls `MapViewer::unit_sprites`, `map_viewer.cpp:1051-1086`); without a pixel hit a second full pass with slack (`map_input.cpp:1151`)
- **Failure scenario:** A late game with 20 000 ships and 100 000 soldiers, mouse over open sea: every frame runs about 240 k sprite computations and allocates the candidate vector, independent of what is on screen. Estimated 24-72 ms per frame.
- **Evidence:** Code path confirmed by the verifier; no guard upstream. Magnitude estimated (needs the original sprites; device run APP-1). The complexity alone misses the stress targets.
- **Solution:** Cull by the camera's visible tile rectangle plus a margin in `units_in` before `unit_box` (a few lines; soldiers and ships have world positions). Better: keep a per-frame list of drawn units with screen boxes from the render pass and pick from it; it also serves FB-9 and box selection. Throttle the status-line pick to mouse movement or 10 Hz. Test with a fake host that counts `unit_sprites` calls.

#### FB-2 [High] Soldier figures are synchronised and animated for every soldier on every frame: 50 ms per frame at 100 000 soldiers before anything is drawn

- **Aliases:** RND-2 · **Dimensions:** D4, D6 · **Effort:** M · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/render/src/land_figures.cpp:251-310` (`SoldierFigures::sync`: `TickTrail::position` and `lookup(m_last_index)` binary searches, `spawn_height`, four vector pushes per soldier, `stable_sort` of `m_index` at `:303` allocating its buffer every frame), called every frame from `engine/app/src/map_viewer.cpp:892-893`; the advance step that follows
- **Failure scenario:** A battle with 50-100 k soldiers: frame time above 16 ms (50 k) or 35 ms (100 k) wherever the camera is.
- **Evidence:** `bench_soldiers`, sync + advance per frame (author / verifier): 10 k 1.38 / 1.92 ms; 50 k 16.5 / 19.8 ms; 100 k 35.6-51.7 / 51.3 ms. The bench has no islands (cheap `spawn_height`) and leaves out the draw-list pass (FB-3) and the sound pass (FB-9), so it understates the cost. **Device:** rendering with the original data stayed vsync-capped (143/240 Hz) and reached 137.6 fps at 4K windowed with 49 572 sprites; peak RSS about 850 MiB, VRAM 0.6-0.8 GiB. A 100 000-soldier view was not measured, so the High at the stress target stands.
- **Solution:** Advance animation lazily (store the last time, catch up when visible; `Ambient` already does this, `ambient.cpp:275-282`); do sprite and z work only for soldiers in the view; replace both binary searches with a merge-walk over the id-sorted lists; keep `m_index` sorted incrementally.

#### FB-3 [Medium] The draw list builds sprites for every figure in the world and culls only by row afterwards, at every zoom level

- **Aliases:** RND-3 · **Dimensions:** D4, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/render/src/world_renderer.cpp:1137-1192` (`land_figure_sprites`, `ship_sprites`, `building_figure_sprites`, `effect_figure_sprites` for every figure; the row test in `add_figure`, `:1137-1141`, comes after); `engine/render/sdl/src/sdl_sprite_renderer.cpp:83-200` (`draw` does no screen culling)
- **Failure scenario:** 100 k figures: at far zoom every figure in the row band across the full world width is built, sorted and sent; at near zoom about 1 800 off-screen figure commands still reach the GPU.
- **Evidence:** Far zoom, 4096², magnification 0.2: draw list 0.92 → 8.94 ms (1080p), 3.93 → 15.55 ms (4K) with 100 k figures. Near zoom (verifier `bench_near`): 0.04 → 4.5 ms (1080p), 0.13 → 4.9 ms (4K); commands 2 263 → 4 084.
- **Solution:** Test the projected position against the viewport (margin `extent`) before calling the `*_sprites` functions, for ships, building figures and effects too; better, bucket figures by world row or a coarse grid so the loop touches only the visible band.

#### FB-4 [Medium] The Kontor city page recomputes all cities' workers and running costs on every frame; the diplomacy panel calls `Diplomacy::mood` per partner on every refresh

- **Aliases:** APP-2 · **Dimensions:** D4, D2 · **Effort:** S · **Depends on:** TB-20, TB-8 · **Fix bundle:** P14
- **Where:** `engine/app/src/selection_panels.cpp:613-625` (`CityPanel::refresh_city`, from `refresh()` `:388`: whole-world `city_workers` and `running_costs`, plus a linear search for its own index `:615-619`); `engine/app/src/diplomacy_panel.cpp:132, 349-354, 397` (`pose()` calls `mood` for up to 11 partners per refresh, TB-8)
- **Failure scenario:** A late 12-player game with the city page open: realistic densities cost about 2-3 ms per frame; the diplomacy panel costs up to 11 × 30 ms per refresh at 100 k soldiers.
- **Evidence:** Author's `city_panel_bench`, verifier re-run: 100 cities / 21 k productions 2.3 ms per frame; 240 cities / 51 k productions 11.6 ms. The bench places about 4× a real city's productions, so realistic late games fall in the lower rows. The root cause in the tick is TB-20.
- **Solution:** Call the single-city `city_workers(world, buildings, city)`; once TB-20 lands, read cached per-city running costs; refresh at most once per tick or every 500 ms like `m_player_figures`. Cache the diplomacy poses per diplomacy change.

#### FB-5 [Medium] Minimap: a full-world RGBA buffer re-uploaded whole every 500 ms after any change, refreshing one island per frame

- **Aliases:** APP-5 · **Dimensions:** D4, D6 · **Effort:** M · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/app/src/hud.cpp:1199-1216` (`build_minimap`, `w × h × 4` bytes), `:1340-1375` (one island per frame; dirty at most every `kMinimapMs`); `engine/render/sdl/src/sdl_ui_renderer.cpp:334-346` (`image` calls `SDL_UpdateTexture` with a null rect)
- **Failure scenario:** Generated 2000² worlds: 16 MiB uploaded twice a second while anything changes (64 MiB on loaded 4096² worlds); a full rebuild at load and every `reset_game`; with 2 000 islands at 60 fps a change shows up to 33 s late. Latent: `m_minimap_dirty` doubles as rebuild and upload flag and is cleared only when a minimap gadget exists, so a mod GAD without one rebuilds the whole minimap every frame.
- **Evidence:** Code; sizes from `World::kMaxSide` and the generator's 2000². "Some GPUs refuse 4096²" is withdrawn as doubtful.
- **Solution:** Downsample to the on-screen size (≤ 512²) or 1 pixel per 4×4 tiles; track dirty rectangles from construction events and upload only those (rect `SDL_UpdateTexture`); refresh islands with events first; split `m_minimap_rebuild` from `m_minimap_upload`.

#### FB-6 [Medium] Ship and city lists: 13 rows per page, page buttons only, no sort, filter or search (rebuilt every frame)

- **Aliases:** APP-7 · **Dimensions:** D6, D4 · **Effort:** M · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/list_panels.cpp:20` (`kRows = 13`), `:38-39` (`refresh` → `collect` every frame), `:139-160` (walks all ships, copies names)
- **Failure scenario:** A player with 2 000 ships needs up to 154 page clicks to find one and cannot find idle or damaged ships. The per-frame walk costs about 0.1 ms at 20 k ships (minor).
- **Evidence:** Code; Medium for usability at the stress targets, the performance part is Low.
- **Solution:** Collect on open, page change or every N ms; mouse-wheel scrolling, first/last page, sort and filter keys (idle, on route, damaged, name); keep the original layout as default view.

#### FB-7 [Medium] Unclear: sprite batching flushes on every atlas page change; the draw-call count is unknown

- **Aliases:** RND-8 · **Dimensions:** D4 · **Effort:** S (counter), M (packer) · **Depends on:** Device run · **Fix bundle:** P19
- **Unclear.** What would settle it: Device run RND-2 (a batch counter in `SdlSpriteRenderer::flush`, batches per frame in a far view and a big battle) was not run; it settles it. The device saw no frame-rate problem (vsync-capped, 137.6 fps at 4K with 49 572 sprites).
- **Where:** `engine/render/sdl/src/sdl_sprite_renderer.cpp:68-76` (`flush`), `:153-156` (flush on page change); `engine/render/src/sprite_atlas.cpp:62` (all sprites sorted by height, so sheets mix across pages)
- **Failure scenario:** Depth-sorted figures alternate pages, one `SDL_RenderGeometry` per run: possibly thousands of draw calls per frame with many figures.
- **Evidence:** Mechanism confirmed; batch count not measured. With the original data everything fits in roughly 10 pages of 4096 px.
- **Solution:** Count and log batches per frame first; if high, pack each figure type's sprites onto one page, or one page per category.

#### FB-8 [Low] The resource bar counts workers of the whole world every frame while the mouse is over a city

- **Aliases:** APP-4 · **Dimensions:** D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P14
- **Where:** `engine/app/src/map_viewer.cpp:1703` (via `resources()`, `:1301`) → `sim::city_workers(world, buildings, *city)` walks every workplace (`engine/sim/src/missions.cpp:321-329`)
- **Failure scenario:** Mouse over any discovered city in a late world.
- **Evidence:** Verifier: 0.36 ms at 21 k productions, 0.86 ms at 51 k; the 16 ms figure needs 255 k productions, which no game reaches.
- **Solution:** Filter the workplace walk by `city.island` first, or cache per (city, tick).

#### FB-9 [Low] Animation sounds check every soldier instance in the world every frame

- **Aliases:** APP-6, RND-V3 · **Dimensions:** D4, D6 · **Effort:** S · **Depends on:** FB-1 or FB-3 · **Fix bundle:** P19
- **Where:** `engine/app/src/game_sound.cpp:412-455` (`GameSound::figures`, at speed 1), called from `engine/app/src/map_viewer.cpp:898` with `m_soldiers.instances()`
- **Failure scenario:** At 100 k soldiers about 1 ms per frame on top of FB-2 and FB-3, only to keep the first figure per type that is on screen.
- **Evidence:** Code; per instance two bounds-checked indexings, plus a small `find` and a `std::function` call for sound-carrying animations.
- **Solution:** Pass only the instances the renderer drew (shared list with FB-1) or cull with the same visible-tile rectangle; `heard` can be a small flat set.

#### FB-10 [Low] Ambient creatures and juggler effects are produced world-wide every frame, not culled by the view

- **Aliases:** RND-V2 · **Dimensions:** D4, D6 · **Effort:** S · **Depends on:** FB-3 · **Fix bundle:** P19
- **Where:** `engine/render/src/ambient.cpp:321-366` (`frame` pushes every live creature and juggler into `m_effects`), then `world_renderer.cpp:1179-1186` builds their sprites
- **Failure scenario:** A 4096² world makes about 3 500 creatures per 10 s; each is animated and goes through the draw list every frame though almost none are on screen.
- **Evidence:** Bench output "creatures made 3494"; code reading.
- **Solution:** Apply the FB-3 view pre-test in `Ambient::frame` (`AmbientView` already exists).

#### FB-11 [Low] All zoom atlases stay resident (documented); a device reset waits synchronously for the shown level

- **Aliases:** RND-7 · **Dimensions:** D4, D3 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/app/src/map_viewer.cpp:514` (page size 4096 when supported), `:525` (all levels resident by design), `:577-581`, `:1644-1654` (`reload_graphics`: all levels through the preloader, waits only for the shown one)
- **Failure scenario:** Rare D3D device loss: a freeze of about 2.2 s at the near level (0.6 s middle, 0.16 s far, software renderer figures from `docs/performance.md:79`). GPU memory of about 650 MB matches the documented minimum spec (1 GB VRAM).
- **Evidence:** Verifier: the report's "re-decodes everything synchronously" is withdrawn; only the shown level is waited for. **Device:** VRAM 0.6-0.8 GiB with the original data after the zoom levels loaded; the device-reset run (Windows) was not done.
- **Solution:** Draw a placeholder while the shown level reloads; optionally RGBA5551 pages to halve memory.

#### FB-12 [Low] Unclear: at far zoom every sea tile is a separate quad (206 k quads per frame at 4K)

- **Aliases:** RND-6 · **Dimensions:** D4, D10 · **Effort:** M · **Depends on:** Device run · **Fix bundle:** P29
- **Unclear.** What would settle it: Device run RND-1 was run only partly (4K windowed with 49 572 sprites: 137.6 fps, vsync-capped at 143/240 Hz); far zoom on the largest world and `SDL_RENDER_DRIVER=software` were not reported. Those settle it.
- **Where:** `engine/render/src/world_renderer.cpp` (`build_world_draw_list` tile loop); `SdlSpriteRenderer`
- **Failure scenario:** A 4K screen at magnification 0.2 on a 4096 world; claimed to overload the software renderer and slow iGPUs.
- **Evidence:** 52 419 commands at 1080p, 205 924 at 4K; draw-list CPU 0.9 / 3.9 ms, within budget. GPU and software cost not measured; `docs/performance.md:63-69` reports 60 fps at far zoom 1080p in software with 23.6 k sprites.
- **Solution:** Rate after the device run. Pre-rendered sea blocks per far zoom level are a large change; do not plan them before the run shows a need.

#### FB-13 [Low] Music tracks are loaded synchronously on the main thread

- **Aliases:** RND-9 · **Dimensions:** D4 · **Effort:** M · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/audio/src/game_audio.cpp:482` (`Music::start` → `SoundBank::load`); speech likewise
- **Failure scenario:** One short hitch per track change, every few minutes; the sim catches up.
- **Evidence:** Verifier `wav_time` on a 4-minute 22 kHz track: 8-bit mono 5.3 MB 3.4 ms; 16-bit stereo 21 MB 17.6 ms, plus the file read.
- **Solution:** Load on a worker thread (preloader pattern) or stream through the `SDL_AudioStream`. Not urgent.

#### FB-14 [Low] Videos are read whole and opened on the main thread

- **Aliases:** APP-25 · **Dimensions:** D4 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/app/src/video_player.cpp:50-80` (`core::read_file` + `Smacker::open`), `video_controller.cpp:139-146`
- **Failure scenario:** The frame stalls while a 20-60 MB SMK is read.
- **Evidence:** Code reading (not re-examined in depth by the verifier).
- **Solution:** Open on the preloader thread or memory-map the file.

#### FB-15 [Low] Save slots: every slot file is read whole at start, and the menu copies all slots every frame

- **Aliases:** APP-21, IO-11 · **Dimensions:** D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P9
- **Where:** `engine/app/src/app_settings.cpp:278-293` (`read_slot_meta`, `core::read_file` of each slot; `save_info` inflates only a 4 KiB prefix, `save_game.cpp:111-115`), called from `map_viewer.cpp:2084, 2165`; `map_viewer.cpp:2445` (`menu->set_slots(m_slots)` every menu frame)
- **Failure scenario:** Many large saves (compressed 1.4-15 MiB each for large worlds) make start-up and the load menu slow; near `kMaxCount` the menu copies about 10 k strings per frame. IO-11's "inflates whole files, hundreds of MB" is withdrawn.
- **Evidence:** Code reading; IO verifier on the prefix inflate.
- **Solution:** Read only the header and 4 KiB prefix, lazily; pass slots by const reference or a version number.

#### FB-16 [Low] Smaller per-frame costs that grow with the world (panels, HUD, Kontor preview, text wrapping)

- **Aliases:** APP-22 · **Dimensions:** D4 · **Effort:** S each · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/app/src/ship_panel.cpp:629-690` (`ShipPanel::context`: 81 cells × `any_of` over cities plus a cost-building scan); `selection_panels.cpp:1040-1047` (`cost_building_at`, linear, several times per frame); `hud.cpp:332` (`selection_panel` rebuilt and looked up in a `std::map<std::string>` about 4× per frame); `build_tool.cpp:181-196` (Kontor preview scans all ships); `engine/ui/src/panel.cpp:178-195` and `form.cpp` (texts wrapped every draw, the UI-9 remainder); `event_presenter.cpp:223` (all cities per island event)
- **Failure scenario:** Each is small; together they add up in late worlds with many ships and cities.
- **Evidence:** Code reading.
- **Solution:** Cache per tick; index cost buildings and cities by island and tile in the sim; cache wrapped text per (text, width).

#### FB-17 [Low] `CombatEffects` rebuilds an `unordered_map` every frame; each tower shot searches towers linearly

- **Aliases:** RND-10 · **Dimensions:** D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/render/src/combat_effects.cpp:62, 207-213` (`m_ship_smoke` cleared and refilled), `:101` (`find_if` over towers)
- **Failure scenario:** Thousands of ships and towers in combat: hashing and allocation per frame, O(shots × towers).
- **Evidence:** Code reading, confirmed.
- **Solution:** A vector indexed by ship slot; a tower map by id.

#### FB-18 [Low] `shipyard_at` searches all cost buildings linearly for each shipyard status icon

- **Aliases:** RND-11 · **Dimensions:** D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P19
- **Where:** `engine/render/src/world_renderer.cpp:493-499, 774`
- **Failure scenario:** Many shipyards on screen in a large world.
- **Evidence:** Code reading, confirmed.
- **Solution:** Look the building up by position through the island's building grid.

#### FB-19 [Low] `AnimState.frame` is 8 bits: animations with more than 255 frames never complete

- **Aliases:** RND-13 · **Dimensions:** D6, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P29
- **Where:** `figure_animation.hpp:18`; `engine/render/src/figure_animation.cpp:38-45` (`while (state.elapsed_ms >= a->speed_ms)`)
- **Failure scenario:** Mod data with `AnimAnz` > 255: `++state.frame < a->count` stays true because the frame wraps, so JumpTo and Random animations never move on. The large-dt loop is bounded (frame delta clamped to 1 000 ms, `map_viewer.cpp:657`; speed ≥ 1).
- **Evidence:** Code reading, verifier.
- **Solution:** Use 16 bits for `frame` or clamp `AnimAnz` at load; reduce elapsed time modulo the cycle length.

#### FB-20 [Low] A `bad_alloc` from the preloader worker's `m_ready.emplace_back` terminates the program

- **Aliases:** RND-14 · **Dimensions:** D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/render/src/graphics_preloader.cpp:123` (outside the try at `:129-135`, on the worker thread)
- **Failure scenario:** Very unlikely memory exhaustion during preloading.
- **Evidence:** Code reading, confirmed.
- **Solution:** Reserve the slots up front or move the emplace inside the try.

#### FB-21 [Low] The UI raster checker is stretched, not tiled, for rects wider than 64 px

- **Aliases:** RND-16 · **Dimensions:** D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/render/sdl/src/sdl_ui_renderer.cpp:398-408` (src clamped to 64, dst is the full rect)
- **Failure scenario:** 50 % fills wider than 64 px show stretched 2-px stripes, at higher UI scales or with wide gadgets.
- **Evidence:** Code reading, confirmed.
- **Solution:** Loop over 64-px tiles, or wrap addressing with `SDL_RenderGeometry` UVs.

#### FB-22 [Low] `SdlUiRenderer::make` and `image` do not check the RGBA size against w × h × 4

- **Aliases:** RND-17 · **Dimensions:** D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/render/sdl/src/sdl_ui_renderer.cpp:118`
- **Failure scenario:** A caller bug leads to an out-of-bounds read in `SDL_UpdateTexture`.
- **Evidence:** Code reading.
- **Solution:** Assert, or return an empty texture.

#### FB-23 [Low] The UI sprite cache is unbounded: one texture per sprite, colour and tint key

- **Aliases:** RND-18 · **Dimensions:** D4 · **Effort:** M · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/render/sdl/src/sdl_ui_renderer.cpp:220` (`m_sprites.emplace`)
- **Failure scenario:** Slow growth over hours with many tints; bounded by the number of distinct keys, not a leak.
- **Evidence:** Code reading.
- **Solution:** LRU with a byte cap, or a UI atlas.

#### FB-24 [Low] The mouse cursor is not scaled on HiDPI displays

- **Aliases:** RND-19 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/platform/src/mouse_cursor.cpp:33` (`SDL_CreateColorCursor` with a 1× surface)
- **Failure scenario:** On Retina or 200 % displays the cursor is half size next to the scaled UI.
- **Evidence:** Code reading; device run RND-6.
- **Solution:** Scale by `SDL_GetWindowDisplayScale` or add alternates with `SDL_AddSurfaceAlternateImage`.

#### FB-25 [Low] Unit tint templates are recomputed for each banner at load

- **Aliases:** RND-20 · **Dimensions:** D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P29
- **Where:** `engine/render/src/world_graphics.cpp`, `engine/render/src/unit_tint.cpp`
- **Failure scenario:** Longer load times with 12 banners.
- **Evidence:** Code reading.
- **Solution:** Compute the templates once; apply only the colour mapping per banner.

#### FB-26 [Low] No exception barrier in `main()`; `replace_world` catches only `bad_alloc`; the save-path guard ignores its error code

- **Aliases:** APP-17 · **Dimensions:** D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/app/src/main.cpp:202-644` (no try/catch around `MapViewer::create`/`run`); `engine/app/src/map_viewer.cpp:2309-2314` (only `std::bad_alloc`); `:2039-2041` (`std::filesystem::absolute` without `error_code`; one `ec` reused for two `weakly_canonical` calls and never checked; an empty root lets the write through)
- **Failure scenario:** A `length_error` or `filesystem_error` while loading or saving ends the program through `std::terminate`, on Windows without a message box and without saving.
- **Evidence:** Code reading, confirmed.
- **Solution:** Catch `std::exception` in `main()` and call `report_fatal`; catch `std::exception` in `replace_world` and keep the old game; use and check the `error_code` overloads.

#### FB-27 [Low] Unclear: the "never write into the installation" check may be case-sensitive on macOS

- **Aliases:** APP-18 · **Dimensions:** D10, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Unclear.** What would settle it: Whether macOS `realpath` returns the on-disk case on APFS; one run on a Mac settles it (not part of the device runs, which were on Linux).
- **Where:** `engine/app/src/map_viewer.cpp:2037-2046` (`weakly_canonical` + `lexically_relative`)
- **Failure scenario:** `--save "/…/anno 1602/x.gam"` passes when the root is `/…/Anno 1602`, if APFS paths are not case-folded.
- **Evidence:** Not verifiable on Linux.
- **Solution:** Compare with `std::filesystem::equivalent` on the existing parent chain (safe either way).

#### FB-28 [Low] Game-folder detection has no macOS locations beyond GOG and `~/.wine`, and reports a missing file when several installations are found

- **Aliases:** APP-19 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/assets/src/game_directory.cpp:112-135` (`installation_candidates`, used by `engine/app/src/game_folder.cpp:57`); `game_folder.cpp:69-85` (`open_chosen_folder` returns the first error)
- **Failure scenario:** On macOS the folder dialog always appears at first start (CrossOver bottles and `/Applications` are not searched; Lutris/Bottles on Linux; CD installs and `HKCU` GOG entries on Windows). Several installations inside the chosen folder give a misleading message.
- **Evidence:** Code reading, confirmed.
- **Solution:** Add the platform paths and a `game_folder.several` message.

#### FB-29 [Low] The game-folder dialog can wipe settings.cfg when reading it fails

- **Aliases:** APP-20 · **Dimensions:** D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/app/src/game_folder_dialog.cpp:147-160` (`save_game_folder` treats any read error as an empty file)
- **Failure scenario:** A file locked by a virus scanner on Windows: settings.cfg is rewritten with only `game_folder`.
- **Evidence:** Code reading, confirmed.
- **Solution:** Write only if the read succeeded or the error is NotFound; otherwise log and skip.

#### FB-30 [Low] `EventPresenter` indexes islands without a bounds check; rival population messages flood the status bar with 11 rivals

- **Aliases:** APP-23 · **Dimensions:** D1, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/event_presenter.cpp:207, 254-257, 296` (`world.islands()[e.island]` unchecked; `ship_events` and `game_sound.cpp` do check); `:123-128` (every rival city crossing 300, 500, … inhabitants announces MELDUNG 0x1F..0x29)
- **Failure scenario:** With 11 computer rivals and dozens of cities each, the status bar and voices are flooded. A bad event index from a sim bug reads out of bounds (events come from the local sim, not from data).
- **Evidence:** Code reading, confirmed.
- **Solution:** Check bounds as elsewhere; rate-limit rival announcements (once per rival per level).

#### FB-31 [Low] F10 "game menu" is bound but not implemented

- **Aliases:** APP-24 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/app/src/map_input.cpp:441` (`not_yet("game menu")`); bound by default in `key_bindings.cpp`
- **Failure scenario:** The default key does nothing useful.
- **Evidence:** Code reading.
- **Solution:** Open the options panel's save/load page, or unbind the key until the menu exists.

#### FB-32 [Low] New-game dialog: the hint and the start error get one line when all world items are shown

- **Aliases:** APP-16 · **Dimensions:** D5, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/game_settings_menu.cpp:192-213` (paragraph area 690 − 4 − 656 = 30 units, one 22-unit line); `ui::Form` drops lines that do not fit
- **Failure scenario:** In the endless scope the second half of `setup.hint` is cut, and an error from `set_error()` (why the game did not start) is truncated to its first line.
- **Evidence:** Computed from the constants (`kTop = 84`, `kRowH = 36`); not re-checked by the verifier.
- **Solution:** Shrink `kRowH` to 32, move the hint to a tooltip or status line, or show errors in a modal message.

#### FB-36 [Low] `settings.cfg` and `slots.cfg` need up to two save/load rounds before a name stays stable

- **Aliases:** DET-6 · **Dimensions:** D1, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/app/src/app_settings.cpp:91` (`from_file` trims) before `:61-65` (the UTF-8/Windows-1252 conversion drops undefined bytes); the writer (`:172, 266`) does not trim; `parse_slots`/`write_slots` (`:239-270`)
- **Failure scenario:** A player name, remembered host or slot name with a byte that Windows-1252 leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D) is written as U+FFFD and dropped on the next read; if such a byte sat before a trailing space, the next write keeps the space and the read after that trims it, so the value changes again. Control characters and NUL are accepted and written. No crash, but a slot name or the player name (shown in multiplayer) visibly changes after a restart. Reachable only by hand-editing the files. Device fuzzing found a second path: `player_name=` followed by bytes 81 0D 8F. Round 1 writes 0x81 and 0x8F as U+FFFD; round 2 drops them in `from_file` (`app_settings.cpp:64-65`) and leaves the CR; round 3 trims the CR (`:16-24, :91`), the name is empty and the line disappears. `remembered_host` (`:121`) behaves the same. Only hand-edited or old `settings.cfg` files reach it.
- **Evidence:** `fuzz_settings` (1 202 s): 90 artifacts (89 `slots.cfg`, 1 `settings.cfg`). With one normalisation round allowed (commit `707d85c`), `crashes/settings/trailing-space-two-rounds.cfg` still fails: `player_name = Spi ` → `Spi` (reproduced by the verifier). **Device fuzzing with the original data (input #5, root-caused):** 488 bytes minimised to 15; cause as above (trimming happens before the conversion).
- **Solution:** Normalise once when a value is accepted: map undefined bytes to `?` or drop them before trimming, and refuse control characters (as `World` does for city names, `world.cpp:21`), so write → read is the identity and the fuzz target can return to a strict round-trip check. Concretely: trim again after `from_file` for the player name and for `remembered_host`.

#### FB-33 [Info] Banner flag frames: per-banner value used, atlas block sized from the template (latent)

- **Aliases:** RND-15 · **Dimensions:** D7, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P29
- **Where:** `banner.hpp:35` (`Banner::flag_frames`), `:90` (`FlagTemplate::frames`); `banner.cpp` (`flag_sprite`), `world_graphics.cpp` (block size)
- **Failure scenario:** Both are 8 and nothing assigns `flag_frames`; a future difference would show the wrong colour.
- **Evidence:** Code reading.
- **Solution:** One source for the frame count, or assert equality at load.

#### FB-34 [Info] `Mixer::set_group_volume` does not affect playing voices (documented as intentional)

- **Aliases:** RND-24 · **Dimensions:** D1 · **Effort:** — · **Depends on:** — · **Fix bundle:** —
- **Where:** `engine/audio/src/mixer.cpp:102-105`
- **Failure scenario:** Ambient loops, if any, keep the old volume until restarted.
- **Evidence:** Code reading.
- **Solution:** None needed unless looping ambient sounds are added.

#### FB-35 [Info] Minor render items: `TextCache` orphan on duplicate insert, `SdlMapCanvas` only grows

- **Aliases:** RND-26 · **Dimensions:** D3, D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P29
- **Where:** `text_cache.hpp` (duplicate insert leaves an orphan entry); `IslandRenderer::rasterize` (bounding box over huge draw lists, tools only); `SdlMapCanvas` (grows by 1/8, never returns memory after a temporary 4K window)
- **Failure scenario:** Small memory effects only.
- **Evidence:** Code reading.
- **Solution:** Fix in passing.

### Game behaviour (GB)

Wrong game behaviour that is neither a security nor a performance problem. This theme was added during the merge because no other theme fits these findings.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| GB-1 | High | Natives' goodwill does not apply to players 5-12 (ids 7..14): unlimited buying from natives in games with more than four players | P7 |
| GB-2 | Medium | An AI shipyard need from the 10-s logistics overwrites the running site search; the paused task then builds with the shipyard's result | P18 |
| GB-3 | Low | AI `task_abandon` has no progress guard and ignores the command result | P18 |
| GB-4 | Low | AI guard posts are cached once per city plan and never refreshed | P18 |
| GB-5 | Low | Dead AI state is saved, and `kMaxStack` is unreachable | P10 |
| GB-6 | Low | `victory.class = 0` is accepted but described as "Settlers or higher": 100 pioneers already win | P22 |
| GB-7 | Info | Goodwill arithmetic can wrap in 32 bits (only reachable with a crafted save) | P7 |
| GB-8 | Info | `Logistics::begin_step` does not clamp `speed_type`; safe today because the figure table clamps it at load | — |

#### GB-1 [High] Natives' goodwill does not apply to players 5-12 (ids 7..14): unlimited buying from natives in games with more than four players

- **Aliases:** SIM-7 · **Dimensions:** D1, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P7
- **Where:** `engine/sim/src/ships.cpp:1128-1132` (limit), `:1155-1159` (spend), `:1210-1212` (earn), all gated by `ship.owner < city.goodwill.size()`; `City::goodwill` (`engine/sim/include/o1602/sim/world.hpp:179`) is filled only from the original record's 7-entry array (`engine/sim/src/world.cpp:586`); the generator sets 200 for ids 0-3 only (`world_generator.cpp:1127-1134`); `player_slot(n)` gives ids 7..14 to players 5-12 (`player.hpp:224-226`)
- **Failure scenario:** Endless setup, the multiplayer lobby and the CLI all allow 12 players, and every generated world has native cities. Players 5-12 are never limited by goodwill when buying from natives (trade window, routes, AI or human); players 1-4 are. Deterministic, so not a desync, but unfair in every game with more than four players.
- **Evidence:** Code reading, confirmed by the verifier; no unit test trades with natives at all (BT-16). Saves need no change: the section format stores `goodwill` as a vector of any length (`save/ships.cpp:106`, applied at `save/apply.cpp:362`) and it is hashed. `savegame_writer.cpp:805` writing 7 slots is harmless because the `.gam` export refuses more than 7 player slots (`:230`).
- **Solution:** Resize `goodwill` to `players().size()` wherever cities are created or loaded and when players are set (`load_cities`, the generator, change of hands); new real-player slots start at 200, faction slots at 0. Treat a missing index as goodwill 0 (refuse), never as "no limit". Test: a natives city, ships of ids 0 and 7, the limit, spend and earn (BT-16).

#### GB-2 [Medium] An AI shipyard need from the 10-s logistics overwrites the running site search; the paused task then builds with the shipyard's result

- **Aliases:** AI-8 · **Dimensions:** D1 · **Effort:** S · **Depends on:** BT-1 · **Fix bundle:** P18
- **Where:** `engine/sim/src/ai/ai_logistics.cpp:590-597` (`order_ship` calls `need_shipyard` only when `top(p) > kSurvey`); `ai_planner.cpp:1187-1215` and `:1623` (`plan_building(kShipyard)` → `start_search` replaces `p.search`, `reserve` refunds the paused task); `ai_sites.cpp:1293-1296` (`search_shipyard` returns only with a finished cursor); effects in `task_houses` (`ai_build.cpp:517-559`), `task_workshop`, `task_farm`, `task_market`, `task_public`, `task_mine`, `task_towers` (`ai_build.cpp:900-974`)
- **Failure scenario:** This is the path that plans every computer's first yard before the merchants, so it fires systematically and each success interrupts a running task. Effects: phantom house failures; a second `slot_fail(kSlotShipyard)`; market or public flags set without building; a stray road from `task_mine`; `task_towers` can build a second shipyard on the tower ring.
- **Evidence:** Traced in the code by author and verifier; not reproduced end to end (the synthetic tables have no yard, weaving hut or tower ids). The original has the same shared search (C+0x1D14) and call site (0x0042233E), so this is likely an original quirk; Open1602 fixes comparable ones (S-096..S-099). **Device:** the AI log of a 12-player run with the original data (301 lines) shows no shipyard anomaly; the bug is code-traced and not yet seen in play. Medium stands.
- **Solution:** Set `p.yard_wanted` in `order_ship` and let the next develop step of the home city call `need_shipyard` (deterministic, no save change). A search per stack entry is not needed. Test with a synthetic shipyard and house (BT-1).

#### GB-3 [Low] AI `task_abandon` has no progress guard and ignores the command result

- **Aliases:** AI-13 · **Dimensions:** D1, D2 · **Effort:** S · **Depends on:** BT-1 · **Fix bundle:** P18
- **Where:** `engine/sim/src/ai/ai_settle.cpp:925-969`
- **Failure scenario:** If a demolish order for a territory building is refused ("unchanged original terrain", `construction.cpp:2006-2012`, possible with island data that carries buildings in the ground layer) or silently does nothing (MD-1), the task never pops and the computer repeats a full-island scan and the order every 50 ms until the hourly re-init. Rare trigger.
- **Evidence:** Code reading. LST's synthetic run showed exactly this loop (815 050 demolish lines in 15 min), caused there by the missing meadow record (see LST-12 under Refuted and MD-1). **Device:** the AI log with the original data shows no repeated demolitions, which matches the cause found for the synthetic loop (missing meadow record, LST-12).
- **Solution:** Count actual removals (`CommandResult::demolished`), skip refused tiles, give up after a pass without progress. Test in the AI arena (BT-1).

#### GB-4 [Low] AI guard posts are cached once per city plan and never refreshed

- **Aliases:** AI-12 · **Dimensions:** D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P18
- **Where:** `engine/sim/src/ai/ai_military.cpp:209-246` (`r.posts` filled only when empty, `:211`; cleared only by `init_player`)
- **Failure scenario:** Markets built later are never guarded; posts at demolished or burnt markets draw soldiers there for up to an hour.
- **Evidence:** Code reading, confirmed.
- **Solution:** Refill when the city's market slot count changes (the survey counts markets), or check the post's tile on use.

#### GB-5 [Low] Dead AI state is saved, and `kMaxStack` is unreachable

- **Aliases:** AI-14 · **Dimensions:** D7, D10 · **Effort:** S · **Depends on:** DS-3 (section versioning) · **Fix bundle:** P10
- **Where:** `engine/sim/src/ai/ai_state.hpp:301-320, 387` (`AiPlayer::attacks` / `AttackPlan`, only reset at `ai_military.cpp:602, 647`); `:337-340` (`IslandWatch::state`, only archived, `ai_archive.hpp:274`); `ai_core.cpp:315-330` (`push()` refuses duplicates, so at most about 55 tasks and `kMaxStack` = 256 never binds; `docs/re/ai.md` describes up to 256)
- **Failure scenario:** No run-time effect; the saved state grows and the documentation misleads.
- **Evidence:** Code reading, confirmed.
- **Solution:** Remove with the next AI state version (`kStateVersion` 2) and document the de-duplicated stack.

#### GB-6 [Low] `victory.class = 0` is accepted but described as "Settlers or higher": 100 pioneers already win

- **Aliases:** APP-V2 · **Dimensions:** D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/endless_setup.cpp:162-165` (accepts `0 ≤ class ≤ kLastWinClass`); the text clamps the label to `kFirstWinClass` (`:375-376`, `:591-594`); `engine/sim/src/victory.cpp:94` counts from `resident_class`
- **Failure scenario:** settings.cfg or a multiplayer host sets `victory.class = 0`. Everyone's mission text says "100 Settlers or higher", but 100 pioneers win.
- **Evidence:** Verifier reproducer `setup_display`: `victory_text: ... Population level: 100 Settlers or higher` with `resident_class 0`.
- **Solution:** Reject 0 on read (`*n >= kFirstWinClass`), matching the dialog; or offer pioneers in the dialog and the text.

#### GB-7 [Info] Goodwill arithmetic can wrap in 32 bits (only reachable with a crafted save)

- **Aliases:** SIM-14 · **Dimensions:** D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P7
- **Where:** `engine/sim/src/ships.cpp:1131` (`city.goodwill[owner] * kTon / per_ton`), `:1211` (uncapped raise); decay `g * 127 / 128` in `engine/sim/src/consumption.cpp:198-201` wraps at g > 2^32/127 ≈ 33.8 M
- **Failure scenario:** Not reachable in play: goodwill decays by 1/128 every 10 s and inflow is bounded by what natives accept (equilibrium in the tens of thousands). A crafted save gives a wrong trade limit.
- **Evidence:** Arithmetic; the verifier found the lower wrap point in the decay.
- **Solution:** Compute in 64 bits and cap goodwill (for example at 1e6). Fix together with GB-1.

#### GB-8 [Info] `Logistics::begin_step` does not clamp `speed_type`; safe today because the figure table clamps it at load

- **Aliases:** SIM-24 · **Dimensions:** D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** `engine/sim/src/logistics.cpp` (`begin_step`); the clamp to 0..3 is in `figure_table.cpp:46`
- **Failure scenario:** None today. A future loader path that skips the clamp would index out of range.
- **Evidence:** Code reading (not re-checked by the verifier).
- **Solution:** Keep the invariant; an assert in `begin_step` documents it.

### Hostile input and loaders (HI)

Savegames, mod and game data, videos and network snapshots as hostile input. Part DET's fuzzing confirmed the open loader problems (HI-2, HI-10) and found new ones (HI-15 to HI-17); fuzzing with the original data added HI-18 and HI-19; the readers it fuzzed without findings are listed under "Scope and method".

| ID | Severity | Title | Bundle |
|---|---|---|---|
| HI-1 | Medium | AI state from a save or resync snapshot is taken without semantic checks; a negative `Search::segment` crashes on the first AI step | P6 |
| HI-2 | Medium | Hostile `.cod`/`.gad` data still allocates without bound: about 280 records per input byte, and a 61-byte `.gad` throws an uncaught `bad_alloc` (earlier IO-10 incomplete) | P20 |
| HI-4 | Medium | Smacker audio decompression bomb: a 1.2 KB `.smk` decodes to 128 MiB, with no total cap and no catch (earlier IO-2 fixed per chunk only) | P20 |
| HI-5 | Medium | `load_soldiers` is O(n² log n): a 4.5 MB hostile `.szs`/`.gam` hangs the loader for over a minute | P20 |
| HI-15 | Medium | A `.GAM` island of width or height 0 is accepted and played, but every save, autosave and multiplayer snapshot of that game fails to load (earlier IO-5 incomplete) | P6 |
| HI-18 | Medium | The sprite and font loaders have no total pixel budget: a 1 KB sprite file asks for 8.5 GB | P20 |
| HI-19 | Medium | A ship's motion state in `.o1602` is loaded unchecked (soldiers likewise): a crafted save or a modified host's snapshot puts a ship outside the map after one tick | P6 |
| HI-3 | Low | The `.cod` symbol table and diagnostics are unbounded (about 95 bytes of memory per input byte), while `cod.md` describes the original's bounded table | P20 |
| HI-6 | Low | `parse_data_file` is quadratic in tables and keys | P20 |
| HI-7 | Low | A 17-byte BSH record can request a 4096 × 4096 decode (2 × 16 MiB allocated before any RLE byte is read) | P20 |
| HI-9 | Low | An absent required save section, a duplicated section and trailing bytes inside a section go unnoticed | P6 |
| HI-10 | Low | `data-limits.md` says amounts are clamped and negative `Nummer` refused, but the loaders wrap and map to 0 | P20 |
| HI-11 | Low | A host snapshot is a hostile input for clients and is amplified by inflate (32 MiB → 256 MiB sections → about 1 GB peak) | P9 |
| HI-12 | Low | Smacker `frame_us` wraps in 32 bits: a crafted rate gives 0 | P20 |
| HI-13 | Low | Out-of-range figure-table gfx values throw `out_of_range` while the atlas is built; all zoom levels fail with an obscure message | P20 |
| HI-16 | Low | A world whose islands average under 64 bytes in MAPW (for example one 1x1 island) also saves a file that does not load | P6 |
| HI-17 | Low | Signed integer overflow (undefined behaviour) for large `Wegspeed` and `Randwachs` in `haeuser.cod` | P20 |
| HI-8 | Info | The BSH decoder ignores the record `length` that `bsh.md` documents and runs on to the end of the chunk | P20 |
| HI-14 | Info | The cost-building merge in `apply` is O(costs × old costs), only on the `.GAM` extension path | P20 |

#### HI-1 [Medium] AI state from a save or resync snapshot is taken without semantic checks; a negative `Search::segment` crashes on the first AI step

- **Aliases:** AI-17 · **Dimensions:** D9, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P6
- **Where:** `engine/sim/src/ai/ai_archive.hpp:69-116, 278-335` and `ComputerPlayers::load` (`ai_core.cpp:618-631`): sizes bounded, values not checked. `Search::segment` is an unchecked `int32`; 17 site searches guard only `s.segment >= segments.size()` and then index `p.segments[static_cast<std::size_t>(s.segment)]` (`ai_sites.cpp:262, 268, 333, 440, 509, 561, 668, 737, 798, 901, 952, 1011, 1051, 1226, 1297, 1365, 1387`)
- **Failure scenario:** A crafted save (state version ≥ 4, `simulation.cpp:186`) or a hostile host's resync snapshot with a building task on the stack and a negative segment crashes every client on the first AI step after loading. Duplicate `AiPlayer` entries for one slot make a computer act twice per tick.
- **Evidence:** Verifier `neg_segment.cpp`: `NEG_SEGMENT=-1` reads silently before the buffer; `NEG_SEGMENT=-2000000000` → segmentation fault (exit 139).
- **Solution:** After loading: drop duplicate or out-of-range players, clamp `segment`, `remaining` and `extra` to their ranges, filter the stack to known task ids, and report a violation as `state_damaged` like the other sections. Add `s.segment < 0` to the guards or make the field unsigned. Add a save-fuzz case for the AI state.

#### HI-2 [Medium] Hostile `.cod`/`.gad` data still allocates without bound: about 280 records per input byte, and a 61-byte `.gad` throws an uncaught `bad_alloc` (earlier IO-10 incomplete)

- **Aliases:** IO-3, DET-2, DET-3 · **Dimensions:** D9, D3, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/assets/src/cod_parser.cpp:455, 529-551` (`obj_fill` clamps each statement to `kMaxObjFill = 10000` copies; copies hold the source's children, and nothing bounds the total); `engine/assets/src/gad.cpp:123` (`layout.gadgets.resize(highest + 1)`, no cap on `Nummer`); the only `bad_alloc` handler is `MapViewer::replace_world` (`map_viewer.cpp:2312`)
- **Failure scenario:** A mod or map pack ships a 61-byte `.gad` with `Nummer: 2147483647` or a small `haeuser.cod` with many `ObjFill` lines: the game crashes at start or uses gigabytes. Local attack vector only: these files come from the game directory (`GameDirectory::resolve` is confined to the root); `mods/` provides only `factions.toml` today.
- **Evidence:** `exp_cod`: `.gad` `Nummer: 2147483647` → `std::bad_alloc`; `Nummer: 10000000` → 728 MB, 1.34 s. Nested `ObjFill`, 114 bytes, N = 1000 → 1 001 000 records, 401 MB; N ≥ 3000 → `bad_alloc` at a 3 GB cap. Verifier `flat_fill` (no nesting): 3 520 bytes → 1 000 001 records, 400 MB, 1.46 s. DET (fuzzing and the `o1602_mod_limits` probe): a 159-byte `haeuser.cod` with nested `ObjFill` passed 5 GB RSS within 30 s; `fuzz_cod` found an 844-byte input taking 8.9 s per run (libFuzzer OOM at 1 GB, reproduced by verify-DET); `fuzz_gad` found 6 OOM inputs, 2 timeouts and 1 slow input (repeated `Include` + `ObjFill: 0,MAXGADGET`) and 3 crash inputs where `gad.cpp:123` asks for 0x2600000000 bytes (2^31 × 76 B); `@Nummer: 200000000` passed 5 GB RSS, and `Nummer 5000` gives 5 001 gadgets without a warning. **Device fuzzing with the original data (input #1, root-caused):** a 48-byte `.gad` (one `Objekt GADGET` with `Nummer: <CR>@Id=45001`; a lone CR does not end a COD statement, `cod_parser.cpp:165`) makes 45 002 empty gadgets through `gad.cpp:123`; parsing takes 2-11 ms, but `GadgetLayout::index_of` (`gad.cpp:88-95`) is a linear scan, so looking up every gadget took 14.5 s. `Nummer: 2000000000` asks for about 152 GB. The game looks gadgets up by id every frame, so a sparse layout must also keep the lookup fast (a map or an id index, not a linear scan).
- **Solution:** A global record budget in the parser (for example 1 M records in all, counted in `obj_fill` and at `Objekt`, plus symbols and diagnostics, HI-3) that fails the parse with a diagnostic; preventing nesting alone is not enough. Cap `.gad` gadgets generously (for example 65 536; the RE notes `ui.md:456`, L-010, record that dropping the original's 300 limit was deliberate). Document both in `data-limits.md` and `gad.md`; regression tests; catch `std::bad_alloc` at the data-loading boundary as a last resort. The budget must count flat statements, not only nesting, and should fail rather than truncate (truncated tables would differ between machines). For gadgets a sparse map keyed by `Nummer` is preferable to any cap, because memory then follows the file and not the largest number. Keep DET's `fuzz_cod`/`fuzz_gad` OOM and timeout inputs and the 159-byte bomb (`review-2026-10/DET/crashes/`) as regression inputs, refused within 100 ms. The doc side is BT-20.

#### HI-4 [Medium] Smacker audio decompression bomb: a 1.2 KB `.smk` decodes to 128 MiB, with no total cap and no catch (earlier IO-2 fixed per chunk only)

- **Aliases:** RND-4 · **Dimensions:** D9, D3, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/video/src/smacker.cpp:58` (`kMaxFrames` 1 000 000), `:523-532` (`decode_dpcm`, 1 MiB per chunk only), `:602-653` (`Smacker::audio` appends without a total limit); caller `engine/app/src/game_sound.cpp:483` via `video_controller.cpp:61/85` → `start_video`, with no catch
- **Failure scenario:** A modded or corrupted `.smk` in the data folder declares many frames; each about 17-byte audio chunk expands to 1 MiB (absent Huffman trees read no bits). Playing it allocates gigabytes: `bad_alloc` and terminate, or the OOM killer on Linux.
- **Evidence:** `smk_bomb`: 64 frames, 1 196 bytes → 128 MiB in 863-971 ms; 256 frames, 4 460 bytes → 512 MiB in 3.0 s. Linear in frames.
- **Solution:** Bound total PCM by `frames × frame_us × rate × channels / 1e6` plus one frame of slack, checked before each append (or a fixed 64 MiB); reject chunks whose declared size exceeds the frame duration; catch exceptions around `start_video`. Test next to the HI-12 test.

#### HI-5 [Medium] `load_soldiers` is O(n² log n): a 4.5 MB hostile `.szs`/`.gam` hangs the loader for over a minute

- **Aliases:** SIM-13 · **Dimensions:** D2, D6, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/sim/src/world.cpp:849-887` (before each `add_soldier` it calls `world.soldier(id)`, which rebuilds `IdIndex` with a `stable_sort` because the size changed; the reference pass at `:879-886` is a further O(n²)); original-format files only (scenarios, `.gam`)
- **Failure scenario:** A broken or hostile scenario with 65 536 SOLDAT3 records (0x44 bytes each, slots `uint16_t`, duplicates accepted) hangs the load. Legitimate original files have at most 400 soldiers.
- **Evidence:** Verifier `bench_load` (same lookup-then-add pattern): 5 000 records 0.28 s, 20 000 records 6.7 s.
- **Solution:** Collect the slots in a set or sorted vector before adding; do the reference pass through a slot → index map; optionally cap SOLDAT3 records at a generous value.

#### HI-15 [Medium] A `.GAM` island of width or height 0 is accepted and played, but every save, autosave and multiplayer snapshot of that game fails to load (earlier IO-5 incomplete)

- **Aliases:** DET-1 · **Dimensions:** D1, D3, D7, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P6
- **Where:** `engine/sim/src/world.cpp:28-56` (`World::add_island` checks only the upper bounds), called for every `.GAM`/`.SZS` island by `add_islands` (`world.cpp:472-503`, call at `:499`) from `load_world` (`:1029`); `IslandMap::from_island` takes width and height straight from the u8 header (`island_map.cpp:163`, `engine/assets/src/island.cpp:27-29`); `sim::validate` (`validate.cpp:78-92`) checks bounds only; the `.o1602` reader refuses at `engine/sim/src/save/map.cpp:224-226` (`w == 0 || h == 0`) or earlier at the list count `map.cpp:211` (`count(64)`, `state_io.hpp:59-66`)
- **Failure scenario:** A damaged, crafted, third-party-editor or modded-template `.GAM`/`.SZS` has an `INSEL` record with width or height 0. `load_world` accepts it, the game runs, and every `.o1602` written from then on is refused with "save: damaged map" or "save: damaged island". In multiplayer `HostSession::send_game` (`host_session.cpp:669-678`) writes the snapshot successfully, and the client's `load_from_host` (`multiplayer.cpp:75-81`) fails, so `ClientSession::load` ends the session with `EndReason::LoadFailed` (`client_session.cpp:220-226`); join, resync and catch-up (`host_session.cpp:532, 641, 1124`) all use `send_game`, so nobody can join or resync that game. Clean refusal: no desync, no crash. Neither the original game (islands come from `.scp` templates) nor Open1602's generator or `.GAM` writer creates such an island.
- **Evidence:** `fuzz_gam_world` (load, validate, 4 ticks, validate, `write_game`, `read_game`): 8 crash files in 1 500 s, all this reload failure. Minimal reproducer `review-2026-10/DET/crashes/gam_world/zero-width-island.gam` (synthetic `small.gam` with byte 21 = 0) prints `save: damaged map … #3 0x15 at 40,30 … fuzz invariant broken: a loaded .GAM saved as .o1602 loads again`. Verifier variants: 15x0 fails, 1x1 fails (HI-16), 1x2, 2x1 and 20x15 round-trip. The multiplayer path is confirmed by code reading, not run. Severity: verifier Medium (DET said High), because only crafted or damaged input triggers it and the failure is clean; settled by device run DET-4: the scan of the original data (845 files, 2 741 islands) found the smallest island 30×30 and none under 2 tiles, so HI-15 stays Medium. **Device fuzzing with the original data (input #3, root-caused):** a 6 576-byte `.GAM` whose `INSEL5` header has width and height 0 (bytes +1, +2) reproduces exactly this: alone the island writes under 64 bytes and `count(64)` fails first ("damaged map", HI-16); with a 40×40 island after it, "damaged island".
- **Solution:** Make `World::add_island` return an error for `w == 0 || h == 0`, and let `add_islands` turn it into a warning and skip the island, as it already does when `add_island` fails (`world.cpp:499-501`), rather than failing the whole load. Add the same check to `sim::validate` so the save path refuses such a world before writing an unreadable file. Fix the reader's minimum record size together with it (HI-16). Unit test: every world that `validate` accepts round-trips through `write_game` → `read_game`, with one 0x8 and one 1x1 island as cases (the fuzz target already encodes this invariant).

#### HI-18 [Medium] The sprite and font loaders have no total pixel budget: a 1 KB sprite file asks for 8.5 GB

- **Aliases:** none (found in the device runs) · **Dimensions:** D9, D3, D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/assets/src/sprite_sheet.cpp:19, 75` (`kMaxDimension = 4096` per sprite only), `:103-107` (`decode` allocates `indices` and `opaque`, 2 × w × h bytes, before reading any RLE); `engine/ui/src/font.cpp:92-104` (`Font::from_sheet` keeps every decoded glyph); `engine/render/src/island_renderer.cpp:178-185` (decode cache with the same pattern, not tested)
- **Failure scenario:** A crafted or damaged `.bsh`/`.zei` file in the game folder or a mod: every sprite record may claim up to 4096×4096 pixels from a few bytes of RLE, and many offset entries may point at the same record. Loading a font or a sprite sheet then allocates gigabytes and the game is killed or throws `bad_alloc` at start. Local files only (the original data has no such sprites); same class as the `.cod`/`.gad` amplification (HI-2) and the Smacker audio bomb (HI-4).
- **Evidence:** Device fuzzing with the original data (input #2, root-caused): a 113 KB BSH chunk with 192 sprites each claiming about 2560-3840 × 3328-3840 px from about 590 bytes of RLE used 43 MB before `Font::from_sheet` and about 4 GB after; a synthetic 1 061-byte proof of concept (256 offset entries pointing at one 4096×4096 record with an empty RLE stream) ran out of memory at 8 508 MB.
- **Solution:** Refuse a sprite whose claimed pixels are implausible for its RLE byte count (each RLE byte can cover a bounded number of pixels per row, and each row needs at least one byte), and give each sheet and font a total pixel budget (for example 64 M pixels, far above the original's largest sheet), failing the load with a message. Decode lazily or share decoded images for repeated offsets. Keep the device inputs as regression tests next to HI-2's.

#### HI-19 [Medium] A ship's motion state in `.o1602` is loaded unchecked (soldiers likewise): a crafted save or a modified host's snapshot puts a ship outside the map after one tick

- **Aliases:** none (found in the device runs) · **Dimensions:** D9, D1, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P6
- **Where:** `engine/sim/src/save/ships.cpp:78-89` (phase, count, `elapsed_us`, `duration_us`, `from_x`, `from_y`, path read unchecked); `engine/sim/src/ships.cpp:496-501` (`Fleet::advance` sets `pos_x/pos_y` from `from_x/from_y` and casts the tile to `uint16_t`, which also wraps); load checks only test the tile, owner and island: `save/apply.cpp:110-113`, `validate.cpp:104-106`. Soldiers: `save/army.cpp:39-48`, `soldiers.cpp:145-150` (same pattern; code reading only). The snapshot path: `multiplayer.cpp:75-77` (`load_from_host` uses the same reader).
- **Failure scenario:** A crafted `.o1602` (the CRC-32 has no key, so anyone can reseal a file) or a modified multiplayer host's snapshot gives a ship in phase `Run` a `from_x` far outside the map and a long `duration_us`. The file loads; after one tick the ship sits outside the world (or, through the `uint16_t` wrap, somewhere else inside it). Open1602 never writes such a state (a run starts at the ship's tile centre, `ships.cpp:551-553`). Every client that loads the snapshot computes the same thing, so there is no desync, but the game is now invalid: every later save is refused on load ("ship outside the world", `apply.cpp:111`), so the game cannot be saved, resynced or migrated any more. **Out-of-bounds access checked (by code reading at `f5a46dd`):** the per-tick sim and render paths that use a ship's tile bound-check it or clamp (`SeaPlanner::plan` refuses a start outside, `sea_paths.cpp:86`; `SeaMap::sailable`; `LandMap::island_at`; pirate wander clamps to the map, `pirates.cpp:324-332`; exploration compares with island rectangles; the soldier grid clamps, `soldiers.cpp:60-61, 79-80`; render, tutorial and ship panel test `World::contains` first). The one unchecked read found is the `.GAM` export, `savegame_writer.cpp:1077` (`World::cell(s.x, s.y)`), which reads out of bounds when that player exports such a game to `.GAM`: a local action, not every client. So the finding stays Medium and is not a remote out-of-bounds access like MS-8.
- **Evidence:** Device fuzzing with the original data (input #4, root-caused): 13 056 bytes minimised to 7 937; synthetic seed world 160×120; ship 4 as loaded: tile 0,0, owner 3, phase Run, count 0, `duration_us` 0xFF000000 (about 71 min), `from_x` 6 488 063 (tile 1 583), `from_y` 0. `validate()` passes after load and fails after tick 1: "game: ship 4 outside the map or of an unknown player". Whether an out-of-map ship causes out-of-bounds reads at run time was not run; the code reading above found none on the tick and render paths.
- **Solution:** After loading (and for every snapshot), check each ship's and soldier's motion: for `Run`/`Turn`, `from_x/from_y` and the run's end inside the world and consistent with `x/y` (within `count` tiles in `direction`), `duration_us` bounded by the longest legal run or turn, `path_index ≤ path.size()`; otherwise reset the motion to `Idle` at the tile centre with a load warning, or refuse with `state_damaged`. Make `validate` check the same. Bound-check `savegame_writer.cpp:1077`. Add the device input as a save-fuzz regression case.

#### HI-3 [Low] The `.cod` symbol table and diagnostics are unbounded (about 95 bytes of memory per input byte), while `cod.md` describes the original's bounded table

- **Aliases:** IO-V2 · **Dimensions:** D9, D10, D7 · **Effort:** S · **Depends on:** HI-2 · **Fix bundle:** P20
- **Where:** `engine/assets/include/o1602/assets/cod_parser.hpp:48-59` (unbounded `unordered_map<std::string, Symbol>`, `sizeof(Symbol)` = 400); `cod_parser.cpp:337-341` (names of any length), `:170-176` (strings), `:364-366` (one diagnostic per new undefined name); `docs/formats/cod.md` §3 (650 entries, 31-character names, `char[64]` strings)
- **Failure scenario:** A modded file referring to many distinct undefined names. Linear growth, so less severe than HI-2. Names longer than 31 characters stay distinct, which may differ from the original.
- **Evidence:** Verifier `sym`: 7.9 MB file → 1 M symbols, 1 M diagnostics, 4.97 s, 739 MB.
- **Solution:** Fold into the HI-2 parse budget (stop recording diagnostics after 1 000) or a documented limit (65 536 symbols); state in `cod.md` which original limits are lifted on purpose.

#### HI-6 [Low] `parse_data_file` is quadratic in tables and keys

- **Aliases:** IO-7 · **Dimensions:** D2, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/core/src/data_file.cpp:270` (`std::any_of` over tables per new table), `:234` and `:295` (`DataTable::find`, linear, per key)
- **Failure scenario:** A hostile or very large settings or mod data file stalls start-up. Real inputs (`settings.cfg`, `factions.toml`) are small.
- **Evidence:** `exp_toml`: 20 k tables (169 KB) 0.63 s; 60 k tables (529 KB) 10.0 s; 60 k keys (649 KB) 9.2 s.
- **Solution:** A temporary `unordered_set<string_view>` for table names and keys during the parse, or caps (4 096 tables, 4 096 keys per table).

#### HI-7 [Low] A 17-byte BSH record can request a 4096 × 4096 decode (2 × 16 MiB allocated before any RLE byte is read)

- **Aliases:** IO-8 · **Dimensions:** D9, D4 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/assets/src/sprite_sheet.cpp:19` (`kMaxDimension = 4096`), `:103-105` (`indices` and `opaque` sized `width × height`)
- **Failure scenario:** A modded `.bsh` with many tiny records each claiming 4096²; the preloader decoding many sprites allocates gigabytes from a small file.
- **Evidence:** Code reading, confirmed.
- **Solution:** Bound the decoded size by a multiple of the record's encoded byte count (the record `length`, HI-8, gives a natural ratio), or cap total decode bytes per sheet.

#### HI-9 [Low] An absent required save section, a duplicated section and trailing bytes inside a section go unnoticed

- **Aliases:** IO-9 · **Dimensions:** D3, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P6
- **Where:** `engine/sim/src/save/sections.cpp` (reader fails only on an unknown required section, comment at `:140`; decoders do not check that they consumed the section; a duplicate section: the last wins, QUEU and STNC append)
- **Failure scenario:** A crafted or hand-edited save without PLAY or MAPW loads with defaults for that part; `validate()` catches some of it. Trailing garbage hides writer/reader version mismatches in tests. Accidental damage is caught by the CRC-32.
- **Evidence:** Code reading; the save fuzz tests do not delete or duplicate sections.
- **Solution:** A bitmask of sections seen, failing when an always-written section is missing; `in.remaining() != 0` at a section's end fails in debug and test builds and warns in release; refuse duplicate non-append sections; add delete/duplicate mutations to `test_save_fuzz`.

#### HI-10 [Low] `data-limits.md` says amounts are clamped and negative `Nummer` refused, but the loaders wrap and map to 0

- **Aliases:** IO-10, DET-5 · **Dimensions:** D10, D9 · **Effort:** M · **Depends on:** HI-2, HI-17 · **Fix bundle:** P20
- **Where:** `docs/modding/data-limits.md:10, 14-16` against `engine/assets/src/building_table.cpp:53` (`record = max(0, nummer)`), `:56-57` (`Size` 0 → 1), `:123` (`Maxlager × kStockUnitsPerTon`), `:237` (`Prozent × 128 / 100`), `figure_table.cpp:47` (`Maxtrag × kStockUnitsPerTon`): u32 products that wrap; plain `static_cast` narrowing after `non_negative` at `building_table.cpp:113-115` (`Ware`, `Rohstoff`, `Workstoff` → u8), `:120` (`Figurnr` → u16), `:121` (`Figuranz`), `:129` (`BGruppe`), `:136` (`Erzbergnr`), `:277`; also `:79-81` (`Ruinenr` → u8), `:99-100` (`RandAnz`/`RandAdd` plain `static_cast<std::uint16_t>`), `:152` (`Money`, no clamp; used at `construction.cpp:914, 1941`)
- **Failure scenario:** `Ware: 258` in a mod becomes ware 2 without a diagnostic; large amounts wrap to small ones. `Size 0` or a negative `Size` becomes 1 although `data-limits.md` says the file is refused. A negative `Money` makes construction pay the player: `next.money > available.money` (`construction.cpp:914`) never fails for a negative cost and `money_plus(p->money, -bill.money)` (`:1941`) adds it; mod data only, and the content fingerprint keeps multiplayer consistent. Only `Radius` and `Arbeiter` are actually clamped.
- **Evidence:** Code reading, extended by verify-IO. Measured by DET's `o1602_mod_limits` probe (`review-2026-10/DET/mod_limits.md`): `Nummer -1` → record 0; `Size 0, 0` / `-3, 2` → 1×1 / 1×2; `Maxlager`/`Maxtrag 200000000` → 2 105 032 704 (u32 wrap of ×32); `Ware`/`Erzbergnr`/`Ruinenr 300` → 44; `Rohstoff 257` → 1; `Figuranz`/`BGruppe 256`, `Figurnr 65536` → 0; `RandAnz -1` / `70000` → 65 535 / 4 464; `Money -1000` kept. Documented behaviour that holds: `Id` outside 20000-23500, `Nummer 65536`, `Size 256`, `Gfx 65535` and figure `Nummer` 1024/−1 are refused; the `.gad` include depth (4) and 1 MiB limit work; an extreme table (255×255 building, Id 23500, Nummer 65535) builds, stays valid for 200 ticks and round-trips. **Device (`o1602_mod_limits`, original-data machine):** the same table: `Maxlager 200000000` → 2 105 032 704, `Ware 300` → 44, `RandAnz -1` → 65 535; `Nummer -1`, `Size 0,0` and `Size -3,2` are accepted although documented as refused.
- **Solution:** Refuse a negative `Nummer`; use `core::saturating_cast` or a saturating multiply for products and narrowing, with one warning per file (rule of LC-15); extend the table in `data-limits.md`; table-driven negative tests (BT-9), with `o1602_mod_limits` as the oracle. Route every field through one helper `field<T>(r, key, lo, hi)` that saturates amounts and refuses or warns for ids (ware, figure, ruin, ore mountain) instead of wrapping; decide whether `Size ≤ 0` is refused or normalised and align the document; refuse a negative `Money`.

#### HI-11 [Low] A host snapshot is a hostile input for clients and is amplified by inflate (32 MiB → 256 MiB sections → about 1 GB peak)

- **Aliases:** IO-14 · **Dimensions:** D9, D4 · **Effort:** S · **Depends on:** DS-4 · **Fix bundle:** P9
- **Where:** `engine/app/src/multiplayer.cpp:75` (`load_from_host` runs `read_game` on host bytes); `engine/net/src/protocol.cpp:202` (`count(kMaxSnapshotBytes)`)
- **Failure scenario:** A hostile host sends a 32 MiB snapshot that inflates to 256 MiB of sections and about 1 GB of peak RSS during decode: memory pressure, not corruption (the loaders are bounds-checked).
- **Evidence:** Mechanism as DS-4 (IO-2 numbers).
- **Solution:** Lower the inflate cap for network snapshots, derived from the announced map size; decode streaming (DS-4).

#### HI-12 [Low] Smacker `frame_us` wraps in 32 bits: a crafted rate gives 0

- **Aliases:** RND-12 · **Dimensions:** D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/video/src/smacker.cpp:250-253`; `engine/app/src/video_player.cpp:128-138`
- **Failure scenario:** Rate 536870912 or INT32_MIN gives `frame_us = 0`: the video ends at once, or a looped video restarts on every update. No division by zero.
- **Evidence:** `smk_rate` prints `frame_us = 0` for both values.
- **Solution:** Compute in 64 bits and reject results that are 0 or above 1 s.

#### HI-13 [Low] Out-of-range figure-table gfx values throw `out_of_range` while the atlas is built; all zoom levels fail with an obscure message

- **Aliases:** RND-5 · **Dimensions:** D3, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/assets/include/o1602/assets/sprite_sheet.hpp:43` (`info` uses `.at()`); `engine/render/src/unit_tint.cpp:354-361` (`TintedUnitSource::size`), `flag_tint.cpp:46-47`; caught by `GraphicsPreloader::prepare` (`graphics_preloader.cpp:129-135`); `tools/o1602-tool/render.cpp:275` and data tests call `load_world_graphics` without a catch
- **Failure scenario:** A bad local mod: the game shows "graphics: vector::_M_range_check ..." for all three levels; the tool terminates.
- **Evidence:** Code reading, verifier.
- **Solution:** Validate `first->gfx + stride × variants` and `t.gfx + t.frames` against the sheet size in `load_world_graphics` and return an error naming the figure.

#### HI-16 [Low] A world whose islands average under 64 bytes in MAPW (for example one 1x1 island) also saves a file that does not load

- **Aliases:** DET-V1 · **Dimensions:** D1, D7 · **Effort:** S · **Depends on:** HI-15 · **Fix bundle:** P6
- **Where:** `engine/sim/src/save/map.cpp:209` (`in.in().count(64)`); `engine/sim/src/save/state_io.hpp:59-66`
- **Failure scenario:** The reader requires `islands × 64 ≤ remaining MAPW bytes`, but a 1x1 island record is about 60 bytes (header about 24, position and size 16, 20 per tile). A MAPW of only 1x1 islands, or many 1x1 islands and few larger ones, is refused as "save: damaged map" although every island is valid and `World::add_island` accepted it.
- **Evidence:** Verifier: `review-2026-10/verify-DET/i1x1.gam` (the HI-15 reproducer with a 1x1 island) breaks the same `fuzz_gam_world` invariant; 1x2 passes.
- **Solution:** Set the minimum to the real smallest record (the header's fixed fields with the string counted as its length prefix only, plus 16, plus one tile of 2 × 10 bytes: about 40), so `write_map` and `read_map` agree. Round-trip test with a 1x1 island. Part of the HI-15 fix.

#### HI-17 [Low] Signed integer overflow (undefined behaviour) for large `Wegspeed` and `Randwachs` in `haeuser.cod`

- **Aliases:** DET-4 · **Dimensions:** D1, D8, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/assets/src/building_table.cpp:85-86` (`std::clamp(percent * 32 / 100, 1, 126)`), `:96` (`r.integer("Randwachs", 0, 100) * 128 / 100`): an unclamped `int32` from the file is multiplied before the clamp
- **Failure scenario:** `Wegspeed: 100000000` or `Randwachs: 20000000` (or large negative values) overflow `int`. In practice it wraps (`walk_speed = 1`, `edge_growth = 0`), but the compiler may assume it cannot happen, which breaks the `data-limits.md` promise that every computer reads the same data the same way. Mod data only. Not covered by HI-10, which lists only the u32 products.
- **Evidence:** UBSan: `crashes/mod/fuzz_cod-wegspeed` → `building_table.cpp:86:76: runtime error: signed integer overflow: 100000000 * 32 cannot be represented in type 'int'` (reproduced by the verifier). Found by the `o1602_mod_limits` probe, not by the fuzzer. **Device:** `o1602_mod_limits` stops on the same UBSan report (`building_table.cpp:86`, `Wegspeed 100000000`).
- **Solution:** Clamp before multiplying (`std::clamp(percent, 0, 400) * 32 / 100`, `std::clamp(v, 0, 200) * 128 / 100`) or compute in `std::int64_t`; grep the table loaders for `r.integer(...) *` for other cases; unit test with both values.

#### HI-8 [Info] The BSH decoder ignores the record `length` that `bsh.md` documents and runs on to the end of the chunk

- **Aliases:** IO-V4 · **Dimensions:** D9, D10 · **Effort:** S · **Depends on:** HI-7 · **Fix bundle:** P20
- **Where:** `engine/assets/src/sprite_sheet.cpp:73` (read, never used), `:108-140` (RLE stops only at 0xFF or the end of the chunk payload); `docs/formats/bsh.md`
- **Failure scenario:** A damaged record without its 0xFF decodes the following records' bytes into its canvas: garbage pixels instead of an error. Bounded and memory-safe.
- **Evidence:** Code reading.
- **Solution:** Limit the reader to `[offset + 16, offset + length)` when `length` is plausible and fail on overrun; this also bounds HI-7.

#### HI-14 [Info] The cost-building merge in `apply` is O(costs × old costs), only on the `.GAM` extension path

- **Aliases:** IO-13 · **Dimensions:** D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/sim/src/save/apply.cpp:325-333`
- **Failure scenario:** Own saves are not affected (the list is empty before the merge). A hostile `.GAM` can inflate both sides, in proportion to its file size.
- **Evidence:** Verifier code reading.
- **Solution:** Sort and merge, or a map by building id.

### Limits and consistency (LC)

Limits that disagree across modules, type widths against the stress targets, and inconsistent conventions. The limits table follows the findings.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| LC-1 | Medium | No single home for limits: the same quantity has up to nine values across modules | P21 |
| LC-2 | Medium | Island numbers are 8 bits in assets and every state section: the generator silently caps at 250 islands, and a 257-island world saves but does not load | P10 |
| LC-3 | Medium | A selection holds at most 21 units (×10 groups): armies of thousands cannot practically be commanded | P22 |
| LC-4 | Medium | Hiring pirates offers only the first three opponents | P22 |
| LC-5 | Low | The resource bar shows the white shield for every banner from 4 on | P22 |
| LC-6 | Low | A save slot shows at most "4 humans" | P22 |
| LC-7 | Low | The new-game dialog shows the nearest offered value, not the one the game will use, and offers worlds only up to 1400 × 980 | P22 |
| LC-8 | Low | settings.cfg and a multiplayer host can raise the game-speed cap to 64, beyond the speed-8 tick budget | P22 |
| LC-9 | Low | Score counters are u16 and saturate below the 100 k-soldier target | P10 |
| LC-10 | Low | ISLE v2 widens deer spots and craters but leaves deposit coordinates and the player mask 8 bits | P10 |
| LC-12 | Low | Default rules cap units and ships far below the stress targets, and the targets are not documented | P30 |
| LC-13 | Low | Seven cities per island (the original's 3-bit field), with `8` literals beside the named constant | P21 |
| LC-14 | Low | Errors from limits reach the player as "the file is damaged" | P21 |
| LC-15 | Low | Loaders clamp, refuse, wrap or default without a rule per input source | P20 |
| LC-16 | Low | Duplicated constants and tables across modules (earlier SC-9, UI-14, AU10 pattern recurs) | P23 |
| LC-17 | Low | Game-relevant decisions are taken in the app layer (data fixes, fingerprint, default names, rule presets) | P26 |
| LC-18 | Low (unclear) | Unclear: a soldier ware range is said to be kept in 8 bits | — |
| LC-24 | Low | Chat and notice lines are echoed to stderr in the game's language: localised game texts in the log, against the English-log rule | P28 |
| LC-11 | Info | MAPW header width and height are written as u8 | P10 |
| LC-19 | Info | Small inconsistencies in `endless_setup.cpp` (unclamped indexes, hard-coded separators, class range) | P22 |
| LC-20 | Info | Naming: `color` and `colour`, German and English identifiers mixed; `World::kSea` doubles as the island-count limit | — |
| LC-21 | Info | Event handling is consistent; the simulation reads two interface buffers to drive logic | — |
| LC-22 | Info | Error-handling styles differ (`Result`, `bool load()`, `optional`, throwing `.at()`, six `catch` sites) | — |
| LC-23 | Info | Static analysis: no real defect; `uint16_t` island loops tie the island count to 65 535, and a few widening casts and unstated invariants | P21 |

#### LC-1 [Medium] No single home for limits: the same quantity has up to nine values across modules

- **Aliases:** CON-1, SIM-16, SIM-23, NET-14 · **Dimensions:** D7, D6 · **Effort:** M · **Depends on:** — · **Fix bundle:** P21
- **Where:** Players: `sim::kMaxPlayers = 12` (`player.hpp:214`), `WorldSettings::kMaxPlayers = 16` (`world_plan.hpp:38`, `world_plan.cpp:744`), original 4/7, highest id 14 (`player_slot`, `player.hpp:224`), `validate` 254, `kMaxLobbySeats = 255` and `kMaxRosterMembers = 256` (`protocol.hpp:33, 41-42`; the roster limit also caps `Desync::seats`, `protocol.cpp:259`), menu `kMaxSeats = 100` (`multiplayer_menu.cpp:59`), ENet 32 connections. Islands: 50, 250, 255, 256, 65535. World side: 1400, 2000, 4096. Units per order: 21, 512, 1024, 4096. Name length 40 in three copies. See the limits table below.
- **Failure scenario:** Consequences already found: goodwill sized 7 (GB-1); more than 256 islands save but do not load (LC-2); 13-16 players pass `plan_world` and fail in `add_players` (`new_game.cpp:288`), the o1602-tool clamps to 16 (BT-13); `HostSession` never caps peers at `kMaxRosterMembers - 1`, so with another transport and more than 255 members every client fails to decode `Roster`; seats 100-254 cannot be shown; large worlds load but cannot be saved (DS-4); game speed capped at 8 by the dialog and 64 by settings (LC-8). Every per-player array must be sized `players().size()` (15); goodwill is the only live one that is not. Device run with the original data: a 16-player stress run fails with "generator: no room for 16 players", so the generator offers (via `WorldSettings::kMaxPlayers`) a player count it cannot place, in addition to `add_players` refusing 13-16.
- **Evidence:** MODCON `sweep.out` (player, island, world, unit and size limits); the cited parts' findings. Device: `o1602-stress` with 16 players → "generator: no room for 16 players" (run failed).
- **Solution:** One rule: every limit checked by more than one module is defined once, in `engine/sim/include/o1602/sim/limits.hpp` (sim quantities) or `net/protocol.hpp` (wire), with a comment naming the type that bounds it; other modules derive (`static_assert(kMaxLobbySeats >= kMaxPlayers + 3)`, `kMaxSeats = kMaxLobbySeats`); `player_capacity(world)` with a debug assert at each `[PlayerId]` index; `plan_world` checks `sim::kMaxPlayers` first and `WorldSettings::kMaxPlayers` is deleted; `net::kMaxMembers` with `Full` in `HostSession`; `kMaxLobbySeats` for `Desync::seats`. Test `test_limits.cpp` asserting the relations plus a round trip at each declared maximum (12 players, 256/257 islands, a 4096² world).

#### LC-2 [Medium] Island numbers are 8 bits in assets and every state section: the generator silently caps at 250 islands, and a 257-island world saves but does not load

- **Aliases:** IO-4, SIM-10, LST-6 · **Dimensions:** D6, D7, D3 · **Effort:** S (one limit), M (widen formats) · **Depends on:** DS-3 if widened · **Fix bundle:** P10
- **Where:** `engine/assets/include/o1602/assets/island.hpp:17` (`IslandHeader::number` `uint8_t`); `engine/sim/src/save/islands.cpp:16`; `save/map.cpp:59, 77`; EPOC (`save/world.cpp:97`), FLEE `discovered`; `engine/sim/src/validate.cpp:67-77` (`std::array<bool, 256>`, duplicates refused); `engine/sim/src/world_plan.cpp:764-765` (clamp to 250, "island numbers are 8 bit"); against `world.cpp:36-37` (`add_island` allows 65 535) and ADR 0007 lines 106-107, 148 ("16 bits"). Net commands carry the island index as `uint16_t` and are not affected.
- **Failure scenario:** On generated worlds from about 1 540² (Normal), 1 326² (Many) or 1 842² (Few) the clamp is active: at 2000² all three island-amount settings give 250 islands instead of 421 (Normal) or 568 (Many), so the setting has no effect and density falls to 44-60 %. The stress target "as many islands as fit" is not met inside the generator's own range. A world with more than 256 islands (not reachable from loaded content today: `add_islands`, `world.cpp:499`, calls `add_island` for every `.GAM`/`.SZS` island, but the original's island numbers are u8 as well) is written, and `read_game` refuses it: the save is lost.
- **Evidence:** LST: 1 444 and 10 000 islands, 3 of 3 round trips fail with "two islands with the number 0"; 256 islands round-trip equal. IO's original scenario ("a loader, mod or scenario adds more than 256") cannot happen and is withdrawn; the generator effect is the verifier's.
- **Solution:** One `kMaxIslands` in `limits.hpp` (LC-1). Either widen the number to u16 in new section versions (ISLE v3, MAPW target, EPOC, FLEE) following the `kWideCoordinates` pattern, with the `.GAM` export keeping u8 and refusing above 255; or make `add_island` refuse above 256 so the error comes at creation. Show or warn in the start dialog when the clamp is active. Round-trip test with 257 islands.

#### LC-3 [Medium] A selection holds at most 21 units (×10 groups): armies of thousands cannot practically be commanded

- **Aliases:** APP-3 · **Dimensions:** D6, D7 · **Effort:** M · **Depends on:** MS-10 · **Fix bundle:** P22
- **Where:** `engine/ui/include/o1602/ui/unit_selection.hpp:36` (`kMaxUnits = 21`), truncation in `set`, `toggle`, `add_to_group`, `recall_group` (`unit_selection.cpp:138, 195, 433, 460`), `kGroups = 10`; `box()` keeps candidates in `world.soldiers()` order
- **Failure scenario:** A box over 300 own soldiers selects the first 21 in world-list order; moving 2 000 takes about 95 selections. The AI orders whole armies at once (TB-2). The limit is the original's formation table (0x004E878C); this is a design limit for very large armies (D6), a deliberate-deviation candidate under ADR 0006/0010, not wrong behaviour.
- **Evidence:** Code reading, confirmed. Double-click `select_alike` exists (`map_input.cpp:1186`) but is capped at 21 too.
- **Solution:** Decouple the selection from formation slots: unlimited selection; formations place the first 21 by the table and the rest in extra rows by the same pattern; keep 21 portraits as a page or summary. Large orders need the per-order cap and chunking of the host's command path (MS-10). Tests: a box over 100 units selects 100; offsets stay within int16.

#### LC-4 [Medium] Hiring pirates offers only the first three opponents

- **Aliases:** APP-8 · **Dimensions:** D6, D7 · **Effort:** S · **Depends on:** BT-2 (panel tests) · **Fix bundle:** P22
- **Where:** `engine/app/src/ship_panel.cpp:611` (`kPirateChoice` + player 0..2), `:1731-1741` (`for k < 3`), `:1757` (`e.id < kPirateChoice + 3`)
- **Failure scenario:** In a 12-player game opponents 4 to 11 can never be the pirates' target.
- **Evidence:** Code reading, confirmed.
- **Solution:** Page the three PIRATORD slots like the diplomacy strip, with scroll arrows from TRANSFER.GAD as `EndScreen` does (`end_screen.cpp:56-80`). Test with 6 opponents.

#### LC-5 [Low] The resource bar shows the white shield for every banner from 4 on

- **Aliases:** APP-11 · **Dimensions:** D6, D5 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/hud.cpp:323` (`icon(m_gfx_baumat + 17 + std::min(r.shield, 3), 666)`)
- **Failure scenario:** Players 5 to 12 see a white shield, the same as the white player's.
- **Evidence:** Code reading.
- **Solution:** Draw with `set_banner_graphic` and a tint, as the end screen and list panels do.

#### LC-6 [Low] A save slot shows at most "4 humans"

- **Aliases:** APP-12 · **Dimensions:** D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/options_panel.cpp:229` (`std::min<std::int32_t>(humans, 4)`)
- **Failure scenario:** A 12-human multiplayer save shows the same icon as a 4-human one.
- **Evidence:** Code reading.
- **Solution:** Show the number as text next to the icon above 4.

#### LC-7 [Low] The new-game dialog shows the nearest offered value, not the one the game will use, and offers worlds only up to 1400 × 980

- **Aliases:** APP-V1, APP-27 · **Dimensions:** D1, D10, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/endless_setup.cpp:302-401` (`setup_choice` with `nearest`, `std::clamp`), `:841-847` (`setup_value`), `:849-951` (`read_setup_line` accepts wider values), `:28-32` (`kSizes` up to 1400 × 980); shown by `game_settings_menu.cpp:193, 196`
- **Failure scenario:** settings.cfg or a host says `world = 2000x2000`, `players = 1`, `victory.minutes = 90`, `game_speed = 64`, `ships = 150`; the dialog shows 1400 × 980, 2, 1 h, 8× and 100, and the game starts with the real values. One click on Players jumps from 1 to 3. The generator allows 2000 and loading 4096.
- **Evidence:** Verifier reproducer `setup_display`: shown values against `actual: world 2000x2000 players 1 ... minutes 90 speed 64 ships 150`.
- **Solution:** Show a non-offered value exactly, marked as custom (for example "2000 × 2000 (settings.cfg)"), and cycle to the next offered value above it; add 2000 × 1400 and 2000 × 2000 entries. Test that `setup_value` round-trips a non-offered value.

#### LC-8 [Low] settings.cfg and a multiplayer host can raise the game-speed cap to 64, beyond the speed-8 tick budget

- **Aliases:** APP-V3 · **Dimensions:** D2, D9 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/endless_setup.cpp:878-881` (accepts 1-64); `engine/sim/src/simulation.cpp:223` (clamps to `max_game_speed`); `engine/app/src/map_input.cpp:428`; `multiplayer_menu.cpp:158` (host setup text through `read_setup`)
- **Failure scenario:** A host with `game_speed = 64` lets players request 8× more ticks per second than the budget allows; in lockstep everyone waits on the slowest machine, and the dialog shows "8×" (LC-7).
- **Evidence:** Code reading, verifier.
- **Solution:** Cap at the dialog's highest value (8) unless a documented debug switch is set, or clamp in `multiplayer_setup` and document higher speeds as single-player only.

#### LC-9 [Low] Score counters are u16 and saturate below the 100 k-soldier target

- **Aliases:** IO-5 · **Dimensions:** D6, D1 · **Effort:** S · **Depends on:** — · **Fix bundle:** P10
- **Where:** `engine/sim/include/o1602/sim/player.hpp:106-108`, `engine/sim/src/save/players.cpp:163-166` (SCOR); incremented through `add_count` (`soldiers.cpp:570`, `ships.cpp:471`); `score.cpp` widens only after reading
- **Failure scenario:** In a long game with 100 k soldiers the kill and loss counters stop at 65 535, so the fight score stops growing.
- **Evidence:** Types (code); not measured in a game.
- **Solution:** u32 counters with a SCOR v2 using the version-dependent `io_` pattern; keep u16 saturated in the `.GAM` export.

#### LC-10 [Low] ISLE v2 widens deer spots and craters but leaves deposit coordinates and the player mask 8 bits

- **Aliases:** IO-6 · **Dimensions:** D6, D7 · **Effort:** S · **Depends on:** LC-2 (same version bump) · **Fix bundle:** P10
- **Where:** `engine/sim/src/save/islands.cpp:34-40` (deposit `ware/x/y/mask/kind` as u8) against `:20-29` (`io_coordinate`); `IslandHeader::Deposit` is u8; `mask` holds only the first 8 players
- **Failure scenario:** An island wider than 255 tiles cannot hold a deposit beyond x/y 255; the model type makes it impossible rather than truncating.
- **Evidence:** Code reading, confirmed.
- **Solution:** Make deposit x/y `io_coordinate` and widen the model type in the next ISLE version, or document that islands stay at most 255 wide (as `building_table` and the writer assume).

#### LC-12 [Low] Default rules cap units and ships far below the stress targets, and the targets are not documented

- **Aliases:** LST-15 · **Dimensions:** D6, D5, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** `engine/sim/include/o1602/sim/rules.hpp:130-133` (`max_ships_per_player = 100`, `max_army = 250`; original 33 / 100 at `:173-174`)
- **Failure scenario:** 12 players can have at most 3 000 soldiers and 1 200 ships; the targets are 100 000 and 20 000. Even the capped numbers are over budget (TB-1, TB-5).
- **Evidence:** Code reading (not re-checked by the verifier).
- **Solution:** Decide which numbers are targets and document them in `docs/performance.md`.

#### LC-13 [Low] Seven cities per island (the original's 3-bit field), with `8` literals beside the named constant

- **Aliases:** MOD-5 · **Dimensions:** D5, D6, D7 · **Effort:** S · **Depends on:** LC-1 · **Fix bundle:** P21
- **Where:** `engine/sim/src/construction.cpp:27, 848, 1549-1552` (`kMaxCitiesPerIsland = 7`); `:992, 999` (8-bit mask `1U << cell.city`), `:1009`; `island_events.cpp:125` (`std::array<const City*, 8>`); `savegame_writer.cpp:453, 1102`
- **Failure scenario:** On very large islands only 7 of 12 players can found a city on the same island; the masks and arrays must change together with the constant.
- **Evidence:** MODCON code reading.
- **Solution:** One constant in `limits.hpp` used by all sites, the original's 7 kept in the `.GAM` writer only; if more are wanted, `std::bitset<kMaxCitiesPerIsland>` masks.

#### LC-14 [Low] Errors from limits reach the player as "the file is damaged"

- **Aliases:** CON-3 · **Dimensions:** D7, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P21
- **Where:** `engine/ui/src/localization.cpp:257-270` maps `OutOfRange` and `InvalidFormat` to `error.damaged` (`data/locale/en.lang:323`); `OutOfRange` is used by `save_game.cpp:90` (too large, on saving), `new_game.cpp:237, 289`, `world_plan.cpp:742, 745`, `world.cpp:33, 37`
- **Failure scenario:** Saving a large world shows "Save failed: the file is damaged or no savegame"; impossible start settings show the same text (`map_viewer.cpp:2509`).
- **Evidence:** Code reading.
- **Solution:** `ErrorCode::LimitExceeded` with its own text naming the limit (`error.limit` with `{what}` and `{max}`); `OutOfRange` stays for indexes in data. Review the 19 `OutOfRange` sites once.

#### LC-15 [Low] Loaders clamp, refuse, wrap or default without a rule per input source

- **Aliases:** CON-4 · **Dimensions:** D7, D9 · **Effort:** S (rule) plus the merged fixes · **Depends on:** — · **Fix bundle:** P20
- **Where:** Original data: refuse (`Id` > 23500), silent 0 (`Nummer < 0`, `building_table.cpp:53`), 1 (`Size 0`), wrap (amount products), clamp (`speed_type`, `ObjFill`). Settings and tool options: silent default (BT-13). Mod TOML: warn and skip (`faction.cpp:98-100`). Own saves: structure refused, `RULE` clamped (`save/world.cpp:105`), AI state unchecked (HI-1), absent sections undetected (HI-9). Network: refuse.
- **Failure scenario:** The same out-of-range value is a crash in one place (HI-1), a silent change in another (`Nummer`), a warning in a third; modders cannot predict which.
- **Evidence:** MODCON code reading.
- **Solution:** One rule per source, written into `CONTRIBUTING.md` and `data-limits.md`: (1) original data: refuse what indexes tables or sizes memory, saturate amounts with one warning per file; (2) mod data: as 1 with file and line, a refused file disables the package, not the game; (3) settings and CLI: refuse with usage (CLI) or warn and default (settings.cfg), never silently; (4) own saves and snapshots: refuse anything out of range, semantic validation after load; (5) network: refuse and drop the peer.

#### LC-16 [Low] Duplicated constants and tables across modules (earlier SC-9, UI-14, AU10 pattern recurs)

- **Aliases:** CON-5 · **Dimensions:** D7 · **Effort:** S · **Depends on:** MD-1 · **Fix bundle:** P23
- **Where:** `kTon = 32` in `ships.cpp:33`, `trader.cpp:16`, `ship_orders.cpp:14`, `ai_engine.hpp:92`, `tutorial.cpp:51` beside `assets::kStockUnitsPerTon`; `kTemplateCount` twice (MD-7); `kUnitFamilies` twice (`game_data.hpp:248`, `figure_table.hpp:153`); `kMaxTribute` in `diplomacy.cpp:630` and `diplomacy_panel.cpp:28`; name length 40 three times; `kFull = 128` and `kUniformSpeed = 3` twice; the colour-name fallback (MD-10) and the mod-folder enumeration (DS-5) twice
- **Failure scenario:** A change in one copy (for example the tribute cap) is not seen by the other: the slider offers what the sim refuses.
- **Evidence:** MODCON `sweep.out`.
- **Solution:** A number used by two translation units lives in a header of the lowest module that needs it; UI limits reference the sim constant; the grep test of MD-1 also catches `kTon =`.

#### LC-17 [Low] Game-relevant decisions are taken in the app layer (data fixes, fingerprint, default names, rule presets)

- **Aliases:** CON-6 · **Dimensions:** D7, D8 · **Effort:** M · **Depends on:** DS-5 · **Fix bundle:** P26
- **Where:** `engine/app/src/map_viewer.cpp:210-219` (data fixes per computer, DS-5); `multiplayer.cpp:26-36` (content fingerprint); `map_viewer.cpp:142-148` (`name_more_players`); `endless_setup.cpp:27-51` (rule value tables); `menus` links `render` without a layering rule (BT-10)
- **Failure scenario:** A dedicated server or replay tool (ADR 0005: sim runnable headless) has to copy app code to start the same game; fingerprint and fixes can diverge between programs.
- **Evidence:** MODCON code reading.
- **Solution:** Whatever changes what the simulation computes lives in `sim` or `assets` behind one `GameSetup → World` function shared by game, tool and tests; `app` only chooses. Add the `menus` rule to `CheckLayering.cmake`.

#### LC-18 [Low] Unclear: a soldier ware range is said to be kept in 8 bits

- **Aliases:** SIM-21 · **Dimensions:** D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Unclear.** What would settle it: The report names no concrete field; `soldier.hpp` has no "ware range" field and its `uint8_t` fields are in range by construction. Name the field or drop the finding.
- **Where:** `engine/sim/include/o1602/sim/soldier.hpp`, `engine/sim/src/soldiers.cpp`
- **Failure scenario:** Mod data with larger values would be truncated silently (suspected).
- **Evidence:** None concrete.
- **Solution:** If a field is found: clamp at load with a warning.

#### LC-24 [Low] Chat and notice lines are echoed to stderr in the game's language: localised game texts in the log, against the English-log rule

- **Aliases:** none (found in the device runs) · **Dimensions:** D7, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/app/src/event_presenter.cpp:425` (`EventPresenter::notify` prints every notice, built with `ui::tr(...)` in the installed game's language, e.g. `map_viewer.cpp:2156, 2192, 2273, 2584, 2595`, `multiplayer.cpp:141, 229`); `event_presenter.cpp:90-91` (`add_chat_line` echoes chat as `multiplayer: name: text`); rule in `CONTRIBUTING.md:17-20` ("log and error messages" in English; in-game texts follow the game's language)
- **Failure scenario:** With a German installation the log mixes English lines (`multiplayer: in as …`) with German notice texts and chat. Log-based diagnosis (bug reports, the review's own log checks for "diverged"/"lagging") has to know every language's texts, and chat (player-written content) ends up in logs users attach to bug reports.
- **Evidence:** Device run 8 (loopback multiplayer, German original data): chat and notice lines on stderr are in German. Code as cited.
- **Solution:** Log notices by key and arguments in English (`notice: notice.game_saved spiel03.o1602`) or from the English catalogue, not the localised text; log chat only as `multiplayer: chat from <player> (<n> bytes)` or behind a debug option.

#### LC-11 [Info] MAPW header width and height are written as u8

- **Aliases:** IO-16 · **Dimensions:** D7 · **Effort:** S · **Depends on:** LC-2 · **Fix bundle:** P10
- **Where:** `engine/sim/src/save/map.cpp:78-79`; `IslandHeader::width/height` are u8 in the model (`island.hpp:18-19`)
- **Failure scenario:** No harm (the loaded map uses its own u32 size); misleading, and truncates for islands wider than 255.
- **Evidence:** Code reading.
- **Solution:** Use `io_coordinate` or drop the fields in the next MAPW version.

#### LC-19 [Info] Small inconsistencies in `endless_setup.cpp` (unclamped indexes, hard-coded separators, class range)

- **Aliases:** APP-V4 · **Dimensions:** D5, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P22
- **Where:** `engine/app/src/endless_setup.cpp:979, 981, 982` (`kAmountNames[...]` without the `std::min` clamp other lines use); `:584-585, 606` (separators `"; "`, `": "` not from `tr`); `:44-45` (`kFirstWinClass = 1`, `kLastWinClass = 4`); `:770` ("automatic" labelled as 500 × 350); `:228-235` (`operator==` ignores `difficulty` and `seed`, uncommented)
- **Failure scenario:** Safe today because all writers validate; inconsistent.
- **Evidence:** Verifier code reading.
- **Solution:** Clamp via a shared helper; separators from `tr`; class range from data; comment the `operator==` choice.

#### LC-20 [Info] Naming: `color` and `colour`, German and English identifiers mixed; `World::kSea` doubles as the island-count limit

- **Aliases:** CON-7 · **Dimensions:** D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** 199 `color` against 529 `colour` identifiers (`assets::PlayerColor`, `kDefaultColors` beside `four_colour_index`); `kKapelle`, `kFigtypKavalerie` beside `prod::kWorkshop`; `world.cpp:32, 36`
- **Failure scenario:** —
- **Evidence:** MODCON sweep.
- **Solution:** British spelling in new identifiers; original German only in `// SYMBOL` comments; give the island limit its own name (LC-1).

#### LC-21 [Info] Event handling is consistent; the simulation reads two interface buffers to drive logic

- **Aliases:** CON-8 · **Dimensions:** D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** All 12 streams use `EventBuffer<T>`, trimmed every tick (`simulation.cpp:601-610`); `Simulation::handle` over `m_construction_events` (`:484-515`) and `m_logistics.handle` over `m_production_events` (`:636-639`) index from `first`
- **Failure scenario:** Safe today because only `sink()` appends inside a step and trimming happens at the tick's start.
- **Evidence:** MODCON code reading.
- **Solution:** Document in `event_buffer.hpp`: never `push`/`append` (which trim) between taking `first` and handling.

#### LC-22 [Info] Error-handling styles differ (`Result`, `bool load()`, `optional`, throwing `.at()`, six `catch` sites)

- **Aliases:** CON-9 · **Dimensions:** D7, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** `bool load()` in four subsystem loaders (BT-15); `std::optional` scenario sub-loaders (`scenario.hpp:260, 264`); `std::map::at` in `savegame_writer.cpp:1020, 1045, 1141`; six `catch` sites (FB-26)
- **Failure scenario:** "Absent" and "damaged" look alike; a missing slot throws.
- **Evidence:** MODCON sweep.
- **Solution:** Every loader returns `Result` with a message; `optional` only for "may be absent"; no throwing accessors in engine code; one `catch (const std::exception&)` barrier per thread entry.

#### LC-23 [Info] Static analysis: no real defect; `uint16_t` island loops tie the island count to 65 535, and a few widening casts and unstated invariants

- **Aliases:** DET-9 · **Dimensions:** D7, D6 · **Effort:** S · **Depends on:** — · **Fix bundle:** P21
- **Where:** `bugprone-too-small-loop-variable` (15): `std::uint16_t` loops over islands and island maps (`simulation.cpp:692`, `houses.cpp:604`, `logistics.cpp:1512, 1528`, …), bounded by `World::add_island`'s cap of 65 535 (`world.cpp:32-37`); `bugprone-misplaced-widening-cast` (8: `ambient.cpp:409/480/481`, `ai_planner.cpp:113/115`, `logistics.cpp:1202`, `soldiers.cpp:111`, `panel.cpp:357`); `bugprone-unchecked-optional-access` (80, all guarded or backed by an unstated invariant, e.g. `logistics.cpp:1415`, `save/apply.cpp:182`); cppcheck `unsignedLessThanZero` at `map_viewer.cpp:939`, `duplicateCondition` at `soldiers.cpp:573`
- **Failure scenario:** The island loops would never end if the island limit were raised to 65 536 (LC-2 discusses widening island numbers). The widening casts multiply in `int` first; no overflow at today's world sizes (≤ 4096²).
- **Evidence:** cppcheck 2.13 and clang-tidy 18 over `engine/` (counts in "Scope and method"); verifier spot-checked the loop bound and `map_viewer.cpp:939` but did not re-run the tools.
- **Solution:** Index islands with `std::size_t`/`std::uint32_t`; cast operands, not the product; state the invariants with an assert where the optional is dereferenced. Consider a clang-tidy job with the review config (`tools/fuzz/clang-tidy-review.yaml`, BT-22).

#### Limits in one place

From the cross-cutting pass (MODCON), with the part IDs replaced by the merged IDs. File names in this table are abbreviated; the full paths are in the findings named in the last column.

| Quantity | Value | Where | Who relies on it | Conflict |
|---|---|---|---|---|
| Players with a start | 12 | `sim::kMaxPlayers`, `player.hpp:214` | UI (`endless_setup.cpp:35,688,926`, `multiplayer_menu.cpp:164,178,823`, `main.cpp:511`), `new_game.cpp:288`, `diplomacy_strip.hpp:31`, `banner.cpp:8` (12 colours) | vs 16 below |
| Players in world plan / tool | 16 | `WorldSettings::kMaxPlayers`, `world_plan.hpp:38`, `world_plan.cpp:744`; tool `bench.cpp:153`, literal 16 `savegames.cpp:232` | `plan_world`, o1602-tool | 13–16 pass plan, fail in `add_players` (LC-1, BT-13) |
| Original players / slots | 4 / 7 | `kOriginalPlayers`, `kFirstExtraPlayer`, `assets::kOriginalPlayerSlots`, `new_game.cpp:28` `kOriginalColours`, `world_plan.cpp:42` `kPlanPlayers`, `world_generator.cpp:1129` `kPlayers = 4` | slot mapping 0–3, then 7…; goodwill array 7 | goodwill not resized for ids 7–14 (GB-1 High) |
| Highest player id / slot count | 14 / 15 | `player_slot()` `player.hpp:224` | every per-player container | must be `players().size()`, not 12 (LC-1) |
| PlayerId range | 0–254 (255 = none) | `kMaxPlayerCount`, `validate.cpp:64` | save validation | ADR 0007 says "254 slots" |
| Lobby seats / roster | 255 / 256 | `protocol.hpp:41,33` | wire decode; `Desync::seats` uses roster limit | host never caps peers (LC-1) |
| Menu seats | 100 | `multiplayer_menu.cpp:59` | gadget id ranges | seats 100–254 not shown |
| Connections | 32 | `enet_transport.hpp:22` | host | spectators unlimited (MS-12) |
| Player bits in deposit mask | 8 | `save/islands.cpp:34-40` | ISLE | rest in XPLR (LC-10) |
| Islands (number) | 8 bit: 256 | `assets/island.hpp:17`, `save/islands.cpp:16`, `save/map.cpp:59,77`, `validate.cpp:70` | sections, validation | vs 65535 in `world.cpp:36-37`, 250 in `world_plan.cpp:765`, 255 in `savegame_writer.cpp:30`, 50 in `scenario.cpp:304`, ADR "16 bits" (LC-2) |
| Island index | u16 | `WorldIsland`, commands | sim, net | fine |
| Island width | 255 | `IslandHeader::width` u8, `building_table` `Size ≤ 255`, deposits u8, MAPW header u8 | ISLE, MAPW, `.GAM` | `add_island` allows 65535 (LC-10, LC-11); ADR "no size limit" |
| Cities per island | 7 | `construction.cpp:27` | build, found | `< 8` literals (LC-13) |
| World side | 4096 | `World::kMaxSide` | load | generator 2000 (`world_plan.hpp:37`), dialog 1400×980 (`endless_setup.cpp:32`, LC-7), saves fail above about 80 % land (DS-4), minimap full RGBA (FB-5) |
| Units in a selection | 21 (×10 groups) | `unit_selection.hpp:36` | UI | AI orders whole armies (TB-2); LC-3 |
| Items in a drag | 512 | `build_tool.cpp:14` | UI | — |
| Items in one command | 4096 | `command_io.hpp:23` | decode | host's own commands not checked (MS-10); a lifted selection would hit it |
| Commands per message / frame | 1024 / 1024÷players | `protocol.hpp:31`, `host_session.cpp:236-256` | net | silent drop (MS-17) |
| Army / ships per player | 250 / 100 (orig 100 / 33) | `rules.hpp:130-133,173-174` | sim | targets 100 000 / 20 000 (LC-12) |
| Score counters | 65535 | `player.hpp:106-108`, `save/players.cpp:163` | end screen | LC-9 |
| Name length | 40 (3 copies) / 16 / 18 / 19 / 255 | `simulation.hpp:69,95`, `ships.hpp:158`, `name_edit.hpp:50`, `options_panel.cpp:512`, `main_menu.cpp:833`, `command_io.hpp:24` | commands, UI | UI ≤ sim: fine, but three copies of 40 |
| Game speed | 8 (orig 4) / 64 | `rules.hpp:139`; settings and host | lockstep | LC-8 (the SIMU `speed` is capped by the rules on load, `simulation.cpp:223`) |
| Tribute | 10 000 | `diplomacy.cpp:630` and `diplomacy_panel.cpp:28` | sim, UI | two copies |
| Save sections (uncompressed) | 256 MiB | `save_game.cpp:27` | save, load, resync | checked after deflate (DS-4) |
| Snapshot (compressed) | 32 MiB | `protocol.hpp:34` | join, resync, migration | client ENet default 32 MiB, message is +22 B (MS-11) |
| Transport message | 32 MiB + 64 KiB | `transport.hpp:18` | host ENet | — |
| Message to host | 1 MiB | `protocol.hpp:37` | host | — |
| File read | 1 GiB | `core/file.hpp:31` | all loaders | — |
| Inflate ratio | 1032 | `compress.cpp:43` | all zlib | — |
| Pending events per buffer | 8192 | `event_buffer.hpp:19` | UI | fine (trimmed every tick, `simulation.cpp:601-610`) |
| Resident classes | data (`resident_classes()`) | `BuildingTable` | `City` vectors | 5 in rules and UI (MD-2) |
| Growth classes | 32 | `world.hpp:369` | growth | fine (original) |
| Unlock levels | 32 | `player.hpp:127` | build, AI, panels | MD-8 |
| Building ids / figures / sprites | 20000–23500 / 1024 / 65535 | `building_table.cpp:22,25`, `figure_table.cpp:23`, `MapCell::sprite` u16 | loaders | data-limits.md differs (HI-10, BT-10) |

### Modding (MD)

Game content compiled into code (D5) against ADR 0007. The order of work for a content pack is in the fix bundles P23-P26.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| MD-1 | Medium | "Game numbers are named in one place" is not true: about 20 tables of ids live outside `game_data.hpp`, and a missing meadow record makes demolition report success but leave the building | P23 |
| MD-2 | Medium | Resident classes are data in the model, but five classes and the original's public-building services are compiled into the rules | P24 |
| MD-3 | Medium | Ware numbering is structural: u8 ids in three ranges laid end to end, with literal ranges in sim, AI, UI and render | P24 |
| MD-4 | Medium | The AI's building knowledge is compiled in; a mod's buildings and wares are invisible to computers | P25 |
| MD-5 | Medium | The build menu is a fixed table of the original's ids although its header and `status.md` say it is data | P25 |
| MD-6 | Low | Castle and shipyard panels hard-code four colours and figure-id arithmetic | P25 |
| MD-7 | Low | Island templates, climates and size classes are compiled in, and the template table exists twice | P26 |
| MD-8 | Low | Unlock levels (INFRA) are a 32-bit mask with literal levels in AI, panels and pirates | P24 |
| MD-9 | Low | Balance values are compile-time only (about 450 named constants) | P26 |
| MD-10 | Low | English fallback names in sim and app; the app recognises the sim's English default name by text | P25 |
| MD-11 | Low | ADR 0007 and `data-limits.md` claim more than the code does | P30 |
| MD-12 | Info | Original interface, sound and video tables are compiled in (faithful; inventory for later packs); player colours are fine | — |
| MD-13 | Info | Original-numbering assumptions in the loaders and the `.GAM` writer | — |
| MD-14 | Info | Localisation catalogues: every used key exists; German lacks 7 language-neutral keys on purpose | P30 |

#### MD-1 [Medium] "Game numbers are named in one place" is not true: about 20 tables of ids live outside `game_data.hpp`, and a missing meadow record makes demolition report success but leave the building

- **Aliases:** MOD-1, SIM-22, RND-21, LST-V2 · **Dimensions:** D5, D7, D10, D1 · **Effort:** S-M · **Depends on:** — · **Fix bundle:** P23
- **Where:** Building ids: `engine/sim/src/construction.cpp:37` (`kMeadowId = 22722`), `volcanoes.hpp:35` (21108), `building_table.cpp:316-317` (22601, `kRuins`), `data_fixes.cpp:12` (20503), `world_plan.cpp:47` (`kCropBuildings`), `tile_status.cpp:31-32` (17 ids), `tutorial.cpp:27-29`, `build_menu.cpp:63-92`. Figure numbers: `logistics.hpp:171`, `volcanoes.hpp:39-40`, `world_renderer.cpp:115-116, 487-491`, `ambient.hpp:57-60`, `figure_table.hpp:153, 157`, `selection_panels.cpp:1049`, `combat_effects.cpp` (`kFigureShipSunk = 67`), `world_graphics.cpp:114`. Local kind copies: `ambient.cpp:19-23`, `land_figures.cpp:12-13, 50-57, 140-142`. Other literals: `world.cpp:539` (`b < 7`), `missions.cpp:143` (`kClasses = 5`), `disasters.cpp` `conducts_fire`, `volcanoes.cpp` `destroyed_by_lava` (raw kind numbers), `pirates.cpp` `pirate_max_ships`, `construction.cpp:321` (`kNoUnitCheck`, `kRealBuildings`). Demolition: `construction.cpp:1595-1610` (`restore_ground` places nothing when 22722 is missing), `:1061-1110` (`Builder::remove` already removed the records and emitted `Removed`).
- **Failure scenario:** A modder follows `docs/modding/data-limits.md:22-25`, changes `game_data.hpp`, and still gets the old numbers in the generator, demolition, volcano, ruins, tile status, tutorial and render. With a table lacking 22722, demolishing a building on former forest removes its `Production`/`House`/`CostBuilding` state and reports `demolished 1`, but the map still shows it: an orphan tile, and orders that repeat forever (the LST abandon loop, see Refuted).
- **Evidence:** MODCON `sweep.out`; LST verifier `ai_probe`: `demolished 1` while the tile shows `top sprite 8` before and after.
- **Solution:** Move every id and figure number into `game_data.hpp` as named roles (`id::kMeadow`, `id::kVolcano`, `figure::kCarrier`, …) and delete the copies; `BuildingTable::require(roles)` / `FigureTable::require(roles)` at load refuse a table without them (this fixes the orphan tile); fall back to the plain ground record in `restore_ground`; a ctest grep (like `CheckLayering.cmake`) failing on `\b2[0-3][0-9]{3}\b` outside `game_data.hpp`, `cod_predefined.cpp` and the original-format readers and writers; correct `data-limits.md` until done.

#### MD-2 [Medium] Resident classes are data in the model, but five classes and the original's public-building services are compiled into the rules

- **Aliases:** MOD-2 · **Dimensions:** D5, D6 · **Effort:** M · **Depends on:** MD-1, content object (P24) · **Fix bundle:** P24
- **Where:** `engine/sim/src/houses.cpp:79-86` (`std::array<ServiceNeeds, 5>`, used at `:496` with `std::min(resident_class, size − 1)`); `engine/sim/include/o1602/sim/houses.hpp:24-53` (`House::services` `uint16_t`, bits fixed to Kapelle … Brunnen, 13 of 16 used; `panel_service` another fixed layout); `selection_panels.cpp:781, 825` (`cls < 4`), `:993` (`kRequired`), `ware_unlocked`; `missions.cpp:143`; `endless_setup.cpp:44-45`; `simulation.cpp:23`, `game_data.hpp:157`, `disasters.cpp:296` (class 2 as "upper")
- **Failure scenario:** A pack adds a sixth class: the sim silently gives it the aristocrats' needs, the house panel shows no requirements and no victory choice. A new public building with a new service has no bit, so no house can need it. `City` vectors are already sized by `resident_classes()`.
- **Evidence:** MODCON code reading.
- **Solution:** A class table in content (needed families, wares, unlock level, monument and arch eligibility); public buildings carry a service-family index; `House::services` a dynamic bitset (or `uint64_t` with a load-time check); `has_services` iterates the class's families; the panel reads the same table.

#### MD-3 [Medium] Ware numbering is structural: u8 ids in three ranges laid end to end, with literal ranges in sim, AI, UI and render

- **Aliases:** MOD-3, AI-11, APP-14 · **Dimensions:** D5, D7 · **Effort:** M-L · **Depends on:** MD-1 · **Fix bundle:** P24
- **Where:** `engine/assets/include/o1602/assets/game_data.hpp:20-82` (trade 2-24, soldiers 25-44, fields 45-58; `is_trade_ware`, `is_soldier_ware` are ranges; `kBasePrices` sized `kLastTrade + 1`); `trader.cpp:434`; literals in `ai_planner.cpp:220, 305`, `ai_settle.cpp:697` (beside `ware::kLastTrade` in `ai_logistics.cpp:19`), `panel_common.hpp:18-19`, `ship_panel.cpp:29-30, 949-960`, `unit_panels.cpp:22-23`, `selection_panels.cpp:73` (literal `prod_kind` cases), `:781, 825, 970, 993, 998-1010`, `event_presenter.cpp:41-71`, `land_figures.cpp:50-57`, `main_menu.cpp:773`, `savegame_writer.cpp`, `consumption.cpp:23`; ware ids `uint8_t` in about 20 model structs
- **Failure scenario:** A new trade good can only be number 59 or above: `is_trade_ware(59)` is false, so Kontors do not store it, ships do not trade it, it has no price, and AI and panels never show it. Numbering it 25 renumbers every soldier ware and field and breaks saves and the original data.
- **Evidence:** MODCON code reading and sweep; AI and APP code reading.
- **Solution:** A ware table in the content object with an explicit group, price, icon and category; replace every range test with a precomputed group bitset; keep u8 ids behind a `WareId` alias. Short term: use `ware::kFirstTrade`/`kLastTrade`, `is_soldier_ware` and `sim::prod::k*` instead of literals.

#### MD-4 [Medium] The AI's building knowledge is compiled in; a mod's buildings and wares are invisible to computers

- **Aliases:** AI-10 · **Dimensions:** D5, D7 · **Effort:** L · **Depends on:** MD-1, MD-3, BT-1 · **Fix bundle:** P25
- **Where:** `engine/sim/src/ai/ai_state.hpp:83-139` (55 fixed slots); `ai_planner.cpp:1252-1291` (32 Need rows), `:568-794` (stage orders), `:1745-1756` (`kPublic`); `ai_build.cpp:35-46` (`kPublicTasks`), `:94-160` (`producer_slot`); `ai_core.cpp:27-38` (first colony crop by `player % 4`); Id arithmetic `id::kKontor1 + 2 * level` (`ai_world.cpp:140`, `ai_planner.cpp:1554`, `ai_build.cpp:845, 1284, 1359`), `kWoodTower + 1` (`ai_build.cpp:1197-1203`); INFRA literals (`ai_world.cpp:14-17`, `ai_planner.cpp:21-22`); Kontor footprint `+3` (`ai_build.cpp:863-866, 911-915`); ship type 0..3 (`ai_logistics.cpp:616-617`); four unit families (`ai_military.cpp:16-25, 36`); cheat amounts in code
- **Failure scenario:** A mod adds a production chain: computers never build it; a changed Kontor id layout makes them build the wrong record; balancing cheats needs a code change.
- **Evidence:** Code locations, confirmed by the verifier.
- **Solution:** Derive the Need rows and `producer_slot` from the building table (ware, raw, prod kind); slots as a dynamic table keyed by building family; stage orders, cheat amounts and INFRA thresholds in a `game_data` file with today's values as defaults.

#### MD-5 [Medium] The build menu is a fixed table of the original's ids although its header and `status.md` say it is data

- **Aliases:** APP-9 · **Dimensions:** D5, D10 · **Effort:** M · **Depends on:** MD-1, DS-5 · **Fix bundle:** P25
- **Where:** `engine/ui/src/build_menu.cpp:53-92` (`original_build_menu()`, used by `hud.cpp:209`, `selection_panels.cpp:59, 277`, `tile_status.cpp:84`); `engine/ui/include/o1602/ui/build_menu.hpp:3-7` ("here it is data … content packs can add buildings"); `docs/status.md:80`; `build_popup` grows upward without bound
- **Failure scenario:** A mod's new building cannot be built from the menu (only through the debug `NextBuilding` key) and has no name in the tile status.
- **Evidence:** Code reading; no loader exists.
- **Solution:** Load `build_menu.toml` from mod packages as `MessageTable::load_mods` does; validate ids against `BuildingTable`; bound rows and icons per row. Until then, correct the header comment and `status.md`.

#### MD-6 [Low] Castle and shipyard panels hard-code four colours and figure-id arithmetic

- **Aliases:** APP-13 · **Dimensions:** D5, D6 · **Effort:** M · **Depends on:** MD-1 · **Fix bundle:** P25
- **Where:** `engine/app/src/selection_panels.cpp:1281, 1302` (`four_colour_index`; ware sprites `21 + colour + 4 * kind`), `:1359, 1420, 1451-1453` (unit figure `1 + colour + 4 * unit`), `:1049` (`kFirstShipFigure = 21`, four ship types), `:1101, 1129`
- **Failure scenario:** A mod's fifth ship type or unit kind cannot appear.
- **Evidence:** Code reading.
- **Solution:** Take ship and unit types from `FigureTable` (kind, buildable) and sprites from figure records, with no id arithmetic.

#### MD-7 [Low] Island templates, climates and size classes are compiled in, and the template table exists twice

- **Aliases:** MOD-4 · **Dimensions:** D5, D7 · **Effort:** M · **Depends on:** — · **Fix bundle:** P26
- **Where:** `engine/sim/src/world_plan.cpp:32` and `world_generator.cpp:29` (identical `kTemplateCount`); `world_generator.cpp:26, 30-32`; `climate == 0/1` literals in `world_plan.cpp:428, 491, 519, 533, 773, 786`, `world_generator.cpp:293, 401, 839, 1160`, `construction.cpp:634-635`, `event_presenter.cpp:70`, `tile_status.cpp:66`
- **Failure scenario:** A pack with more templates is never drawn from; a third climate is treated as north or south; the two tables can drift apart.
- **Evidence:** MODCON code reading.
- **Solution:** One climate and size registry built from the template folders at load (keep the original's exceptions as data); `climate.id` comparisons.

#### MD-8 [Low] Unlock levels (INFRA) are a 32-bit mask with literal levels in AI, panels and pirates

- **Aliases:** MOD-6 · **Dimensions:** D5 · **Effort:** S-M · **Depends on:** MD-1 · **Fix bundle:** P24
- **Where:** `engine/sim/include/o1602/sim/player.hpp:84, 127` (`uint32_t infra_mask`, `infra <= 32`); literals in `ai_world.cpp:14-17`, `ai_planner.cpp:21-22`, `selection_panels.cpp` `ware_unlocked`, `pirates.cpp` `pirate_max_ships`
- **Failure scenario:** A longer tech tree cannot unlock beyond 32 levels; a renumbered level silently changes AI and panel behaviour.
- **Evidence:** MODCON code reading.
- **Solution:** Name the levels as roles in content and size the mask by the unlock table.

#### MD-9 [Low] Balance values are compile-time only (about 450 named constants)

- **Aliases:** MOD-7 · **Dimensions:** D5 · **Effort:** M · **Depends on:** Content object, DS-3 · **Fix bundle:** P26
- **Where:** `soldiers.cpp:14-38`, `trader.cpp:16-22`, `castles.cpp:16-20, 271-306`, `consumption.cpp:16-26`, `shipyards.cpp:15-22`, `world_generator.cpp:39-48`, `disasters.cpp:20-36`; `Rules` (`rules.hpp:120-160`) exposes 15 switches
- **Failure scenario:** A balance mod needs a code change, against ADR 0007 ("a mod changes data instead of code").
- **Evidence:** MODCON sweep.
- **Solution:** A `balance` TOML table overriding named values, part of the content fingerprint and the save (like `RULE`); start with trader, castle, deposit and disaster values.

#### MD-10 [Low] English fallback names in sim and app; the app recognises the sim's English default name by text

- **Aliases:** MOD-8, APP-15 · **Dimensions:** D5, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P25
- **Where:** `engine/sim/src/new_game.cpp:55-56` ("Red player" …), `:308` (`"Player " + n`); `construction.cpp:1565` (`"City " + n`); `trader.cpp:494` (`"Ship " + n`); `engine/app/src/map_viewer.cpp:146` (`name_more_players` compares with `"Player " + n`), `:152-163` (`colour_player_name`); also `multiplayer.cpp:617, 657`
- **Failure scenario:** With a `Text.cod` lacking `[PLAYCOL]`, `[STAEDTE]` or ship names, German players see English names; if the sim's default text changes, the app's recognition breaks silently.
- **Evidence:** Code reading.
- **Solution:** Keep the sim language-free: a `default_name` flag (or empty name) on `Player`, `City`, `Ship`; the UI renders `tr("player.default", {n})` and `tr` keys for the colours; remove the string compare.

#### MD-11 [Low] ADR 0007 and `data-limits.md` claim more than the code does

- **Aliases:** MOD-10 · **Dimensions:** D10, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** ADR 0007 lines 106-107 and 148 (island numbers "16 bits" ✅: they are u8), 151 ("the world plan supports 16 players": the game supports 12), 60 ("up to 254 slots": validation allows 255), 103 ("islands have no size limit": 255 wide); `docs/modding/data-limits.md:22-25` ("one place", MD-1)
- **Failure scenario:** Contributors and modders rely on the status table; an island editor built on it would produce unloadable saves (LC-2).
- **Evidence:** MODCON reading.
- **Solution:** Correct the table with the real limits and link it to the limits table of LC-1.

#### MD-12 [Info] Original interface, sound and video tables are compiled in (faithful; inventory for later packs); player colours are fine

- **Aliases:** MOD-11, RND-22, RND-23 · **Dimensions:** D5 · **Effort:** — · **Depends on:** — · **Fix bundle:** —
- **Where:** `engine/audio/src/game_audio.cpp:18-58` (93 sample files, 0x70 message voices from 0x0049B210); `engine/app/src/game_sound.cpp:21-38`; about 65 sprite and gadget constants in `app/src` and `render`; `hud.cpp:313, 323`; `banner.hpp` `kDefaultColors` (12, `static_assert == kMaxPlayers`)
- **Failure scenario:** —
- **Evidence:** Code reading.
- **Solution:** No change now; keep as the inventory for a skin or sound pack.

#### MD-13 [Info] Original-numbering assumptions in the loaders and the `.GAM` writer

- **Aliases:** IO-15 · **Dimensions:** D5 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** `engine/assets/src/building_table.cpp:22` (`kMaxId = 23500`, `kIdOffset = 20000`); `figure_table.cpp:23` (`kMaxFigures = 1024`); `savegame_writer.cpp:29-44` (wares 2..24 and 14..24; 50/255 islands, 75 cities, 7 players, 150 ships, 400 soldiers, 350 towers, 255 wide); `cod_predefined.cpp`; `data_fixes.hpp`
- **Failure scenario:** Correct for the original-format layer; a mod with more wares or players cannot export to `.GAM`, and the writer should say so clearly.
- **Evidence:** Code reading.
- **Solution:** Keep; make the `.GAM` writer's refusal messages name the limit (LC-14).

#### MD-14 [Info] Localisation catalogues: every used key exists; German lacks 7 language-neutral keys on purpose

- **Aliases:** APP-26 · **Dimensions:** D5 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** `data/locale/en.lang`, `data/locale/de.lang`; `tests/unit/test_localization.cpp:131`
- **Failure scenario:** `ui::tr` returns references into the catalogue; `set_language` replaces it. Safe because the language is set once (`map_viewer.cpp:2080`), but would dangle if switching is added.
- **Evidence:** Key comparison script: 118 literal keys present in en.lang; only-en keys `keys.group_digits`, `keys.modifier.alt`, `keys.name.*`.
- **Solution:** A test that de.lang is a superset of en.lang except a listed exemption set.

### Build, tests, docs (BT)

Build, CI, tests, tools and documentation.

| ID | Severity | Title | Bundle |
|---|---|---|---|
| BT-1 | Medium | The AI, the largest share of the tick, has 36 % line coverage without data; site searches 8.5 %; the main menu 0 % (earlier BD-6 never fully fixed) | P15 |
| BT-2 | Medium | The largest interface files are untested and outside `o1602_menus`; the command line is parsed by hand in `main()` (earlier UI-5 partly regressed) | P28 |
| BT-3 | Low | `docs/performance.md` never states the 6.25 ms budget and derives the minimum CPU from the mean tick | P30 |
| BT-4 | Low | Tick-time p99 and max from a single wall-clock run measure preemption, not code (`o1602-tool bench`, `tick_timed`) | P15 |
| BT-5 | Low | No ThreadSanitizer job and no sanitizers off Linux (a TSan run of the whole suite is clean) | P27 |
| BT-6 | Low | Windows and macOS CI build RelWithDebInfo only: asserts and library debug checks never run there | P27 |
| BT-7 | Low | The `debug` and `asan` presets overwrite `CMAKE_CXX_FLAGS`, which drops MSVC's `/EHsc` and any user `CXXFLAGS` | P27 |
| BT-8 | Low | Real-UDP tests use fixed ports and fail instead of skipping when a port is taken | P27 |
| BT-9 | Low | Documented data limits have no negative tests; there are no coverage-guided fuzz targets | P20 |
| BT-10 | Low | Documentation drift: test counts, layer diagram, hash interval, tool header, data limits (earlier BD-10 and BD-11 regressed) | P30 |
| BT-11 | Low | The symbol table does not hold the addresses the code cites, and 27 rows are malformed CSV | P30 |
| BT-12 | Low | CI's Clang and ASan jobs compile ENet (C) with GCC | P27 |
| BT-13 | Low | o1602-tool: bad numbers fall back to defaults silently, flags swallow positionals, players clamp to 16 instead of 12 | P27 |
| BT-14 | Low | A few tests check little; most data tests depend on one maintainer's savegames | P27 |
| BT-15 | Low | Inconsistent conventions: `bool` state loaders, raw `getenv`, a second unchecked zlib path, test temp folder, UTF-8 manifest missing on tests | P27 |
| BT-16 | Low | No test covers trading with natives (goodwill limit, spend, earn) | P7 |
| BT-17 | Low | Network documentation and test gaps: stale protocol comment, ADR 0011 numbering and numbers, tests only on 64×64 worlds over loopback | P30 |
| BT-18 | Low | `gam.md` gives MARKT2 "11 entries of 16 bytes", but the record and the loader have 16 | P30 |
| BT-19 | Info | Small drifts between `scp.md`, `gam.md`, `chunks.md` and the loaders | P30 |
| BT-20 | Info | `gad.md` does not say that Open1602 lifts the original's 300-gadget limit, and nothing replaces it | P30 |
| BT-21 | Info | Compiler warnings beyond the project's set: few and none worrying | — |
| BT-22 | Info | Further CI and platform notes (coverage without threshold, Python never checked, no clang-tidy, no macOS bundle) | — |
| BT-23 | Info | SDL tests cover only the dummy driver and the software renderer | — |
| BT-24 | Info | ENet's `offsetof` idiom stops UBSan builds that use `-fno-sanitize-recover` | P27 |

#### BT-1 [Medium] The AI, the largest share of the tick, has 36 % line coverage without data; site searches 8.5 %; the main menu 0 % (earlier BD-6 never fully fixed)

- **Aliases:** BLD-1, AI-16 · **Dimensions:** D10, D2, D3 · **Effort:** M · **Depends on:** — · **Fix bundle:** P15
- **Where:** `engine/sim/src/ai/*.cpp`; `tests/unit/test_ai.cpp` (synthetic table with ground, sea, beach, a tree, roads and the Kontor only, `:43-60`; the only long test founds one city in 90 s); `engine/app/src/main_menu.cpp`; CI coverage job `.github/workflows/ci.yml:211-254`
- **Failure scenario:** A regression in site search, building, military or planning passes every CI job; data tests, which would drive these paths, never run in CI.
- **Evidence:** gcovr after `o1602_tests --no-data` (GCC 13 Debug): engine 77.4 % lines (30 490 / 39 407), branches 62.4 %; `sim` without AI 86.7 %; `sim/ai` 36.0 % (2 155 / 5 990): `ai_sites.cpp` 8.5 %, `ai_build.cpp` 17.3 %, `ai_military.cpp` 21.5 %, `ai_planner.cpp` 28.4 %; `main_menu.cpp` 0/604. The CI total leaves `map_viewer.cpp`, `map_input.cpp`, `hud.cpp` out of the denominator. **Device, with the original data:** engine 88.9 % lines, 95.1 % functions (from gcov JSON; gcovr was not installed). The per-file figures for the AI with data are not available, so the AI's coverage with data is unknown. The finding stands because CI runs without data.
- **Solution:** A synthetic AI arena: extend the synthetic table with market, house, woodcutter, fisher, shipyard and weaving hut (and the meadow record), run two or three computers for 30-60 game minutes, check cities, houses, ships, soldiers and diplomacy events; add tests for reaching pioneers, resuming a search across save/load, GB-2 and GB-3. A non-gating cost report with 11 computers measured as in BT-4. A `main_menu` test. Per-directory coverage gating or a ratchet in CI.

#### BT-2 [Medium] The largest interface files are untested and outside `o1602_menus`; the command line is parsed by hand in `main()` (earlier UI-5 partly regressed)

- **Aliases:** APP-10 · **Dimensions:** D10, D7 · **Effort:** S (CLI), L (panels) · **Depends on:** — · **Fix bundle:** P28
- **Where:** `engine/app/menus/CMakeLists.txt` (22 sources, none of hud, map_input, map_viewer, selection_panels, ship_panel, unit_panels, list_panels, side_panels, options_panel, diplomacy_panel, panel_common); `engine/app/src/main.cpp:202-560` (inline parsing; `--video` via unchecked `strtoul` cast to `uint16_t`, `:284-285`); `map_viewer.cpp` 2 762 lines, about 70 members (62 after #38)
- **Failure scenario:** Regressions in panel logic go unnoticed; the 12-player limits found in this review (LC-4, LC-5, LC-6) all live in untested files. `--video abc` plays video 0, `--video 70000` wraps.
- **Evidence:** CMake source list; no `test_ship_panel` or `test_hud`.
- **Solution:** S: extract `parse_command_line(argc, argv) -> Result<ViewerOptions>` into `o1602_menus` and test it. L: move the panels into `o1602_menus` (they depend on `ui::Panel`, sim and assets) and add panel tests with synthetic GAD layouts.

#### BT-3 [Low] `docs/performance.md` never states the 6.25 ms budget and derives the minimum CPU from the mean tick

- **Aliases:** BLD-3 · **Dimensions:** D10, D2 · **Effort:** S · **Depends on:** BT-4 · **Fix bundle:** P30
- **Where:** `docs/performance.md:10, 32-34, 40, 57-59`; `tools/o1602-tool/bench.cpp:228-231` ("one core is x % busy" from the mean only)
- **Failure scenario:** The stated minimum machine is justified by the mean only. The page's tail numbers (p99 3.5-7.1 ms, max 20-60 ms) come from single wall-clock runs and are not reliable evidence either way (BT-4), so the BLD conclusion that the minimum machine misses the budget is withdrawn; the methodology and the missing budget remain.
- **Evidence:** `grep -rn "6.25\|budget" docs` finds nothing; verifier analysis. **Device:** the device machine (Ryzen 9 7900X3D) is about 2-3× faster than the minimum machine; the report scales device numbers with that factor where severity depends on it (TB-26). `docs/performance.md` should state the factor or measure the minimum machine.
- **Solution:** State the budget (`1000 / (20 × max_game_speed)` ms); measure tails as in BT-4; re-derive the minimum from p99; let `bench` print the share of ticks over budget; update the hash and peak numbers (TB-26).

#### BT-4 [Low] Tick-time p99 and max from a single wall-clock run measure preemption, not code (`o1602-tool bench`, `tick_timed`)

- **Aliases:** BLD-V1, AI-V2 · **Dimensions:** D10, D2 · **Effort:** S · **Depends on:** — · **Fix bundle:** P15
- **Where:** `tools/o1602-tool/bench.cpp:47-50` (`steady_clock`), `:220-231`; `engine/sim/src/simulation.cpp:577-580` (`tick_timed` per step); `docs/performance.md:21-45`
- **Failure scenario:** On a machine with more runnable threads than cores the same deterministic run gave a founding-phase max of 12-45 ms against 2.9 ms minimum per tick, and p99 4.4-5.0 ms against 0.49 ms. A gating cost test on such numbers is flaky, and decisions based on them are wrong.
- **Evidence:** AI verifier: min-per-tick over 4-6 runs against single runs (table in the AI verification).
- **Solution:** `--repeat N` reporting the minimum per tick (the run is deterministic), or the per-thread CPU clock (`CLOCK_THREAD_CPUTIME_ID`, `GetThreadTimes`, `thread_info` on macOS, as `tools/stress` already does); instruction counts (callgrind) for CI gating; print load average and share of ticks over 6.25 ms; keep wall-clock p99 for device runs on an idle machine.

#### BT-5 [Low] No ThreadSanitizer job and no sanitizers off Linux (a TSan run of the whole suite is clean)

- **Aliases:** BLD-4 · **Dimensions:** D10, D3 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `.github/workflows/ci.yml:30-34, 89-92`; threads in `engine/net/src/lan.cpp` and `engine/render/src/graphics_preloader.cpp`
- **Failure scenario:** A future race between the preloader and the main thread, or in the LAN resolver, would go unnoticed. No race is known today.
- **Evidence:** Verifier: Clang 18 TSan build, `o1602_tests --no-data`: 645 run, 0 failed, 0 TSan warnings.
- **Solution:** Add `{name: Linux TSan, preset: tsan}` with `O1602_SANITIZE=thread` running `ctest -R unit` (it passes as is); optionally an MSVC `/fsanitize=address` job.

#### BT-6 [Low] Windows and macOS CI build RelWithDebInfo only: asserts and library debug checks never run there

- **Aliases:** BLD-5 · **Dimensions:** D10 · **Effort:** S · **Depends on:** BT-7 · **Fix bundle:** P27
- **Where:** `CMakePresets.json:34-42` (`ci` = RelWithDebInfo); `ci.yml:33-34, 91-92`; on macOS `-D_GLIBCXX_ASSERTIONS` does nothing with libc++
- **Failure scenario:** An out-of-range `operator[]` in a Windows- or macOS-only path, or one only MSVC's STL detects, passes CI.
- **Evidence:** No CI job uses `debug` or `asan` off Linux.
- **Solution:** Run the unit tests once per platform in Debug (after BT-7), or add `_LIBCPP_HARDENING_MODE_EXTENSIVE` and `_ITERATOR_DEBUG_LEVEL` checks to the `ci` preset there.

#### BT-7 [Low] The `debug` and `asan` presets overwrite `CMAKE_CXX_FLAGS`, which drops MSVC's `/EHsc` and any user `CXXFLAGS`

- **Aliases:** BLD-6 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `CMakePresets.json:16` and `:30` (`"CMAKE_CXX_FLAGS": "-D_GLIBCXX_ASSERTIONS"`); docs sending contributors there: `README.md:61-62`, `tests/README.md:15-19`, `CONTRIBUTING.md:50-51`; `cmake/Sanitizers.cmake:6` (silently ignored on MSVC)
- **Failure scenario:** A Windows contributor runs `cmake --build --preset debug && ctest --preset debug`: cl compiles without `/EHsc` (C4530 floods the build; exceptions propagate without unwinding locals), so the test framework's throwing `REQUIRE`/`SKIP` skip RAII cleanup. On Linux and macOS user `CXXFLAGS` are ignored. Local developer builds only, never CI or the shipped game.
- **Evidence:** Verifier reproduced the cache override with a toolchain file (`_INIT` marker lost when the preset sets `CMAKE_CXX_FLAGS`). `/GR` is irrelevant: CMP0117 is NEW under `cmake_minimum_required(3.25)` and cl enables RTTI by default. Not run on Windows.
- **Solution:** Remove `CMAKE_CXX_FLAGS` from the presets; add `add_compile_definitions($<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:_GLIBCXX_ASSERTIONS>)` in `CMakeLists.txt` or an `O1602_HARDENING` option; make `Sanitizers.cmake` warn or use `/fsanitize=address` on MSVC.

#### BT-8 [Low] Real-UDP tests use fixed ports and fail instead of skipping when a port is taken

- **Aliases:** BLD-7 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `tests/unit/test_net.cpp:1084-1116` (port 46031), `:1118-1137` (46033); compare `:789-795`, which skips on 46021; `LanBeacon` binds without `SO_REUSEADDR` (`net/src/lan.cpp:51-64`)
- **Failure scenario:** Two test runs on one machine (parallel presets, several CI jobs on one runner) or any program on those ports fail the suite. The broadcast test also depends on the host's network configuration.
- **Evidence:** Reproduced: with the ports bound, `27 run, 2 failed, 1 skipped` (`CHECK failed: beacon.ok()`).
- **Solution:** Open with port 0 and read the bound port back, or skip like the ENet test; skip the broadcast test when no answer arrives on a host without a broadcast interface.

#### BT-9 [Low] Documented data limits have no negative tests; there are no coverage-guided fuzz targets

- **Aliases:** BLD-8 · **Dimensions:** D9, D10 · **Effort:** S (tests), M (fuzz job) · **Depends on:** — · **Fix bundle:** P20
- **Where:** `engine/assets/src/building_table.cpp:44-62, 131, 168-173`, `figure_table.cpp:23-26`, `cod_parser.cpp:455-541`; tests: only `test_construction.cpp:616-619` (sprite overflow); fuzzing is fixed-seed mutation inside the unit suite (`test_save_fuzz.cpp` 400 iterations, `test_net.cpp:337` 20 000 noise messages)
- **Failure scenario:** A later change loosens a bound (the `Id` range that sizes `m_by_id`, `Size > 255`, `Nummer > 0xFFFF`, figure `Nummer ≥ 1024`, the `ObjFill` clamp) and a mod `haeuser.cod` indexes out of bounds or allocates without limit; no test notices.
- **Evidence:** `grep -rn '!.*BuildingTable::\|!.*FigureTable::' tests` finds only `test_construction.cpp:619`.
- **Solution:** One table-driven test per documented limit (row of `data-limits.md` → expected refuse or clamp). libFuzzer targets for `net::decode`, `sim::read_game`, `ChunkFile::from_bytes` + `load_world`, `SpriteSheet`, `Smacker`, `cod::parse` in a nightly Clang job. Part DET built these: 11 libFuzzer targets, the seed writer `o1602_fuzz_seeds`, the `o1602_mod_limits` probe and a proposed `.github/workflows/fuzz.yml` on `review-tools-det` (merged into the working branch); adopting them closes the fuzz half. The working branch (`79ecf09`) already builds `o1602_fuzz_seeds` and `o1602_mod_limits` only with `O1602_FUZZ` and widens fuzz.yml's path filter (DET-V3).

#### BT-10 [Low] Documentation drift: test counts, layer diagram, hash interval, tool header, data limits (earlier BD-10 and BD-11 regressed)

- **Aliases:** BLD-9 · **Dimensions:** D10, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** `docs/status.md:171` (584 test cases; counted 644 in 69 files, 74 in 11, 36 SDL), `:174` (607 functions; `functions.csv` has 613), `:172` and `README.md:61-62` (presets without `coverage`); ADR 0004 line 13 ("a hash every tick"; the code fingerprints every `hash_interval = 100` frames); ADR 0005 lines 14-16 and `engine/CMakeLists.txt:9` (menus without render; `engine/app/menus/CMakeLists.txt:34` links `o1602::render`, and `main_menu.hpp`, `end_screen.hpp`, `event_presenter.hpp` include it); `cmake/CheckLayering.cmake:18` (`engine/app` exempt, so the menus layering is unchecked); `tools/o1602-tool/main.cpp:5-20` (9 of 14 commands); `docs/modding/data-limits.md:10-11` (`Size 0` and negative `Nummer`; sprite limit missing); `re/scripts/pe_imports.py:5` (wrong output name); `open1602-re/docs/re/haus-record.md:10` (old header path)
- **Failure scenario:** Contributors rely on stale counts, a wrong module diagram and an unchecked layering for the SDL-free menus library.
- **Evidence:** Counts and citations checked by the verifier (line numbers corrected).
- **Solution:** Fix the texts; let status.md say "counted by ctest" or generate counts in the CI step summary; add a rule over the 22 menus sources to `CheckLayering.cmake`.

#### BT-11 [Low] The symbol table does not hold the addresses the code cites, and 27 rows are malformed CSV

- **Aliases:** BLD-10 · **Dimensions:** D10 · **Effort:** S (CSV, checker), M (naming 176 functions) · **Depends on:** — · **Fix bundle:** P30
- **Where:** `open1602-re/re/symbols/functions.csv`; citations across `engine/**`; `re/scripts/ghidra/import_symbols.py:26-34`
- **Failure scenario:** A maintainer cannot check comments such as `0x0040E570` (`ai_settle.cpp:107`); the Ghidra import cuts 27 rows at the first comma.
- **Evidence:** 716 distinct cited addresses, 262 function starts not in `functions.csv`, 176 of them in no document either (not recounted by the verifier); 27 rows with a wrong field count (verified with Python `csv`).
- **Solution:** Re-export with `export_symbols.py` (`csv.DictWriter`); add the cited entry points as rows; a checker listing addresses cited in `engine/` but missing from the CSVs and `docs/re`.

#### BT-12 [Low] CI's Clang and ASan jobs compile ENet (C) with GCC

- **Aliases:** BLD-11 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `.github/workflows/ci.yml:31-32, 73` (only `-DCMAKE_CXX_COMPILER`); `third_party/enet/CMakeLists.txt:7`
- **Failure scenario:** The Clang job never compiles ENet with Clang; the ASan job links GCC-instrumented ENet into a Clang ASan runtime (works today, fragile).
- **Evidence:** Reproduced: `-DCMAKE_CXX_COMPILER=clang++` gives `C=GNU CXX=Clang`.
- **Solution:** Pass `-DCMAKE_C_COMPILER` as well, or set `CC`/`CXX` in the job env.

#### BT-13 [Low] o1602-tool: bad numbers fall back to defaults silently, flags swallow positionals, players clamp to 16 instead of 12

- **Aliases:** BLD-12 · **Dimensions:** D7, D5, D10 · **Effort:** S · **Depends on:** LC-1 · **Fix bundle:** P27
- **Where:** `tools/o1602-tool/tool.hpp:44` (`Args::number` returns `optional`; callers use `value_or`); `bench.cpp:148-157` (clamp to `WorldSettings::kMaxPlayers`); `savegames.cpp:224-232` (literal 16)
- **Failure scenario:** `bench --minutes 6O` runs 600 minutes; `render-world --owners a.gam out.png` takes `a.gam` as the option value; `--players 13..16` passes the tool and fails in `new_game.cpp:288`.
- **Evidence:** Code reading (line numbers corrected by the verifier).
- **Solution:** `Args::number` returns a `Result` and fails with usage; a list of value-less flags; clamp to `sim::kMaxPlayers` (LC-1).

#### BT-14 [Low] A few tests check little; most data tests depend on one maintainer's savegames

- **Aliases:** BLD-13 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `tests/unit/test_command_sink.cpp:80-84` (`CHECK(true)`); `tests/data/fixtures.hpp:30-34` (five savegames pinned by size and CRC)
- **Failure scenario:** On any other installation the content tests against `game00/03/06/07/10.gam` skip, and the pre-release data run still reports success.
- **Evidence:** Script over all 755 test cases. **Device:** with the user's original data 717 data test cases pass (0 failed); ctest 21/22 with only the review label failing on purpose.
- **Solution:** Check that the probe session's command count stays 0; print a summary of skipped fixture tests at the end of `--only-data` and document which savegames to create (or generate own `.o1602` fixtures in a data test).

#### BT-15 [Low] Inconsistent conventions: `bool` state loaders, raw `getenv`, a second unchecked zlib path, test temp folder, UTF-8 manifest missing on tests

- **Aliases:** BLD-14 · **Dimensions:** D7, D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `bool load(core::ByteReader&)` in `logistics.hpp:229`, `military.hpp:186`, `ai.hpp:53`, `ships.hpp:314` (DS-3 replaces them); `ai_core.cpp:68` (`std::getenv`); `engine/core/src/png_writer.cpp:35-37` (`compress2` with an unchecked `uLong` cast, 32-bit on Windows) against `compress.cpp:24-26`; `test_world_generator.cpp:178`; `cmake/WindowsUtf8.cmake` applied to `open1602` and `o1602-tool` only
- **Failure scenario:** A refused section cannot say what was wrong; small portability traps.
- **Evidence:** Code reading, confirmed.
- **Solution:** As listed: `Result` loaders, `core::environment_variable`, one deflate function, `test::temp_path`, the manifest on the test executables.

#### BT-16 [Low] No test covers trading with natives (goodwill limit, spend, earn)

- **Aliases:** SIM-V2 · **Dimensions:** D10 · **Effort:** S · **Depends on:** GB-1 · **Fix bundle:** P7
- **Where:** `tests/unit`: only `test_pirates.cpp:464-477` checks the goodwill decay; nothing calls `move_goods` against a natives city
- **Failure scenario:** GB-1 went unnoticed; any change to the gates in `ships.cpp:1128-1212` is untested.
- **Evidence:** grep of tests.
- **Solution:** A test with a natives city, ships of player ids 0 and 7, and checks of the limit, the spend and the earn.

#### BT-17 [Low] Network documentation and test gaps: stale protocol comment, ADR 0011 numbering and numbers, tests only on 64×64 worlds over loopback

- **Aliases:** NET-15 · **Dimensions:** D10, D7 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** `engine/net/include/o1602/net/protocol.hpp:117` ("speed and pause: not part of the savegame"; state version 6 saves both); ADR 0011 (points numbered 1-11, 14, 15, 12, 13, 15; point 5 numbers out of date, TB-26; point 6 promises reconnecting by token, MS-4); tests use 64×64 worlds over loopback, which has no message size limit
- **Failure scenario:** ENet limits (MS-11) cannot show up in tests; the untested paths are those of MS-1, MS-2, MS-4, MS-6/MS-10 and the flood budget (MS-9).
- **Evidence:** Code and ADR reading.
- **Solution:** Fix the comment and ADR numbering; add the tests named in each MS finding; a benchmark of `state_hash()` and `send_game` on a 2000² world.

#### BT-18 [Low] `gam.md` gives MARKT2 "11 entries of 16 bytes", but the record and the loader have 16

- **Aliases:** IO-V1 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** `docs/formats/gam.md` (MARKT2 row) against `engine/assets/src/savegame.cpp:24, 32, 183-201` (`kConsumptionSize = 0x104`, 16 goods) and `savegame_writer.cpp:57`
- **Failure scenario:** Someone following the doc writes 11 entries and shifts everything after them.
- **Evidence:** `0x104 = 4 + 16 × 16`.
- **Solution:** Write "16 slots of 16 bytes (11 consumer goods used)" and give the slot layout.

#### BT-19 [Info] Small drifts between `scp.md`, `gam.md`, `chunks.md` and the loaders

- **Aliases:** IO-V3 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P30
- **Where:** `island.hpp:24` ("INSEL5 only", but `island.cpp:33` reads any header ≥ 0x66 bytes); `island.cpp:100-102` (climate 0-3 accepted, 0-1 documented); `scenario.cpp:68-71` (PLAYER4 score offsets missing from `gam.md`); `scenario.cpp:382` (`SZENE_KAMPAGNE` not in `chunks.md`); `scenario.cpp:243-247` (SHIP4 route stride 8 undocumented)
- **Failure scenario:** —
- **Evidence:** Verifier comparison of docs and loaders.
- **Solution:** Update the docs and the header comment.

#### BT-20 [Info] `gad.md` does not say that Open1602 lifts the original's 300-gadget limit, and nothing replaces it

- **Aliases:** IO-V5 · **Dimensions:** D10 · **Effort:** S · **Depends on:** HI-2 · **Fix bundle:** P30
- **Where:** `docs/formats/gad.md` (MAXGADGET = 300) against `gad.cpp:123`; RE note `ui.md:456` (L-010)
- **Failure scenario:** The doc implies a bound the code does not have (HI-2).
- **Evidence:** Doc reading.
- **Solution:** Document the chosen cap of HI-2 in `gad.md` and `data-limits.md`.

#### BT-21 [Info] Compiler warnings beyond the project's set: few and none worrying

- **Aliases:** BLD-15 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** Project set: 0 warnings with GCC 13 and Clang 18. GCC extras: 263 (mostly `-Wswitch-enum` style, `-Wfloat-equal` in tests). Clang `-Weverything`: 104, including `-Wdeprecated-copy-with-dtor` on `render/include/o1602/render/sprite_source.hpp:15` (a copyable polymorphic base) and `-Wtautological-type-limit-compare` at `core/src/compress.cpp:24`
- **Failure scenario:** —
- **Evidence:** BLD builds.
- **Solution:** Optionally `-Wswitch-enum` for `sim`; delete the copy operations of `SpriteSource`.

#### BT-22 [Info] Further CI and platform notes (coverage without threshold, Python never checked, no clang-tidy, no macOS bundle)

- **Aliases:** BLD-16 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** —
- **Where:** `ci.yml:232-247` (coverage only reports); Python scripts never compiled in CI; no clang-tidy or cppcheck job; `data/icons/open1602.icns` unused (no `MACOSX_BUNDLE`); the game loop never runs in CI
- **Failure scenario:** —
- **Evidence:** BLD reading.
- **Solution:** A `py_compile` step; coverage ratchet (BT-1); consider an app bundle on macOS.

#### BT-23 [Info] SDL tests cover only the dummy driver and the software renderer

- **Aliases:** RND-25 · **Dimensions:** D10 · **Effort:** — · **Depends on:** — · **Fix bundle:** —
- **Where:** `tests/sdl/CMakeLists.txt:26-27`
- **Failure scenario:** D3D11/12, Metal, Vulkan and OpenGL device-loss and render-target paths are never tested automatically.
- **Evidence:** Code reading.
- **Solution:** Cover through the device runs (RND list).

#### BT-24 [Info] ENet's `offsetof` idiom stops UBSan builds that use `-fno-sanitize-recover`

- **Aliases:** DET-7 · **Dimensions:** D10 · **Effort:** S · **Depends on:** — · **Fix bundle:** P27
- **Where:** `third_party/enet/protocol.c:1021` (`(size_t) & ((ENetProtocolHeader *) 0) -> sentTime`)
- **Failure scenario:** A UBSan run with no-recover aborts in the first network test, so a strict UBSan CI job is impossible today; suppressions do not help with no-recover.
- **Evidence:** DET's UBSan run aborted there; with ENet compiled `-fno-sanitize=undefined` all 645 tests pass with no report. Verifier confirmed the line.
- **Solution:** Set `-fno-sanitize=undefined` on the `enet` target in `third_party/CMakeLists.txt` when `O1602_SANITIZE` contains `undefined`, then add a UBSan no-recover job.

## Earlier review items that regressed

Checked against `docs/review.md` (the review of `468c58a`). Items not listed still hold as fixed or as left on purpose.

| Earlier item | What it was | State at `f5a46dd` | Now |
|---|---|---|---|
| MP-1 | Reconnect tokens allowed seat hijacking (fixed #20) | Regressed: the successor receives every token in `Standby` and the running host accepts them | MS-1 |
| MP-2 | ENet limit on sending snapshots (fixed #20) | Holds on the host; the client's ENet limit is 22 bytes short of the largest snapshot message | MS-11 |
| MP-5 | Flooding players crowd out others' commands, dropped silently (fixed #20) | Partly: the share limit holds, but queueing is quadratic and drops are still silent | MS-9, MS-17 |
| BD-6 | AI barely covered without data (fixed #23, #27, #36) | Never fully fixed: the AI has 36 % line coverage without data | BT-1 |
| BD-10 | Dependency diagram and layering check (fixed #29) | Regressed: `menus` links `render`, the docs do not say so, and the layering check exempts `engine/app` | BT-10 |
| BD-11 | Counts and status in `status.md` (fixed #20) | Regressed: test, file and symbol counts are stale | BT-10 |
| UI-5 | `MapViewer` god class (fixed #24, #28, #38) | Partly regressed: about 70 data members against 62 after #38; `map_viewer.cpp` 2 762 lines and untested | BT-2 |
| UI-9 | Per-frame allocations (fixed #29) | Partly: `stable_sort` buffers, the ship-smoke map and text wrapping still allocate every frame | FB-2, FB-16, FB-17 |
| IO-10 (earlier) | `ObjFill` blow-up, unbounded `Nummer`, saturating casts (fixed #19, #22) | Incomplete: the per-statement clamp does not bound the total, `.gad` `Nummer` is unbounded, several fields still wrap, and `Wegspeed`/`Randwachs` overflow (confirmed by DET's fuzzing) | HI-2, HI-10, HI-17 |
| IO-5 (earlier) | Island limits enforced on load | Incomplete: zero-size islands are still accepted from `.GAM` and refused from `.o1602` | HI-15, HI-16 |
| IO-2 (earlier) | Smacker unpacked size unbounded (fixed #19) | Fixed per chunk only; no total cap | HI-4 |
| SC-14 | `state_hash` cost and linear city lookups (hash fixed #66; lookups won't fix) | The hash fix holds only for small worlds (8-9 ms on a real 2000² game on a fast desktop, about 100 ms on dense synthetic 2000² worlds); the linear lookup now sits inside a per-tick loop over all cities | TB-26, TB-21 |
| AU8 | Allocations per path plan (won't fix, about 0.1 %) | Contradicted for ships: `in_circle` makes 96 % of all allocations | TB-3 |
| SC-9, UI-14, AU10 | Local copies of `game_data.hpp` constants and game numbers (fixed #22, #25) | The named files stay fixed, but the pattern recurs in render, panels and the AI | MD-1, LC-16 |
| AI note in the earlier review | "AI looks at one foreign city after every step … fine at about 1 ms per tick" | No longer true at the stress targets | TB-9 |
| `docs/performance.md` | Hash 11-28 ms, peaks around 10 ms | Out of date: 8-9 ms hash on a real 2000² game on a fast desktop (about 2-3× that on the minimum machine), peaks up to 6 ms in the real-data soak and seconds with many ships | TB-26, BT-3 |

## Refuted or withdrawn

### Refuted findings

- **SIM-15** Mission records drop targets of player ids ≥ 7. Refuted: `mission_to_record` is used only by the `.GAM` export, which refuses more than 7 player slots (`savegame_writer.cpp:230`); normal saves keep missions in the section format. The `kClasses = 5` literal went to MD-1.
- **LST-12** Computer players: suspected abandon loop of refused demolitions. Refuted as reported: the orders were not refused; the loop came from the synthetic table missing meadow record 22722 (fixed in the tool, `b2c6600`). The AI part is GB-3 (AI-13), the engine part is MD-1 (LST-V2). The G1-G3 "computers 89-98 %" shares were measured inside this loop and are not used.

### Withdrawn parts of confirmed findings

- **SIM-1:** "A save loaded with many units under way causes a planning burst": courses are saved (`save/ships.cpp:84-85`).
- **SIM-3 / LST-8:** "Seconds per build order": the UI caps an order at 512 items and generated islands are at most about 120 tiles wide; realistic cost is 30-70 ms (TB-19). Part of LST-8's G1 stack samples came from the synthetic abandon loop.
- **AI (summary), AI-6, AI-7:** Founding-phase p99 of 4.2-8.4 ms and max 8.7-24 ms: preemption on the shared machine; the minimum per tick over 6 runs gives p99 0.49 ms.
- **AI-1:** The 50 % callgrind share of `free_land`: an artefact of the synthetic table (no houses, every city stays at stage 0).
- **AI-3:** "A few hundred beach tiles" and "45 µs per tile": only tiles within 24 of a target market are scored (50-150), and 4.7 µs per tile was measured.
- **IO-1:** Cold first-call hash times (26 ms / 471 ms): the real call reuses its buffer; warm times are 2-4× lower (TB-26).
- **IO-2:** "An uncompressed copy is made first": only on the uncompressed path.
- **IO-4:** "A loader, mod or scenario adds more than 256 islands": loaded content cannot reach it because the original's island numbers are u8; the real effect is the generator clamp (LC-2). (verify-IO's reason, "the only `add_island` caller is the MAPW loader", is itself wrong: `add_islands`, `world.cpp:499`, calls it for every `.GAM`/`.SZS` island, as verify-DET noted. The conclusion stands.)
- **IO-11:** "Inflates and parses whole files, hundreds of MB": only a 4 KiB prefix is inflated (FB-15).
- **NET-4:** The first-fragment, low-bandwidth variant on Linux and macOS (see MS-7 for the narrower path that holds).
- **SYNC-1:** The build-order half of the review test: the stall is from rectangle demolish orders (MS-3).
- **SYNC-7:** "The game froze for 10.2 s": the game does not freeze; Medium only through TB-26 on large worlds (MS-15).
- **RND-7:** "A device reset re-decodes everything synchronously": only the shown level is waited for (FB-11).
- **APP-5:** "Some GPUs refuse a 4096² texture": every targeted desktop GPU supports at least 8192.
- **BLD-3:** "The stated minimum CPU misses the budget by its own measurements": those tail numbers are single wall-clock runs; the methodology finding remains (BT-3).
- **BLD-2:** "Only one golden value": `test_save_format.cpp:709-911` pins the encoding by CRC; the gap in the simulation's evolution remains (DS-2).

### Review tools and report errata (not repository findings)

- **DET-V2:** `tests/fuzz` was not clang-format clean (13 files, 33 violations with clang-format 18.1.3), so ci.yml's format job (`ci.yml:268`) failed on the integration branch. Fixed on the working branch in `79ecf09`.
- **DET-V3:** fuzz.yml's pull-request path filter missed `engine/sim/src/world.cpp` (HI-15), `island_map.cpp`, `save_game.cpp`, `savegame*.cpp`, `validate.cpp`, `engine/core/**`, `tools/fuzz/**`, `cmake/Fuzzing.cmake` and `fuzz.yml` itself. Fixed on the working branch in `79ecf09`.
- **DET (tool notes):** verify-DET also noted: `o1602_fuzz_seeds` and `o1602_mod_limits` were built in every default build (now only with `O1602_FUZZ`, `79ecf09`); `run_fuzzers.sh` and `add_original_seeds.sh` need bash ≥ 4.3 and GNU `stat` (not macOS's defaults); DET's scope overstated the SHA pinning of fuzz.yml (only `upload-artifact` was by tag; pinned in `c9d5ae1`). The DET report's "resync would presumably fail" is confirmed by code reading (HI-15).
- **LST-V1:** The stress tool's synthetic content lacked meadow record 22722 and the AI's buildings, so the computers fell into an abandon loop and every synthetic AI share was skewed. Fixed on the tool branch in `b2c6600` (meadow record, a note that synthetic runs do not exercise the AI). G1-G3 should be re-measured with the original data (device run LST `generated`).
- **BLD-V2:** The BLD report cited wrong line numbers (`CMakePresets.json:86/100/104-111` → 16/30/34-42, `CheckLayering.cmake:216-227` → 18, `tool.hpp:175-200` → 44, `menus/CMakeLists.txt:30` → 34). Corrected in BT-7, BT-6, BT-10 and BT-13.
- **DEVICE (tool bug):** In an `O1602_FUZZ` build without sanitizers, `o1602_mod_limits` and `o1602_fuzz_seeds` did not link (undefined `__sanitizer_cov_trace_*`). Fixed on the tool branch in `ee79729`.

## Measurements with the original data

The maintainer's machine: AMD Ryzen 9 7900X3D, 61 GiB RAM, Radeon RX 7900 XTX, CachyOS (Linux), with the original game data. Measured commit `b2c6600` (the review tool branch on `f5a46dd`; engine code unchanged), later `197aa6d` for fuzzing and coverage. About 2.5 h of wall time on 2026-10-23. Valgrind does not run on this system (its AVX-512 system libraries give SIGILL), so there is no callgrind or massif; perf, heaptrack and gcovr were not installed (coverage summarised from gcov JSON). Raw logs stay on the user's machine. This machine is roughly 2-3× faster than the minimum machine of `docs/performance.md`; where a severity depends on absolute time, the report scales with that factor and says so.

### Results

| Run | Result | Findings |
|---|---|---|
| Build and tests | Release build 30 s, 0 errors. ctest 21/22 pass, data tests included (717 test cases with data, 0 failed). Only `review:sync` fails, on purpose: 10 of 17 checks reproduce the session findings (MS-2, MS-3, MS-5, MS-7, MS-9, MS-14, MS-15, MS-17). | BT-14, MS-* |
| 24-game-hour soak, 12 players | Mean 257 µs, p99 1 770 µs, max 5 983 µs per tick (budget 6 250 µs); memory 27 MB at the end, 38 MB peak, no growth. **PASS.** | TB-29 |
| Normal games | 12 and 8 players over 600 game minutes, late game, dense cities: within budget. | — |
| 2000×2000 world | Max tick 8 855 µs. | TB-26, TB-28 |
| Soldiers | 10 000 in battle: p99 6 262 µs, max about 7.7 ms. 50 000 / 100 000: mean 34-40 ms per tick. | TB-1, TB-5 |
| Ships | Single ticks of 0.64 s (1 000 ships), 3.1 s (5 000), 12.7 s (20 000, mean 83 ms). Default rules with 10 000 soldiers and 2 000 ships: one tick of 1.25 s. | TB-1 |
| 16 players | "generator: no room for 16 players": run failed. | LC-1 |
| Snapshots and saves | Largest stress snapshot 2.09 MiB (6.5 % of 32 MiB). 12 players, 2000×2000, 600 game minutes: snapshot 2.21 MiB (6.9 %), state hash 8.2-8.6 ms, save 108.8-113 ms (at most 2.39 MB), max tick 18.6 ms (ships 16.6 ms). | TB-26, MS-11, DS-4 |
| Replay and determinism | No divergence anywhere: 8 players 480 game minutes (104 s); `check-roundtrip` 7 919 frames exit 0; a recorded multiplayer run against its replayed command log with round trips: 997 identical; all 19 savegames (15 original `.gam`, 4 own `.o1602`) exit 0; 12 players 2000×2000 600 game minutes. | DS-2 |
| AI | 12 players: computers 65.5 % of the tick, ships 18.6 %; mean 178 µs, max 3.6 ms; the mean grows from 53 to 430 µs over 240 game minutes. 8 players: computers 60.2 %. The AI log (301 lines) shows nothing unusual (no repeated demolitions, no shipyard anomalies). No profile (no perf; callgrind fails). | TB-9, TB-11 to TB-14, GB-2, GB-3, BT-1 |
| Coverage with data | Engine 88.9 % lines, 95.1 % functions (gcov JSON). Per-file AI figures not available. | BT-1 |
| Rendering | Frame rate capped by vsync (143/240 Hz), no headroom visible; 4K windowed with 49 572 sprites still 137.6 fps; peak RSS about 850 MiB; VRAM about 0.6-0.8 GiB. | FB-2, FB-11, FB-12 |
| Loopback multiplayer | 2000×2000, 12 players, 10 minutes: the client came in as player 1, both founded the same cities, no "diverged", "lagging" or "resync". Speed 4 not confirmed from the logs. Chat and notice lines on stderr are in German. | LC-24 |
| Island scan | 845 files, 2 741 islands, smallest 30×30, none under 2 tiles. | HI-15 (stays Medium) |
| Mod limits | `o1602_mod_limits` stops on the UBSan overflow at `building_table.cpp:86` (HI-17). Full table: many fields wrap instead of clamping (`Maxlager 2e8` → 2 105 032 704, `Ware 300` → 44, `RandAnz -1` → 65 535); values documented as refused are accepted (`Nummer -1`, `Size 0,0`, `Size -3,2`). | HI-10, HI-17 |
| Fuzzing seeded with the real data | 10 minutes per target, two at a time. `cod`, `chunks`, `command`, `command_apply`, `net_message`, `media`: no findings. Five findings in `gad`, `sprites`, `gam_world`, `o1602_save` and `settings`, all reproducible and root-caused (below). | see below |

### Fuzzing with the original data (5 inputs, root-caused)

ASan+UBSan build at `ee79729`, seeded with the original data, 10 minutes per target. All five inputs reproduce and were minimised; none comes from the original data (0 of 2 741 real islands are tiny; the other inputs are crafted). Three are new evidence for existing findings, two are new findings.

| # | Target | Input | Cause | Finding |
|---|---|---|---|---|
| 1 | `fuzz_gad` | 12 471 B → 48 B: 14.5 s | `Nummer 45001` makes 45 002 empty gadgets; `index_of` is linear and the harness looks up every gadget | HI-2 (evidence added) |
| 2 | `fuzz_sprites` | 113 KB → 1 061 B synthetic PoC: 8.5 GB | Per-sprite size limit only; decode allocates 2 × w × h before the RLE; the font keeps all glyphs | **HI-18 (new, Medium)** |
| 3 | `fuzz_gam_world` | 23 288 B → 6 576 B | `.GAM` island header 0×0 accepted, `.o1602` reader refuses it | HI-15 / HI-16 (evidence added) |
| 4 | `fuzz_o1602_save` | 13 056 B → 7 937 B | A ship's saved motion state is loaded unchecked; tick 1 moves it to tile 1 583 on a 160-wide map | **HI-19 (new, Medium)** |
| 5 | `fuzz_settings` | 488 B → 15 B | Dropping undefined Windows-1252 bytes exposes a CR that only the next read trims | FB-36 (extended) |

### Effect on the findings

- **Re-judged:** TB-26 High → Medium (real 2000² game: hash 8.2-8.6 ms, save 109-113 ms on the device; scaled 2-3× for the minimum machine: 16-26 ms per fingerprint and a 0.2-0.35 s freeze per join or resync; the 100 ms / 2-12 s figures hold only for dense synthetic worlds). MS-11 Medium → Low (real snapshots 2.1-2.2 MiB, 6.5-6.9 % of the limit; the concern stays for dense synthetic or hand-made 4096² worlds).
- **Confirmed:** TB-1 (ship planning; the soldier spikes did not reproduce in the 10 000-soldier battle), TB-5, MS-2, MS-3, MS-5, MS-7, MS-9, MS-14, MS-15, HI-10, HI-17, HI-15 stays Medium (no tiny islands in the original data).
- **Extended:** LC-1 (the generator refuses 16 players), BT-1 (88.9 % line coverage with data; AI per file unknown), BT-14, DS-2 and DS-4 (real-data replays, save sizes), the AI findings TB-9, TB-11 to TB-14, GB-2 and GB-3 (the AI is within budget in normal games: max 3.6 ms with 12 players; severities stand because they concern the stress targets or code-traced bugs), TB-29 (24-hour soak with real data: PASS).
- **New:** LC-24 (localised chat and notice texts in the log), HI-18 (sprite and font loaders without a total pixel budget) and HI-19 (unchecked ship and soldier motion state in saves and host snapshots), both Medium. For HI-19 the code was checked for out-of-bounds access from an out-of-map ship: the tick and render paths bound-check or clamp, only the local `.GAM` export reads unchecked, so it is not High.
- **Still open:** FB-7, FB-12, FB-27 (unclear, their runs were not done), TB-27 and TB-28 (not isolated).

### Planned runs and their state

| Run | Part | What to run | Settles | Result on the device |
|---|---|---|---|---|
| LST-1 | LST | `tools/stress/run-on-device.sh build/release stress-results` (stages machine, synthetic, generated, late, load, snapshot) | TB-1, TB-5, TB-9..TB-14 on real content, DS-1 ("going down at the end"), TB-26, TB-28, DS-4 | Run: see the stress rows above (synthetic, generated, late, snapshot). TB-1 and TB-5 confirmed; TB-26 re-judged to Medium. |
| LST-2 | LST | `run-on-device.sh ... soak heap` (24 game hours; 2 h under heaptrack) | TB-29, memory growth | Run: PASS, flat memory (heaptrack not installed; RSS only). |
| LST-3 | LST | `run-on-device.sh ... g9` (4K window, zoom 2/1/0, 5 000 soldiers, 3 000 ships, MangoHud or PresentMon) | FB-2, FB-3, FB-12 | Partly: 4K windowed, 49 572 sprites, 137.6 fps (vsync-capped). The 5 000-soldier / 3 000-ship scene not reported. |
| AI-1 | AI | `o1602-tool bench --players 12 --world 1000x700 --seed 7 --minutes 240 --period 10 --out late12.o1602`, and `--players 8` | TB-11..TB-14 (spike census of the real game; measure as in BT-4) | Run (12 and 8 players, 240 game minutes): computers 65.5 % / 60.2 % of the tick, mean 178 µs, max 3.6 ms. |
| AI-2 | AI | `perf record -g` or callgrind on `late12.o1602`, top 40 symbols under `ComputerPlayers::tick` | TB-11, TB-12 attribution | Not run: no perf, valgrind fails with SIGILL. |
| AI-3 | AI | `O1602_AI_LOG=1 o1602-tool bench --players 12 --minutes 180` | GB-2, GB-3, TB-15 (stuck states) | Run: 301 log lines, nothing unusual. |
| NET-1 | NET | `o1602-tool bench --players 12 --world 2000x2000 --minutes 600 --period 60 --out late12_2000.o1602`, then 1 800 minutes and 4096² | TB-26, MS-11 (snapshot size and hash time on real data) | Run for 600 game minutes on 2000×2000: snapshot 2.21 MiB, hash 8.2 ms, save 108.8 ms. 1 800 minutes and 4096² not run. |
| NET-2 | NET | LAN smoke test, 2-3 machines, 2000² 12-player game: late join, successor cable pull, follower network restart | TB-26 (join stall), MS-2, MS-4 | Not run on a LAN; a loopback run on one machine instead (clean). MS-2 and MS-4 confirmed by the review tests only. |
| SYNC-1 | SYNC | `o1602-replay` with the data: 8 computers 480 min; round trip 240 min; recorded commands; every savegame; 12 players 2000² 600 min; Windows and macOS hash comparison | DS-1, DS-2, DS-4, MS-11 | Run on Linux: no divergence anywhere. Windows and macOS hash comparison not run. |
| SYNC-2 | SYNC | Two or three real instances over LAN: baseline, German against English data, successor link block, vacated slot, CPU-limited client | DS-5, MS-2, MS-5, MS-15 | Not run with separate machines; loopback only. |
| SYNC-3 | SYNC | `ctest --preset release -L review` on Linux and Windows | the review tests behind MS-1..MS-14 | Run on Linux: 10 of 17 review checks fail as expected. Windows not run. |
| APP-1 | APP | `O1602_DEBUG_TITLE=1 open1602 --load late.o1602 --frames 600 --input "move 400,300@10"`, over sea and over the side panel | FB-1 | Not run. |
| APP-2 | APP | Same save with `--select I,X,Y` on an own Kontor | FB-4 | Not run. |
| APP-3 | APP | 2000² 12-player game, watch for hitches every 0.5 s | FB-5 | Not run. |
| RND-1 | RND | Far zoom (magnification 0.2) on the largest world, 1080p and 4K, GPU backends and `SDL_RENDER_DRIVER=software` | FB-3, FB-12 | Partly (see LST-3); far zoom and the software renderer not reported. |
| RND-2 | RND | Batch counter in `SdlSpriteRenderer::flush`, batches per frame in run RND-1 and a big battle | FB-7 | Not run. |
| RND-3 | RND | VRAM after all three zoom levels load | FB-11 | Run: VRAM 0.6-0.8 GiB. |
| RND-4 | RND | Forced device reset on Windows, reload time and picture | FB-11 | Not run (Windows). |
| RND-5 | RND | Time `Music::start` at a track change | FB-13 | Not run. |
| RND-6 | RND | 200 % display: cursor size, event coordinates | FB-24 | Not run. |
| RND-7 | RND | Play the `smk_bomb` files as an intro video | HI-4 | Not run. |
| BLD-1 | BLD | Windows Native Tools prompt: `cmake --preset debug`, build, `ctest --preset debug`; count C4530; `CMAKE_CXX_FLAGS` in the cache | BT-7 | Not run (Windows). |
| BLD-2 | BLD | Linux with the data: `coverage` preset, `o1602_tests`, gcovr summary; count `[ SKIP ]` lines | BT-1, BT-14 | Run: 88.9 % lines, 95.1 % functions; 717 data test cases pass. `[ SKIP ]` count not reported. |
| DET-1 | DET | Fuzzing seeded with the real data: `tools/fuzz/add_original_seeds.sh "$ANNO1602_DIR" fuzz-work <saves>`, then `FUZZ_JOBS=2 FUZZ_KEEP_GOING=1 tools/fuzz/run_fuzzers.sh build/fuzz 1800 fuzz-work` (about 6 h); collect artifacts, the last `#N:` line per log, and the output for each new artifact; do not share the corpus | HI-2, HI-15 (expected), anything else is new | Run, 10 minutes per target: 5 findings, root-caused (above): HI-2, HI-15/HI-16 and FB-36 extended, HI-18 and HI-19 new. |
| DET-2 | DET | `o1602_mod_limits` on a non-sanitized build, then the real `haeuser.cod`/`figuren.cod` with one value changed at a time through `fuzz_cod` single-run mode | HI-10, HI-17 on real tables | Run: confirms HI-10 and HI-17. |
| DET-3 | DET | `fuzz_o1602_save <spielNN.o1602>` and `fuzz_gam_world <file.gam>` once for each original `.GAM`/`.SZS` and each saved game | HI-15, HI-16 (no "fuzz invariant broken" expected) | Not run as fuzz single runs; all 19 savegames load and replay cleanly in `o1602-replay` (SYNC-1). |
| DET-4 | DET | `python3 -I tools/fuzz/scan_islands.py "$ANNO1602_DIR"` (lists INSEL3/4/5 headers in `.gam/.szs/.szm/.scp`, flags 0xN and 1x1 islands) | HI-15 severity: High if real scenario packs contain such islands | Run: none under 2 tiles; HI-15 stays Medium. |
| APP-4 | APP | One run on macOS: `--save` into the installation with different letter case | FB-27 | Not run (macOS). |

## Proposed fix order

Each bundle fits one pull request. The order is: critical multiplayer security first, then desync and save problems, then the tick budget, the frame budget, hostile input, limits, content, and finally CI, robustness and documentation. Bundles without a dependency can run in parallel. A finding with two bundles is split between them. Findings without a bundle ("—") need an owner's decision or no change.

| Bundle | Title | Findings | Depends on | Content |
|---|---|---|---|---|
| P1 | Seat tokens and `Standby` validation | MS-1, MS-8 | — | Critical security first. Verifier tokens for the successor, no replacement of a live connection, bounds on a carried-over seat. |
| P2 | Host admission of commands and content | MS-3, MS-9, MS-10, DS-5, MS-16, MS-17, MS-18 | — | One pass over `HostSession::queue`/`close_frame`: a weighted per-frame work budget per player (MS-3), linear queueing (MS-9), decode limits on the host's own commands (MS-10), lobby rate limit, drop feedback, kick by member id, fingerprint of everything the sim reads (DS-5). |
| P3 | Rejoin and reserved slots | MS-4, MS-5, MS-6, MS-14 | P1 | The program sends its token; a vacated slot stays reserved for its owner, also during migration and with "join running game" off. |
| P4 | Explicit host leaving and migration | MS-2, MS-13, MS-22, MS-25 | P1, P3 | Tell "host left" from "my link broke" (MS-2); clients try the old host before following a successor; protocol version refusal; successor address only to players. |
| P5 | ENet limits and chunked transfers | MS-7, MS-12, MS-11, MS-21, MS-23, MS-24, MS-26 | — | Application-level chunking of snapshots, then a 1-2 MiB ENet packet limit and `duplicatePeers` of about 4; connection-slot accounting; beacon reflection; snapshot copies. |
| P6 | Load-path correctness | DS-1, HI-1, HI-15, HI-19, HI-9, HI-16 | — | The false desync of sinking ships (DS-1, with a unit test from the verifier's `sink_rt`), post-load validation of AI state (HI-1) and of the section list (HI-9), zero-size and tiny islands that save but do not load (HI-15, HI-16), ship and soldier motion checks after load (HI-19) with a "every valid world round-trips" test. |
| P7 | Natives' goodwill | GB-1, BT-16, GB-7 | — | Resize goodwill to the player count, missing slot means no buying, 64-bit arithmetic, the first natives-trade test. |
| P8 | Incremental fingerprint and background snapshots | MS-15, TB-26, MS-19 | P5 (for streamed snapshots) | Fingerprint without serialising, snapshots on a worker, time-based stall and lag thresholds. |
| P9 | Large-world saves | DS-4, DS-6, HI-11, FB-15 | P8 (same encoding) | Size check before deflate, cap derived from the world, compact MAPW, directory fsync, header-only slot reading, snapshot inflate cap. |
| P10 | Save versioning and wider fields | LC-2, DS-3, GB-5, LC-9, LC-10, DS-7, LC-11 | — | One section per subsystem with real versions and a golden-save corpus (DS-3) first; then 16-bit island numbers, u32 score counters, wide deposit coordinates, AI state cleanup. |
| P11 | Path-planning budget | TB-1, TB-2, TB-3, TB-4 | — | A deterministic per-tick planning queue keyed by unit id, idle-timer jitter, goal masks, `circle_table` instead of the copying conditional. |
| P12 | Unit census and combat | TB-5, TB-7, TB-8, TB-6, TB-17, TB-23 | — | Early return in `Combat::update` without enemies, dense id → slot table, per-island owner tables of soldiers and cities (used by `mood`, `unit_in_the_way`, natives), grid clearing of used squares. |
| P13 | Spread periodic work | TB-27, TB-14, TB-28 | — | Per-block phases for the sea scenery, staggered island epochs, staggered AI timers. All deterministic. |
| P14 | Construction events and city lookups | FB-4, TB-19, TB-20, FB-8, TB-21, TB-22, TB-24, TB-25 | — | Dirty islands and rectangles per tick, (island, index) → city table, cached per-city running costs and workers (also for the UI). |
| P15 | AI test arena and measurement method | BT-1, BT-4 | — | Synthetic AI arena tests and min-of-N or thread-CPU tick timing before any AI optimisation, so the AI bundles can be checked. |
| P16 | AI census | TB-9, TB-10, TB-16 | P12, P15 | One soldier and ship census per tick for the computers; `plan_landing` from the census. |
| P17 | AI work caps and budget | TB-11, TB-12, TB-13, TB-14, TB-15, TB-18 | P15, P16 | Incremental survey and layout, capped site and settlement searches, then a shared per-tick work budget. |
| P18 | AI correctness | GB-2, GB-3, GB-4 | P15 | Deferred shipyard need, progress guard in `task_abandon`, refreshed guard posts. |
| P19 | Frame-budget culling | FB-1, FB-2, FB-3, FB-5, FB-7, FB-9, FB-10, FB-16, FB-17, FB-18 | — | View culling for picking, soldier figures, the draw list, ambient effects and sounds; downsampled minimap with rect uploads; batch counter. |
| P20 | Hostile loaders | HI-2, HI-4, HI-5, HI-18, HI-3, HI-6, HI-7, BT-9, HI-10, HI-12, HI-13, LC-15, HI-17, HI-8, HI-14 | — | Global parse budget, sparse or capped gadgets with a fast id lookup, a pixel budget for sprites and fonts (HI-18), Smacker total cap, quadratic loaders, saturating loader rules and the signed overflows (HI-17), negative tests, and adopting DET's fuzz targets and `fuzz.yml` with DET's crash files as regression inputs. |
| P21 | Limits in one place | LC-1, LC-13, LC-14, LC-23 | — | `limits.hpp`, `player_capacity`, a limits test, `LimitExceeded` error texts, cities per island. |
| P22 | Large-army and 12-player interface | LC-3, LC-4, FB-6, LC-5, GB-6, LC-6, LC-7, LC-8, FB-30, FB-32, LC-19 | P2 (per-order caps), P21 | Unlimited selection with generated formation rows, pirate target paging, shields and slot icons for 12 players, lists with sort and filter, honest new-game dialog. |
| P23 | Content roles | MD-1, LC-16 | — | Every id and figure number as a named role in `game_data.hpp`, `require(roles)` at load, the grep test, deduplicated constants. |
| P24 | Content object: wares, classes, unlock levels | MD-2, MD-3, MD-8 | P23, P10 | The runtime `GameContent` object (MODCON order of work, step 1), in the fingerprint and the save before any mod may change it. |
| P25 | Data-driven build menu, panels and AI | MD-4, MD-5, MD-6, MD-10 | P24, P15, P2 | Build menu from data, panels from the figure table, AI needs and producers from the building table, language-free default names. |
| P26 | World registry, balance, `GameSetup` | MD-7, MD-9, LC-17 | P24, P10, P2 | Climate and size registry, balance table, one `GameSetup → World` function shared by game, tools and tests. |
| P27 | CI | DS-2, BT-5, BT-6, BT-7, BT-8, BT-12, BT-13, BT-14, BT-15, BT-24 | P15 (measurement method) | More golden hashes and the determinism workflow from `review-tools-sync`, TSan job, Debug off Linux, preset flags, free ports in tests, C compiler in CI, tool argument parsing, conventions. |
| P28 | App robustness | BT-2, FB-20, FB-22, LC-24, FB-26, FB-27, FB-28, FB-29, FB-31, FB-36 | — | Exception barriers, error codes, settings write, game-folder detection, F10, the command-line parser and panel tests. |
| P29 | Rendering and audio small items | FB-11, FB-12, FB-13, FB-14, FB-19, FB-21, FB-23, FB-24, FB-25, FB-33, FB-35 | — | Placeholder on device reset, audio and video loading off the main thread, animation frame width, UI raster, sprite cache, cursor scale. |
| P30 | Documentation | BT-3, BT-10, MD-11, BT-11, LC-12, BT-17, BT-18, MD-14, BT-19, BT-20 | after P8 and P15 for the numbers | Performance page with the budget and new numbers, status counts, ADR corrections, format docs, symbol table. |

Not bundled: LC-18, MS-20, GB-8, MD-12, MD-13, LC-20, LC-21, BT-21, LC-22, BT-22, BT-23, TB-29, FB-34.
