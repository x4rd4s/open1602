#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# add_original_seeds.sh <anno1602-dir> <work-dir> [o1602-savegame-folder]
#
# Adds files of an Anno 1602 installation (and, if given, Open1602's own savegames) to the fuzz
# corpus of tools/fuzz/run_fuzzers.sh, with the mode byte the targets expect where they have one.
# Files over 4 MiB are left out (libFuzzer works on small inputs). The corpus then holds the game's
# data: keep it on the machine.
set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 <anno1602-dir> <work-dir> [o1602-savegame-folder]" >&2
    exit 2
fi
game=$1
work=$2
saves=${3:-}
corpus="$work/corpus"
mkdir -p "$corpus"/{cod,gad,sprites,chunks,gam_world,o1602_save,media}

# copy <target> <mode-byte or ""> <files...>
copy() {
    local target=$1 mode=$2
    shift 2
    local f
    for f in "$@"; do
        [ -f "$f" ] || continue
        [ "$(stat -c %s "$f")" -le $((4 << 20)) ] || continue
        local name
        name=$(basename "$f" | tr ' ' '_')
        if [ -n "$mode" ]; then
            { printf "\\x$mode"; cat "$f"; } >"$corpus/$target/orig-$name"
        else
            cp "$f" "$corpus/$target/orig-$name"
        fi
    done
}

find_i() {  # find_i <pattern...>: files under the game folder, case-insensitive
    local args=() p
    for p in "$@"; do
        args+=(-o -iname "$p")
    done
    find "$game" -type f \( -false "${args[@]}" \) 2>/dev/null | sort
}

mapfile -t haeuser < <(find_i 'haeuser.cod')
mapfile -t figuren < <(find_i 'figuren.cod')
copy cod 02 "${haeuser[@]}"
copy cod 03 "${figuren[@]}"
mapfile -t gads < <(find_i '*.gad')
copy gad "" "${gads[@]}"
mapfile -t sheets < <(find_i '*.bsh' '*.zei' '*.col' | head -n 40)
copy sprites "" "${sheets[@]}"
mapfile -t islands < <(find_i '*.scp' | head -n 60)
mapfile -t scenarios < <(find_i '*.szs' '*.szm' '*.gam')
copy chunks "" "${islands[@]}" "${scenarios[@]}"
copy gam_world "" "${scenarios[@]}"
mapfile -t videos < <(find_i '*.smk' | head -n 10)
copy media 00 "${videos[@]}"
mapfile -t sounds < <(find_i '*.wav' | head -n 40)
copy media 01 "${sounds[@]}"
if [ -n "$saves" ]; then
    mapfile -t own < <(find "$saves" -type f -iname '*.o1602' | sort)
    copy o1602_save "" "${own[@]}"
fi
for t in cod gad sprites chunks gam_world o1602_save media; do
    printf '%-12s %5d files\n' "$t" "$(find "$corpus/$t" -type f -name 'orig-*' | wc -l)"
done
