#!/usr/bin/env bash
# Autoroute the placed board with Freerouting, then import the result. Run on the host:
#   hardware/board/gen/route.sh
# Needs Docker, the eclipse-temurin:21-jre image, and tools/.cache/freerouting.jar
# (freerouting-2.5.0.jar from https://github.com/freerouting/freerouting/releases).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
NAME=elrs-sim-rx-board
OUT=hardware/board/out
cd "$ROOT"
[ -f tools/.cache/freerouting.jar ] || { echo "tools/.cache/freerouting.jar is missing" >&2; exit 1; }
[ -f "$OUT/$NAME.dsn" ] || { echo "$OUT/$NAME.dsn is missing: run tools/kicad.sh hardware/board/gen/build.sh first" >&2; exit 1; }
rm -f "$OUT/$NAME.ses"
docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT:/work" -w /work eclipse-temurin:21-jre \
    java -Djava.awt.headless=true -jar tools/.cache/freerouting.jar \
    -de "$OUT/$NAME.dsn" -do "$OUT/$NAME.ses" -mp "${PASSES:-30}" 2>&1 | tail -n "${LOG_LINES:-15}"
[ -f "$OUT/$NAME.ses" ] || { echo "Freerouting wrote no session file" >&2; exit 1; }
tools/kicad.sh python3 hardware/board/gen/route_import.py
tools/kicad.sh hardware/board/gen/build.sh check
