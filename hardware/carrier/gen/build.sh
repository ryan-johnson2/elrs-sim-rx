#!/usr/bin/env bash
# Regenerate the carrier draft and check it. Run through the container:
#   tools/kicad.sh hardware/carrier/gen/build.sh [sch|pcb|check]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CARRIER="$(dirname "$HERE")"
NAME=elrs-sim-rx-carrier
STEP="${1:-all}"

# kicad-cli wants the global library tables to resolve stock symbols and footprints.
CFG="${HOME}/.config/kicad/9.0"
mkdir -p "$CFG" "$CARRIER/out"
cp -n /usr/share/kicad/template/sym-lib-table /usr/share/kicad/template/fp-lib-table "$CFG/"

cd "$CARRIER"
if [[ $STEP == all || $STEP == sch ]]; then
    python3 "$HERE/gen_sch.py"
fi
kicad-cli sch erc --severity-all -o out/erc.rpt "$NAME.kicad_sch" || true
kicad-cli sch export netlist --format kicadsexpr -o out/$NAME.net "$NAME.kicad_sch"
kicad-cli sch export pdf -o out/schematic.pdf "$NAME.kicad_sch" >/dev/null
kicad-cli sch export svg -o out "$NAME.kicad_sch" >/dev/null
if [[ $STEP == all || $STEP == pcb ]]; then
    python3 "$HERE/gen_pcb.py" out/$NAME.net
fi
if [[ -f $NAME.kicad_pcb ]]; then
    kicad-cli pcb drc --severity-all --schematic-parity -o out/drc.rpt "$NAME.kicad_pcb" || true
    kicad-cli pcb render --side top --width 1400 --height 1400 -o out/render-top.png "$NAME.kicad_pcb" >/dev/null
    kicad-cli pcb render --side bottom --width 1400 --height 1400 -o out/render-bottom.png "$NAME.kicad_pcb" >/dev/null
    kicad-cli pcb export svg --mode-single --exclude-drawing-sheet --page-size-mode 2 \
        -l F.Cu,F.SilkS,F.Fab,Edge.Cuts,User.Drawings -o out/layout-top.svg "$NAME.kicad_pcb" >/dev/null
    kicad-cli pcb export svg --mode-single --exclude-drawing-sheet --page-size-mode 2 --mirror \
        -l B.Cu,B.SilkS,Edge.Cuts -o out/layout-bottom.svg "$NAME.kicad_pcb" >/dev/null
fi
echo "--- ERC"; grep -v "^$" out/erc.rpt | tail -n +4
if [[ -f out/drc.rpt ]]; then echo "--- DRC"; grep -v "^$" out/drc.rpt | tail -n +4; fi
