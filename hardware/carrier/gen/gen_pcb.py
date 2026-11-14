#!/usr/bin/env python3
"""Generate elrs-sim-rx-carrier.kicad_pcb from the schematic netlist.

Run inside the kicad/kicad:9.0 container (see tools/kicad.sh and build.sh). Placement and
routing are written out by hand below; this bootstraps the first draft. Once the board is
edited in KiCad, stop regenerating.

Board coordinates are millimetres from the top-left corner of the board, USB end up.
"""
import os
import sys

import pcbnew
from pcbnew import VECTOR2I, FromMM

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import parse, find, first

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "elrs-sim-rx-carrier"
OUT = os.path.normpath(os.path.join(HERE, "..", NAME + ".kicad_pcb"))
FPDIR = "/usr/share/kicad/footprints"
OX, OY = 100.0, 60.0          # board origin on the page
W, RADIUS = 22.0, 1.5

# Distance along the board from the last pin of the side rows (GP14 / GP8) to the 1x5 end row
# (GP9..GP13). Zero: the vendor photo of the DWEII board shows all seven pins across the end
# in one straight line on the 2.54 mm grid. Confirm with calipers before ordering.
ROW5_DY = 0.0

SIG, PWR = 0.25, 0.5          # track widths
VIA_D, VIA_DRILL = 0.6, 0.3
F, B = pcbnew.F_Cu, pcbnew.B_Cu


def pt(x, y):
    return VECTOR2I(FromMM(OX + x), FromMM(OY + y))


# ---------------------------------------------------------------- placement
# ref: (x, y, rotation). Rotation is counter-clockwise, KiCad convention.
ROW_L, ROW_R, PIN1_Y = 11 - 7.62, 11 + 7.62, 2.5
ROW5_Y = PIN1_Y + 8 * 2.54 + ROW5_DY
D = round(ROW5_Y + 3.6 - 26.5, 1)   # everything below the module shifts with the end row
K = ROW5_DY - 1.27                  # so do the parts just above it

# Receiver (Happymodel EP2): parts on both faces, so it cannot lie flat on the board. It is
# soldered antenna side up by its castellated edge and hangs over a cut-out corner of the
# board, pads toward the module. Pad order from the EP2 TCXO manual: seen from the antenna
# side with the pad edge toward you, RX TX +5V GND left to right, so with the pad edge toward
# the module they read GND 5V TX RX. The ceramic antenna ends up at the open outer corner.
# Pad pitch: the manual gives none. A photo of a real EP2 on a steel rule (2026-11-14) shows
# the outer pads 7.6 mm apart centre to centre (76 % of the 10 mm board width), so 2.54 mm,
# with the group centred on the edge. The same photos show nothing taller than a bare pad
# within 2.2 mm of the pad edge on the ESP side, so RX_LAND = 1.5 is clear.
RX_W, RX_L = 10.0, 10.0             # body width (along the pad edge) and length
RX_PITCH = 2.54                     # castellation pitch
RX_PAD_W = 1.5                      # carrier pad width
RX_C = 6.8                          # x of the receiver centre line
RX_E = round(ROW5_Y + 4.5, 2)       # y of the receiver's castellated edge
RX_PAD_OUT, RX_PAD_IN = 1.5, 0.8    # pad length outside / underneath the receiver edge
RX_LAND = 1.5                       # board left under the receiver's pad edge before the cut-out
CUT_X = RX_C + RX_W / 2 + 0.5       # cut-out: everything left of this and below CUT_Y is removed
CUT_Y = RX_E + RX_LAND
H = round(RX_E + RX_L + 1.8, 1)

