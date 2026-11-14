// ELRS Sim RX: desk case for the Phase 1b perfboard prototype (14 x 20 hole, 4 x 6 cm board).
//
// Two printed parts:
//   tray   open box. The board drops in from the top and rests on ledges along its long
//          sides and far end. USB-C plug enters through the end wall. Print floor down.
//   lid    carries the panel button. A lip slides inside the tray walls and clicks in; four
//          posts reach down and hold the board on its ledges. Print top face down.
//
// Coordinates: x along the board from the USB end (row A) to the receiver end (row T),
// y across from the column 1 edge to the column 14 edge, z up from the tray's underside.
// On the trimmed board the USB-C socket overhangs the row A edge, so the cavity also runs
// a little past that edge and two stops locate the board there.
// The SOT-23 adapter overhangs the column 1 edge, so the cavity is wider on that side and
// two blocks locate the board there instead of the wall.

part = "assembly"; // [assembly, tray, lid, check]
explode = 0;
show_lid = true;
check_n = 0;

/* [Measured: check against the real board] */
board_l = 57;        // along the rows, USB end trimmed (60 untrimmed). Measured
board_w = 40;
board_t = 1.6;
row_a = 2.9;         // USB-end board edge to the centre of the row A holes. Derived: 5.87 untrimmed, less the 3 mm trimmed off
col_1 = 3.5;         // column 1 board edge to the centre of the column 1 holes
pitch = 2.54;
pins = 1.6;          // solder joints and wire runs below the board. Measured
q1_over = 3.2;       // SOT-23 adapter overhang past the column 1 edge
usb_zc = 5.5;        // centre of the USB-C socket above the board's top face. Measured: socket top 8.7 above the board's underside
usb_col = 7;         // socket centre line (module pins are in columns 4 and 10)
tall_parts = 14.4;   // tallest part above the board anywhere. Measured: 16 above the board's underside
low_parts = 3;       // tallest part under the button (nothing there now; kept as a margin)

/* [Button: FILN 12 mm] */
btn_d = 12.2;        // panel hole
btn_depth = 12;      // body behind the panel. Measured
btn_wire = 0;        // extra room under the body. None: the leads leave at 90 degrees inside btn_depth
btn_nut = 15.6;      // across the corners (14 across the flats). Measured
btn_row = 18.4;      // empty corner at the receiver end, column 1 side: as far from C1 and C2
btn_col = 2.2;       // as the nut allows against the lid's lip

/* [Fit] */
fit = 0.25;          // board to tray
lid_fit = 0.2;       // lid lip to tray wall
floor_t = 1.6;
wall = 2.0;
top_t = 2.0;
ledge = 1.2;         // tray ledge under the board's long edges and far end
lip_t = 1.4;
lap = 4;             // how far the lid lip reaches into the tray
bump = 0.4;          // click ridge height
usb_open = [13.5, 8]; // opening for a USB-C plug body
ant_notch = true;    // two slots at the receiver end for RP3 antenna leads
ant_w = 2.4;
ant_gap = 14;

/* [Hidden] */
$fn = 64;
e = 0.01;
pocket = pins + 0.5;
T = floor_t + pocket + board_t;                       // z of the board's top face
H = max(tall_parts + 2, low_parts + btn_wire + btn_depth); // inside height above the board
gap_l = q1_over + 0.8;                                // cavity beyond the column 1 edge
usb_front = rx(1) - 2.6;                              // front of the USB-C socket; past the board edge once trimmed
xa = min(-fit, usb_front - 0.4); xb = board_l + fit;
ya = -gap_l; yb = board_w + fit;
Z = T + H;                                            // top of the tray wall
function rx(r) = row_a + (r - 1) * pitch;
function cy(c) = col_1 + (c - 1) * pitch;
bx = rx(btn_row); by = cy(btn_col);
snap_z = Z - lap / 2;
snap_l = 24;

echo(str("OUTER SIZE: ", xb - xa + 2 * wall, " x ", yb - ya + 2 * wall, " x ", Z + top_t));
echo(str("inside height above the board: ", H, ", button centre at x=", bx, " y=", by));

module box(a, b) { translate(a) cube([b[0] - a[0], b[1] - a[1], b[2] - a[2]]); }
module rbox(a, b, r = 2.5) {
    translate([a[0], a[1], a[2]]) linear_extrude(b[2] - a[2])
        offset(r) offset(-r) square([b[0] - a[0], b[1] - a[1]]);
}
module xrod(y, z, r) { translate([(xa + xb) / 2 - snap_l / 2, y, z]) rotate([0, 90, 0]) cylinder(r = r, h = snap_l); }
module ant_cuts() {
    if (ant_notch) for (s = [-1, 1])
        box([xb - lid_fit - lip_t - e, board_w / 2 + s * ant_gap / 2 - ant_w / 2, Z - lap - 0.5],
            [xb + wall + e, board_w / 2 + s * ant_gap / 2 + ant_w / 2, Z + e]);
}

