#!/usr/bin/env python3
"""Place the integrated board from the schematic netlist and write an unrouted .kicad_pcb.

Run inside the kicad/kicad:9.0 container (see build.sh). Tracks are added afterwards by
route.sh (Freerouting) and route_import.py; this script only places parts, draws the outline
and zones, and exports the Specctra DSN the autorouter reads.

Board coordinates are millimetres from the top-left corner, USB end up. The outline, the
receiver pads, the button pads and the mounting hole are in the same places as on the carrier
board v0.2, so the same case fits apart from the USB opening height.
"""
import os
import sys

import pcbnew
from pcbnew import VECTOR2I, FromMM

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import parse, find, first

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "elrs-sim-rx-board"
OUT = os.path.normpath(os.path.join(HERE, "..", NAME + ".kicad_pcb"))
FPDIR = "/usr/share/kicad/footprints"
OX, OY = 100.0, 60.0
W, H, RADIUS = 22.0, 39.1, 1.5
F, B = pcbnew.F_Cu, pcbnew.B_Cu


def pt(x, y):
    return VECTOR2I(FromMM(OX + x), FromMM(OY + y))


# ---------------------------------------------------------------- receiver landing (as carrier v0.2)
RX_W, RX_L = 10.0, 10.0
RX_PITCH = 2.54                     # checked against a photo of a real EP2 on a steel rule, see the carrier
RX_PAD_W = 1.5
RX_C = 6.8
RX_E = 27.32
RX_PAD_OUT, RX_PAD_IN = 1.5, 0.8
RX_LAND = 1.5
CUT_X = RX_C + RX_W / 2 + 0.5
CUT_Y = RX_E + RX_LAND

RX_FP = "ELRS_RX_4pad_castellated"
PRETTY = os.path.normpath(os.path.join(HERE, "..", "board.pretty"))
os.makedirs(PRETTY, exist_ok=True)
_st = '(stroke (width %s) (type solid))'
_hw = RX_W / 2
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
        + "".join(
            '\t(pad "%d" smd roundrect (at %.3f %.3f) (size %.2f %.2f) (layers "F.Cu" "F.Mask") (roundrect_rratio 0.15))\n'
            % (i + 1, (i - 1.5) * RX_PITCH, (RX_PAD_IN - RX_PAD_OUT) / 2, RX_PAD_W, RX_PAD_IN + RX_PAD_OUT)
            for i in range(4))
        + ')\n')
with open(os.path.join(os.path.dirname(PRETTY), "fp-lib-table"), "w") as f:
    f.write('(fp_lib_table\n\t(version 7)\n\t(lib (name "board")(type "KiCad")'
            '(uri "${KIPRJMOD}/board.pretty")(options "")(descr "ELRS Sim RX board parts"))\n)\n')

# ---------------------------------------------------------------- placement
# ref: (x, y, rotation). Rotation is counter-clockwise, KiCad convention.
CX, CY = 11.2, 16.1                 # RP2040 centre
PLACE = {
    "J1": (11.0, 3.15, 180),        # USB-C, mouth 0.5 mm past the top edge
    "U1": (CX, CY, 0),              # pin 1 top left: QSPI and USB on the top side, GP0..GP7 down the left
    "U2": (3.0, 13.95, 270),         # flash, pins 1..4 along its top edge, pin 1 nearest the RP2040
    "U3": (2.6, 2.6, 0),            # 3.3 V regulator, top left corner
    "C1": (5.05, 2.2, 90), "C2": (1.1, 5.0, 0), "C3": (3.6, 5.3, 0),
    "R4": (3.8, 7.5, 90), "R3": (17.2, 6.4, 90),            # USB-C CC pull-downs
    "SW1": (19.6, 3.4, 90),         # BOOT, top right corner
    # A row between the USB socket and the RP2040, kept right of the QSPI pins (x 8.6..10.6) so
    # those can fan out to the flash. 2 mm of routing channel is left below it.
    "R12": (6.9, 9.45, 90), "R7": (7.9, 9.45, 90),
    "C15": (11.2, 9.45, 90), "C9": (12.2, 9.45, 90), "C10": (13.2, 9.45, 90),
    "R1": (14.2, 9.45, 90), "R2": (15.2, 9.45, 90),
    "C13": (16.2, 9.45, 90), "C12": (17.2, 9.45, 90), "C11": (18.2, 9.45, 90),
    # left and right sides of the RP2040
    "C4": (6.45, 12.4, 90), "C5": (6.45, 17.5, 90), "C16": (4.75, 19.85, 90),
    "C8": (16.2, 13.4, 90), "C7": (16.2, 17.0, 90),
    "R8": (17.7, 15.2, 90), "D1": (19.6, 15.0, 90),
    "TP1": (17.9, 19.4, 0), "TP2": (20.0, 19.4, 0), "R6": (17.6, 21.6, 0),
    # below the RP2040: decoupling, crystal with its load capacitors above it
    "C6": (11.4, 21.2, 90), "C14": (12.4, 21.2, 90), "R5": (11.5, 23.3, 90),
    "Y1": (8.6, 23.45, 0), "C17": (7.7, 21.0, 0), "C18": (9.6, 21.0, 0),
    # receiver power switch, bottom left
    "U4": (3.4, 22.5, 0), "C19": (0.85, 21.9, 90), "R9": (0.85, 24.3, 90), "C20": (5.95, 23.3, 90),
    "C21": (2.2, 19.8, 0), "C22": (5.95, 21.3, 90),
    # LED ring driver, beside the button pads
    "Q1": (14.8, 24.3, 0), "R10": (18.3, 22.8, 0), "R11": (18.3, 24.0, 0),
    # same as the carrier
    "J3": (RX_C, RX_E, 0), "J4": (18.62, 27.4, 0), "H1": (14.36, 37.0, 0),
}

