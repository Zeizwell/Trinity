#!/usr/bin/env python3
# +5V шины: общая ёмкость, как у MultiSound (C75 100u/16V, C33 10u) - в нашей копии её не было. Рядом с разъёмом
# шины, в координатах корня. Предохранителя нет: у карт на шине (NeoGS, MultiSound, MultiSound Max) его нет.
#   python bulk_gen.py -> bulk.kicad_sch + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

NETS, NO_CONNECT = {}, []


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def build(d):
    s = d.sheet("bulk.kicad_sch", "+5V bulk", paper="A1")
    x, y = 500.38, 400.05
    s.texts.append(("+5V BUS BULK (as MultiSound C75, C33)", x - 6.35, y - 12.7, 2.0066, "left bottom", False))
    for c, sym, v, fp, dx in (("C114", "Device:C_Polarized", "100u/16V", "Capacitor_SMD:CP_Elec_6.3x7.7", 0),
                              ("C115", "Device:C", "10u", "Capacitor_SMD:C_0805_2012Metric", 10.16)):
        s.place(c, sym, v, x + dx, y, props={"Footprint": fp})
        s.stub(c, 1, 2.54, power="+5V")
        s.stub(c, 2, 2.54, power="GND")
        net("+5V", c, 1); net("GND", c, 2)
    return s


def main():
    d = Design(HERE, "bulk", title="bulk", rev="A")
    build(d)
    d.write(center=False, force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    if "--text" in sys.argv:
        d.check_text(show=40)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
