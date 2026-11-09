#!/usr/bin/env bash
# Run a command inside the build container with the repo mounted at /work.
# Builds the image on first use.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${ELRS_SIM_RX_IMAGE:-elrs-sim-rx-build}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">> Building $IMAGE (first run, takes a few minutes)"
    docker build -t "$IMAGE" "$REPO_ROOT/tools"
fi

TTY_FLAGS=()
if [ -t 0 ] && [ -t 1 ]; then
    TTY_FLAGS=(-it)
fi

exec docker run --rm "${TTY_FLAGS[@]}" \
    --user "$(id -u):$(id -g)" \
    -v "$REPO_ROOT":/work \
    -w /work \
    -e HOME=/tmp \
    "$IMAGE" "$@"
