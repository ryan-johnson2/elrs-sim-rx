#!/usr/bin/env bash
# Build the firmware in the container. Output: firmware/build/elrs_sim_rx.uf2
# Usage: tools/build.sh [clean]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ "${1:-}" = "clean" ]; then
    rm -rf firmware/build
fi

tools/docker-run.sh bash -c '
    set -e
    cmake -S firmware -B firmware/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build firmware/build
    echo
    ls -la firmware/build/*.uf2
'
