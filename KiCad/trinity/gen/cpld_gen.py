#!/usr/bin/env python3
# Блок CPLD (EPM3128ATC100, U1) на корневом листе, в координатах корня (слева от RP-AY, где стоял символ). Стиль - как
# блоки GS / TURBO SOUND FM: шина слева и справа, каждая ножка - проводом на ввод, метка у ввода.
# Раскладка ножек - под расстановку на плате (2026-10-25): левая сторона корпуса - шина ZX с левой части ламелей,
# нижняя - с правой части, верхняя - шина AY (RP-AY над CPLD), правая - шина GS (RP-GS справа-сверху).
# Шина ZX - глобальные метки, как у разъёма J4 (MultiSound). Ниже символа - питание 3.3 В, развязка, JTAG.
#   python cpld_gen.py -> cpld.kicad_sch + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

U = "U1"
UX, UY = 60.96, 104.14                       # символ CPLD
LB, RB = 17.78, 101.6                        # шины блока: левая, правая (RP-AY - своя шина на x 109.22)
FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C08 = "Capacitor_SMD:C_0805_2012Metric"
NETS, NO_CONNECT = {}, []

ZX_LEFT = [  # ножки левой стороны корпуса (сверху вниз) - по порядку контактов ламелей слева направо
    (1, "A14"), (2, "A15"), (5, "A12"), (6, "A13"), (7, "D7"), (8, "~{DOS}"), (9, "D0"), (10, "D1"), (12, "D2"),
    (13, "A0"), (14, "D6"), (16, "A1"), (17, "D5"), (19, "A2"), (20, "D3"), (21, "A3"), (22, "D4"),
    (23, "~{IORQGE}"), (24, "~{CSROMCE_OE}"), (25, "~{MREQ}")]
ZX_BOTTOM = [  # нижняя сторона (слева направо) - правая часть ламелей
    (27, "~{IORQ}"), (28, "~{RD}"), (29, "~{WR}"), (30, "~{RST}"), (31, "~{IODOS}"), (32, "A7"), (35, "~{WAIT}"),
    (36, "A6"), (37, "A5"), (40, "A4"), (41, "~{M1}"), (42, "CSROM"), (44, "A8"), (45, "A9"), (46, "A10"), (47, "A11")]
AY = [(76, "AY_D0"), (77, "AY_D1"), (79, "AY_D2"), (80, "AY_D3"), (81, "AY_D4"), (83, "AY_D5"), (84, "AY_D6"),
      (85, "AY_D7"), (92, "AY_WR"), (93, "AY_RD"), (94, "AY_C0"), (96, "AY_C1"), (97, "AY_RDY"), (98, "AY_RST")]
GS = [(52, "GS_C0"), (54, "GS_C1"), (55, "GS_C2"), (56, "GS_C3"), (57, "GS_C4"), (58, "GS_C5"), (60, "GS_WR"),
      (61, "GS_D0"), (63, "GS_D1"), (64, "GS_D2"), (67, "GS_D3"), (68, "GS_D4"), (69, "GS_D5"), (70, "GS_D6"),
      (71, "GS_D7"), (72, "GS_RD"), (75, "GS_RDY")]
MISC = [(87, "CPLD_CLK"), (4, "JTAG_TDI"), (15, "JTAG_TMS"), (62, "JTAG_TCK"), (73, "JTAG_TDO")]
SPARE = (48, 49, 50, 88, 89, 90, 99, 100)    # DIP-5 убран (2026-10-25): настройки карты - во флеше RP (Trinity Setup)
VCC = (3, 18, 34, 39, 51, 66, 82, 91)
GND = (11, 26, 33, 38, 43, 53, 59, 65, 74, 78, 86, 95)
GLOBAL = {n for _, n in ZX_LEFT + ZX_BOTTOM} - {"~{CSROMCE_OE}", "CSROM"}   # как у J4 (CSROM / CSROMCE - новые)


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def lab(s, n, x, y, angle):
    if n in GLOBAL:                                   # шина ZX - глобальная метка, как у J4
        s.labels.append(("global_label", n, x, y, angle, "right" if angle == 180 else "left", "bidirectional"))
    else:
        s.label(n, x, y, angle=angle)


