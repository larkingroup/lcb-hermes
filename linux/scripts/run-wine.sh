#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary="$root/desktop/build/windows-portable/lcb-hermes.exe"
if [ ! -f "$binary" ]; then
    printf '%s\n' 'Fetch the portable release first: python3 linux/scripts/fetch-portable.py' >&2
    exit 1
fi
if ! command -v wine >/dev/null 2>&1; then
    printf '%s\n' 'Wine is required. On Fedora: sudo dnf install wine-core wine-common' >&2
    exit 1
fi
export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/lcb-hermes-wine}"
export WINEDLLOVERRIDES="mscoree,mshtml=${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
export WINEDEBUG="${WINEDEBUG:--all}"
exec wine "$binary" "$@"
