// ELRS Sim RX: case for the carrier board v0.2 (hardware/carrier), button version.
//
// The button sits in a bay next to the board instead of above it, so the case is only as
// tall as the button is deep. By default the bay is in line with the board, past the receiver
// end, which keeps the case narrow; set inline = false for a bay beside the button pads.
// Two printed parts:
//   tray   the board drops in onto ledges and a pin through its M2 hole. A bay on the
//          button-pad side holds the button body. Print floor down.
//   lid    carries the button, clicks into the tray, and holds the board down along its
//          two long edges. Print top face down.
//
// Coordinates are the board's own (hardware/carrier/gen/gen_pcb.py): x across the board from
// the 5V-pin edge, y along it from the USB end, z up from the tray's underside. The board file
// has y running down the page seen from the top, so with z up those axes are left-handed:
// everything is modelled in them and handed() mirrors the result into the real shape.

part = "assembly"; // [assembly, tray, lid, check]
inline = true;       // button bay past the receiver end (true) or beside the board (false)
explode = 0;
show_lid = true;
check_n = 0;

/* [Carrier board v0.2, from gen_pcb.py] */
board_w = 22;
board_l = 39.1;
board_t = 1.6;
cut_x = 12.3;        // the corner x < cut_x, y > cut_y is cut away for the receiver
cut_y = 28.82;
hole = [14.36, 37.0]; // M2 mounting hole
row_x = [3.38, 18.62]; // module pin rows
pin1_y = 2.5;
mod_y0 = 0.91;       // module outline along y
mod_l = 23.5;
rx_edge = 27.32;     // receiver's pad edge
rx_x = [1.8, 11.8];
rx_l = 10;
pads_y = [27.4, 35.02]; // button pads at x = row_x[1]

/* [Measured on the prototype] */
pins = 1.6;          // solder joints below the board
usb_zc = 5.5;        // centre of the USB-C socket above the board's top face
usb_over = 0.4;      // socket overhang past the board's USB edge
btn_d = 12.2;        // panel hole
btn_depth = 12;      // button body behind the panel
btn_nut = 15.6;      // across the corners

/* [Estimated] */
rx_up = 5;           // receiver above the board's top face (antenna)
rx_down = 1.2;       // receiver parts hanging below the board's top face

/* [Fit] */
fit = 0.25;          // board to tray
lid_fit = 0.2;
floor_t = 1.6;
wall = 2.0;
top_t = 2.0;
ledge = 1.2;
rib = 1.2;           // rib between the board and the button bay, as high as the board's top face
lip_t = 1.4;
lap = 4;
bump = 0.4;
usb_open = [13.5, 8];
bay = btn_nut + 1.0 + lip_t + lid_fit; // button bay depth: the nut, 0.5 either side, and the lid's lip on the outer wall

/* [Hidden] */
$fn = 64;
e = 0.01;
pocket = pins + 0.5;
T = floor_t + pocket + board_t;                 // z of the board's top face
usb_top = usb_zc + usb_open[1] / 2;
H = max(usb_top + 0.1, btn_depth + 0.6 + floor_t - T); // inside height above the board
Z = T + H;                                      // top of the tray wall
xa = -fit;
xr = board_w + fit;                             // board's right edge
yr = board_l + fit;                             // board's far end
xb = inline ? xr : xr + rib + bay;              // inside of the right wall
ya = min(-fit, -usb_over - 0.4);
yb = inline ? yr + rib + bay : yr;              // inside of the far wall
usb_x = (row_x[0] + row_x[1]) / 2;
bx = inline ? usb_x : xr + rib + 0.5 + btn_nut / 2;
by = inline ? yr + rib + 0.5 + btn_nut / 2 : 21;
snap_z = Z - lap / 2;
snap_l = 20;

echo(str("OUTER SIZE: ", xb - xa + 2 * wall, " x ", yb - ya + 2 * wall, " x ", Z + top_t));
echo(str("inside height above the board: ", H, ", button body ends ", Z - btn_depth - floor_t, " above the floor"));

module box(a, b) { translate(a) cube([b[0] - a[0], b[1] - a[1], b[2] - a[2]]); }
module rbox(a, b, r = 3) {
    translate([a[0], a[1], a[2]]) linear_extrude(b[2] - a[2])
        offset(r) offset(-r) square([b[0] - a[0], b[1] - a[1]]);
}
module yrod(x, z, r) { translate([x, (ya + yb) / 2 - snap_l / 2, z]) rotate([-90, 0, 0]) cylinder(r = r, h = snap_l); }
module usb_cut(z1) {
    box([usb_x - usb_open[0] / 2, ya - wall - e, T + usb_zc - usb_open[1] / 2],
        [usb_x + usb_open[0] / 2, ya + lid_fit + lip_t + e, z1]);
}

