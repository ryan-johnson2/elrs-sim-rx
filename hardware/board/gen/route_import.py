#!/usr/bin/env python3
"""Import the Freerouting session into the placed board, fill the ground zones, save.

Run inside the kicad/kicad:9.0 container, after route.sh has produced out/<name>.ses.
"""
import os

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "elrs-sim-rx-board"
PCB = os.path.normpath(os.path.join(HERE, "..", NAME + ".kicad_pcb"))
SES = os.path.normpath(os.path.join(HERE, "..", "out", NAME + ".ses"))

board = pcbnew.LoadBoard(PCB)
if not pcbnew.ImportSpecctraSES(board, SES):
    raise SystemExit("could not import " + SES)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
pcbnew.SaveBoard(PCB, board)
tracks = list(board.GetTracks())
vias = sum(1 for t in tracks if t.Type() == pcbnew.PCB_VIA_T)
print("imported %d track segments and %d vias" % (len(tracks) - vias, vias))
