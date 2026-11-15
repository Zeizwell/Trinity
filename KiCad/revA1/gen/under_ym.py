#!/usr/bin/env python3
# Мелочь YM / FM (резисторы, керамика) - наверх, ВНУТРЬ панелек YM (панелька - одна рамка, без перемычек: между рядами
# ножек пусто, чип в панельке на 3-4 мм над платой, 0603 / 0805 / 1206 под ним встают). Низ под YM освобождается под
# дорожки. Отступ от ножек 2.5 мм, от торцов 4 мм. Разрешение в правилах - trinity.kicad_dru (courtyard U4 / U10).
#   "C:\Program Files\KiCad\10.0\bin\python.exe" under_ym.py <in.kicad_pcb> <out.kicad_pcb>
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_analog as P  # noqa: E402

YM = ("U4", "U10")
FAMILY = {"U4", "U10", "U2", "U7", "U6", "U12"}       # YM и их ЦАП FM / буферы
POWER = P.POWER | {"/3V3_GS"}


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    mm = pcbnew.ToMM
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    zones = {}
    for u in YM:
        xs = [mm(p.GetPosition().x) for p in fps[u].Pads()]
        ys = sorted({round(mm(p.GetPosition().y), 2) for p in fps[u].Pads()})
        zones[u] = (min(xs) + 4.0, ys[0] + 2.5, max(xs) - 4.0, ys[-1] - 2.5)
    pads_by_net = {}
    for f in fps.values():
        for p in f.Pads():
            pads_by_net.setdefault(p.GetNetname(), []).append((f.GetReference(), p))
    move = []
    for r, f in fps.items():                             # нижние R / C, связанные с YM / FM
        if not f.IsFlipped() or r[0] not in "RC":
            continue
        links = [(q.GetPosition(), o) for p in f.Pads() if p.GetNetname() not in POWER
                 for o, q in pads_by_net.get(p.GetNetname(), []) if o != r]
        if any(o in FAMILY for _, o in links) or any(fps[u].GetBoundingBox(False, False).Contains(f.GetPosition())
                                                     for u in YM):
            move.append((r, links))
    for r, f in fps.items():
        if r not in {m for m, _ in move}:
            P.block(f)
    for u in YM:                                         # панелька - рамка: сверху заняты только ножки, середина свободна
        body = P.rect(fps[u], P.GAP / 2)
        P.OCC[False] = [o for o in P.OCC[False] if o != body]
        for p in fps[u].Pads():
            q = p.GetBoundingBox()
            P.OCC[False].append((mm(q.GetLeft()) - 0.6, mm(q.GetTop()) - 0.6, mm(q.GetRight()) + 0.6,
                                 mm(q.GetBottom()) + 0.6))
    for r, links in move:
        f = fps[r]
        c = f.GetPosition()
        best = min(links, key=lambda l: math.hypot(l[0].x - c.x, l[0].y - c.y))[0] if links else c
        px, py = mm(best.x), mm(best.y)
        f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        order = sorted(YM, key=lambda u: abs((zones[u][1] + zones[u][3]) / 2 - py))
        if not any(P.put(f, zones[u], near=(px, py)) for u in order):
            f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
            P.block(f)
            print(r, "stays bottom (no room)")
        else:
            print(r, "-> top under", [u for u in order if zones[u][1] - 1 <= mm(f.GetPosition().y) <= zones[u][3] + 1])
    b.Save(sys.argv[2])
    print("saved")


if __name__ == "__main__":
    main()
