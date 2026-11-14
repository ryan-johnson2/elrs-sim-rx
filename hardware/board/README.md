# Integrated board (draft v0.1, placed, not routed)

KiCad 9 project for the next board after the carrier: the RP2040 itself is on the board, so
there is no module to solder. The fab fits everything except the receiver and the button leads.

| | |
|---|---|
| Board | 22 x 39.1 mm, 2 layers, same outline, receiver cut-out, button pads and M2 hole as carrier v0.2 |
| MCU | RP2040, W25Q16 flash, 12 MHz crystal, XC6206 3.3 V regulator, USB-C socket |
| Kept from the module | BOOT button. No RESET button; the RUN pin has a pull-up |
| Status light | One plain LED on GP25. The firmware drives GP25 and the RP2040-Zero's RGB LED on GP16 with the same pattern, so one build serves both boards |
| Receiver | Happymodel EP2 soldered by its castellated edge over the cut-out, as on the carrier |
| Button | Four pads: LED+, LED-, BTN, BTN. Leave the button off for the no-button version |
| Circuit | Otherwise the carrier's: TPS22918 load switch on GP2, 2N7002 LED driver on GP4, button on GP3, CRSF on GP0 / GP1 |
| Debug | SWCLK and SWDIO test pads |

## State

- Schematic: complete, ERC clean. It is drawn label-style (every pin carries a net name, no
  wires) because it is generated from a parts table in `gen/gen_sch.py`.
- Placement: all 48 footprints placed, no courtyard overlaps.
- Routing: none yet. `gen/route.sh` runs the Freerouting autorouter on the exported
  `out/elrs-sim-rx-board.dsn` and imports the result; it has not been run.
- Part numbers: only the RP2040, USB-C socket, regulator, load switch and LED driver have LCSC
  numbers. The flash, crystal, button, LED and all the 0402 passives still need choosing, and
  the crystal's load capacitors (drawn as 15 pF) depend on the crystal picked.

## To do before this can be ordered

1. Route it, then review the result by hand: USB pair, crystal, decoupling and the ground pour.
2. Choose and confirm every part number; pick the crystal and set C17 / C18 to match.
3. Decide on USB ESD protection (none drawn).
4. Check the EP2 pad pitch (shared with the carrier; `RX_PITCH` in `gen/gen_pcb.py`).
5. Firmware: confirm the build boots with this flash and crystal. The pin map is unchanged.

## Regenerate and check

```
tools/kicad.sh hardware/board/gen/build.sh          # schematic, placement, ERC, DRC, renders
hardware/board/gen/route.sh                          # autoroute and import (run on the host)
```
