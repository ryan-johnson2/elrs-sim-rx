#!/usr/bin/env python3
"""Schematic helpers for the integrated board generator (copied from the carrier generator).

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
PROJECT = "elrs-sim-rx-board"
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
    sym[1] = Q("board:" + name)
    lib_symbols["board:" + name] = sym
    lib = copy.deepcopy(sym)
    lib[1] = Q(name)
    out = os.path.join(os.path.dirname(OUT), "board.kicad_sym")
    with open(out, "w") as f:
        f.write(dump(["kicad_symbol_lib", ["version", "20241209"], ["generator", Q("kicad_symbol_editor")],
                      ["generator_version", Q("9.0")], lib]) + "\n")
    with open(os.path.join(os.path.dirname(OUT), "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n\t(version 7)\n\t(lib (name "board")(type "KiCad")'
                '(uri "${KIPRJMOD}/board.kicad_sym")(options "")(descr "ELRS Sim RX board parts"))\n)\n')


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

