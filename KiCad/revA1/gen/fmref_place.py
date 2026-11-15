#!/usr/bin/env python3
# Опорные ЦАП FM (керамика 1206) - вниз, к своей ножке: C13 -> U6 Mp, C14 -> U2A+, C22 -> U12 Mp, C23 -> U12 Rb.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" fmref_place.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_rest as R  # noqa: E402

R.TODO = {"C13": ("U6", True), "C14": ("U2", True), "C22": ("U12", True), "C23": ("U12", True)}
R.main()
