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
    python3 - <<PY
import json, hashlib, os, subprocess, datetime, re
uf2 = "firmware/build/elrs_sim_rx.uf2"
cache = open("firmware/build/CMakeCache.txt").read()
version = re.search(r"^FW_VERSION:STRING=(.*)$", cache, re.M).group(1)
board = re.search(r"^PICO_BOARD:STRING=(.*)$", cache, re.M).group(1)
try:
    commit = subprocess.check_output(["git", "-c", "safe.directory=*", "rev-parse", "--short", "HEAD"], text=True).strip()
    dirty = subprocess.call(["git", "-c", "safe.directory=*", "diff", "--quiet"]) != 0
except Exception:
    commit, dirty = "unknown", False
data = open(uf2, "rb").read()
manifest = {"files": [{
    "name": os.path.basename(uf2), "path": os.path.basename(uf2), "version": version, "board": board,
    "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
    "commit": commit + ("-dirty" if dirty else ""),
    "built": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
}]}
json.dump(manifest, open("firmware/build/manifest.json", "w"), indent=2)
print(json.dumps(manifest["files"][0], indent=2))
PY
    echo
    ls -la firmware/build/*.uf2
'
