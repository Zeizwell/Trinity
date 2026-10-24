#!/usr/bin/env python3
# Добавки к RP-AY (символ уже стоит на корневом листе в (124.46, 119.38), левые ножки подключены метками Y1*/Y2*).
# Рисуем в координатах корня (без раскладки и центровки): правый край - на шину блока TURBO SOUND FM (x = 162.56)
# вводами с метками, 5V через Шоттки, земля, RUN снизу; отдельно (other=True) - подтяжки /CS /WR /RD чипов к +5V.
# Временный символ RPTMP и шина-заглушка при вставке отбрасываются (merge_into_root.py --skip-ref RPTMP --skip-bus).
#   python rpay_gen.py -> rpay.kicad_sch (добавки у RP) и rpay_pu.kicad_sch (подтяжки) + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

X, Y = 124.46, 119.38                                     # RP-AY на корневом листе
BUS_X = 162.56
U = "RPTMP"
FP_R = "Resistor_SMD:R_0603_1608Metric"

# правый край RP-AY: ножка -> сигнал (к CPLD - данные, стробы, код, ГОТОВ, сброс шины; UART к RP-GS; /RD и /IC чипов)
RIGHT = [("GP28", "AY_D0"), ("GP29", "AY_D1"), ("GP30", "AY_D2"), ("GP31", "AY_D3"), ("GP32", "AY_D4"),
         ("GP33", "AY_D5"), ("GP34", "AY_D6"), ("GP35", "AY_D7"), ("GP36", "AY_WR"), ("GP37", "AY_RD"),
         ("GP38", "AY_C0"), ("GP39", "AY_C1"), ("GP40", "AY_RDY"), ("GP41", "AY_RST"),
         ("GP42", "~{Y1RD}"), ("GP43", "~{Y2RD}"), ("GP44", "GS_RX"), ("GP45", "GS_TX"), ("GP46", "~{RSTOUT}"),
         ("GP47", "AY_GP47")]                                 # GP47 - запасная, на гребёнку J6 (блок GS)
NETS, NO_CONNECT = {}, []


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def build_rp(d):
    s = d.sheet("rpay.kicad_sch", "rp-ay addons", paper="A2")
    s.place(U, "Trinity:RP2350B-MINI-A", "RP-AY", X, Y)
    s.bus((BUS_X, 100.33), (BUS_X, 195.58))                 # заглушка: в корне шина уже есть
    for p, n in RIGHT:
        px, py = s.pin(U, p)
        s.wire((px, py), (BUS_X - 2.54, py))
        s.bus_entry(BUS_X - 2.54, py, 2.54, -2.54)
        s.label(n, BUS_X - 2.54, py, angle=180)           # метка у ввода, текст к ножке (как у образца слева)
        net(n, U, p)
    for p in ("3V3", "EN", "GP20", "GP26", "GP27"):        # 3V3 модуля и EN - не нужны; светодиод; торец без дыр
        s.nc(U, p); NO_CONNECT.append((U, p))
    # 5V модуля через Шоттки (катод - модуль, анод - +5V платы), земля
    p5 = s.pin(U, "5V")
    s.place("D2", "Device:D_Schottky", "SS14", p5[0] + 5.08, p5[1], rot=0, props={"Footprint": "Diode_SMD:D_SMA"})
    k, a = s.pin("D2", 1), s.pin("D2", 2)
    s.wire(p5, k)
    s.wire(a, (a[0] + 1.27, a[1]))
    s.power("+5V", a[0] + 1.27, a[1])
    net("AY_5V", U, "5V"); net("AY_5V", "D2", 1); net("+5V", "D2", 2)
    g = s.pin(U, "GND")
    s.wire(g, (g[0] + 2.54, g[1]))
    s.power("GND", g[0] + 2.54, g[1])
    net("GND", U, "GND")
    # низ: RUN от RP-GS, земли модуля
    s.stub(U, "R", 5.08, label="AY_RUN"); net("AY_RUN", U, "R")
    for p in ("G1", "G2"):
        s.stub(U, p, 2.54, power="GND"); net("GND", U, p)
    return s


def build_pu(d):
    s = d.sheet("rpay_pu.kicad_sch", "AY pull-ups", paper="A4")
    s.block_start("AY CHIP STROBES PULL-UP (RP pins float while RP boots)")
    for k, n in enumerate(("~{Y1CS}", "~{Y1WR}", "~{Y1RD}", "~{Y2CS}", "~{Y2WR}", "~{Y2RD}")):
        r = f"R{220 + k}"
        x = 40.64 + k * 10.16
        s.place(r, "Device:R", "10k", x, 50.8, props={"Footprint": FP_R})
        s.stub(r, 1, 2.54, power="+5V")
        s.stub(r, 2, 5.08, label=n)
        net("+5V", r, 1); net(n, r, 2)
    s.block_end()
    return s


def main():
    ok = True
    for name, build in (("rpay", build_rp), ("rpay_pu", build_pu)):
        NETS.clear(); NO_CONNECT.clear()
        d = Design(HERE, name, title=name, rev="A")
        build(d)
        d.write(center=False, force="--force" in sys.argv, relieve=False)
        ok = d.verify(NETS, NO_CONNECT) and ok
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
