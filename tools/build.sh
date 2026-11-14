#!/usr/bin/env bash
# Build the firmware in the container. Output: firmware/build/elrs_sim_rx.uf2 (full) and
# firmware/build/elrs_sim_rx_lite.uf2 (no button, receiver wired direct)
# The version comes from git, see tools/version.sh.
# Usage: tools/build.sh [clean]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ "${1:-}" = "clean" ]; then
    rm -rf firmware/build
fi

tools/docker-run.sh bash -c '
    set -e
    version=$(tools/version.sh)
    echo ">> firmware version $version"
    cmake -S firmware -B firmware/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFW_VERSION="$version"
    cmake --build firmware/build
    python3 - <<PY
import json, hashlib, os, subprocess, datetime, re
cache = open("firmware/build/CMakeCache.txt").read()
version = re.search(r"^FW_VERSION:STRING=(.*)$", cache, re.M).group(1)
board = re.search(r"^PICO_BOARD:STRING=(.*)$", cache, re.M).group(1)
try:
    commit = subprocess.check_output(["git", "-c", "safe.directory=*", "rev-parse", "--short", "HEAD"], text=True).strip()
    dirty = subprocess.call(["git", "-c", "safe.directory=*", "diff", "--quiet"]) != 0
except Exception:
    commit, dirty = "unknown", False
built = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
files = []
# The page lists these in order and selects the first, so the full build is the default.
for name, variant, label in (("elrs_sim_rx.uf2", "full", "Full: button, LED ring, power switch"),
                             ("elrs_sim_rx_lite.uf2", "lite", "Lite: no button, receiver wired direct")):
    data = open("firmware/build/" + name, "rb").read()
    files.append({"name": name, "path": name, "label": label, "variant": variant, "version": version,
                  "board": board, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                  "commit": commit + ("-dirty" if dirty else ""), "built": built})
json.dump({"files": files}, open("firmware/build/manifest.json", "w"), indent=2)
for f in files:
    print(f["name"], f["variant"], f["size"], f["sha256"][:12])
PY
    echo
    ls -la firmware/build/*.uf2
'