RX_FP = "ELRS_RX_4pad_castellated"
PRETTY = os.path.normpath(os.path.join(HERE, "..", "carrier.pretty"))
os.makedirs(PRETTY, exist_ok=True)
_pads = "".join(
    '\t(pad "%d" smd roundrect (at %.3f %.3f) (size %.2f %.2f) (layers "F.Cu" "F.Mask") (roundrect_rratio 0.15))\n'
    % (i + 1, (i - 1.5) * RX_PITCH, (RX_PAD_IN - RX_PAD_OUT) / 2, RX_PAD_W, RX_PAD_IN + RX_PAD_OUT)
    for i in range(4))
_hw = RX_W / 2
_st = '(stroke (width %s) (type solid))'
with open(os.path.join(PRETTY, RX_FP + ".kicad_mod"), "w") as f:
    f.write(
        '(footprint "%s"\n\t(version 20241229)\n\t(generator "pcbnew")\n\t(generator_version "9.0")\n'
        '\t(layer "F.Cu")\n'
        '\t(descr "ELRS receiver soldered by its castellated edge over a board cut-out, pads 1..4 = GND 5V TX RX. '
        'Origin at the centre of the castellated edge, body towards +Y. Pad pitch not yet measured.")\n'
        '\t(attr smd exclude_from_pos_files exclude_from_bom)\n'
        '\t(property "Reference" "REF**" (at 0 -3 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))\n'
        '\t(property "Value" "%s" (at 0 %.2f 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))\n'
        % (RX_FP, RX_FP, RX_L / 2)
        + '\t(fp_rect (start %.2f 0) (end %.2f %.2f) %s (fill no) (layer "F.Fab"))\n' % (-_hw, _hw, RX_L, _st % "0.1")
        + '\t(fp_rect (start %.2f %.2f) (end %.2f %.2f) %s (fill no) (layer "F.CrtYd"))\n'
        % (-_hw - 0.1, -RX_PAD_OUT - 0.25, _hw + 0.1, RX_L + 0.25, _st % "0.05")
        + "".join('\t(fp_line (start %.2f %.2f) (end %.2f %.2f) %s (layer "F.Fab"))\n' % (a + b + (_st % "0.1",))
                  for a, b in (((-_hw - 0.1, 0), (-_hw - 0.1, RX_L + 0.1)), ((-_hw - 0.1, RX_L + 0.1), (_hw + 0.1, RX_L + 0.1)),
                               ((_hw + 0.1, RX_L + 0.1), (_hw + 0.1, 0))))
        + _pads + ')\n')
with open(os.path.join(os.path.dirname(PRETTY), "fp-lib-table"), "w") as f:
    f.write('(fp_lib_table\n\t(version 7)\n\t(lib (name "carrier")(type "KiCad")'
            '(uri "${KIPRJMOD}/carrier.pretty")(options "")(descr "ELRS Sim RX carrier parts"))\n)\n')
PLACE = {
    "J1": (ROW_L, PIN1_Y, 0),      # module left row, pin 1 = 5V at the USB end
    "J2": (ROW_R, PIN1_Y, 0),      # module right row, pin 1 = GP0
    "J5": (11 - 5.08, ROW5_Y, 90),  # module back row, GP13..GP9: unconnected holes so the pins need no clipping
    "J3": (RX_C, RX_E, 0),             # receiver pads, left to right: GND 5V TX RX
    "J4": (ROW_R, 27.5 + D, 0),        # button pads, top to bottom: LED+ LED- BTN BTN (the second BTN pad is ground)
    "U1": (9.0, 5.0, 0),
    "C1": (6.0, 4.2, 270),         # pad 1 (+5V) up
    "C2": (12.6, 5.95, 0),
    "R1": (6.8, 7.58, 180),        # pad 1 (RX_EN) right
    "C3": (7.4, 21.3 + K, 0),
    "C4": (7.3, 19.4 + K, 0),
    "R2": (15.0, 12.66, 180),      # pad 1 (LED_EN) right
    "R3": (11.0, 19.6 + K, 180),      # pad 1 (gate) right
    "Q1": (14.6, 20.55 + K, 0),
    "H1": (14.36, 37.0, 0),        # fits exactly between the receiver and J4 courtyards
}

