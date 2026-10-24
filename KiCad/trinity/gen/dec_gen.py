#!/usr/bin/env python3
# Развязка 100 нФ у каждой ножки питания аналоговых микросхем, которой не было (чеклист: 100n у каждой ножки
# вплотную): ОУ U2 / U3 / U7 (ножка 8, +5VA), ЦАП FM U6 / U12 (ножка 1, +5VA_1 / +5VA_2). На плате - у своей ножки.
# В координатах корня, рядом с ёмкостью +5V шины.   python dec_gen.py -> dec.kicad_sch + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

NETS, NO_CONNECT = {}, []
FP_C = "Capacitor_SMD:C_0603_1608Metric"
CAPS = [("C117", "+5VA", "U2.8"), ("C118", "+5VA", "U3.8"), ("C119", "+5VA", "U7.8"),
        ("C120", "+5VA_1", "U6.1"), ("C121", "+5VA_2", "U12.1")]


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def build(d):
    s = d.sheet("dec.kicad_sch", "analog decoupling", paper="A1")
    x0, y = 450.85, 445.77
    s.texts.append(("DECOUPLING 100n, each at its pin: " + ", ".join(f"{c} {p}" for c, _, p in CAPS),
                    x0 - 6.35, y - 17.78, 1.27, "left bottom", False))
    for k, (c, rail, _) in enumerate(CAPS):
        x = x0 + k * 15.24
        s.place(c, "Device:C", "100n", x, y, props={"Footprint": FP_C})
        if rail == "+5VA":
            s.stub(c, 1, 2.54, power=rail)
        else:
            s.stub(c, 1, 5.08, label=rail)
        s.stub(c, 2, 2.54, power="GND")
        net(rail, c, 1); net("GND", c, 2)
    return s


def main():
    d = Design(HERE, "dec", title="dec", rev="A")
    build(d)
    d.write(center=False, force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    if "--text" in sys.argv:
        d.check_text(show=40)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
