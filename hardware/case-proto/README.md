# Case for the Phase 1b perfboard prototype

Desk case for the soldered prototype on the 14 x 20 hole (4 x 6 cm) perfboard: RP2040-Zero,
receiver power switch, LED driver, an ELRS receiver at the far end and the FILN 12 mm button
on the lid.

| Part | What it does | How to print |
|---|---|---|
| `tray` | Open box. The board drops in and rests on ledges along its long sides and far end. USB-C plug enters through the end wall. | Floor down, as exported. No supports. |
| `lid` | Carries the button. A lip slides inside the tray walls and clicks in; four posts hold the board down. | Top face down, as exported. No supports. |

Outer size 61.5 x 48.3 x 23.7 mm with the default parameters.

`./build.sh` exports the STLs and pictures to `out/` and checks that the two parts, and the
case and the electronics, do not overlap. It needs Docker only.

The cavity is 4 mm wider on the column 1 side because the SOT-23 adapter overhangs that edge
by 3.2 mm; two blocks locate the board there. The height comes from the tallest part on the board (14.4 mm above its top face) plus 2 mm;
the button's 12 mm body then ends 4.4 mm above the board, over the empty corner at the
receiver end on the column 1 side.

Sizes are parameters at the top of `case.scad`. Not yet printed. The sizes were measured on
the real board, except `row_a`, which is derived from the trimmed board length.
The BOOT button is not reachable with the lid on; use the config page's update mode.
