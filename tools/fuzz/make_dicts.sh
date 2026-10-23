#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make_dicts.sh: writes the libFuzzer dictionaries tests/fuzz/cod.dict and tests/fuzz/gad.dict
# from the keys the loaders read, so that the fuzzers find "Wegspeed:" without guessing it.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
src=$root/engine/assets/src
keys() {  # string literals passed to integer(...), field(...), records(...), flag(r, ...)
    grep -ohE '(integer|field|records|flag)\((r, )?"[A-Za-z_]+"' "$@" | sed -E 's/.*"([A-Za-z_]+)"/\1/' | sort -u
}
{
    echo '# Keys of haeuser.cod / figuren.cod as the loaders read them (tools/fuzz/make_dicts.sh)'
    for w in Objekt EndObj ObjFill Nummer @Nummer Version HAUS HAUS_PRODTYP HAUS_BAUKOST FIGUR ANIM; do
        echo "\"$w\""
    done
    keys "$src/building_table.cpp" "$src/figure_table.cpp" | sed 's/.*/"&"/'
    printf '"%s"\n' ': ' ' = ' ', ' '+' '-' '.' '2147483647' '99999999999.5' '65535' '256' '\x0a'
} >"$root/tests/fuzz/cod.dict"
{
    echo '# Keys of GAD layouts (tools/fuzz/make_dicts.sh)'
    keys "$src/gad.cpp" | sed 's/.*/"&"/'
    for w in GADGET MAXGADGET GAD_GFX GAD_TEXTL GAD_TEXTFL GAD_BALKEN Objekt EndObj ObjFill @Nummer Include; do
        echo "\"$w\""
    done
    printf '"%s"\n' ': ' ' = ' 'Include: \"' '2147483647' '\x0a'
} >"$root/tests/fuzz/gad.dict"
wc -l "$root/tests/fuzz/cod.dict" "$root/tests/fuzz/gad.dict"
