# Carrier board (draft v0.2)

KiCad 9 project for the Phase 2 carrier in [docs/HBOM.md](../../docs/HBOM.md). v0.2 follows the
Phase 1 breadboard and perfboard builds: the module is soldered in directly, the back-row
socket is gone, and the receiver mount was redrawn for the Happymodel EP2. Not yet ordered.

| | |
|---|---|
| Board | 22 x 39.1 mm with the lower left corner cut away for the receiver, 2 layers, one M2 hole |
| MCU | RP2040-Zero soldered in by its own pin headers: two 1x9 rows of plated holes (J1, J2) and five unconnected holes for the back-row pins (J5), so the module drops in unmodified |
| Receiver | Happymodel EP2, antenna side up, soldered by its castellated edge onto four SMD pads (J3) and hanging over the cut-out, since it has parts on both faces |
| Button | Four pads (J4) marked by function: LED+, LED-, BTN, BTN. The switch's two leads go to the two BTN pads, either way round (one of them is ground) |
| Fab-assembled | U1, Q1, R1 to R3, C1 to C4. All top side, all under the module |
| Hand-soldered | The module, the receiver, the button leads |

## Circuit

Electrically the same as the Phase 1 breadboard build with the planned Phase 2 swaps:

- TPS22918 load switch replaces AO3401A + 2N7000 + 10 k. GP2 drives ON directly; R1 (100 k)
  holds the receiver off until firmware takes over. QOD is tied to VOUT so the rail collapses
  quickly, which the power-cycle bind sequence needs.
- 2N7002 replaces the 2N7000 LED ring driver, same 100 R gate resistor and 100 k pull-down.
- Pin map is unchanged from `firmware/src/board.h`. No firmware changes are needed.

## Check before ordering

1. **EP2 pad order.** GND, 5V, TX, RX from the left with the pad edge toward the module and
   the antenna side up, from the EP2 TCXO manual: ground is the end pad on the antenna's side.
   The EP2 itself has no pad labels. Compare with the EP2 wired to the working prototype.
2. **EP2 pad pitch and underside: checked.** A photo of a real EP2 on a steel rule
   (2026-11-14) shows the outer pads 7.6 mm apart, so 2.54 mm pitch, centred on the edge, and
   nothing taller than a bare pad within 2.2 mm of the pad edge on the ESP side.
3. **RP2040-Zero pin grid.** Two rows of nine, 15.24 mm apart, pin 1 (5V left, GP0 right) at
   the USB end, and the five back-row holes in line with the last pin of each side row.
   Not yet checked: an unmodified module should drop into plain 0.1 inch perfboard.
4. **Height under the module.** The SMD parts sit under the soldered-in module, which stands
   about 2.5 mm off the board on its header spacers. The tallest part is the 0805 capacitor.
5. **LCSC part numbers** in the schematic `LCSC` field were filled in from memory. Confirm
   each one, and add one for U1, before generating a JLCPCB BOM.
6. **Mounting.** One M2 hole on the arm beside the receiver. The receiver is held only by
   its four solder joints, so the case should support its free end.

## Files

- `elrs-sim-rx-carrier.kicad_pro`, `.kicad_sch`, `.kicad_pcb`: the project.
- `carrier.kicad_sym`, `sym-lib-table`: project symbol library (TPS22918).
- `carrier.pretty/`, `fp-lib-table`: project footprint library (receiver landing).
- `gen/`: the scripts that produced the draft. Placement and routing are written out by hand
  in `gen_pcb.py`. Regenerating overwrites the schematic and board, so stop using the scripts
  once you start editing in KiCad.

## Regenerate and check

KiCad runs in the official container; nothing is installed on the host.

```
tools/kicad.sh hardware/carrier/gen/build.sh          # regenerate, ERC, DRC, previews
tools/kicad.sh hardware/carrier/gen/build.sh check    # ERC, DRC, previews only
```

Outputs land in `out/` (gitignored): `schematic.pdf`, `render-top.png`, `render-bottom.png`,
`layout-top.svg`, `layout-bottom.svg`, `erc.rpt`, `drc.rpt`.

ERC is clean. DRC reports three `lib_footprint_mismatch` warnings for J1, J2 and J5, which are
expected: their courtyards are trimmed where the three rows of holes meet, and J5 has no
silkscreen. The v0.1 files are kept in `out/v0.1-backup/`.

`tools/kicad.sh hardware/carrier/gen/fab.sh` writes the JLCPCB order files to `out/fab/`:
`gerbers.zip`, `bom.csv` and `cpl.csv`. Only the fab-assembled parts are listed.

After opening the board in the KiCad GUI, press B once to refill zones.
