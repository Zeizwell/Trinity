#!/usr/bin/env python3
# Хвосты расстановки (остальное на плате не трогаем): каждая деталь - к ножке своей микросхемы, с которой у неё общая
# сигнальная цепь, в ближайшее свободное место; сначала на нужной стороне, иначе на другой.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" place_rest.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_analog as P  # noqa: E402

# деталь: (к какой микросхеме, сторона сначала: True - низ)
TODO = {"C20": ("U2", True), "R22": ("U4", True), "R20": ("U3", True), "C116": ("U105", False),
        "R201": ("U5", False), "R202": ("U5", False), "R203": ("U5", False),
        "R204": ("U8", True), "R205": ("U8", True),
        "R206": ("J5", True), "R207": ("J5", True), "R208": ("J5", True), "R209": ("J5", True), "C201": ("J5", True),
        "R220": ("RP2350B-MINI-A1", False), "R221": ("RP2350B-MINI-A1", False), "R222": ("RP2350B-MINI-A1", False),
        "R223": ("RP2350B-MINI-A1", False), "R224": ("RP2350B-MINI-A1", False), "R225": ("RP2350B-MINI-A1", False)}
POWER = P.POWER | {"/3V3_GS"}


def target_pad(f, ic):
    """Ножка микросхемы с общей сигнальной цепью (иначе - с любой общей)."""
    nets = [p.GetNetname() for p in f.Pads()]
    for pick in ([n for n in nets if n not in POWER], nets):
        for p in ic.Pads():
            if p.GetNetname() in pick:
                return p
    raise SystemExit(f"no common net {f.GetReference()} - {ic.GetReference()}")


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    for r, f in fps.items():
        if r not in TODO:
            P.block(f)
    for r, v in TODO.items():
        u, bottom = v[0], v[1]
        f, ic = fps[r], fps[u]
        pad = next(p for p in ic.Pads() if p.GetNumber() == v[2]) if len(v) > 2 else target_pad(f, ic)
        px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
        ok = False
        only = bottom == "top"                             # "top" - только сверху (разъёмы, переключатели), ищем шире
        for side in ((False,) if only else (bottom, not bottom)):
            if f.IsFlipped() != side:
                f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
            for d in ((10, 20, 30, 45, 60) if only else (10, 20)):
                win = (max(px - d, P.BOARD[0]), max(py - d, P.BOARD[1]), min(px + d, P.BOARD[2]),
                       min(py + d, P.BOARD[3]))
                if P.put(f, win, near=(px, py)):
                    ok = True
                    break
            if ok:
                break
        print(r, "->", u, pad.GetNumber(), "bottom" if f.IsFlipped() else "top", "" if ok else "NO ROOM")
    b.Save(sys.argv[2])
    print("saved")


if __name__ == "__main__":
    main()