net_sexpr = parse(open(sys.argv[1]).read())
board = pcbnew.NewBoard(OUT)
tb = board.GetTitleBlock()
tb.SetTitle("ELRS Sim RX carrier board")
tb.SetRevision("0.2 draft")
tb.SetDate("2026-11-13")

nets = {}
pad_net = {}
for n in find(first(net_sexpr, "nets"), "net"):
    name = str(first(n, "name")[1])
    item = pcbnew.NETINFO_ITEM(board, name)
    board.Add(item)
    nets[name.lstrip("/")] = item
    for node in find(n, "node"):
        pad_net[(str(first(node, "ref")[1]), str(first(node, "pin")[1]))] = item

fps = {}
for comp in find(first(net_sexpr, "components"), "comp"):
    ref = str(first(comp, "ref")[1])
    lib, fpname = str(first(comp, "footprint")[1]).split(":")
    fp = pcbnew.FootprintLoad(PRETTY if lib == "carrier" else "%s/%s.pretty" % (FPDIR, lib), fpname)
    fp.SetFPID(pcbnew.LIB_ID(lib, fpname))
    fp.SetReference(ref)
    fp.SetValue(str(first(comp, "value")[1]))
    fp.SetPath(pcbnew.KIID_PATH("/" + str(first(comp, "tstamps")[1])))
    x, y, angle = PLACE[ref]
    fp.SetPosition(pt(x, y))
    fp.SetOrientationDegrees(angle)
    for pad in fp.Pads():
        net = pad_net.get((ref, pad.GetNumber()))
        if net:
            pad.SetNet(net)
    fp.Reference().SetVisible(False)            # no room for designators; see F.Fab
    val = fp.Value()
    val.SetTextSize(VECTOR2I(FromMM(0.5), FromMM(0.5)))
    val.SetTextThickness(FromMM(0.08))
    val.SetVisible(ref[0] not in "JH")
    board.Add(fp)
    fps[ref] = fp

# The back-row holes sit in line with the last pins of the two side rows, as on the module,
# so the three stock courtyards (0.5 mm oversize) overlap. Redraw them flush where they meet.
def courtyard(ref, x0, y0, x1, y1):
    fp = fps[ref]
    for g in list(fp.GraphicalItems()):
        if g.GetLayer() == pcbnew.F_CrtYd:
            fp.Remove(g)
    s = pcbnew.PCB_SHAPE(fp)
    s.SetShape(pcbnew.SHAPE_T_RECT)
    s.SetStart(pt(x0, y0))
    s.SetEnd(pt(x1, y1))
    s.SetLayer(pcbnew.F_CrtYd)
    s.SetWidth(FromMM(0.05))
    fp.Add(s)


END_Y = PIN1_Y + 8 * 2.54
courtyard("J1", ROW_L - 1.8, PIN1_Y - 1.8, ROW_L + 1.26, END_Y + 1.8)
courtyard("J2", ROW_R - 1.26, PIN1_Y - 1.8, ROW_R + 1.8, END_Y + 1.8)
courtyard("J5", ROW_L + 1.28, ROW5_Y - 1.52, ROW_R - 1.28, ROW5_Y + 1.52)
for g in list(fps["J5"].GraphicalItems()):       # its silk box would overprint J1 and J2
    if g.GetLayer() == pcbnew.F_SilkS:
        fps["J5"].Remove(g)


def P(ref, num):
    for pad in fps[ref].Pads():
        if pad.GetNumber() == str(num):
            p = pad.GetPosition()
            return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)
    raise KeyError((ref, num))


def track(net, layer, width, *pts):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pt(*a))
        t.SetEnd(pt(*b))
        t.SetWidth(FromMM(width))
        t.SetLayer(layer)
        t.SetNet(nets[net])
        board.Add(t)


def via(net, x, y):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y))
    v.SetDrill(FromMM(VIA_DRILL))
    v.SetWidth(F, FromMM(VIA_D))
    v.SetLayerPair(F, B)
    v.SetNet(nets[net])
    board.Add(v)


