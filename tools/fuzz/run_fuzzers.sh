#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# run_fuzzers.sh <build-dir> <seconds-per-target> <work-dir> [target ...]
#
# Runs the libFuzzer targets of a build configured with -DO1602_FUZZ=ON (tests/fuzz/README.md) one
# after the other, each for the given time, on a corpus in <work-dir>/corpus/<target> that starts
# from the synthetic seeds (o1602_fuzz_seeds) and grows from run to run. Crashes, leaks, timeouts
# and out-of-memory inputs land in <work-dir>/artifacts/<target>/. Exits 1 if any target found one.
#
# Extra seeds (e.g. from the original game data) go into <work-dir>/corpus/<target> beforehand.
# FUZZ_JOBS=n runs n targets at the same time (default 1). FUZZ_KEEP_GOING=1 goes on after a
# finding (libFuzzer's fork mode), for long runs.
set -u

if [ $# -lt 3 ]; then
    echo "usage: $0 <build-dir> <seconds-per-target> <work-dir> [target ...]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
build=$1
seconds=$2
work=$3
shift 3
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
    targets=(cod gad sprites chunks gam_world o1602_save command command_apply net_message settings media)
fi

mkdir -p "$work/seeds"
"$build/tests/fuzz/o1602_fuzz_seeds" "$work/seeds" >/dev/null || exit 2

run_one() {
    local t=$1
    local corpus="$work/corpus/$t" artifacts="$work/artifacts/$t"
    mkdir -p "$corpus" "$artifacts"
    if [ -d "$work/seeds/$t" ]; then
        cp -n "$work/seeds/$t"/* "$corpus"/ 2>/dev/null
    fi
    local extra=()
    local dict="$here/../../tests/fuzz/$t.dict"
    if [ -f "$dict" ]; then
        extra+=(-dict="$dict")
    fi
    if [ "${FUZZ_KEEP_GOING:-0}" = 1 ]; then
        # Long runs: one fuzzing process at a time that is restarted after each finding.
        extra+=(-fork=1 -ignore_crashes=1 -ignore_ooms=1 -ignore_timeouts=1)
    fi
    # -rss_limit_mb / -malloc_limit_mb: a single input that asks for more is a finding (hostile
    # files must not make the game allocate gigabytes). -timeout: one input slower than 10 s is one.
    ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:allocator_may_return_null=0} \
    UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1} \
    "$build/tests/fuzz/fuzz_$t" -max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 \
        -malloc_limit_mb=1024 -print_final_stats=1 -artifact_prefix="$artifacts/" "${extra[@]}" "$corpus" \
        >"$work/$t.log" 2>&1
    local rc=$?
    local found
    found=$(find "$artifacts" -type f | wc -l)
    printf '%-14s exit %3d  corpus %5d  artifacts %d\n' "$t" "$rc" "$(find "$corpus" -type f | wc -l)" "$found"
    return $((found > 0 ? 1 : 0))
}

jobs=${FUZZ_JOBS:-1}
status=0
running=0
for t in "${targets[@]}"; do
    run_one "$t" &
    running=$((running + 1))
    if [ "$running" -ge "$jobs" ]; then
        wait -n || status=1
        running=$((running - 1))
    fi
done
while [ "$running" -gt 0 ]; do
    wait -n || status=1
    running=$((running - 1))
done
exit $status
