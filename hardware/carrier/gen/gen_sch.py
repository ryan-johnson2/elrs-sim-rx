#!/usr/bin/env python3
"""Generate elrs-sim-rx-carrier.kicad_sch from the KiCad stock symbol libraries.

Run inside the kicad/kicad:9.0 container (see tools/kicad.sh). This bootstraps the
first draft; once the schematic is edited by hand in KiCad, stop regenerating.

Coordinates are in schematic grid units (1 unit = 1.27 mm) so everything stays on grid.
"""
import copy
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sexpr import Q, parse, dump, find, first

SYMDIR = "/usr/share/kicad/symbols"
PROJECT = "elrs-sim-rx-carrier"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", PROJECT + ".kicad_sch")
U = 1.27
NS = uuid.UUID("6f1d3c52-0c1e-4d7e-9d3b-e15a51a0c0de")
_n = [0]


def uid(tag=""):
    _n[0] += 1
    return Q(str(uuid.uuid5(NS, "%s-%d" % (tag, _n[0]))))


ROOT = Q(str(uuid.uuid5(NS, "root")))


def mm(v):
    return ("%.4f" % (v * U)).rstrip("0").rstrip(".")


# ---------------------------------------------------------------- library symbols
_libs = {}
lib_symbols = {}


def load_symbol(lib_id):
    if lib_id in lib_symbols:
        return lib_symbols[lib_id]
    lib, name = lib_id.split(":")
    if lib not in _libs:
        _libs[lib] = parse(open("%s/%s.kicad_sym" % (SYMDIR, lib)).read())
    by_name = {s[1]: s for s in find(_libs[lib], "symbol")}
    sym = copy.deepcopy(by_name[name])
    ext = first(sym, "extends")
    if ext:  # flatten derived symbols: parent graphics, child properties
        parent = copy.deepcopy(by_name[ext[1]])
        props = find(sym, "property")
        body = [c for c in parent[2:] if not (isinstance(c, list) and c[0] == "property")]
        for c in body:
            if isinstance(c, list) and c[0] == "symbol":
                c[1] = Q(c[1].replace(ext[1], name, 1))
        split = next(i for i, c in enumerate(body) if isinstance(c, list) and c[0] == "symbol")
        sym = ["symbol", Q(name)] + body[:split] + props + body[split:]
    sym[1] = Q(lib_id)
    lib_symbols[lib_id] = sym
    return sym


