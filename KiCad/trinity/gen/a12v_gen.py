#!/usr/bin/env python3
# ANALOG POWER: запасной вход +12 В для аналога (как MultiSound), по умолчанию - 5 В шины (как NeoGS).
# Рисуем в координатах корня (без раскладки): три фильтра +5VA / +5VA_1 / +5VA_2 начинаются на x = 125.73
# (y 349.25 / 374.65 / 400.05) - их входы соединяем вертикалью и ведём на общий вывод перемычки JP101:
#   JP101 1-2 (запаяно) - +5V шины; 2-3 - выход U104 (78M05 от +12V, DNP, конденсаторы по даташиту: 0.33u / 0.1u).
# Символы +5V на входах фильтров и замену ферритов на катушки делает patch_a12v.py.
#   python a12v_gen.py -> a12v.kicad_sch + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

FP_C = "Capacitor_SMD:C_0603_1608Metric"
X = 125.73                                                # входы фильтров
YS = (349.25, 374.65, 400.05)
NETS, NO_CONNECT = {}, []


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def build(d):
    s = d.sheet("a12v.kicad_sch", "analog power: +12V option", paper="A1")
    # вертикаль по входам фильтров -> общий вывод перемычки
    s.wire((X, YS[0]), (X, YS[1]))
    s.wire((X, YS[1]), (X, YS[2]))
    s.wire((X, YS[2]), (X, 412.75))
    s.label("AN_IN", X, YS[0])                            # временная: в корне тут вход фильтра (patch_a12v.py уберёт)
    # JP101: повёрнута, общий вывод 2 - вверх к вертикали; 1 (справа) - +5V шины, 3 (слева) - выход 78M05
    s.place("JP101", "Jumper:SolderJumper_3_Bridged12", "5V/12V", X, 416.56, rot=180,
            props={"Footprint": "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm"})
    p1, p2, p3 = s.pin("JP101", 1), s.pin("JP101", 2), s.pin("JP101", 3)
    s.wire((X, 412.75), p2)
    s.wire(p1, (p1[0] + 5.08, p1[1]))
    s.power("+5V", p1[0] + 5.08, p1[1])
    net("+5V", "JP101", 1); net("AN_IN", "JP101", 2)
    # U104 78M05 (DNP): вход +12V через 0.33u, выход 0.1u -> вывод 3 перемычки
    s.place("U104", "Regulator_Linear:MC78M05_TO252", "78M05", 105.41, 416.56,
            props={"Footprint": "Package_TO_SOT_SMD:TO-252-2"}, dnp=True)
    vi, vo, vg = s.pin("U104", 1), s.pin("U104", 3), s.pin("U104", 2)
    s.wire(vo, p3)
    s.wire(vi, (vi[0] - 7.62, vi[1]))
    s.power("+12V", vi[0] - 7.62, vi[1])
    s.stub("U104", 2, 2.54, power="GND")
    net("AN_12", "U104", 3); net("AN_12", "JP101", 3); net("+12V", "U104", 1); net("GND", "U104", 2)
    for c, v, x, n in (("C112", "0.33u", vi[0] - 3.81, "+12V"), ("C113", "0.1u", vo[0] + 3.81, "AN_12")):
        s.place(c, "Device:C", v, x, vi[1] + 3.81, props={"Footprint": FP_C}, dnp=True)
        s.stub(c, 2, 2.54, power="GND")
        net(n, c, 1); net("GND", c, 2)
    return s


def main():
    d = Design(HERE, "a12v", title="a12v", rev="A")
    build(d)
    d.write(center=False, force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    if "--text" in sys.argv:
        d.check_text(show=40)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
