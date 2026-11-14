#!/usr/bin/env python3
"""Generate elrs-sim-rx-board.kicad_sch: RP2040 on the board, EP2 receiver soldered on.

Run inside the kicad/kicad:9.0 container (tools/kicad.sh hardware/board/gen/build.sh).

The sheet is drawn label-style: every pin carries a net label and there are no wires. That
keeps the generator a plain table of parts and nets, which is what gets reviewed; open the
project in KiCad to redraw it conventionally once the design settles.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schlib import *          # noqa: F401,F403  (place, pin, label, nc, text, flag, lib_symbols, ...)
from schlib import items, lib_symbols, placed, OUT, ROOT, Q, dump, load_symbol, first, find

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"

project_symbol("TPS22918DBV", "Power_Management:TPS22917DBV",
               "https://www.ti.com/lit/ds/symlink/tps22918.pdf",
               "5.5 V, 2 A, 52 mOhm load switch, adjustable rise time and quick output discharge, SOT-23-6",
               passive_pins=("QOD",))


def names(lib_id):
    """pin name -> [pin numbers] for a library symbol."""
    out = {}
    for unit in find(load_symbol(lib_id), "symbol"):
        for p in find(unit, "pin"):
            out.setdefault(str(first(p, "name")[1]), []).append(str(first(p, "number")[1]))
    return out


def part(lib_id, ref, value, x, y, nets, footprint="", angle=0, by_name=False, fields=None, bom=True, **kw):
    """Place a symbol and put a net label (or a no-connect) on every pin.

    nets maps pin number (or pin name with by_name) to a net name; None = not connected.
    Every pin must be listed."""
    place(lib_id, ref, value, x, y, angle, footprint=footprint, fields=fields, bom=bom, **kw)
    pins = placed[ref][3]
    table = {}
    if by_name:
        nm = names(lib_id)
        for name, net in nets.items():
            if name not in nm:
                raise SystemExit("%s: no pin named %s (have %s)" % (ref, name, sorted(nm)))
            for num in nm[name]:
                table[num] = net
    else:
        table = {str(k): v for k, v in nets.items()}
    missing = sorted(set(pins) - set(table))
    if missing:
        raise SystemExit("%s: pins without a net: %s" % (ref, missing))
    for num, net in table.items():
        px, py = pin(ref, num)
        if net is None:
            nc(px, py)
        else:
            label(net, px, py, left=px < x)


# ================================================================ RP2040
text("ELRS Sim RX board: RP2040, load switch, LED driver, EP2 receiver pads", 14, 14, 2.0)
text("Label-style sheet: nets join by name. Same pin jobs as firmware/src/board.h:\n"
     "GP0 CRSF to receiver, GP1 CRSF from receiver, GP2 receiver power, GP3 button, GP4 button LED ring.\n"
     "GP25 drives the on-board status LED (the RP2040-Zero's RGB LED on GP16 is not fitted here).", 14, 18)

gp = {"GPIO%d" % n: None for n in range(30)}
gp.update({"GPIO26/ADC0": None, "GPIO27/ADC1": None, "GPIO28/ADC2": None, "GPIO29/ADC3": None})
for k in ("GPIO26", "GPIO27", "GPIO28", "GPIO29"):
    del gp[k]
gp.update({"GPIO0": "CRSF_TX", "GPIO1": "CRSF_RX", "GPIO2": "RX_EN", "GPIO3": "BTN", "GPIO4": "LED_EN",
           "GPIO25": "STATUS"})
part("MCU_RaspberryPi:RP2040", "U1", "RP2040", 70, 80, dict(gp, **{
    "IOVDD": "+3V3", "DVDD": "+1V1", "VREG_VIN": "+3V3", "VREG_VOUT": "+1V1", "USB_VDD": "+3V3",
    "ADC_AVDD": "+3V3", "GND": "GND", "TESTEN": "GND",
    "XIN": "XIN", "XOUT": "XOUT", "SWCLK": "SWCLK", "SWDIO": "SWDIO", "RUN": "RUN",
    "USB_DP": "USB_DP", "USB_DM": "USB_DM",
    "QSPI_SD0": "QSPI_SD0", "QSPI_SD1": "QSPI_SD1", "QSPI_SD2": "QSPI_SD2", "QSPI_SD3": "QSPI_SD3",
    "QSPI_SCLK": "QSPI_SCLK", "~{QSPI_SS}": "QSPI_SS"}),
     footprint="Package_DFN_QFN:QFN-56-1EP_7x7mm_P0.4mm_EP3.2x3.2mm", by_name=True,
     ref_at=(-10, -50), val_at=(4, -50), fields={"LCSC": "C2040", "MPN": "RP2040"})

# ================================================================ flash, crystal, boot
part("Memory_Flash:W25Q16JVSS", "U2", "W25Q16JVSSIQ", 130, 44, {
    "~{CS}": "QSPI_SS", "DO/IO_{1}": "QSPI_SD1", "~{WP}/IO_{2}": "QSPI_SD2", "GND": "GND",
    "DI/IO_{0}": "QSPI_SD0", "CLK": "QSPI_SCLK", "~{HOLD}/~{RESET}/IO_{3}": "QSPI_SD3", "VCC": "+3V3"},
     footprint="Package_SO:SOIC-8_5.3x5.3mm_P1.27mm", by_name=True, ref_at=(-8, -9), val_at=(2, -9),
     fields={"LCSC": "", "MPN": "W25Q16JVSSIQ"})
part("Device:Crystal_GND24", "Y1", "12MHz", 130, 70, {1: "XIN", 2: "GND", 3: "XOUT_X", 4: "GND"},
     footprint="Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", ref_at=(-4, -6), val_at=(2, -6), fields={"LCSC": ""})
part("Switch:SW_Push", "SW1", "BOOT", 130, 88, {1: "BOOT", 2: "GND"},
     footprint="Button_Switch_SMD:SW_SPST_B3U-1000P", ref_at=(-4, -4), val_at=(2, -4), fields={"LCSC": ""})
part("Device:LED", "D1", "green", 130, 100, {1: "GND", 2: "STATUS_A"},
     footprint="LED_SMD:LED_0603_1608Metric", ref_at=(-4, -4), val_at=(2, -4), fields={"LCSC": ""})

# ================================================================ USB-C and 3.3 V
part("Connector:USB_C_Receptacle_USB2.0_16P", "J1", "USB-C", 190, 60, {
    "GND": "GND", "VBUS": "+5V", "CC1": "CC1", "CC2": "CC2", "D+": "USB_CONN_DP", "D-": "USB_CONN_DM",
    "SBU1": None, "SBU2": None, "SHIELD": "GND"},
     footprint="Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", by_name=True,
     ref_at=(-8, -22), val_at=(2, -22), fields={"LCSC": "C165948", "MPN": "TYPE-C-31-M-12"})
part("Regulator_Linear:XC6206PxxxMR", "U3", "XC6206P332MR", 190, 100, {"GND": "GND", "VO": "+3V3", "VI": "+5V"},
     footprint="Package_TO_SOT_SMD:SOT-23", by_name=True, ref_at=(-8, -7), val_at=(2, -7),
     fields={"LCSC": "C5446", "MPN": "XC6206P332MR"})

# ================================================================ receiver power switch, LED driver
part("board:TPS22918DBV", "U4", "TPS22918DBVR", 240, 50, {
    1: "+5V", 2: "GND", 3: "RX_EN", 4: "RX_CT", 5: "RX_5V", 6: "RX_5V"},
     footprint="Package_TO_SOT_SMD:SOT-23-6", ref_at=(-6, -7), val_at=(2, -7),
     fields={"LCSC": "C131941", "MPN": "TPS22918DBVR"})
part("Transistor_FET:2N7002", "Q1", "2N7002", 240, 80, {1: "LED_GATE", 2: "GND", 3: "LED_K"},
     footprint="Package_TO_SOT_SMD:SOT-23", ref_at=(5, -1), val_at=(5, 1), fields={"LCSC": "C8545"})

# ================================================================ off-board connections
part("Connector_Generic:Conn_01x04", "J3", "ELRS RX (EP2)", 240, 110,
     {1: "GND", 2: "RX_5V", 3: "CRSF_RX", 4: "CRSF_TX"},
     footprint="board:ELRS_RX_4pad_castellated", bom=False, ref_at=(-2, -6), val_at=(-2, 6))
text("EP2 pads, pad edge toward the chip, antenna side up:\nGND, 5V, TX, RX from the left.", 250, 108)
part("Connector_Generic:Conn_01x04", "J4", "Button + LED ring", 240, 130,
     {1: "+5V", 2: "LED_K", 3: "BTN", 4: "GND"},
     footprint="Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical", bom=False,
     ref_at=(-2, -6), val_at=(-2, 6))
text("LED+, LED-, BTN, BTN. Leave the button off for\nthe no-button version; nothing else changes.", 250, 128)
for i, (ref, net) in enumerate((("TP1", "SWCLK"), ("TP2", "SWDIO"))):
    part("Connector:TestPoint", ref, net, 190, 124 + 8 * i, {1: net},
         footprint="TestPoint:TestPoint_Pad_D1.0mm", bom=False, ref_at=(3, -1), val_at=(3, 1))
place("Mechanical:MountingHole", "H1", "M2", 190, 142,
      footprint="MountingHole:MountingHole_2.2mm_M2", ref_at=(3, -1), val_at=(3, 1), bom=False)

# ================================================================ passives
# (ref, value, footprint, net on pin 1, net on pin 2, what it is for)
PASSIVES = [
    ("C1",  "1uF",   C0402, "+5V",   "GND", "regulator input"),
    ("C2",  "1uF",   C0402, "+3V3",  "GND", "regulator output"),
    ("C3",  "10uF",  C0603, "+3V3",  "GND", "3.3 V bulk"),
    ("C4",  "100nF", C0402, "+3V3",  "GND", "IOVDD pin 1"),
    ("C5",  "100nF", C0402, "+3V3",  "GND", "IOVDD pin 10"),
    ("C6",  "100nF", C0402, "+3V3",  "GND", "IOVDD pin 22"),
    ("C7",  "100nF", C0402, "+3V3",  "GND", "IOVDD pin 33"),
    ("C8",  "100nF", C0402, "+3V3",  "GND", "IOVDD pin 42"),
    ("C9",  "100nF", C0402, "+3V3",  "GND", "IOVDD pin 49"),
    ("C10", "100nF", C0402, "+3V3",  "GND", "USB_VDD"),
    ("C11", "100nF", C0402, "+3V3",  "GND", "ADC_AVDD"),
    ("C12", "1uF",   C0402, "+3V3",  "GND", "VREG_VIN"),
    ("C13", "1uF",   C0402, "+1V1",  "GND", "VREG_VOUT"),
    ("C14", "100nF", C0402, "+1V1",  "GND", "DVDD pin 23"),
    ("C15", "100nF", C0402, "+1V1",  "GND", "DVDD pin 50"),
    ("C16", "100nF", C0402, "+3V3",  "GND", "flash VCC"),
    ("C17", "15pF",  C0402, "XIN",   "GND", "crystal load, value to match the crystal"),
    ("C18", "15pF",  C0402, "XOUT_X", "GND", "crystal load, value to match the crystal"),
    ("C19", "1uF",   C0402, "+5V",   "GND", "load switch input"),
    ("C20", "1nF",   C0402, "RX_CT", "GND", "load switch rise time"),
    ("C21", "10uF",  C0805, "RX_5V", "GND", "receiver supply bulk"),
    ("C22", "100nF", C0402, "RX_5V", "GND", "receiver supply"),
    ("R1",  "27R",   R0402, "USB_DP", "USB_CONN_DP", "USB series"),
    ("R2",  "27R",   R0402, "USB_DM", "USB_CONN_DM", "USB series"),
    ("R3",  "5k1",   R0402, "CC1",   "GND", "USB-C device pull-down"),
    ("R4",  "5k1",   R0402, "CC2",   "GND", "USB-C device pull-down"),
    ("R5",  "1k",    R0402, "XOUT",  "XOUT_X", "crystal drive limit"),
    ("R6",  "10k",   R0402, "+3V3",  "RUN", "RUN pull-up"),
    ("R7",  "1k",    R0402, "QSPI_SS", "BOOT", "BOOT button to flash select"),
    ("R8",  "1k",    R0402, "STATUS", "STATUS_A", "status LED"),
    ("R9",  "100k",  R0402, "RX_EN", "GND", "receiver off until firmware runs"),
    ("R10", "100R",  R0402, "LED_EN", "LED_GATE", "LED driver gate"),
    ("R11", "100k",  R0402, "LED_GATE", "GND", "LED off until firmware runs"),
    ("R12", "10k",   R0402, "+3V3",  "QSPI_SS", "flash select pull-up"),
]
text("PASSIVES", 14, 150, 1.6)
for i, (ref, value, fp, n1, n2, why) in enumerate(PASSIVES):
    col, row = i % 12, i // 12
    x, y = 20 + 22 * col, 160 + 16 * row
    part("Device:C" if ref[0] == "C" else "Device:R", ref, value, x, y, {1: n1, 2: n2}, footprint=fp,
         ref_at=(2, -1), val_at=(2, 1), fields={"LCSC": "", "Use": why})

# power flags so ERC knows these nets are driven from outside the sheet
for i, net in enumerate(("+5V", "GND")):
    flag(260 + 10 * i, 150)
    label(net, 260 + 10 * i, 150)

text("DRAFT v0.1. Generated by hardware/board/gen/gen_sch.py. Everything is fab-assembled, top side,\n"
     "except the receiver (J3) and the button leads (J4). LCSC part numbers are mostly still to choose.", 14, 210)

sheet = ["kicad_sch", ["version", "20250114"], ["generator", Q("eeschema")],
         ["generator_version", Q("9.0")], ["uuid", ROOT], ["paper", Q("A3")],
         ["title_block", ["title", Q("ELRS Sim RX board")], ["date", Q("2026-11-14")],
          ["rev", Q("0.1 draft")], ["comment", "1", Q("RP2040 + ELRS receiver, USB gamepad dongle")]],
         ["lib_symbols"] + list(lib_symbols.values())] + items + [
    ["sheet_instances", ["path", Q("/"), ["page", Q("1")]]], ["embedded_fonts", "no"]]

with open(OUT, "w") as f:
    f.write(dump(sheet) + "\n")
print("wrote", os.path.normpath(OUT), "with", len(placed), "symbols")
