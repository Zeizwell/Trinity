#!/usr/bin/env python3
# У разъёма шины J4 (в координатах корня): выходы карты на шину через буферы 74LVC1G125 на 5 В, как у MultiSound:
#   IORQGE (контакт a13): CPLD держит ~{IORQGE} в 0 -> буфер выдаёт 5 В (иначе отпущен);
#   CSROMCE (a15, окно памяти как NeoGS ZX-DMA): CPLD держит ~{CSROMCE_OE} в 0 -> 5 В на CSROMCE гасят флешку Евы
#     (перебивают её 680 Ом; 3.3 В с CPLD на это не хватит);
#   CSROM (a25) - вход в CPLD (Ева: окно 0 читает ПЗУ).
# Временный J4TMP (на месте настоящего J4) при вставке отбрасывается (merge_into_root.py --skip-ref J4TMP).
#   python zxbus_gen.py -> zxbus.kicad_sch + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

FP_C = "Capacitor_SMD:C_0603_1608Metric"
NETS, NO_CONNECT = {}, []
JX, JY = 548.64, 281.94                      # J4 на корневом листе


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def buffer(s, u, c, x, y, oe_net, global_oe):
    """74LVC1G125 на 5 В (ножки символа: 1 ~OE и 5 VCC сверху, 2 A слева, 4 Y справа, 3 GND снизу): A и VCC - +5V,
    ~OE - от CPLD; 100n у питания."""
    s.place(u, "74xGxx:74LVC1G125", "74LVC1G125", x, y, props={"Footprint": "Package_TO_SOT_SMD:SOT-23-5"})
    s.stub(u, 2, 2.54, power="+5V")
    s.stub(u, 5, 2.54, power="+5V")
    s.stub(u, 3, 2.54, power="GND")
    p1 = s.pin(u, 1)
    s.wire(p1, (p1[0], p1[1] - 2.54), (p1[0] + 7.62, p1[1] - 2.54))
    if global_oe:
        s.labels.append(("global_label", oe_net, p1[0] + 7.62, p1[1] - 2.54, 0, "left", "input"))
    else:
        s.label(oe_net, p1[0] + 7.62, p1[1] - 2.54)
    net("+5V", u, 2); net("+5V", u, 5); net("GND", u, 3); net(oe_net, u, 1)
    s.place(c, "Device:C", "100n", x - 22.86, y, props={"Footprint": FP_C})
    s.stub(c, 1, 2.54, power="+5V")
    s.stub(c, 2, 2.54, power="GND")
    net("+5V", c, 1); net("GND", c, 2)


def build(d):
    s = d.sheet("zxbus.kicad_sch", "ZX-BUS outputs", paper="A1")
    s.place("J4TMP", "Connector_Generic:Conn_02x30_Row_Letter_First", "TMP", JX, JY)
    a13, a15, a25 = s.pin("J4TMP", "a13"), s.pin("J4TMP", "a15"), s.pin("J4TMP", "a25")
    # IORQGE: буфер на уровне a13, выход - прямо на контакт
    buffer(s, "U21", "C133", a13[0] - 20.32, a13[1], "~{IORQGE}", True)
    s.wire(s.pin("U21", 4), a13)
    net("IORQGE", "U21", 4); net("IORQGE", "J4TMP", "a13")
    # CSROMCE: буфер левее (у a15 тесно - метки соседних контактов), связь меткой
    buffer(s, "U22", "C134", a13[0] - 40.64, a13[1] + 35.56, "~{CSROMCE_OE}", False)
    y22 = s.pin("U22", 4)
    s.wire(y22, (y22[0] + 10.16, y22[1]))
    s.label("CSROMCE", y22[0] + 10.16, y22[1], angle=180)
    s.wire(a15, (a15[0] - 5.08, a15[1]))
    s.label("CSROMCE", a15[0] - 5.08, a15[1], angle=180)
    net("CSROMCE", "U22", 4); net("CSROMCE", "J4TMP", "a15")
    # CSROM: вход - метка у контакта
    s.wire(a25, (a25[0] - 5.08, a25[1]))
    s.label("CSROM", a25[0] - 5.08, a25[1], angle=180)
    net("CSROM", "J4TMP", "a25")
    return s


def main():
    d = Design(HERE, "zxbus", title="zxbus", rev="A")
    build(d)
    d.write(center=False, force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    if "--text" in sys.argv:
        d.check_text(show=40)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
