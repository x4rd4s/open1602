#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a measurement plan of the computer players (docs/ai-measurement.md) on this machine.
#
#   tools/measure/run.sh PLAN.toml ARM=/path/to/o1602-tool [ARM=...]
#
# One o1602-tool per arm of the plan, each built from that arm's sources. The script
# - refuses to start while the game runs (a person may be playing on this machine), and starts no
#   new run while it runs later (the running ones finish; the pause goes into PAUSES),
# - refuses a plan that git does not track or that differs from the last commit, and a plan that
#   breaks its stage's rules (measure.py check),
# - writes only into a new folder under /tmp, which it prints,
# - runs every game headless, without a window or sound,
# - and never writes to a repository: it reads git, nothing more.
# At the end it collects the results (measure.py collect); the plan's evaluation adds its decision
# metrics as ARM/PHASE/GAME.metrics.json, then `measure.py collect` and `measure.py verdict` again.
set -euo pipefail

if [ $# -lt 2 ]; then
    sed -n '4,16p' "$0" >&2
    exit 2
fi
plan=$1
shift
here=$(cd "$(dirname "$0")" && pwd)
measure="python3 -I $here/measure.py"

game_runs() {
    pgrep -x 1602.exe >/dev/null 2>&1 || pgrep -x open1602 >/dev/null 2>&1
}
if game_runs; then
    echo "refused: the game runs on this machine" >&2
    exit 1
fi
if ! git ls-files --error-unmatch -- "$plan" >/dev/null 2>&1; then
    echo "refused: commit the plan before its runs ($plan is not tracked)" >&2
    exit 1
fi
if ! git diff --quiet HEAD -- "$plan"; then
    echo "refused: the plan differs from the last commit" >&2
    exit 1
fi
$measure check "$plan"

declare -A tool
for a in "$@"; do
    name=${a%%=*}
    path=${a#*=}
    if [ "$name" = "$a" ] || [ ! -x "$path" ]; then
        echo "refused: $a is no ARM=/path/to/o1602-tool" >&2
        exit 1
    fi
    tool[$name]=$path
done

out=$(mktemp -d /tmp/o1602-measure-XXXXXX)
case $out in
    /tmp/*) ;;
    *) echo "refused: no folder under /tmp" >&2; exit 1 ;;
esac
echo "runs in $out"
git rev-parse HEAD >"$out/COMMIT"
cp -- "$plan" "$out/plan.toml"

unset DISPLAY WAYLAND_DISPLAY
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

jobs=$out/jobs.txt
$measure jobs "$plan" >"$jobs"
while IFS=$'\t' read -r arm phase game _; do
    if [ -z "${tool[$arm]:-}" ]; then
        echo "refused: no o1602-tool for arm $arm" >&2
        exit 1
    fi
    mkdir -p "$out/$arm/$phase"
done <"$jobs"

run_one() {
    local arm=$1 phase=$2 game=$3 rc=0
    shift 3
    nice -n 10 "${tool[$arm]}" bench "$@" >"$out/$arm/$phase/$game.txt" 2>"$out/$arm/$phase/$game.err" || rc=$?
    echo "$rc" >"$out/$arm/$phase/$game.rc"
}
while IFS=$'\t' read -r -a job; do
    while [ "$(jobs -rp | wc -l)" -ge "$(nproc)" ]; do
        wait -n || true
    done
    # A person may start the game during a long run: no new run starts while it runs (the running
    # ones finish), and the pause is logged.
    if game_runs; then
        echo "$(date -u +%FT%TZ) paused: the game runs; no new run starts until it ends" | tee -a "$out/PAUSES" >&2
        while game_runs; do
            sleep 30
        done
        echo "$(date -u +%FT%TZ) resumed" | tee -a "$out/PAUSES" >&2
    fi
    run_one "${job[@]}" &
done <"$jobs"
wait
if grep -L '^0$' "$out"/*/*/*.rc | grep -q .; then
    echo "a run failed; see the .err files in $out" >&2
    exit 1
fi
$measure collect "$plan" "$out"
