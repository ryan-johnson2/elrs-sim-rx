#!/usr/bin/env bash
# Fabrication outputs for JLCPCB into out/fab/. Run through the container after build.sh:
#   tools/kicad.sh hardware/carrier/gen/fab.sh
# Writes gerbers.zip (Gerbers + drill), bom.csv and cpl.csv. Only parts with an LCSC number
# are listed; the module, receiver, button pads and mounting hole are fitted by hand.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CARRIER="$(dirname "$HERE")"
NAME=elrs-sim-rx-carrier
CFG="${HOME}/.config/kicad/9.0"
mkdir -p "$CFG"
cp -n /usr/share/kicad/template/sym-lib-table /usr/share/kicad/template/fp-lib-table "$CFG/"

cd "$CARRIER"
FAB=out/fab
rm -rf "$FAB"; mkdir -p "$FAB/gerbers"
kicad-cli pcb export gerbers -o "$FAB/gerbers/" --no-protel-ext --subtract-soldermask \
    -l F.Cu,B.Cu,F.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts "$NAME.kicad_pcb" >/dev/null
kicad-cli pcb export drill -o "$FAB/gerbers/" --format excellon --excellon-separate-th \
    --excellon-units mm --generate-map --map-format gerberx2 "$NAME.kicad_pcb" >/dev/null
kicad-cli pcb export pos -o "$FAB/pos-raw.csv" --format csv --units mm --side front "$NAME.kicad_pcb" >/dev/null
kicad-cli sch export bom -o "$FAB/bom-raw.csv" --fields 'Reference,Value,Footprint,LCSC,MPN' \
    --labels 'Reference,Value,Footprint,LCSC,MPN' "$NAME.kicad_sch" >/dev/null

python3 - "$FAB" <<'PY'
import csv, sys, zipfile, os, glob
fab = sys.argv[1]
rows = [r for r in csv.DictReader(open(f"{fab}/bom-raw.csv")) if r["LCSC"].strip()]
keep = {r["Reference"] for r in rows}
# JLCPCB's part library holds these packages turned half a turn from KiCad's footprints, so
# the placement angle needs correcting or they land with pin 1 at the wrong corner. Seen on
# the fab's own placement preview for this board (2026-11-14).
JLC_ROTATION = {"SOT-23": 180, "SOT-23-6": 180}
turn = {r["Reference"]: JLC_ROTATION.get(r["Footprint"].split(":")[-1], 0) for r in rows}
groups = {}
for r in rows:
    groups.setdefault((r["Value"], r["Footprint"].split(":")[-1], r["LCSC"], r["MPN"]), []).append(r["Reference"])
with open(f"{fab}/bom.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
    for (value, fp, lcsc, mpn), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        w.writerow([mpn or value, ",".join(sorted(refs)), fp, lcsc])
n = 0
with open(f"{fab}/cpl.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in csv.DictReader(open(f"{fab}/pos-raw.csv")):
        if r["Ref"] in keep:
            rot = (float(r["Rot"]) + turn[r["Ref"]]) % 360
            w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm", "Top", "%.6f" % rot]); n += 1
if n != len(keep):
    sys.exit(f"placement file has {n} parts, BOM has {len(keep)}")
with zipfile.ZipFile(f"{fab}/gerbers.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for p in sorted(glob.glob(f"{fab}/gerbers/*")):
        z.write(p, os.path.basename(p))
os.remove(f"{fab}/bom-raw.csv"); os.remove(f"{fab}/pos-raw.csv")
print(f"{len(groups)} BOM lines, {n} placements")
print(open(f"{fab}/bom.csv").read())
PY
ls -la "$FAB" "$FAB/gerbers"
