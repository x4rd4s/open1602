#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The stress runs of the code review with the original game (Linux, macOS; Windows: Git Bash or
# MSYS2, see the notes at the end). Everything lands in OUT_DIR; send that folder back.
#
#   export ANNO1602_DIR=/path/to/Anno1602        # the installation (as for the data tests)
#   tools/stress/run-on-device.sh [BUILD_DIR] [OUT_DIR] [STAGE...]
#
# BUILD_DIR defaults to build/release (cmake --preset release && cmake --build build/release
# --target o1602-stress o1602-tool). STAGE picks parts: machine synthetic generated late load soak
# heap snapshot g9 (default: all but soak, heap and g9, which take hours or need a window).
# Times: synthetic about 1-2 h, generated 1-3 h, late 30 min, load 1-2 h, soak 24 game hours
# (2-6 h), heap 2 game hours (1-3 h), g9 a few minutes.
#
# Close other programs and plug in a laptop: the numbers are compared with the 6.25 ms budget of
# docs/performance.md. The tool prints wall and thread CPU time per tick; on an idle machine both
# should agree.
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-$root/build/release}
out=${2:-$root/stress-results}
shift $(( $# > 2 ? 2 : $# ))
stages=("$@")
[ ${#stages[@]} -gt 0 ] || stages=(machine synthetic generated late load snapshot)
stress="$build/tools/stress/o1602-stress"
tool="$build/tools/o1602-tool/o1602-tool"
mkdir -p "$out"

want() { local s; for s in "${stages[@]}"; do [ "$s" = "$1" ] && return 0; done; return 1; }
# run NAME OPTIONS... : one o1602-stress run with its log and CSV.
run() {
    local name=$1; shift
    echo "== $name: o1602-stress $*"
    "$stress" "$@" --csv "$out/$name.csv" > "$out/$name.log" 2>&1
    echo "exit $?" >> "$out/$name.log"
    grep -E "LIMIT|SUMMARY|^CPU|VERDICT|snapshot|ROUND|SAVE|savegame" "$out/$name.log"
}

[ -x "$stress" ] || { echo "build first: cmake --build $build --target o1602-stress o1602-tool" >&2; exit 1; }

if want machine; then
    {
        date; uname -a
        if command -v lscpu > /dev/null; then lscpu | grep -E "Model name|^CPU\(s\)|MHz"; fi
        if command -v sysctl > /dev/null; then sysctl -n machdep.cpu.brand_string hw.ncpu hw.memsize 2> /dev/null; fi
        if command -v free > /dev/null; then free -m; fi
        git -C "$root" log --oneline -1
    } > "$out/machine.txt" 2>&1
fi

# 1. The synthetic suite (no data needed): the same numbers as in the container, on this machine.
if want synthetic; then
    TIMEOUT=${TIMEOUT:-1800} "$root/tools/stress/run-synthetic.sh" "$build" "$out/synthetic"
fi

[ -n "${ANNO1602_DIR:-}" ] || { echo "ANNO1602_DIR is not set: only the synthetic stages ran" >&2; exit 0; }

# 2. Worlds from the island generator, every player a computer (G1, G2, G3; players beyond 12).
if want generated; then
    run gen-g1-12p-600min --data --players 12 --minutes 600 --period 30
    run gen-g1-8p-600min --data --players 8 --minutes 600 --period 30
    run gen-g2-2000 --data --players 12 --world 2000x2000 --minutes 240 --period 30
    run gen-g1-16p --data --players 16 --minutes 60 --period 10
    run gen-g4-1000-dense-isl --data --players 12 --world 1000x1000 --minutes 120 --period 30
fi

# 3. A late game to put load on: ten game hours of 12 computers, kept as late.o1602.
if want late || want load || want g9; then
    if [ ! -f "$out/late.o1602" ]; then
        run late-12p --data --players 12 --minutes 600 --period 60 --out "$out/late.o1602"
    fi
fi

# 4. Load on the late game (G4-G7): dense cities, soldiers, ships, disasters.
if want load; then
    late=$out/late.o1602
    run load-g6-dense --data --load "$late" --dense 600 --minutes 60 --period 10
    run load-g4-10k-battle --data --load "$late" --soldiers 10000 --battle --minutes 5 --period 1
    run load-g4-10k-spread --data --load "$late" --soldiers 10000 --minutes 5 --period 1
    run load-g4-50k-spread --data --load "$late" --soldiers 50000 --minutes 3 --period 1
    run load-g4-100k-spread --data --load "$late" --soldiers 100000 --minutes 3 --period 1
    run load-g5-1k --data --load "$late" --ships 1000 --minutes 10 --period 1
    run load-g5-5k --data --load "$late" --ships 5000 --minutes 5 --period 1
    run load-g5-20k --data --load "$late" --ships 20000 --minutes 3 --period 1
    run load-g7-disasters --data --load "$late" --dense 400 --fires 1000 --plagues 1000 --reignite --eruptions \
        --minutes 20 --period 2
    run load-rules-default --data --load "$late" --soldiers 10000 --ships 2000 --keep-rules --minutes 5 --period 1
fi

# 5. Savegame, load time and resync snapshot of the large worlds (G1, G2, G6): see the last lines
#    of each log ("savegame ... written in", "read in", "resync snapshot"). Also the original's bench.
if want snapshot; then
    "$tool" bench --players 12 --minutes 120 --period 30 > "$out/bench-12p.log" 2>&1
    "$tool" bench --players 12 --world 2000x2000 --minutes 60 --period 30 > "$out/bench-2000.log" 2>&1
    grep -E "state hash|peak" "$out"/bench-*.log
fi

# 6. Soak: 24 game hours, all computers, the multiplayer fingerprint at speed 8 (every 800 ticks).
if want soak; then
    run soak-24h --data --players 12 --minutes 1440 --period 30 --hash-every 800
fi

# 7. Memory under heaptrack (Linux) for two game hours; open the .zst with heaptrack_gui or
#    heaptrack_print --print-leaks.
if want heap; then
    if command -v heaptrack > /dev/null; then
        heaptrack -o "$out/heap-2h" "$stress" --data --players 12 --minutes 120 --period 10 \
            --csv "$out/heap-2h.csv" > "$out/heap-2h.log" 2>&1
    elif command -v valgrind > /dev/null; then
        valgrind --tool=massif "--massif-out-file=$out/heap-2h.massif" "$stress" --data --players 12 --minutes 120 \
            --period 10 --csv "$out/heap-2h.csv" > "$out/heap-2h.log" 2>&1
    else
        echo "neither heaptrack nor valgrind: skipped" >&2
    fi
fi

# 8. G9: the game in a window at 4K, fully zoomed out, with thousands of units and ships. The game
#    prints "N frames in T ms (F fps), S sprites in the last frame" at the end; memory from time -v.
#    Needs the game built (-DO1602_BUILD_GAME=ON). For frame times beyond the mean use MangoHud
#    (Linux: MANGOHUD=1 MANGOHUD_CONFIG=log_duration=60,autostart_log=5,output_folder=$out) or
#    PresentMon (Windows), and watch the window title (F-key debug title shows fps and sprites).
if want g9; then
    game="$build/engine/app/open1602"
    [ -x "$game" ] || game="$build/open1602"
    run g9-scene --data --load "$out/late.o1602" --soldiers 5000 --ships 3000 --minutes 1 --period 1 \
        --out "$out/g9.o1602"
    for zoom in 2 1 0; do
        echo "== g9 zoom $zoom"
        timer=()
        if /usr/bin/time -v true > /dev/null 2>&1; then timer=(/usr/bin/time -v); fi
        "${timer[@]}" "$game" --load "$out/g9.o1602" --size 3840x2160 --zoom "$zoom" --no-sound --no-music \
            --frames 1800 > "$out/g9-zoom$zoom.log" 2>&1
        grep -E "frames in|Maximum resident" "$out/g9-zoom$zoom.log"
    done
    # Free zoom further out than zoom 2 (wheel notches down at frame 5).
    "$game" --load "$out/g9.o1602" --size 3840x2160 --zoom 2 --no-sound --no-music --frames 1800 \
        --input "wheel 1920,1080,-20@5" > "$out/g9-wheel.log" 2>&1
    grep -E "frames in" "$out/g9-wheel.log"
fi

echo "results in $out"

# Windows (PowerShell, from the build folder of the vcpkg preset):
#   $env:ANNO1602_DIR = "C:\Games\Anno1602"
#   .\tools\stress\o1602-stress.exe --data --players 12 --minutes 600 --period 30 --csv g1.csv > g1.log
#   (the same options as above; peak memory is printed by the tool itself)
#   G9: Measure-Command { .\open1602.exe --load g9.o1602 --size 3840x2160 --zoom 2 --frames 1800 }
#       and (Get-Process open1602).PeakWorkingSet64 while it runs, or PresentMon for frame times.