# ---------------------------------------------------------------- routing
GATE = next(n for n in nets if "Q1" in n)       # unnamed gate node between R2, R3 and Q1

# The back-row holes are a wall of through-hole pads across the board. Tracks cross it
# through the 0.84 mm gaps between pads, one track per gap.
GAP = [11 - 5.08 + 1.27 + 2.54 * i for i in range(4)]      # x of gaps A..D
ABOVE, BELOW = ROW5_Y - 1.6, ROW5_Y + 1.5

# +5V: module pin -> C1 -> load switch input (top), and round the left edge and under the
# receiver to the button LED (bottom)
c1 = P("C1", 1)
ledp = P("J4", 1)
y5 = RX_E + 0.65                # on the strip of board left under the receiver's pad edge
track("+5V", F, PWR, P("J1", 1), (5.2, 2.5), c1, (7.0, c1[1]), P("U1", 1))
track("+5V", B, PWR, P("J1", 1), (1.25, 2.5), (1.25, y5 - 0.8), (2.05, y5), (16.4, y5),
      (16.4, ledp[1] + 0.9), (17.3, ledp[1]), ledp)

# RX_EN: GP2 along y = 7.58 to the load switch ON pin and its pull-down
on = P("U1", 3)
track("RX_EN", F, SIG, P("J2", 3), (on[0], 7.58), P("R1", 1))
track("RX_EN", F, SIG, (on[0], 7.58), on)

# CT capacitor
track("Net-(U1-CT)", F, SIG, P("U1", 4), P("C2", 1))

# The receiver pads are top-side SMD, so the three bottom lanes each come up through a via in
# the channel between the end-row socket and the receiver.
YT = BELOW                                      # via / top-track line just below the end row
pad_top = RX_E - RX_PAD_OUT

# RX_5V: VOUT + QOD -> via -> bottom lane -> gap A -> via -> receiver 5V pad. Bulk caps tap
# the lane through another via just above the end row.
vout = P("U1", 6)
rx5 = P("J3", 2)
c3, c4 = P("C3", 1), P("C4", 1)
track("RX_5V", F, PWR, P("U1", 5), vout, (11.3, vout[1]))
via("RX_5V", 11.3, vout[1])
track("RX_5V", B, PWR, (11.3, vout[1]), (11.3, 9.0), (GAP[0], 9.0 + 11.3 - GAP[0]), (GAP[0], ABOVE))
track("RX_5V", B, 0.3, (GAP[0], ABOVE), (GAP[0], YT))
via("RX_5V", GAP[0], YT)
track("RX_5V", F, PWR, (GAP[0], YT), (rx5[0], YT + abs(GAP[0] - rx5[0])), rx5)
tap = 17.8 + K
via("RX_5V", GAP[0], tap)
track("RX_5V", F, PWR, (GAP[0], tap), (c4[0], tap + GAP[0] - c4[0]), c4, c3)

# CRSF. GP1 comes through gap C, changes to the top layer and runs west to the receiver TX
# pad. GP0 comes through gap B, passes under that track and surfaces right above the RX pad.
rxp, txp = P("J3", 4), P("J3", 3)
track("CRSF_RX", B, SIG, P("J2", 2), (14.0, 5.04), (14.0, 12.0), (GAP[2], 12.0 + 14.0 - GAP[2]), (GAP[2], YT))
via("CRSF_RX", GAP[2], YT)
track("CRSF_RX", F, SIG, (GAP[2], YT), (txp[0] + 0.4, YT), (txp[0], YT + 0.4), txp)
tv = (rxp[0], YT + 0.95)
track("CRSF_TX", B, SIG, P("J2", 1), (12.6, 2.5), (12.6, 10.5), (GAP[1], 10.5 + 12.6 - GAP[1]),
      (GAP[1], tv[1] - abs(tv[0] - GAP[1])), tv)