net_sexpr = parse(open(sys.argv[1]).read())
board = pcbnew.NewBoard(OUT)
tb = board.GetTitleBlock()
tb.SetTitle("ELRS Sim RX board")
tb.SetRevision("0.1 draft")
tb.SetDate("2026-11-14")

# Fine-pitch rules: the RP2040's pads are 0.2 mm wide with 0.2 mm between them.
ds = board.GetDesignSettings()
ds.m_TrackMinWidth = FromMM(0.15)
ds.m_MinClearance = FromMM(0.15)
ds.m_ViasMinSize = FromMM(0.5)
ds.m_MinThroughDrill = FromMM(0.25)
ds.m_CopperEdgeClearance = FromMM(0.3)
nc_default = ds.m_NetSettings.GetDefaultNetclass()
nc_default.SetClearance(FromMM(0.15))
nc_default.SetTrackWidth(FromMM(0.2))
nc_default.SetViaDiameter(FromMM(0.6))
nc_default.SetViaDrill(FromMM(0.3))

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
missing = []
for comp in find(first(net_sexpr, "components"), "comp"):
    ref = str(first(comp, "ref")[1])
    lib, fpname = str(first(comp, "footprint")[1]).split(":")
    fp = pcbnew.FootprintLoad(PRETTY if lib == "board" else "%s/%s.pretty" % (FPDIR, lib), fpname)
    fp.SetFPID(pcbnew.LIB_ID(lib, fpname))
    fp.SetReference(ref)
    fp.SetValue(str(first(comp, "value")[1]))
    fp.SetPath(pcbnew.KIID_PATH("/" + str(first(comp, "tstamps")[1])))
    if ref not in PLACE:
        missing.append(ref)
        continue
    x, y, angle = PLACE[ref]
    fp.SetPosition(pt(x, y))
    fp.SetOrientationDegrees(angle)
    for pad in fp.Pads():
        net = pad_net.get((ref, pad.GetNumber()))
        if net:
            pad.SetNet(net)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    board.Add(fp)
    fps[ref] = fp
if missing:
    raise SystemExit("no placement for: " + " ".join(sorted(missing)))


def P(ref, num):
    for pad in fps[ref].Pads():
        if pad.GetNumber() == str(num):
            p = pad.GetPosition()
            return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)
    raise KeyError((ref, num))


# ---------------------------------------------------------------- outline, zones
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
ri = 1.0
ki = ri * (1 - 0.7071068)
E = pcbnew.Edge_Cuts
line(E, (r, 0), (W - r, 0)); arc(E, (W - r, 0), (W - k, k), (W, r))
line(E, (W, r), (W, H - r)); arc(E, (W, H - r), (W - k, H - k), (W - r, H))
line(E, (W - r, H), (CUT_X + r, H)); arc(E, (CUT_X + r, H), (CUT_X + k, H - k), (CUT_X, H - r))
line(E, (CUT_X, H - r), (CUT_X, CUT_Y + ri)); arc(E, (CUT_X, CUT_Y + ri), (CUT_X - ki, CUT_Y + ki), (CUT_X - ri, CUT_Y))
line(E, (CUT_X - ri, CUT_Y), (r, CUT_Y)); arc(E, (r, CUT_Y), (k, CUT_Y - k), (0, CUT_Y - r))
line(E, (0, CUT_Y - r), (0, r)); arc(E, (0, r), (k, k), (r, 0))


def zone(layers, x0, y0, x1, y1, net):
    z = pcbnew.ZONE(board)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(FromMM(OX + x), FromMM(OY + y))
    z.SetNet(nets[net])
    z.SetLocalClearance(FromMM(0.2))
    z.SetMinThickness(FromMM(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(FromMM(0.25))
    z.SetThermalReliefSpokeWidth(FromMM(0.3))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(z)
    return z


zone([F], 0, 0, W, H, "GND").SetZoneName("GND top")
zone([B], 0, 0, W, H, "GND").SetZoneName("GND bottom")


# ---------------------------------------------------------------- silkscreen and documentation
def text(s, x, y, layer=pcbnew.F_SilkS, size=0.8, angle=0, thick=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size)))
    t.SetTextThickness(FromMM(thick or size * 0.15))
    t.SetTextAngleDegrees(angle)
    if layer in (pcbnew.B_SilkS, pcbnew.B_Fab):
        t.SetMirrored(True)
    board.Add(t)


BSILK, DOC = pcbnew.B_SilkS, pcbnew.Dwgs_User
for i, name in enumerate(["G", "5V", "T", "R"]):
    text(name, P("J3", i + 1)[0], RX_E + 0.6, BSILK, size=0.8)
for i, name in enumerate(["LED+", "LED-", "BTN", "BTN"]):
    py = P("J4", i + 1)[1]
    text(name, 15.0, py, size=0.8)
    text(name, 15.0, py, BSILK, size=0.8)
text("BOOT", 19.6, 6.7, size=0.8)
text("ELRS Sim RX", 11, 12.0, BSILK, size=1.3, thick=0.2)
text("board v0.1 draft", 11, 14.2, BSILK, size=0.9)
rect(DOC, RX_C - RX_W / 2, RX_E, RX_C + RX_W / 2, RX_E + RX_L, 0.1)
text("EP2, antenna side up", RX_C, RX_E + 4.0, DOC, size=0.7)

pcbnew.SaveBoard(OUT, board)
dsn = os.path.join(os.path.dirname(OUT), "out", NAME + ".dsn")
ok = pcbnew.ExportSpecctraDSN(board, dsn)
print("wrote", OUT, "with", len(fps), "footprints; DSN export", "ok" if ok else "FAILED")
