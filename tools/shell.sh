#!/usr/bin/env bash
# Interactive shell inside the build container.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
exec tools/docker-run.sh bash