def to_bus(s, p, n):
    """Ножка -> провод -> ввод на свою шину (левая колонка символа - левая шина); метка у ввода."""
    px, py = s.pin(U, p)
    if px < UX:
        s.wire((px, py), (LB + 2.54, py))
        s.bus_entry(LB + 2.54, py, -2.54, -2.54)
        lab(s, n, LB + 2.54, py, 0)
    else:
        s.wire((px, py), (RB - 2.54, py))
        s.bus_entry(RB - 2.54, py, 2.54, -2.54)
        lab(s, n, RB - 2.54, py, 180)
    net(n, U, p)


def rail(s, pins, kind):
    """Ножки питания одной колонки - стабами к общей вертикали, один символ питания на её конце."""
    pts = sorted(s.pin(U, p) for p in pins)
    x = pts[0][0] - 2.54
    for px, py in pts:
        s.wire((px, py), (x, py))
    ys = sorted(py for _, py in pts)
    s.wire((x, ys[0]), (x, ys[-1]))
    if kind == "GND":
        s.wire((x, ys[-1]), (x, ys[-1] + 2.54))
        s.power("GND", x, ys[-1] + 2.54)
    else:
        s.wire((x, ys[0]), (x, ys[0] - 2.54))
        s.power("+3V3", x, ys[0] - 2.54)
    for p in pins:
        net(kind, U, p)


def build(d):
    s = d.sheet("cpld.kicad_sch", "CPLD", paper="A1")
    s.place(U, "EPM:EPM3128ATC100-10N", "EPM3128ATC100-10N", UX, UY,
            props={"Footprint": "trinity:QFP50P1600X1600X120-100N"})
    for p, n in ZX_LEFT + ZX_BOTTOM + AY + GS + MISC:
        to_bus(s, p, n)
    for p in SPARE:
        s.nc(U, str(p)); NO_CONNECT.append((U, str(p)))
    rail(s, VCC, "+3V3")
    rail(s, GND, "GND")
    ys = [s.pin(U, str(p))[1] for p in list(range(1, 101))]
    top, bot = min(ys) - 10.16, max(ys) + 7.62
    s.bus((LB, top), (RB, top), (RB, bot), (LB, bot), (LB, top))
    s.texts.append(("CPLD (ZX-BUS decoder)", LB + 1.27, top - 0.635, 2.0066, "left bottom", False))
    support(s, bot + 12.7)
    return s


