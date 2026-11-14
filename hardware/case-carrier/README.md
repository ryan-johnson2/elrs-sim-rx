# Case for the carrier board v0.2

Case for the button version on the carrier PCB (`hardware/carrier`). The FILN 12 mm button
sits in a bay past the receiver end of the board instead of above it, so the case stays narrow
and is only as tall as the button is deep. `inline = false` in `case.scad` puts the bay beside
the board instead (45.9 x 44.2 x 16.9 mm).

| Part | What it does | How to print |
|---|---|---|
| `tray` | The board drops in onto ledges and a pin through its M2 hole. A bay past the receiver end holds the button body. USB-C plug enters through a slot in the end wall. | Floor down, as exported. No supports. |
| `lid` | Carries the button, clicks into the tray, holds the board down along its two long edges and closes the top of the USB slot. | Top face down, as exported. No supports. |

Outer size 26.5 x 63.6 x 16.9 mm with the default parameters.

`./build.sh` exports the STLs and pictures to `out/` and checks that the two parts, and the
case and the electronics, do not overlap. It needs Docker only.

Board dimensions at the top of `case.scad` are copied from `hardware/carrier/gen/gen_pcb.py`;
change them together. The USB socket height, button and solder-joint sizes were measured on
the perfboard prototype. The receiver's height above and below the board (`rx_up`, `rx_down`)
is estimated from the EP2's listed 6 mm thickness.

Not yet printed. The button's body is plastic; its metal nut ends up about 4 mm from the
receiver's antenna corner, which is accepted since range does not matter for a desk dongle.
The receiver's free end rests on nothing: there is a shelf under it for a
foam pad. The BOOT button is not reachable with the lid on; use the config page's update mode.
