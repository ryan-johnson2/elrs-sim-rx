#!/usr/bin/env bash
# Build and run the host-side unit tests in the container.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

tools/docker-run.sh bash -c '
    set -e
    cmake -S tests -B tests/build -G Ninja
    cmake --build tests/build
    ctest --test-dir tests/build --output-on-failure
'