module tray() {
    difference() {
        union() {
            difference() {
                rbox([xa - wall, ya - wall, 0], [xb + wall, yb + wall, Z]);
                box([xa, ya, floor_t], [xb, yb, Z + e]);
            }
            // ledge under the board's left edge, as far as the cut-out
            box([xa, ya, floor_t - e], [ledge, cut_y - 0.3, T - board_t]);
            // ledge under its right edge
            box([board_w - ledge, ya, floor_t - e], [inline ? xr : xr + rib, yr, T - board_t]);
            // rib between the board and the button bay; the button leads pass over it
            if (inline) box([xa, yr, floor_t - e], [xr, yr + rib, T]);
            else box([xr, ya, floor_t - e], [xr + rib, yr, T]);
            // stops at the USB end, where the cavity runs past the board for the socket
            box([xa, ya, floor_t - e], [1.5, -fit, T]);
            box([board_w - 1.5, ya, floor_t - e], [inline ? xr : xr + rib, -fit, T]);
            // boss and pin for the M2 hole
            translate([hole[0], hole[1], floor_t - e]) cylinder(d = 5, h = T - board_t - floor_t + e);
            translate([hole[0], hole[1], floor_t - e]) cylinder(d = 1.9, h = T + 0.8 - floor_t);
            // shelf under the receiver's free end, for a foam pad
            box([xa, rx_edge + 4, floor_t - e], [cut_x - 0.5, yr, T - rx_down - 1.0]);
        }
        // USB-C plug: a slot open to the top, closed by the lid
        usb_cut(Z + e);
        // click grooves
        yrod(xa, snap_z, bump + 0.1);
        yrod(xb, snap_z, bump + 0.1);
    }
}

module lid() {
    difference() {
        union() {
            rbox([xa - wall, ya - wall, Z], [xb + wall, yb + wall, Z + top_t]);
            difference() {
                box([xa + lid_fit, ya + lid_fit, Z - lap], [xb - lid_fit, yb - lid_fit, Z + e]);
                box([xa + lid_fit + lip_t, ya + lid_fit + lip_t, Z - lap - e],
                    [xb - lid_fit - lip_t, yb - lid_fit - lip_t, Z + 2 * e]);
            }
            intersection() {
                union() { yrod(xa + lid_fit, snap_z, bump); yrod(xb - lid_fit, snap_z, bump); }
                box([xa - bump, ya, Z - lap], [xb + bump, yb, Z]);
            }
            // tabs that hold the board down along both long edges, clear of the module: under
            // the lip where a wall runs along that edge, on posts where the button bay does
            for (y = [[3, 9], [17, 23]]) {
                box([xa + lid_fit, y[0], T + 0.2], [xa + lid_fit + lip_t, y[1], Z - lap + e]);
                if (inline) box([xr - lid_fit - lip_t, y[0], T + 0.2], [xr - lid_fit, y[1], Z - lap + e]);
                else box([board_w - 1.7, y[0], T + 0.2], [xr + rib - 0.2, y[1], Z + e]);
            }
        }
        translate([bx, by, Z - e]) cylinder(d = btn_d, h = top_t + 2 * e);
        usb_cut(Z + e);       // the lip must not block the plug
    }
}

// Stand-ins for the electronics, for the pictures and the interference check
module electronics() {
    color("seagreen") difference() {
        union() { box([0, 0, T - board_t], [board_w, cut_y, T]); box([cut_x, cut_y - e, T - board_t], [board_w, board_l, T]); }
        translate([hole[0], hole[1], T - board_t - 1]) cylinder(d = 2.2, h = board_t + 2);
    }
    // module: header spacers, board, parts on top, USB-C socket
    color("gold") for (x = row_x) box([x - 1.27, pin1_y - 1.3, T], [x + 1.27, pin1_y + 8 * 2.54 + 1.3, T + 2.5]);
    color("navy") box([2, mod_y0, T + 2.5], [20, mod_y0 + mod_l, T + 3.5]);
    color("dimgray") box([4, 8, T + 3.5], [18, 22, T + 5.5]);
    color("silver") box([usb_x - 4.5, -usb_over, T + usb_zc - 1.6], [usb_x + 4.5, 6.9, T + usb_zc + 1.6]);
    // solder joints below
    color("silver") for (x = row_x) box([x - 0.9, pin1_y - 0.9, T - board_t - pins], [x + 0.9, pin1_y + 8 * 2.54 + 0.9, T - board_t]);
    color("silver") box([row_x[1] - 0.9, pads_y[0] - 0.9, T - board_t - pins], [row_x[1] + 0.9, pads_y[1] + 0.9, T - board_t]);
    // receiver, hanging over the cut-out
    color("purple") box([rx_x[0], rx_edge, T - rx_down], [rx_x[1], rx_edge + rx_l, T + rx_up]);
}
module button() {
    color("gray") translate([bx, by, Z - btn_depth]) cylinder(d = 12, h = btn_depth + top_t + 2);
    color("gray") translate([bx, by, Z + top_t]) cylinder(d = 14, h = 2);
    color("gray") translate([bx, by, Z - 2.5]) cylinder(d = btn_nut, h = 2.5, $fn = 6);
}
module usb_plug() {
    color("white") box([usb_x - 6.25, -24, T + usb_zc - 3.25], [usb_x + 6.25, -usb_over, T + usb_zc + 3.25]);
}

// Board coordinates to the real, right-handed shape: mirrored across the board's centre line.
module handed() { translate([board_w, 0, 0]) mirror([1, 0, 0]) children(); }

handed() {
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
}
