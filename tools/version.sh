#!/usr/bin/env bash
# Print the firmware version for this checkout. Used by tools/build.sh.
#
#   on a release tag vX.Y.Z, clean tree   X.Y.Z
#   anywhere else                         <next patch>-dev.<commits since that tag>+<commit>[.dirty]
#   no release tag yet                    0.1.0-dev.<commit count>+<commit>[.dirty]
#
# "That tag" is the highest vX.Y.Z tag in the repository, whether or not it is an ancestor
# of this commit, so the devel branch counts on from the last release made on main.
# Board tags (carrier-v0.2) and the rolling devel-build tag are ignored.
# Set FW_VERSION in the environment to override.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ -n "${FW_VERSION:-}" ]; then echo "$FW_VERSION"; exit 0; fi

g() { git -c safe.directory='*' "$@"; }
if ! g rev-parse --git-dir >/dev/null 2>&1; then echo "0.0.0-nogit"; exit 0; fi

strict='^v[0-9]+\.[0-9]+\.[0-9]+$'
sha=$(g rev-parse --short=7 HEAD)
dirty=""
g diff --quiet HEAD -- || dirty=".dirty"

here=$(g tag --points-at HEAD --sort=-v:refname | grep -E "$strict" | head -n1 || true)
if [ -n "$here" ] && [ -z "$dirty" ]; then echo "${here#v}"; exit 0; fi

latest=$(g tag -l --sort=-v:refname | grep -E "$strict" | head -n1 || true)
if [ -z "$latest" ]; then
    base="0.1.0"
    n=$(g rev-list --count HEAD)
else
    IFS=. read -r major minor patch <<<"${latest#v}"
    base="$major.$minor.$((patch + 1))"
    n=$(g rev-list --count "$latest..HEAD")
fi
echo "$base-dev.$n+$sha$dirty"