via("CRSF_TX", *tv)
track("CRSF_TX", F, SIG, tv, rxp)

# BTN: GP3 down the right edge on the bottom layer
sw = P("J4", 3)
track("BTN", B, SIG, P("J2", 4), (20.1, 10.12), (20.6, 10.62), (20.6, sw[1] - 0.5), (20.1, sw[1]), sw)

# LED driver; the switched LED return goes through gap D on the top layer
r2a, r2b = P("R2", 1), P("R2", 2)
g, d = P("Q1", 1), P("Q1", 3)
track("LED_EN", F, SIG, P("J2", 5), r2a)
track(GATE, F, SIG, r2b, (g[0] + 0.9, r2b[1]), (g[0], r2b[1] + 0.9), g)
track(GATE, F, SIG, P("R3", 1), g)
ledk = P("J4", 2)
track("LED_K", F, SIG, d, (d[0], d[1] + 1.0), (GAP[3], d[1] + 1.0 + d[0] - GAP[3]), (GAP[3], BELOW),
      (16.3, BELOW + 16.3 - GAP[3]), (16.3, ledk[1] - 1.14), (17.44, ledk[1]), ledk)

# GND: explicit links where the pour might neck down, plus stitching vias
track("GND", F, SIG, P("U1", 2), (P("C1", 2)[0], P("U1", 2)[1]), P("C1", 2))
for vx, vy in ((5.1, 6.0), (6.5, 10.5), (16.2, 9.6), (11.0, 15.0), (16.6, 16.0), (8.5, 22.4 + K),
               (2.6, pad_top - 1.2), (18.6, H - 2.0)):
    via("GND", vx, vy)

# ---------------------------------------------------------------- outline, zones, keepout
def shape(layer, width=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetLayer(layer)
    s.SetWidth(FromMM(width))
    board.Add(s)
    return s


def line(layer, a, b, width=0.1):
    s = shape(layer, width)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pt(*a))
    s.SetEnd(pt(*b))


def rect(layer, x0, y0, x1, y1, width=0.1):
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        line(layer, a, b, width)


def arc(layer, start, mid, end, width=0.1):
    s = shape(layer, width)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetArcGeometry(pt(*start), pt(*mid), pt(*end))


r, k = RADIUS, RADIUS * (1 - 0.7071068)
E = pcbnew.Edge_Cuts
line(E, (r, 0), (W - r, 0)); arc(E, (W - r, 0), (W - k, k), (W, r))
line(E, (W, r), (W, H - r)); arc(E, (W, H - r), (W - k, H - k), (W - r, H))
# the lower left corner is cut away for the receiver, leaving an arm on the right
ri = 1.0
ki = ri * (1 - 0.7071068)
line(E, (W - r, H), (CUT_X + r, H)); arc(E, (CUT_X + r, H), (CUT_X + k, H - k), (CUT_X, H - r))
line(E, (CUT_X, H - r), (CUT_X, CUT_Y + ri)); arc(E, (CUT_X, CUT_Y + ri), (CUT_X - ki, CUT_Y + ki), (CUT_X - ri, CUT_Y))
line(E, (CUT_X - ri, CUT_Y), (r, CUT_Y)); arc(E, (r, CUT_Y), (k, CUT_Y - k), (0, CUT_Y - r))
line(E, (0, CUT_Y - r), (0, r)); arc(E, (0, r), (k, k), (r, 0))


