#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the same game with several builds of o1602-replay and compares their hash sequences.
#   tools/o1602-replay/compare_builds.sh OUTDIR BUILD... -- [o1602-replay arguments]
# Default arguments: a synthetic world, two game hours, 60 made-up commands a minute. The first
# build records the command log, the others replay it, so that they get the very same commands.
# Exit code 0 when all hash sequences are the same, 1 otherwise.
set -u
out=$1
shift
builds=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    builds+=("$1")
    shift
done
[ $# -gt 0 ] && shift
args=("$@")
[ ${#args[@]} -eq 0 ] && args=(--synthetic --islands 3 --minutes 120)
mkdir -p "$out"
log="$out/commands.log"
status=0
first=""
for i in "${!builds[@]}"; do
    b=${builds[$i]}
    name=$(echo "$b" | tr '/' '_')
    if [ "$i" -eq 0 ]; then
        "$b" "${args[@]}" --random-commands 60 --record "$log" > "$out/$name.txt" 2> "$out/$name.err"
        first="$out/$name.txt"
    else
        "$b" "${args[@]}" --commands "$log" > "$out/$name.txt" 2> "$out/$name.err"
    fi
    if [ -n "$first" ] && [ "$first" != "$out/$name.txt" ]; then
        if diff -q "$first" "$out/$name.txt" > /dev/null; then
            echo "same:    $b"
        else
            echo "DIFFERS: $b (first difference below)"
            diff "$first" "$out/$name.txt" | head -3
            status=1
        fi
    fi
done
exit $status
