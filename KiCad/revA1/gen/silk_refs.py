#!/usr/bin/env python3
# Шелкография: позиционные обозначения (R12, C5...) - мелким шрифтом и на свободное место рядом с деталью: не на
# площадках, не друг на друге, не у края. Пробуем места вокруг детали (сверху, снизу, слева, справа, повёрнуто);
# мелочь - шрифт 0.8 мм (0.12 толщина), крупные (CPLD, YM, RP, разъёмы) не трогаем. Что не встало - оставляем как
# было и печатаем список.   "C:\Program Files\KiCad\10.0\bin\python.exe" silk_refs.py <in.kicad_pcb> <out.kicad_pcb>
import sys

import pcbnew

MM = pcbnew.FromMM
BIG = {"U4", "U10", "U5", "RP2350B-MINI-A1", "U8", "J4", "J5", "J6", "J2", "U6", "U12"}
EDGE = (58.6, 70.6, 228.6, 127.57)                       # плата (без ламелей) - отступ 0.3


def rect(b):
    return (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()), pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))


def hit(r, rs, gap=0.15):
    return any(not (r[2] + gap <= o[0] or o[2] + gap <= r[0] or r[3] + gap <= o[1] or o[3] + gap <= r[1]) for o in rs)


def seg_rects(g):
    """Прямоугольники препятствия от графики шелкографии: длинную линию режем на куски (её рамка - не вся площадь)."""
    if g.GetClass() == "PCB_SHAPE" and g.GetShape() == pcbnew.SHAPE_T_SEGMENT:
        s, e = g.GetStart(), g.GetEnd()
        x0, y0, x1, y1 = (pcbnew.ToMM(v) for v in (s.x, s.y, e.x, e.y))
        n = max(1, int(max(abs(x1 - x0), abs(y1 - y0)) / 0.5))
        w = pcbnew.ToMM(g.GetWidth()) / 2
        out = []
        for k in range(n):
            a, b2 = k / n, (k + 1) / n
            xa, ya, xb, yb = x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * b2, y0 + (y1 - y0) * b2
            out.append((min(xa, xb) - w, min(ya, yb) - w, max(xa, xb) + w, max(ya, yb) + w))
        return out
    return [rect(g.GetBoundingBox())]


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    fps = list(b.GetFootprints())
    pads = {False: [], True: []}                         # открытая медь (окна маски) по сторонам
    for f in fps:
        for p in f.Pads():
            r = rect(p.GetBoundingBox())
            for side, lay in ((False, pcbnew.F_Mask), (True, pcbnew.B_Mask)):
                if p.IsOnLayer(lay):
                    pads[side].append(r)
    texts = {False: [], True: []}
    todo = []
    for f in fps:
        t = f.Reference()
        if t.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            continue
        t.SetVisible(True)                               # прежде спрятанные - снова в работу
        if f.GetReference() in BIG:
            texts[t.GetLayer() == pcbnew.B_SilkS].append(rect(t.GetBoundingBox()))
        else:
            todo.append(f)
    for f in fps:                                        # прочие надписи шелкографии - тоже препятствия
        for g in f.GraphicalItems():
            if g.GetLayer() == pcbnew.Edge_Cuts:          # вырезы под модулями RP - на обеих сторонах
                r = rect(g.GetBoundingBox())
                texts[False].append(r)
                texts[True].append(r)
            if g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):    # надписи и контуры деталей
                for r in seg_rects(g):
                    texts[g.GetLayer() == pcbnew.B_SilkS].append(r)
    # детали внутри панелек YM паяются до панельки - их обозначения нужны (пользователь), ставим как остальные
    todo.sort(key=lambda f: -len(f.Pads()))
    left = []
    for f in todo:
        t = f.Reference()
        side = t.GetLayer() == pcbnew.B_SilkS
        t.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
        t.SetTextThickness(MM(0.12))
        fb = rect(f.GetBoundingBox(False, False))
        cx, cy = (fb[0] + fb[2]) / 2, (fb[1] + fb[3]) / 2
        w, h = fb[2] - fb[0], fb[3] - fb[1]
        cands = []
        for ang in (0, 90):
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0), (-1, -1), (1, -1), (-1, 1), (1, 1)):
                for extra in (0.6, 1.2, 2.0, 3.0, 4.0):
                    cands.append((ang, cx + dx * (w / 2 + extra), cy + dy * (h / 2 + extra)))
        old = (t.GetPosition(), t.GetTextAngleDegrees())
        placed = False
        for size, ang, x, y in [(0.8,) + c for c in cands]:
            t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
            t.SetTextThickness(MM(0.12 if size >= 0.8 else 0.1))
            t.SetTextAngleDegrees(ang)
            t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            r = rect(t.GetBoundingBox())
            if r[0] < EDGE[0] + 0.3 or r[2] > EDGE[2] - 0.3 or r[1] < EDGE[1] + 0.3 or r[3] > EDGE[3] - 0.3:
                continue
            if hit(r, pads[side]) or hit(r, texts[side]):
                continue
            texts[side].append(r)
            placed = True
            break
        if not placed:                                   # совсем некуда - прячем (деталь видно в списке / iBOM)
            t.SetPosition(old[0])
            t.SetTextAngleDegrees(old[1])
            t.SetVisible(False)
            left.append(f.GetReference())
    b.Save(sys.argv[2])
    print("placed", len(todo) - len(left), "of", len(todo), "; hidden (no room):", left)


if __name__ == "__main__":
    main()
