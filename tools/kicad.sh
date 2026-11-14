#!/usr/bin/env bash
# Run a command inside the official KiCad 9 container with the repo mounted at /work.
#   tools/kicad.sh kicad-cli sch erc hardware/carrier/elrs-sim-rx-carrier.kicad_sch
#   tools/kicad.sh python3 hardware/carrier/gen/gen_pcb.py
#   tools/kicad.sh hardware/carrier/gen/build.sh      (regenerate + ERC + DRC + previews)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "${ROOT}:/work" -w /work "${KICAD_IMAGE:-kicad/kicad:9.0}" "$@"