def zone(layers, x0, y0, x1, y1, net=None):
    z = pcbnew.ZONE(board)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(FromMM(OX + x), FromMM(OY + y))
    if net:
        z.SetNet(nets[net])
        z.SetLocalClearance(FromMM(0.3))
        z.SetMinThickness(FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(FromMM(0.3))
        z.SetThermalReliefSpokeWidth(FromMM(0.4))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(z)
    return z


zone([F], 0, 0, W, H, "GND").SetZoneName("GND top")
zone([B], 0, 0, W, H, "GND").SetZoneName("GND bottom")

# Receiver body outline (10 x 10 mm class) and the antenna keepout under its far end.
RX0, RX1, RY0, RY1 = RX_C - RX_W / 2, RX_C + RX_W / 2, RX_E, RX_E + RX_L
# No antenna keepout zone is needed: the receiver hangs over the cut-out, so there is no board
# under its antenna end at all.

# The scripted zone filler does not apply the board's hole clearance to NPTH holes, so give
# the mounting hole (and its screw head) an explicit copper-free ring.
import math
hx, hy = PLACE["H1"][0], PLACE["H1"][1]
hk = pcbnew.ZONE(board)
ls = pcbnew.LSET()
ls.AddLayer(F)
ls.AddLayer(B)
hk.SetLayerSet(ls)
hk.Outline().NewOutline()
for i in range(32):
    a = 2 * math.pi * i / 32
    hk.Outline().Append(FromMM(OX + hx + 2.0 * math.cos(a)), FromMM(OY + hy + 2.0 * math.sin(a)))
hk.SetIsRuleArea(True)
hk.SetZoneName("M2 screw head")
hk.SetDoNotAllowCopperPour(True)
hk.SetDoNotAllowTracks(True)
hk.SetDoNotAllowVias(True)
hk.SetDoNotAllowPads(False)
hk.SetDoNotAllowFootprints(False)
board.Add(hk)

# ---------------------------------------------------------------- silkscreen and documentation
def text(s, x, y, layer=pcbnew.F_SilkS, size=0.8, angle=0, thick=None, just=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size)))
    t.SetTextThickness(FromMM(thick or size * 0.15))
    t.SetTextAngleDegrees(angle)
    if layer in (pcbnew.B_SilkS, pcbnew.B_Fab):
        t.SetMirrored(True)
    if just is not None:
        t.SetHorizJustify(just)
    board.Add(t)


SILK, BSILK, DOC = pcbnew.F_SilkS, pcbnew.B_SilkS, pcbnew.Dwgs_User
for i, name in enumerate(["G", "5V", "T", "R"]):
    px = P("J3", i + 1)[0]
    text(name, px, RX_E + 0.6, BSILK, size=0.8)
text("EP2", 14.6, 24.3, size=0.9)
for i, name in enumerate(["LED+", "LED-", "BTN", "BTN"]):   # by function, not lead colour
    py = P("J4", i + 1)[1]
    text(name, 15.0, py, size=0.8)
    text(name, 15.0, py, BSILK, size=0.8)
rect(DOC, RX0, RY0, RX1, RY1, 0.1)
text("EP2, antenna side up", RX_C, RX_E + 4.0, DOC, size=0.7)
text("antenna", RX0 + 2.6, RY1 - 1.2, DOC, size=0.7)
text("USB", 11, 1.2, size=1.0)
text("ELRS Sim RX", 11, 12.0, BSILK, size=1.3, thick=0.2)
text("carrier v0.2 draft", 11, 14.2, BSILK, size=0.9)
text("5V", ROW_L + 1.9, PIN1_Y + 1.3, BSILK, size=0.8)
text("GP0", ROW_R - 2.4, PIN1_Y + 1.3, BSILK, size=0.8)

# Module outline for reference: 18 x 23.5 mm, pin rows inset 1.59 mm from each end.
MY0 = PIN1_Y - (23.5 - 8 * 2.54) / 2
rect(DOC, 2.0, MY0, 20.0, MY0 + 23.5, 0.1)
rect(DOC, 11 - 4.5, MY0 - 1.3, 11 + 4.5, MY0 + 6.0, 0.1)
text("RP2040-Zero outline", 11, MY0 + 9.5, DOC, size=0.8)

pcbnew.ZONE_FILLER(board).Fill(board.Zones())
pcbnew.SaveBoard(OUT, board)
print("wrote", OUT)
