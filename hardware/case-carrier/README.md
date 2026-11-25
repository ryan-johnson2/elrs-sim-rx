# Case for the carrier board v0.2

Case for the button version on the carrier PCB (`hardware/carrier`). The FILN 12 mm button
sits in a bay past the receiver end of the board instead of above it, so the case stays narrow
and is only as tall as the button is deep. `inline = false` in `case.scad` puts the bay beside
the board instead (45.9 x 44.2 x 16.9 mm).

| Part | What it does | How to print |
|---|---|---|
| `tray` | The board drops in onto ledges and is held by one M2 screw through its mounting hole, which cuts its own thread in a 1.8 mm hole in the boss below. A bay past the receiver end holds the button body. USB-C plug enters through a slot in the end wall. | Floor down, as exported. No supports. |
| `lid` | Carries the button, clicks into the tray, holds the board down along its two long edges and closes the top of the USB slot. | Top face down, as exported. No supports. |

Outer size 26.5 x 63.6 x 16.9 mm with the default parameters.

The lid carries the label `SIMRX` (`label` in `case.scad`, empty for none) in its top face,
which is the face on the build plate. Two ways to print it in a second colour:

- `lid.stl` + `lid_text.stl`: the lid has the letters as a 0.6 mm recess and `lid_text.stl` is
  the letters alone, in place. Load both as one object with two parts and give the letters
  their own filament. Printed alone, `lid.stl` gives a debossed label.
- `lid_flush.stl`: one piece for painting in the slicer. The letters are outlined by a 0.05 mm
  step, too shallow to be printed, so the face comes out flat; it is there so the fill tool
  stops at the letter edges. Fill each letter with the second colour.

`./build.sh` exports the STLs and pictures to `out/` and checks that the two parts, and the
case and the electronics, do not overlap. It needs Docker only.

Board dimensions at the top of `case.scad` are copied from `hardware/carrier/gen/gen_pcb.py`;
change them together. The USB socket height, button and solder-joint sizes were measured on
the perfboard prototype. The receiver's height above and below the board (`rx_up`, `rx_down`)
is estimated from the EP2's listed 6 mm thickness.

The board screw is an M2, 5 or 6 mm long (the board's hole is 2.2 mm, so an M3 does not fit).
The hole goes through the floor: there is 3.7 mm of plastic under the board, so a 5 mm screw
ends inside it and a 6 mm one stands 0.7 mm out underneath. Widen `screw_pilot` if the screw
is too stiff to drive. The lid
clicks on two 30 mm ridges; `bump` and `lid_fit` set how firmly, and `snap_play` should stay
equal in the groove's size and drop so the lid has no vertical play.

First print (2026-11-24) showed the tray mirrored, since fixed. The second (2026-11-25) had a
printed split pin in place of the screw: the lid held well, but the board would not go over
the pin, so it was replaced by the screw, which is not yet printed. The button's body is plastic; its metal nut ends up about 4 mm from the
receiver's antenna corner, which is accepted since range does not matter for a desk dongle.
The receiver's free end rests on nothing: there is a shelf under it for a
foam pad. The BOOT button is not reachable with the lid on; use the config page's update mode.
