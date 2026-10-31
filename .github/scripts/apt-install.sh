#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# apt-get update and install with time limits and retries: a mirror that hangs or fails costs a
# few minutes and another try, not the job's whole time limit (runs of 2026-10-31 hung for 25
# minutes and more in "Install dependencies").
# Usage: .github/scripts/apt-install.sh PACKAGE...
set -u
options="-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30"
for attempt in 1 2 3; do
    # shellcheck disable=SC2086 # the options are words
    if timeout 300 sudo apt-get $options update &&
        timeout 300 sudo apt-get $options install -y "$@"; then
        exit 0
    fi
    echo "apt-get failed or took too long (attempt $attempt of 3)" >&2
    sleep 15
done
exit 1