def support(s, y0):
    """Под блоком: 3.3 В для CPLD, развязка у каждой ножки питания, гребёнка JTAG."""
    s.texts.append(("CPLD POWER, JTAG", LB + 1.27, y0 - 0.635, 2.0066, "left bottom", False))
    # 3.3 В: AMS1117-3.3 от +5V (как MultiSound Max), по даташиту: 10u на входе, 22u на выходе
    x, y = 35.56, y0 + 12.7
    s.place("U23", "Regulator_Linear:AMS1117-3.3", "AMS1117-3.3", x, y,
            props={"Footprint": "Package_TO_SOT_SMD:SOT-223-3_TabPin2"})
    vi, vo = s.pin("U23", 3), s.pin("U23", 2)          # AMS1117: 1 GND, 2 VO, 3 VI
    s.wire(vi, (vi[0] - 7.62, vi[1]))
    s.power("+5V", vi[0] - 7.62, vi[1])
    s.wire(vo, (vo[0] + 15.24, vo[1]))
    s.power("+3V3", vo[0] + 15.24, vo[1])
    s.stub("U23", 1, 2.54, power="GND")
    net("+5V", "U23", 3); net("+3V3", "U23", 2); net("GND", "U23", 1)
    for c, v, fp, cx, n in (("C122", "10u", FP_C08, vi[0] - 3.81, "+5V"),
                            ("C123", "22u", "Capacitor_SMD:CP_Elec_4x5.4", vo[0] + 5.08, "+3V3")):
        sym = "Device:C_Polarized" if fp.startswith("Capacitor_SMD:CP") else "Device:C"
        s.place(c, sym, v, cx, vi[1] + 3.81, props={"Footprint": fp})
        s.stub(c, 2, 2.54, power="GND")
        net(n, c, 1); net("GND", c, 2)
    # развязка: 100n у каждой из 8 ножек питания CPLD (на плате - вплотную к своей) + 10u общий
    yc = y0 + 40.64                                    # гребёнкой: общие +3V3 сверху и GND снизу
    xs = [30.48 + k * 12.7 for k in range(9)]
    for k, cx in enumerate(xs):
        c = f"C{124 + k}"
        v, fp = ("10u", FP_C08) if k == 8 else ("100n", FP_C)
        s.place(c, "Device:C", v, cx, yc, props={"Footprint": fp})
        s.wire(s.pin(c, 1), (cx, yc - 6.35))
        s.wire(s.pin(c, 2), (cx, yc + 6.35))
        net("+3V3", c, 1); net("GND", c, 2)
    s.wire((xs[0] - 5.08, yc - 6.35), (xs[-1], yc - 6.35))
    s.wire((xs[0] - 5.08, yc + 6.35), (xs[-1], yc + 6.35))
    s.power("+3V3", xs[0] - 5.08, yc - 6.35)
    s.power("GND", xs[0] - 5.08, yc + 6.35)
    # JTAG: гребёнка 2x5 (как ByteBlaster / USB-Blaster: 1 TCK, 2 GND, 3 TDO, 4 VCC, 5 TMS, 9 TDI, 10 GND);
    # подтяжки по рекомендации Altera: TCK 1к вниз, TMS и TDI 10к вверх. Те же линии - от RP-GS (шьёт CPLD файлом SVF).
    jx, jy = 45.72, y0 + 71.12
    s.place("J7", "Connector_Generic:Conn_02x05_Odd_Even", "JTAG", jx, jy,
            props={"Footprint": "Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Vertical"})
    for p, n in ((1, "JTAG_TCK"), (3, "JTAG_TDO"), (5, "JTAG_TMS"), (9, "JTAG_TDI")):
        s.stub("J7", p, 7.62, label=n); net(n, "J7", p)
    p2, p4, p10 = s.pin("J7", 2), s.pin("J7", 4), s.pin("J7", 10)   # правый ряд: земли и питание - врозь
    s.wire(p2, (p2[0] + 5.08, p2[1]), (p2[0] + 5.08, p2[1] - 5.08))
    s.power("GND", p2[0] + 5.08, p2[1] - 5.08, rot=180)
    s.wire(p4, (p4[0] + 10.16, p4[1]))
    s.power("+3V3", p4[0] + 10.16, p4[1])
    s.stub("J7", 10, 2.54, power="GND")
    net("GND", "J7", 2); net("GND", "J7", 10); net("+3V3", "J7", 4)
    for p in (6, 7, 8):
        s.nc("J7", p); NO_CONNECT.append(("J7", str(p)))
    for k, (r, v, n, to) in enumerate((("R226", "1k", "JTAG_TCK", "GND"), ("R227", "10k", "JTAG_TMS", "+3V3"),
                                       ("R228", "10k", "JTAG_TDI", "+3V3"))):
        rx = 81.28 + k * 10.16
        s.place(r, "Device:R", v, rx, jy, props={"Footprint": FP_R})
        s.stub(r, 1, 5.08, label=n)
        s.stub(r, 2, 2.54, power=to)
        net(n, r, 1); net(to, r, 2)


def main():
    d = Design(HERE, "cpld", title="cpld", rev="A")
    build(d)
    d.write(center=False, force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    if "--text" in sys.argv:
        d.check_text(show=40)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
