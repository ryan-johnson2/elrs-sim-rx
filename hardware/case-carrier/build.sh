#!/usr/bin/env bash
# Export the STLs and preview images into out/, using the official OpenSCAD container.
#   ./build.sh [-D 'name=value' ...]      extra arguments go to every openscad call
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-out}"
mkdir -p "${HERE}/${OUT}"
scad() {
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "${HERE}:/work" -w /work \
        "${OPENSCAD_IMAGE:-openscad/openscad:dev}" openscad --backend=manifold -q "$@"
}
IMG=(--imgsize=1200,850 --colorscheme=Tomorrow --render)

for p in tray lid; do
    scad "$@" -D "part=\"$p\"" -o "${OUT}/$p.stl" case.scad
    scad "$@" -D "part=\"$p\"" "${IMG[@]}" --viewall --autocenter -o "${OUT}/part-$p.png" case.scad
done

C=11,28,8   # camera: look-at x,y,z then rotation x,y,z then distance
scad "$@" "${IMG[@]}" --camera=$C,60,0,215,220 -o "${OUT}/assembly.png" case.scad
scad "$@" "${IMG[@]}" --camera=$C,50,0,215,220 -D show_lid=false -o "${OUT}/assembly-open.png" case.scad
scad "$@" "${IMG[@]}" --camera=$C,60,0,215,270 -D explode=24 -o "${OUT}/exploded.png" case.scad
scad "$@" "${IMG[@]}" --camera=$C,90,0,180,170 --projection=o -o "${OUT}/end-usb.png" case.scad
scad "$@" "${IMG[@]}" --camera=11,28,8,0,0,0,210 --projection=o -D show_lid=false -o "${OUT}/top-open.png" case.scad

# interference checks: each pair must share less than 0.5 mm3
names=(tray-lid case-electronics)
fail=0
for n in "${!names[@]}"; do
    rm -f "${HERE}/${OUT}/.check.stl"
    scad "$@" -D 'part="check"' -D "check_n=$n" --export-format=binstl -o "${OUT}/.check.stl" case.scad 2>/dev/null || true
    vol=$(python3 - "${HERE}/${OUT}/.check.stl" <<'PY'
import os, struct, sys
f = sys.argv[1]
vol = 0.0
if os.path.exists(f):
    b = open(f, "rb").read()
    for i in range(struct.unpack("<I", b[80:84])[0]):
        a, p, c = (struct.unpack("<3f", b[96 + i * 50 + k * 12:108 + i * 50 + k * 12]) for k in range(3))
        vol += (a[0] * (p[1] * c[2] - p[2] * c[1]) - a[1] * (p[0] * c[2] - p[2] * c[0]) + a[2] * (p[0] * c[1] - p[1] * c[0])) / 6
print(f"{abs(vol):.2f}")
PY
)
    echo "check ${names[$n]}: ${vol} mm3"
    python3 -c "import sys; sys.exit(0 if float('$vol') >= 0.5 else 1)" && fail=1
done
rm -f "${HERE}/${OUT}/.check.stl"
docker run --rm -v "${HERE}:/work" -w /work "${OPENSCAD_IMAGE:-openscad/openscad:dev}" openscad -o /tmp/x.stl case.scad 2>&1 | grep ECHO || true
[ "$fail" = 0 ] && echo "interference checks: all clear" || echo "INTERFERENCE FOUND"
exit $fail
