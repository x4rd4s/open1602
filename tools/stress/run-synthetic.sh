#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The stress scenarios of the code review (G1-G7, T4) on synthetic worlds: no game data needed.
#
#   tools/stress/run-synthetic.sh BUILD_DIR OUT_DIR [SCENARIO...] [-- extra o1602-stress options]
#
# BUILD_DIR holds tools/stress/o1602-stress (configure with -DCMAKE_BUILD_TYPE=Release). Each
# scenario writes OUT_DIR/<name>.log (and .csv); without SCENARIO all except the long ones (soak*,
# heap*) run. MINUTES=<n> shortens or lengthens every run (default per scenario). Run `grep -E
# "SUMMARY|CPU|VERDICT" OUT_DIR/*.log` for the overview. TIMEOUT=<seconds> stops a run that takes
# longer (its log and CSV keep the periods measured so far). The heap* scenarios run under heaptrack
# (or, without it, valgrind --tool=massif).
#
# The synthetic tables hold only the buildings the scenarios place, not what the computers need to
# develop their cities, so the computers' share of the tick in these runs says nothing about the AI:
# measure that with run-on-device.sh and the original data.
set -u
build=${1:?build directory}
out=${2:?output directory}
shift 2
bin="$build/tools/stress/o1602-stress"
[ -x "$bin" ] || { echo "no $bin" >&2; exit 1; }
mkdir -p "$out"

wanted=()
extra=()
while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; extra=("$@"); break; fi
    wanted+=("$1"); shift
done

# name | default game minutes | options
scenarios=(
    # G1: 12 computer players on the size a 12-player game gets, cities on every island, start ships.
    # ... and the computers settling themselves from their ships (no cities handed to them).
    "g1-12p-settle|120|--players 12 --world 866x606 --cities 0 --start-ships --period 10"
    "g1-12p-866x606|60|--players 12 --world 866x606 --start-ships --period 10"
    "g1-12p-2000|60|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --start-ships --period 10"
    # G2: the largest worlds: 2000 (generator limit) and 4096 (World::kMaxSide), 256 islands each.
    "g2-2000|20|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --period 5"
    "g2-4096|20|--players 12 --world 4096x4096 --island-side 240 --island-gap 16 --period 5"
    "g2-4096-sea|20|--players 12 --world 4096x4096 --islands 250 --period 5"
    # G3: as many islands as fit (beyond 256 the savegame cannot be read back: see the round trip).
    "g3-256|20|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --period 5"
    "g3-1444|10|--players 12 --world 2000x2000 --period 5"
    "g3-10000|5|--players 12 --world 2000x2000 --island-side 16 --island-gap 4 --period 5"
    # G4: soldiers in one battle and spread.
    "g4-10k-battle|10|--players 12 --world 1000x1000 --soldiers 10000 --battle --period 1"
    "g4-50k-battle|3|--players 12 --world 1000x1000 --soldiers 50000 --battle --period 1"
    "g4-100k-battle|3|--players 12 --world 1000x1000 --soldiers 100000 --battle --period 1"
    "g4-10k-spread|10|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --soldiers 10000 --period 1"
    "g4-50k-spread|3|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --soldiers 50000 --period 1"
    "g4-100k-spread|3|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --soldiers 100000 --period 1"
    # G5: ships, half on trade routes, half warships among hostile ones.
    "g5-1k|10|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --ships 1000 --period 1"
    "g5-5k|5|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --ships 5000 --period 1"
    "g5-20k|3|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --ships 20000 --period 1"
    # G6: a dense late game for 12 players.
    "g6-dense|30|--players 12 --world 866x606 --cities 2 --dense 400 --period 5"
    "g6-dense-2000|20|--players 12 --world 2000x2000 --island-side 110 --island-gap 14 --dense 1500 --period 5"
    # G7: fires, plagues and eruptions on many islands at once.
    "g7-disasters|20|--players 12 --world 866x606 --dense 400 --volcanoes --eruptions --fires 2000 --plagues 2000 --reignite --period 2"
    # T4: soak, every player a computer, all kinds of load, the multiplayer fingerprint as a session takes it.
    # Event buffers left lying (nobody takes the events): memory must stay bounded.
    "t4-nodrain|120|--players 12 --world 866x606 --cities 2 --dense 200 --ships 300 --soldiers 1200 --fires 30 --reignite --no-drain --period 10"
    "heap-2h|120|--players 12 --world 866x606 --cities 2 --dense 200 --start-ships --ships 300 --soldiers 1200 --fires 30 --plagues 30 --reignite --volcanoes --eruptions --period 10"
    "soak-24h|1440|--players 12 --world 866x606 --cities 2 --dense 200 --start-ships --ships 600 --soldiers 2400 --fires 30 --plagues 30 --reignite --volcanoes --eruptions --hash-every 800 --period 30"
)

for entry in "${scenarios[@]}"; do
    IFS='|' read -r name minutes options <<< "$entry"
    if [ ${#wanted[@]} -eq 0 ]; then
        case "$name" in soak*|heap*) continue ;; esac
    else
        match=0
        for w in "${wanted[@]}"; do case "$name" in $w) match=1 ;; esac; done
        [ $match -eq 1 ] || continue
    fi
    minutes=${MINUTES:-$minutes}
    echo "== $name ($minutes game minutes)"
    prefix=()
    if [ -n "${TIMEOUT:-}" ]; then prefix=(timeout "$TIMEOUT"); fi
    case "$name" in
        heap*)
            if command -v heaptrack > /dev/null; then
                prefix+=(heaptrack -o "$out/$name.heaptrack")
            else
                prefix+=(valgrind --tool=massif "--massif-out-file=$out/$name.massif")
            fi ;;
    esac
    # shellcheck disable=SC2086
    "${prefix[@]}" "$bin" $options --minutes "$minutes" --csv "$out/$name.csv" "${extra[@]}" > "$out/$name.log" 2>&1
    echo "exit $?" >> "$out/$name.log"
    grep -E "LIMIT|SUMMARY|^CPU|VERDICT|snapshot|ROUND|SAVE" "$out/$name.log"
done
