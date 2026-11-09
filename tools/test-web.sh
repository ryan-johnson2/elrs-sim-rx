#!/usr/bin/env bash
# Run the Web UI browser test (Playwright, Chromium) against the mock device.
# Screenshots land in tests/web/shots/ (gitignored).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
PW_IMAGE="${PW_IMAGE:-mcr.microsoft.com/playwright:v1.49.1-jammy}"
PW_VERSION="${PW_IMAGE##*:v}"; PW_VERSION="${PW_VERSION%%-*}"
mkdir -p tests/web/shots tests/web/.cache
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e npm_config_cache=/tmp/.npm \
    -v "$PWD":/work -v "$PWD/tests/web/.cache":/pw -w /pw "$PW_IMAGE" bash -c "
        set -e
        [ -f package.json ] || echo '{\"name\":\"webui-test\",\"type\":\"module\",\"private\":true}' > package.json
        [ -d node_modules/playwright ] || npm i --silent playwright@${PW_VERSION} >/dev/null 2>&1
        cp /work/tests/web/test_webui.mjs /pw/test_webui.mjs
        SHOTS_DIR=/work/tests/web/shots node /pw/test_webui.mjs
    "