module tray() {
    difference() {
        rbox([xa - wall, ya - wall, 0], [xb + wall, yb + wall, Z]);
        // room above the ledges
        box([xa, ya, floor_t + pocket], [xb, yb, Z + e]);
        // pocket for the solder side. No ledge at the USB end: row A joints sit close to it
        box([xa, ledge, floor_t], [board_l - ledge, board_w - ledge, floor_t + pocket + e]);
        // USB-C plug
        box([xa - wall - e, cy(usb_col) - usb_open[0] / 2, T + usb_zc - usb_open[1] / 2],
            [xa + e, cy(usb_col) + usb_open[0] / 2, T + usb_zc + usb_open[1] / 2]);
        // click grooves
        xrod(ya, snap_z, bump + 0.1);
        xrod(yb, snap_z, bump + 0.1);
        ant_cuts();
    }
    // blocks that locate the column 1 edge, clear of the SOT-23 adapter in the middle
    box([xa, ya, floor_t + pocket - e], [8, -fit, T]);
    box([board_l - 12, ya, floor_t + pocket - e], [xb, -fit, T]);
    // stops at the USB end, where the cavity runs past the board edge to clear the socket
    if (xa < -fit - e) {
        box([xa, ya, floor_t + pocket - e], [-fit, 3, T]);
        box([xa, board_w - 3, floor_t + pocket - e], [-fit, yb, T]);
    }
}

module lid() {
    difference() {
        union() {
            rbox([xa - wall, ya - wall, Z], [xb + wall, yb + wall, Z + top_t]);
            // lip
            difference() {
                box([xa + lid_fit, ya + lid_fit, Z - lap], [xb - lid_fit, yb - lid_fit, Z + e]);
                box([xa + lid_fit + lip_t, ya + lid_fit + lip_t, Z - lap - e],
                    [xb - lid_fit - lip_t, yb - lid_fit - lip_t, Z + 2 * e]);
            }
            // click ridges
            intersection() {
                union() { xrod(ya + lid_fit, snap_z, bump); xrod(yb - lid_fit, snap_z, bump); }
                box([xa, ya - bump, Z - lap], [xb, yb + bump, Z]);
            }
            // posts that hold the board down at its corners
            for (x = [[0.5, 3.0], [board_l - 3.0, board_l - 0.5]]) {
                box([x[0], ya + lid_fit, T + 0.2], [x[1], 2.0, Z + e]);
                box([x[0], board_w - 2.0, T + 0.2], [x[1], yb - lid_fit, Z + e]);
            }
        }
        translate([bx, by, Z - e]) cylinder(d = btn_d, h = top_t + 2 * e);
        ant_cuts();
    }
}

// Stand-ins for the electronics, for the pictures and the interference check
module electronics() {
    color("seagreen") box([0, 0, T - board_t], [board_l, board_w, T]);
    color("silver") box([1, ledge + 0.3, T - board_t - pins], [board_l - ledge - 0.3, board_w - ledge - 0.3, T - board_t]);
    // RP2040-Zero and its USB-C socket
    color("navy") box([rx(1) - 1.6, cy(4) - 1.4, T + 2.5], [rx(9) + 1.6, cy(10) + 1.4, T + 3.5]);
    color("silver") box([rx(1) - 2.6, cy(usb_col) - 4.5, T + 3.5], [rx(1) + 4.7, cy(usb_col) + 4.5, T + 6.7]);
    // everything else on the board, as one block kept clear of the edges
    color("dimgray", 0.5) box([2, 3, T], [board_l - 2, board_w - 3, T + low_parts]);
    // SOT-23 adapter overhang
    color("royalblue") box([rx(12) - 4, -q1_over, T + 0.5], [rx(14) + 4, cy(2) + 1.3, T + 6]);
    // receiver at the far end
    color("black") box([rx(17) - 1, cy(8) - 1, T], [rx(20) + 1, cy(13) + 1, T + 6]);
}
module button() {
    color("gray") translate([bx, by, Z - btn_depth]) cylinder(d = 12, h = btn_depth + top_t + 2);
    color("gray") translate([bx, by, Z + top_t]) cylinder(d = 14, h = 2);
    color("gray") translate([bx, by, Z - 2.5]) cylinder(d = btn_nut, h = 2.5, $fn = 6);
    color("gray", 0.4) translate([bx, by, Z - btn_depth - btn_wire]) cylinder(d = 9, h = btn_wire);
}
module usb_plug() {
    color("white") box([-22, cy(usb_col) - 6.25, T + usb_zc - 3.25], [rx(1) - 2.6, cy(usb_col) + 6.25, T + usb_zc + 3.25]);
}

if (part == "tray") tray();
else if (part == "lid") translate([0, 0, Z + top_t]) rotate([180, 0, 0]) translate([0, -(ya + yb), 0]) lid();
else if (part == "check") {
    if (check_n == 0) intersection() { tray(); lid(); }
    if (check_n == 1) intersection() { union() { tray(); lid(); } union() { electronics(); button(); usb_plug(); } }
} else {
    color("gainsboro") tray();
    electronics();
    usb_plug();
    translate([0, 0, explode]) { if (show_lid) color("orange", 0.85) lid(); button(); }
}
