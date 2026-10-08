#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary="$root/desktop/build/linux/lcb-hermes"
if [ ! -x "$binary" ]; then
    printf '%s\n' 'Build first with linux/scripts/build.sh.' >&2
    exit 1
fi
exec "$binary" "$@"
