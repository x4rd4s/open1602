#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts the game twice; original data is not needed.
# 1. With an unknown option: it must load (SDL3 found, on Windows SDL3.dll next to the program) and
#    answer with its usage text and exit code 2.
# 2. With a folder that is no installation: it must report that as a fatal error, exit code 1.
#    SDL's dummy drivers keep the error message box away (report_fatal shows one when stderr is not
#    a terminal, on Windows always): with SDL_VIDEO_DRIVER=dummy, SDL only asks the dummy driver,
#    which has no message box. In the ASan build this runs the start and the error path under the
#    sanitizers.
set -u
game="$1"
if [[ "${RUNNER_OS:-}" == "Windows" && ! -f "$(dirname "$game")/SDL3.dll" ]]; then
    echo "SDL3.dll is missing next to $game" >&2
    exit 1
fi
export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy

"$game" --no-such-option 2> usage.txt
code=$?
head -n 1 usage.txt
if [[ $code -ne 2 ]] || ! grep -q '^usage: open1602' usage.txt; then
    echo "the game did not start as expected (exit code $code)" >&2
    exit 1
fi

empty=$(mktemp -d)
"$game" --game-dir "$empty" 2> fatal.txt
code=$?
rmdir "$empty"
cat fatal.txt
if [[ $code -ne 1 ]] || ! grep -q '^error: .*does not look like an Anno 1602 installation' fatal.txt; then
    echo "the game did not report the missing data as expected (exit code $code)" >&2
    exit 1
fi
