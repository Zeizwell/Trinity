#!/usr/bin/env python3
# Мелочь микшера (резисторы, керамика) - снизу наверх, кучкой у U3, как у MultiSound; что не влезло - остаётся снизу
# на своём месте. Остальное на плате не трогаем.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" mix_top.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys
from collections import deque

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_analog as P  # noqa: E402

ANCHORS = ("U3", "SW101", "U103")                   # микшер, DIP-4 узкого стерео, -5 В
STOP = {"U2", "U7", "U6", "U12", "U4", "U10", "U8"}  # дальше этих по графу не идём (FM, YM, ЦАП)


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    net_fps = {}
    for f in fps.values():
        for p in f.Pads():
            n = p.GetNetname()
            if n and n not in P.POWER and not n.startswith("unconnected"):
                net_fps.setdefault(n, set()).add(f.GetReference())
    adj = {r: set() for r in fps}
    for refs in net_fps.values():
        for r in refs:
            adj[r] |= refs - {r}
    dist, q = {r: 0 for r in ANCHORS}, deque(ANCHORS)          # детали микшера: по связям от микшера
    while q:
        r = q.popleft()
        for n in adj[r]:
            if n not in dist and n not in STOP and P.is_analog(n):
                dist[n] = dist[r] + 1
                q.append(n)
    move = [fps[r] for r in sorted(dist, key=lambda r: (dist[r], r))
            if fps[r].IsFlipped() and r[0] in "RC" and dist[r] <= 2]
    for f in fps.values():
        if f not in move:
            P.block(f)
    u = P.rect(fps["U3"])
    cx, cy = (u[0] + u[2]) / 2, (u[1] + u[3]) / 2
    win = (max(cx - 25, P.BOARD[0]), max(cy - 25, P.BOARD[1]), min(cx + 25, 117.0), P.BOARD[3])  # до YM
    up = []
    for f in move:
        pos, ang = f.GetPosition(), f.GetOrientationDegrees()
        f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        if P.put(f, win, near=(cx, cy)):
            up.append(f.GetReference())
            continue
        f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)  # не влезла - назад, как было
        f.SetPosition(pos)
        f.SetOrientationDegrees(ang)
        P.block(f)
    print("up:", up)
    print("stay bottom:", [f.GetReference() for f in move if f.GetReference() not in up])
    b.Save(sys.argv[2])
    print("saved")


if __name__ == "__main__":
    main()
