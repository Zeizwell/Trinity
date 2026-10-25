#!/usr/bin/env python3
# Развязка CPLD - снизу в середину под корпус QFP (между рядами площадок пусто), а не по краю: низ под краем CPLD и
# между ней и ламелями - под дорожки шины от контактов ряда b. Подтяжки JTAG - к RP-GS (GP44-47).
#   "C:\Program Files\KiCad\10.0\bin\python.exe" cpld_dec_center.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_analog as P  # noqa: E402
import place_rest as R  # noqa: E402

VCC = ("3", "18", "34", "39", "51", "66", "82", "91")
CAPS = [f"C{124 + k}" for k in range(9)]


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    mm = pcbnew.ToMM
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    u = fps["U1"]
    xs = [mm(p.GetPosition().x) for p in u.Pads()]
    ys = [mm(p.GetPosition().y) for p in u.Pads()]
    inner = (min(xs) + 1.6, min(ys) + 1.6, max(xs) - 1.6, max(ys) - 1.6)    # внутри кольца площадок
    print("inner", [round(v, 1) for v in inner])
    for r, f in fps.items():
        if r not in CAPS:
            P.block(f)
    for k, c in enumerate(CAPS):
        f = fps[c]
        if not f.IsFlipped():
            f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        pin = VCC[k] if k < 8 else "39"
        pad = next(p for p in u.Pads() if p.GetNumber() == pin)
        ok = P.put(f, inner, near=(mm(pad.GetPosition().x), mm(pad.GetPosition().y)))
        print(c, "under U1" if ok else "NO ROOM")
    b.Save(sys.argv[2])
    R.TODO = {"R226": ("U5", True, "GP44"), "R227": ("U5", True, "GP45"), "R228": ("U5", True, "GP46")}
    sys.argv[1] = sys.argv[2]
    R.main()


if __name__ == "__main__":
    main()