def project_symbol(name, base_lib_id, datasheet, description, passive_pins=()):
    """Derive a project-library symbol (carrier:<name>) from a stock one."""
    sym = copy.deepcopy(load_symbol(base_lib_id))
    base = base_lib_id.split(":")[1]
    del lib_symbols[base_lib_id]
    for c in sym[2:]:
        if c[0] == "symbol":
            c[1] = Q(c[1].replace(base, name, 1))
            for p in find(c, "pin"):
                if first(p, "name")[1] in passive_pins:
                    p[1] = "passive"
        elif c[0] == "property" and c[1] == "Value":
            c[2] = Q(name)
        elif c[0] == "property" and c[1] == "Datasheet":
            c[2] = Q(datasheet)
        elif c[0] == "property" and c[1] == "Description":
            c[2] = Q(description)
    sym[1] = Q("carrier:" + name)
    lib_symbols["carrier:" + name] = sym
    lib = copy.deepcopy(sym)
    lib[1] = Q(name)
    out = os.path.join(os.path.dirname(OUT), "carrier.kicad_sym")
    with open(out, "w") as f:
        f.write(dump(["kicad_symbol_lib", ["version", "20241209"], ["generator", Q("kicad_symbol_editor")],
                      ["generator_version", Q("9.0")], lib]) + "\n")
    with open(os.path.join(os.path.dirname(OUT), "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n\t(version 7)\n\t(lib (name "carrier")(type "KiCad")'
                '(uri "${KIPRJMOD}/carrier.kicad_sym")(options "")(descr "ELRS Sim RX carrier parts"))\n)\n')


def pin_table(sym):
    pins = {}
    for unit in find(sym, "symbol"):
        for p in find(unit, "pin"):
            at = first(p, "at")
            pins[str(first(p, "number")[1])] = (float(at[1]) / U, float(at[2]) / U)
    return pins


# ---------------------------------------------------------------- sheet items
items = []
placed = {}
pwr_count = [0]


def rot(dx, dy, angle):
    for _ in range((angle // 90) % 4):  # KiCad rotates counter-clockwise on screen
        dx, dy = dy, -dx
    return dx, dy


def effects(justify=None, hide=False, size=1.27):
    e = ["effects", ["font", ["size", str(size), str(size)]]]
    if justify:
        e.append(["justify"] + justify.split())
    if hide:
        e.append(["hide", "yes"])
    return e


def place(lib_id, ref, value, x, y, angle=0, footprint="", ref_at=None, val_at=None,
          fields=None, bom=True, board=True, just="left"):
    sym = load_symbol(lib_id)
    pins = pin_table(sym)
    power = lib_id.startswith("power:")
    ref_at = ref_at or (2, -1)
    val_at = val_at or (2, 1)
    fa = "90" if (angle % 180 == 90 and not power) else "0"
    node = ["symbol", ["lib_id", Q(lib_id)], ["at", mm(x), mm(y), str(angle)], ["unit", "1"],
            ["exclude_from_sim", "no"], ["in_bom", "yes" if bom else "no"],
            ["on_board", "yes" if board else "no"], ["dnp", "no"], ["uuid", uid(ref)],
            ["property", Q("Reference"), Q(ref), ["at", mm(x + ref_at[0]), mm(y + ref_at[1]), fa],
             effects(just, hide=power)],
            ["property", Q("Value"), Q(value), ["at", mm(x + val_at[0]), mm(y + val_at[1]), fa],
             effects("" if power else just, hide=(value == "PWR_FLAG"))],
            ["property", Q("Footprint"), Q(footprint), ["at", mm(x), mm(y), "0"], effects(hide=True)],
            ["property", Q("Datasheet"), Q(""), ["at", mm(x), mm(y), "0"], effects(hide=True)],
            ["property", Q("Description"), Q(""), ["at", mm(x), mm(y), "0"], effects(hide=True)]]
    for k, v in (fields or {}).items():
        node.append(["property", Q(k), Q(v), ["at", mm(x), mm(y), "0"], effects(hide=True)])
    for num in pins:
        node.append(["pin", Q(num), ["uuid", uid(ref + num)]])
    node.append(["instances", ["project", Q(PROJECT),
                               ["path", Q("/" + ROOT), ["reference", Q(ref)], ["unit", "1"]]]])
    items.append(node)
    placed[ref] = (x, y, angle, pins)


def pin(ref, num):
    x, y, angle, pins = placed[ref]
    px, py = pins[str(num)]
    dx, dy = rot(px, -py, angle)
    return (x + dx, y + dy)


def wire(*pts):
    for a, b in zip(pts, pts[1:]):
        items.append(["wire", ["pts", ["xy", mm(a[0]), mm(a[1])], ["xy", mm(b[0]), mm(b[1])]],
                      ["stroke", ["width", "0"], ["type", "default"]], ["uuid", uid("w")]])


def junction(x, y):
    items.append(["junction", ["at", mm(x), mm(y)], ["diameter", "0"], ["color", "0", "0", "0", "0"],
                  ["uuid", uid("j")]])


def label(name, x, y, left=False):
    items.append(["label", Q(name), ["at", mm(x), mm(y), "180" if left else "0"],
                  effects("right bottom" if left else "left bottom"), ["uuid", uid("l")]])


def nc(x, y):
    items.append(["no_connect", ["at", mm(x), mm(y)], ["uuid", uid("nc")]])


def text(s, x, y, size=1.27, justify="left"):
    items.append(["text", Q(s.replace("\n", "\\n")), ["exclude_from_sim", "no"], ["at", mm(x), mm(y), "0"],
                  effects(justify, size=size), ["uuid", uid("t")]])


def power(name, x, y, angle=0):
    pwr_count[0] += 1
    off = (0, -3) if name != "GND" else (0, 3)
    off = rot(off[0], off[1], angle)
    place("power:" + name, "#PWR%02d" % pwr_count[0], name, x, y, angle,
          ref_at=(0, 0), val_at=off, bom=False, just="")


def flag(x, y, angle=0):
    pwr_count[0] += 1
    place("power:PWR_FLAG", "#FLG%02d" % pwr_count[0], "PWR_FLAG", x, y, angle, bom=False, just="")


R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
# Plain 1x9 rows of plated holes: the module's own pin headers are soldered straight in.
SOCKET = "Connector_PinHeader_2.54mm:PinHeader_1x09_P2.54mm_Vertical"
PADS4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"

# ================================================================ MCU sockets
text("RP2040-ZERO MODULE (soldered in by its own pin headers)", 14, 30, 1.6)
text("Two 1x9 rows of holes 15.24 mm apart, USB-C end at pin 1, plus five unconnected\n"
     "holes for the back-row pins (GP9 to GP13) so the module drops in unmodified.", 14, 33)

place("Connector:Conn_01x09_Socket", "J1", "RP2040-Zero left row", 40, 48, footprint=SOCKET,
      ref_at=(-2, -11), val_at=(-2, 11))
for i, name in enumerate(["5V", "GND", "3V3", "GP29", "GP28", "GP27", "GP26", "GP15", "GP14"]):
    text(name, 43, 40 + 2 * i)
p = pin("J1", 1)
wire(p, (30, p[1]), (24, p[1]))
power("+5V", 24, p[1])
flag(30, p[1])
p = pin("J1", 2)
wire(p, (30, p[1]), (24, p[1]))
power("GND", 24, p[1])
flag(30, p[1], 180)
for n in range(3, 10):
    nc(*pin("J1", n))

place("Connector:Conn_01x09_Socket", "J2", "RP2040-Zero right row", 40, 80, footprint=SOCKET,
      ref_at=(-2, -11), val_at=(-2, 11))
for i in range(9):
    text("GP%d" % i, 43, 72 + 2 * i)
for n, net in enumerate(["CRSF_TX", "CRSF_RX", "RX_EN", "BTN", "LED_EN"], start=1):
    p = pin("J2", n)
    wire(p, (32, p[1]))
    label(net, 32, p[1], left=True)
for n in range(6, 10):
    nc(*pin("J2", n))
place("Connector_Generic:Conn_01x05", "J5", "RP2040-Zero back row", 68, 80,
      footprint="Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical", ref_at=(-2, -7), val_at=(-2, 7))
for n in range(1, 6):
    nc(*pin("J5", n))
text("GP13 to GP9, all unused.\nHoles only, so the module's\nback-row pins have somewhere to go.", 71, 80)
text("GP0  UART0 TX, CRSF to receiver\nGP1  UART0 RX, CRSF from receiver, 420000 baud\n"
     "GP2  receiver power enable, high = on\nGP3  button, active low, internal pull-up\n"
     "GP4  button LED ring, high = on", 14, 100)

# ================================================================ load switch
text("RECEIVER POWER SWITCH", 88, 30, 1.6)
text("TPS22918: 5.5 V, 2 A, 52 mOhm. R1 holds the receiver off until firmware\n"
     "drives GP2. C2 sets the output rise time (roughly 1 to 2 ms at 5 V) to limit\n"
     "inrush into C3. QOD tied to VOUT discharges the rail fast on power-off,\n"
     "which the power-cycle bind sequence relies on.", 88, 35)

# Derived from the stock TPS22917DBV symbol (same SOT-23-6 pinout). QOD is made passive
# because TI intends it to be tied to VOUT, which ERC rejects for an open-collector pin.
project_symbol("TPS22918DBV", "Power_Management:TPS22917DBV",
               "https://www.ti.com/lit/ds/symlink/tps22918.pdf",
               "5.5 V, 2 A, 52 mOhm load switch, adjustable rise time and quick output discharge, SOT-23-6",
               passive_pins=("QOD",))
place("carrier:TPS22918DBV", "U1", "TPS22918DBVR", 122, 50,
      footprint="Package_TO_SOT_SMD:SOT-23-6", ref_at=(-6, -6), val_at=(1, -6),
      fields={"LCSC": "C131941", "MPN": "TPS22918DBVR"})
vin, ct, on = pin("U1", 1), pin("U1", 4), pin("U1", 3)
vout, qod, gnd = pin("U1", 6), pin("U1", 5), pin("U1", 2)

place("Device:C", "C1", "1uF", 97, vin[1] + 4, footprint=C0603, fields={"LCSC": "C15849"})
wire(vin, (97, vin[1]), (92, vin[1]))
wire((97, vin[1]), pin("C1", 1))
junction(97, vin[1])
power("+5V", 92, vin[1])
power("GND", *pin("C1", 2))

place("Device:C", "C2", "1nF", 105, ct[1] + 4, footprint=C0603, fields={"LCSC": "C1588"})
wire(ct, (105, ct[1]), pin("C2", 1))
power("GND", *pin("C2", 2))

place("Device:R", "R1", "100k", 97, on[1] + 12, footprint=R0603, fields={"LCSC": "C25803"})
wire(on, (112, on[1]), (112, on[1] + 8), (97, on[1] + 8), (90, on[1] + 8))
wire((97, on[1] + 8), pin("R1", 1))
junction(97, on[1] + 8)
label("RX_EN", 90, on[1] + 8, left=True)
power("GND", *pin("R1", 2))

wire(gnd, (gnd[0], gnd[1] + 2))
power("GND", gnd[0], gnd[1] + 2)

place("Device:C", "C3", "10uF", 140, vout[1] + 4, footprint=C0805, fields={"LCSC": "C15850"})
place("Device:C", "C4", "100nF", 149, vout[1] + 4, footprint=C0603, fields={"LCSC": "C14663"})
wire(vout, (134, vout[1]), (140, vout[1]), (149, vout[1]), (156, vout[1]))
wire(qod, (134, qod[1]), (134, vout[1]))
junction(134, vout[1])
for c in ("C3", "C4"):
    wire((pin(c, 1)[0], vout[1]), pin(c, 1))
    junction(pin(c, 1)[0], vout[1])
    power("GND", *pin(c, 2))
label("RX_5V", 156, vout[1])

# ================================================================ receiver pads
text("ELRS RECEIVER (castellated edge soldered to pads)", 168, 30, 1.6)
text("Pad names are the ones printed on the receiver.\n"
     "Order G / 5V / T / R is unverified against the EP2:\ncheck before soldering.", 168, 35)
place("Connector_Generic:Conn_01x04", "J3", "ELRS RX (EP2)", 196, 50,
      footprint="carrier:ELRS_RX_4pad_castellated", bom=False,
      ref_at=(-2, -5), val_at=(-2, 6))
for i, name in enumerate(["GND", "5V", "TX", "RX"]):
    text(name, 199, 48 + 2 * i)
p = pin("J3", 1)
wire(p, (176, p[1]))
power("GND", 176, p[1])
for n, net in ((2, "RX_5V"), (3, "CRSF_RX"), (4, "CRSF_TX")):
    p = pin("J3", n)
    wire(p, (188, p[1]))
    label(net, 188, p[1], left=True)

# ================================================================ LED driver
text("BUTTON LED RING DRIVER", 88, 78, 1.6)
text("Low-side switch. The FILN ring has its own series resistor\nand runs from 5 V.", 88, 81)
place("Transistor_FET:2N7002", "Q1", "2N7002", 120, 94, footprint="Package_TO_SOT_SMD:SOT-23",
      ref_at=(5, -1), val_at=(5, 1), fields={"LCSC": "C8545"})
g, d, s = pin("Q1", 1), pin("Q1", 3), pin("Q1", 2)
place("Device:R", "R2", "100R", 104, g[1], 90, footprint=R0603, ref_at=(0, -3.5), val_at=(0, -2),
      fields={"LCSC": "C22775"}, just="")
place("Device:R", "R3", "100k", 112, g[1] + 4, footprint=R0603, fields={"LCSC": "C25803"})
ra, rb = sorted([pin("R2", 1), pin("R2", 2)])
wire((94, g[1]), ra)
label("LED_EN", 94, g[1], left=True)
wire(rb, (112, g[1]), g)
wire((112, g[1]), pin("R3", 1))
junction(112, g[1])
power("GND", *pin("R3", 2))
wire(s, (s[0], s[1] + 3))
power("GND", s[0], s[1] + 3)
wire(d, (d[0], d[1] - 3), (130, d[1] - 3))
label("LED_K", 130, d[1] - 3)

# ================================================================ button connector
text("FRONT-PANEL BUTTON (FILN 12 mm, four leads)", 168, 78, 1.6)
place("Connector_Generic:Conn_01x04", "J4", "Button + LED ring", 196, 94, footprint=PADS4,
      ref_at=(-2, -5), val_at=(-2, 6))
for i, name in enumerate(["LED +", "LED -", "switch", "switch (GND)"]):
    text(name, 199, 92 + 2 * i)
p = pin("J4", 1)
wire(p, (176, p[1]))
power("+5V", 176, p[1])
for n, net in ((2, "LED_K"), (3, "BTN")):
    p = pin("J4", n)
    wire(p, (188, p[1]))
    label(net, 188, p[1], left=True)
p = pin("J4", 4)
wire(p, (176, p[1]))
power("GND", 176, p[1])

# ================================================================ mechanical
place("Mechanical:MountingHole", "H1", "M2", 96, 116,
      footprint="MountingHole:MountingHole_2.2mm_M2", ref_at=(3, -1), val_at=(3, 1), bom=False)

text("DRAFT v0.2. Generated by hardware/carrier/gen/gen_sch.py from docs/HBOM.md Phase 2 and\n"
     "firmware/src/board.h. The module (J1, J2), receiver (J3) and button leads (J4) are hand-soldered;\n"
     "everything else is fab-assembled SMD, top side.",
     14, 122)

sheet = ["kicad_sch", ["version", "20250114"], ["generator", Q("eeschema")],
         ["generator_version", Q("9.0")], ["uuid", ROOT], ["paper", Q("A4")],
         ["title_block", ["title", Q("ELRS Sim RX carrier board")], ["date", Q("2026-11-13")],
          ["rev", Q("0.2 draft")], ["comment", "1", Q("RP2040-Zero + ELRS receiver, USB gamepad dongle")]],
         ["lib_symbols"] + list(lib_symbols.values())] + items + [
    ["sheet_instances", ["path", Q("/"), ["page", Q("1")]]], ["embedded_fonts", "no"]]

with open(OUT, "w") as f:
    f.write(dump(sheet) + "\n")
print("wrote", os.path.normpath(OUT))
