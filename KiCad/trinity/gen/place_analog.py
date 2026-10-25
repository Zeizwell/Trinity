#!/usr/bin/env python3
# Черновая расстановка аналога на плате (пользователь двигает дальше руками). Не трогаем: YM, RP, ЦАП, SD, ламели,
# CPLD, J2, J6 и всю цифру. Аналог - между SD / ЦАП (слева) и YM (справа), x 76.5..117: детали собраны кучками
# по связям (через сигнальные цепи, без питания и земли) к своим микросхемам и уложены полками в зоны.
# Развязка - к своей ножке.   "C:\Program Files\KiCad\10.0\bin\python.exe" place_analog.py <in.kicad_pcb> <out.kicad_pcb>
import sys
from collections import deque

import pcbnew

MM = pcbnew.FromMM
POWER = {"GND", "+5V", "+5VA", "+5VA_1", "+5VA_2", "-5VA", "+12V"}
FIXED = {"U4", "U10", "RP2350B-MINI-A1", "U5", "U8", "J5", "J4", "U1", "J2", "J6", "U21"}
DIGITAL = {f"R{n}" for n in list(range(201, 210)) + list(range(220, 226))} | {"C201", "C202", "C114", "C115", "C116",
                                                                              "D1", "D2"}  # блок GS, подтяжки, 5 В
ANALOG_KEEP = {"R210", "R211"}                                        # 1к микшер -> гнездо: аналог
# зоны: (x0, y0, x1, y1), мм; сверху - микросхемы, катушки, электролиты; снизу (как MultiSound) - мелочь
ZONES = {
    "PWR": (76.5, 72.0, 100.0, 92.0),      # катушки и 47u
    "FM1": (100.0, 72.0, 114.8, 84.5),     # ЦАП FM верхнего YM (U4)
    "FM2": (100.0, 92.0, 114.8, 104.5),    # ЦАП FM нижнего YM (U10)
    "REG": (203.5, 107.0, 228.0, 127.2),   # 78M08 / 78M05 (не паяем) - справа от CPLD, у +12 В ламелей
    "MIX": (80.5, 92.0, 100.0, 127.3),     # микшер, DIP-4, -5 В, JP101 (левее - J2 у ЦАП)
}
ZONES_B = {                                # обратная сторона
    "PWR": [(76.5, 72.0, 100.0, 91.5)],
    "FM1": [(118.5, 76.6, 167.0, 86.6)],   # под панелькой U4, между рядами ножек
    "FM2": [(118.5, 96.9, 167.0, 107.1)],  # под панелькой U10
    "REG": [(203.5, 107.0, 228.0, 127.2)],
    "MIX": [(76.5, 92.5, 100.0, 127.3), (61.5, 93.5, 73.0, 117.5), (100.0, 72.0, 116.0, 116.0)],
}
GROUPS = {"FM1": ["U2"], "FM2": ["U7"], "MIX": ["U3", "SW101", "U103"], "PWR": ["L101", "JP101"],
          "REG": ["U104", "U105"]}
EXPLICIT = {"C101": "PWR", "C102": "PWR", "C107": "MIX", "C110": "MIX",
            "R8": "FM1", "R9": "FM1", "R10": "FM1", "R35": "FM1", "R36": "FM1", "R37": "FM1"}
NEAR = {"U6": ("U4", "22", False), "U12": ("U10", "22", False),       # ЦАП FM - к ножке SH своего YM
        "L102": ("U4", "21", False), "C103": ("U4", "21", False),     # фильтр каждого YM - у его ножки питания
        "L103": ("U10", "21", False), "C105": ("U10", "21", False),
        "D2": ("RP2350B-MINI-A1", "5V", True), "D1": ("U5", "5V", True),      # не аналог, но лежали в его зоне:
        "C202": ("J5", "4", True), "C115": ("C114", "1", False)}             #   к своей ножке (True - снизу)
EARLY = ("U6", "U12", "L102", "C103", "L103", "C105")
DECOUPLE = {"C117": ("U2", "8"), "C118": ("U3", "8"), "C119": ("U7", "8"), "C120": ("U6", "1"),
            "C121": ("U12", "1"), "C111": ("U3", "4"), "C104": ("U4", "21"), "C106": ("U10", "21")}
