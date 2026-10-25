#!/usr/bin/env python3
# Узкое стерео: обвязка - к новому маленькому свитчу SW101, сверху (что не влезло - снизу):
# 10u (C1, C2, C19, C21) - к ножке свитча, 82k (R2, R4, R32, R33) - к своему 10u.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" nar_place.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_rest as R  # noqa: E402

CAPS = ("C1", "C2", "C19", "C21")
b = pcbnew.LoadBoard(sys.argv[1])
cap_of = {}
for c in CAPS:
    nets = {p.GetNetname() for p in b.FindFootprintByReference(c).Pads()}
    for r in ("R2", "R4", "R32", "R33"):
        if nets & {p.GetNetname() for p in b.FindFootprintByReference(r).Pads()} - R.POWER:
            cap_of[r] = c
R.TODO = {**{c: ("SW101", False) for c in CAPS}, **{r: (c, False) for r, c in cap_of.items()}}
R.main()
