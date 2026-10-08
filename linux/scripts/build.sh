#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cmake -S "$root/desktop" -B "$root/desktop/build/linux" -DCMAKE_BUILD_TYPE=MinSizeRel -DLCB_BUILD_TESTS=ON
cmake --build "$root/desktop/build/linux" --parallel
ctest --test-dir "$root/desktop/build/linux" --output-on-failure