GAP = 0.8
BOARD = (59.1, 72.1, 228.1, 127.0)                  # плата без ламелей, с отступом от края


def is_analog(ref):
    if ref in FIXED:
        return False
    if ref in ANALOG_KEEP:
        return True
    return ref not in DIGITAL                            # (было по префиксу - "R20" цеплял и R20, "C20" - C20)


def on_top(f, g):
    """Сверху - микросхемы, катушки, DIP, перемычка, электролиты (кроме обвязки FM - её не паяем); прочее - снизу."""
    r, fp = f.GetReference(), str(f.GetFPID().GetLibItemName())
    if r in ("U2", "U7", "JP101"):                       # буферы ЦАП FM, перемычка 5 / 12 В - снизу
        return False
    if r[0] in "ULSJ":
        return True
    return fp.startswith("CP_Elec") and g not in ("FM1", "FM2")


def bbox(f):
    r = f.GetBoundingBox(False, False)
    return pcbnew.ToMM(r.GetLeft()), pcbnew.ToMM(r.GetTop()), pcbnew.ToMM(r.GetWidth()), pcbnew.ToMM(r.GetHeight())


def move_tl(f, x, y):
    """Поставить так, чтобы левый верхний угол рамки был в (x, y)."""
    l, t, _, _ = bbox(f)
    p = f.GetPosition()
    f.SetPosition(pcbnew.VECTOR2I(p.x + MM(x - l), p.y + MM(y - t)))


OCC = {False: [], True: []}                   # занятые прямоугольники (x0, y0, x1, y1) по сторонам: False - верх


def rect(f, pad=0.0):
    l, t, w, h = bbox(f)
    return (l - pad, t - pad, l + w + pad, t + h + pad)


def hit(r, side):
    return any(not (r[2] <= o[0] or o[2] <= r[0] or r[3] <= o[1] or o[3] <= r[1]) for o in OCC[side])


def block(f):
    """Деталь на месте: занять её рамку на её стороне; дырки (THT) и вырезы - на обеих."""
    OCC[f.IsFlipped()].append(rect(f, GAP / 2))
    for p in f.Pads():
        other = pcbnew.F_Cu if f.IsFlipped() else pcbnew.B_Cu   # площадка есть и на другой стороне (дырка, торец)
        if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) or p.IsOnLayer(other):
            r = p.GetBoundingBox()
            OCC[not f.IsFlipped()].append((pcbnew.ToMM(r.GetLeft()) - 0.6, pcbnew.ToMM(r.GetTop()) - 0.6,
                                           pcbnew.ToMM(r.GetRight()) + 0.6, pcbnew.ToMM(r.GetBottom()) + 0.6))
    for g in f.GraphicalItems():
        if g.GetLayer() == pcbnew.Edge_Cuts:
            r = g.GetBoundingBox()
            e = (pcbnew.ToMM(r.GetLeft()) - 0.5, pcbnew.ToMM(r.GetTop()) - 0.5,
                 pcbnew.ToMM(r.GetRight()) + 0.5, pcbnew.ToMM(r.GetBottom()) + 0.5)
            OCC[False].append(e)
            OCC[True].append(e)


def put(f, zone, near=None):
    """Первое свободное место в зоне (по рядам; с near - ближайшее к точке). -> True / False."""
    x0, y0, x1, y1 = zone
    _, _, w, h = bbox(f)
    side = f.IsFlipped()
    best = None
    y = y0
    while y + h <= y1 + 1e-6:
        x = x0
        while x + w <= x1 + 1e-6:
            r = (x - GAP / 2, y - GAP / 2, x + w + GAP / 2, y + h + GAP / 2)
            if not hit(r, side):
                if near is None:
                    move_tl(f, x, y)
                    block(f)
                    return True
                d = (x + w / 2 - near[0]) ** 2 + (y + h / 2 - near[1]) ** 2
                if best is None or d < best[0]:
                    best = (d, x, y)
            x += 0.5
        y += 0.5
    if best:
        move_tl(f, best[1], best[2])
        block(f)
        return True
    return False


def put_any(f, zone):
    """Как есть, иначе повёрнутой на 90."""
    if put(f, zone):
        return True
    a = f.GetOrientationDegrees()
    f.SetOrientationDegrees(a + 90)
    if put(f, zone):
        return True
    f.SetOrientationDegrees(a)
    return False


def pack(items, zone):
    """Крупные первыми, каждая - на первое свободное место. -> список не влезших."""
    return [f for f in sorted(items, key=lambda f: -bbox(f)[2] * bbox(f)[3]) if not put_any(f, zone)]


def near_place(fps, todo):
    """Каждую - к своей ножке, на стороне микросхемы (или как в NEAR), в ближайшее свободное место (окно 20 мм)."""
    for c, (u, pn) in todo:
        f, ic = fps[c], fps[u]
        bottom = NEAR[c][2] if c in NEAR else ic.IsFlipped()
        if f.IsFlipped() != bottom:
            f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        pad = next(p for p in ic.Pads() if p.GetNumber() == pn)
        px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
        win = (max(px - 20, BOARD[0]), max(py - 20, BOARD[1]), min(px + 20, BOARD[2]), min(py + 20, BOARD[3]))
        if not put(f, win, near=(px, py)):
            print("no room near", u, pn, "for", c)


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    # граф деталей через сигнальные цепи
    net_fps = {}
    for f in fps.values():
        for p in f.Pads():
            n = p.GetNetname()
            if n and n not in POWER and not n.startswith("unconnected"):
                net_fps.setdefault(n, set()).add(f.GetReference())
    adj = {r: set() for r in fps}
    for refs in net_fps.values():
        for r in refs:
            adj[r] |= refs - {r}
    # кучки: ближайшая по графу (только через аналог)
    group_of = {r: g for g, rs in GROUPS.items() for r in rs}
    group_of.update(EXPLICIT)
    dist = {}
    for g, rs in GROUPS.items():
        q = deque((r, 0) for r in rs)
        seen = set(rs)
        while q:
            r, d = q.popleft()
            if dist.get(r, (99, ""))[0] > d:
                dist[r] = (d, g)
            for n in adj[r]:
                if n not in seen and is_analog(n):
                    seen.add(n)
                    q.append((n, d + 1))
    for r in fps:
        if r not in group_of and is_analog(r) and r not in DECOUPLE and r not in NEAR and r in dist:
            group_of[r] = dist[r][1]
    moving = set(group_of) | set(DECOUPLE) | set(NEAR)
    for r, f in fps.items():
        if r not in moving:
            block(f)
    near_place(fps, [(r, NEAR[r][:2]) for r in EARLY])   # фильтры YM - первыми, у ножки питания
    # REG - повернуть на 90 (узкой стороной), потом укладка
    for r in GROUPS["REG"]:
        fps[r].SetOrientationDegrees(90)
    spill = []
    for g, zone in ZONES.items():
        mine = [fps[r] for r, gg in group_of.items() if gg == g and r in fps and r not in DECOUPLE]
        top = [f for f in mine if on_top(f, g)]
        bot = [f for f in mine if not on_top(f, g)]
        left = pack(top, zone)
        if g == "MIX":                                   # электролиты микшера - ещё под ЦАП FM, рядом
            for z in ((100.0, 85.0, 114.8, 91.8), (100.0, 105.0, 117.0, 127.3)):
                left = pack(left, z)
        print(g, "top", len(top), "left", [f.GetReference() for f in left])
        spill += left
        for f in bot:
            f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        for z in ZONES_B[g]:
            bot = pack(bot, z)
        print(g, "bottom", len(mine) - len(top), "left", [f.GetReference() for f in bot])
        spill += bot
    left = []
    for f in spill:                                      # не влезшее в зону - ближайшее свободное место у U3
        c = rect(fps["U3"])
        cx, cy = (c[0] + c[2]) / 2, (c[1] + c[3]) / 2
        if not put(f, (max(cx - 40, BOARD[0]), max(cy - 40, BOARD[1]), min(cx + 40, BOARD[2]), BOARD[3]), near=(cx, cy)):
            left.append(f)
    if left:
        print("NOT PLACED:", [f.GetReference() for f in left])
    near_place(fps, [(c, v) for c, v in DECOUPLE.items()] + [(r, (u, pn)) for r, (u, pn, _) in NEAR.items()
                                                                if r not in EARLY])
    b.Save(sys.argv[2])
    print("saved")


if __name__ == "__main__":
    main()
