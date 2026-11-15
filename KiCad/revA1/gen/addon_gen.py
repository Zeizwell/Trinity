#!/usr/bin/env python3
# Добавочные блоки к корневому листу Тринити (рядом с MIXER от MultiSound, patch_mixer.py):
#   NARROW STEREO - DIP-4: AY_A1 / AY_A2 -> NAR1 / NAR2 (входы правого сумматора, 82к), AY_C1 / AY_C2 -> NAR3 / NAR4
#                   (левого); ON - узко (~30 %), OFF - обычное ABC;
#   ANALOG POWER  - +5VA (микшер), +5VA_1 / +5VA_2 (каждому YM свой фильтр), -5VA (TPS60400, V- у U3).
# Спецификация - doc/trinity-board-analog.md.   python addon_gen.py -> addon.kicad_sch + сверка netlist
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402
import analog_gen as g  # noqa: E402  (хелперы: rail, hpart, vpart, net, ref)


def build(d):
    s = d.sheet("addon.kicad_sch", "Trinity addon: narrow stereo DIP, analog power", paper="auto",
                comment="doc/trinity-board-analog.md; refs from 101")

    s.block_start("NARROW STEREO (DIP ON = narrow ~30%, OFF = plain ABC)")
    s.place("SW101", "Switch:SW_DIP_x04", "NARROW", 50.8, 50.8,
            props={"Footprint": "Button_Switch_THT:SW_DIP_SPSTx04_Slide_9.78x12.34mm_W7.62mm_P2.54mm"})
    for k, (src, dst) in enumerate((("AY_A1", "NAR1"), ("AY_A2", "NAR2"), ("AY_C1", "NAR3"), ("AY_C2", "NAR4")), 1):
        s.stub("SW101", k, 5.08, label=src)
        s.stub("SW101", 9 - k, 5.08, label=dst)
        g.net(src, "SW101", k)
        g.net(dst, "SW101", 9 - k)
    s.note("NAR1/NAR2 -> right summer (A1, A2), NAR3/NAR4 -> left (C1, C2):\n"
           "MIXER inputs ex MIDI / SAA, 10u + 82k. Width: 100k ~25%, 82k ~30%, 68k ~35%.", 30.48, 66.04, 1.0)
    s.block_end()

    s.block_start("ANALOG POWER (from +5V bus, no 12 V)")
    g.rail(s, 25.4, 30.48, "+5V", "600R@100MHz", "+5VA",
           [("Device:C_Polarized", "47u", g.FP_CP), ("Device:C", "100n", g.FP_C)], flag=True)
    g.rail(s, 25.4, 55.88, "+5V", "600R@100MHz", "+5VA_1",
           [("Device:C_Polarized", "47u", g.FP_CP), ("Device:C", "100n", g.FP_C)], dst_glob=True, flag=True)
    g.rail(s, 25.4, 81.28, "+5V", "600R@100MHz", "+5VA_2",
           [("Device:C_Polarized", "47u", g.FP_CP), ("Device:C", "100n", g.FP_C)], dst_glob=True, flag=True)
    # -5VA: TPS60400 (datasheet: Cin 2.2u, Cfly 1u, Cout 10u), затем RC 10 Ом + 10u, 100n у ножки 4 U3
    xt, yt = 50.8, 121.92
    s.place("U103", "Regulator_SwitchedCapacitor:TPS60400DBV", "TPS60400", xt, yt,
            props={"Footprint": "Package_TO_SOT_SMD:SOT-23-5"})
    pin_in, pin_out = s.pin("U103", 2), s.pin("U103", 1)
    pfp, pfn = s.pin("U103", 5), s.pin("U103", 3)
    s.stub("U103", 4, 2.54, power="GND")
    g.net("GND", "U103", 4)
    xin = pin_in[0] - 10.16
    s.power("+5VA", xin, pin_in[1])
    cin = g.ref("C")
    s.wire((xin, pin_in[1]), (pin_in[0] - 5.08, pin_in[1]))
    g.vpart(s, cin, "Device:C", "2.2u", pin_in[0] - 5.08, pin_in[1], g.FP_C)
    s.stub(cin, 2, 2.54, power="GND")
    s.wire((pin_in[0] - 5.08, pin_in[1]), pin_in)
    g.net("+5VA", "U103", 2); g.net("+5VA", cin, 1); g.net("GND", cin, 2)
    cf = g.ref("C")
    xc = pfp[0] + 7.62
    s.place(cf, "Device:C", "1u", xc, round(pfp[1] + 1.27, 2), rot=0, props={"Footprint": g.FP_C})
    q1, q2 = s.pin(cf, 1), s.pin(cf, 2)
    s.wire(pfp, (xc - 2.54, pfp[1]), (xc - 2.54, q1[1]), q1)
    s.wire(pfn, q2)
    g.net("CFLY_P", "U103", 5); g.net("CFLY_P", cf, 1)
    g.net("CFLY_N", "U103", 3); g.net("CFLY_N", cf, 2)
    x1 = pin_out[0] + 15.24
    s.wire(pin_out, (x1, pin_out[1]))
    co = g.ref("C")
    g.vpart(s, co, "Device:C", "10u", x1, pin_out[1], g.FP_C08)
    s.stub(co, 2, 2.54, power="GND")
    rr = g.ref("R")
    r1, r2 = g.hpart(s, rr, "Device:R", "10", x1 + 7.62, pin_out[1], g.FP_R)
    s.wire((x1, pin_out[1]), r1)
    x2 = r2[0] + 3.81
    s.wire(r2, (x2, pin_out[1]))
    c2 = g.ref("C")
    g.vpart(s, c2, "Device:C", "10u", x2, pin_out[1], g.FP_C08)
    s.stub(c2, 2, 2.54, power="GND")
    x3 = x2 + 7.62
    s.wire((x2, pin_out[1]), (x3, pin_out[1]))
    c3 = g.ref("C")
    g.vpart(s, c3, "Device:C", "100n", x3, pin_out[1], g.FP_C)
    s.stub(c3, 2, 2.54, power="GND")
    s.wire((x3, pin_out[1]), (x3 + 7.62, pin_out[1]))
    s.power("-5VA", x3 + 7.62, pin_out[1])
    s.wire((x3 + 5.08, pin_out[1]), (x3 + 5.08, pin_out[1] - 5.08))
    s.power("PWR_FLAG", x3 + 5.08, pin_out[1] - 5.08)
    g.net("NEG_RAW", "U103", 1); g.net("NEG_RAW", co, 1); g.net("GND", co, 2)
    g.net("NEG_RAW", rr, 1); g.net("-5VA", rr, 2); g.net("-5VA", c2, 1); g.net("GND", c2, 2)
    g.net("-5VA", c3, 1); g.net("GND", c3, 2)
    s.note("-5VA: V- of the mixer op-amp U3 (pin 4); 100n near that pin.\n"
           "+5VA_1 / +5VA_2: own filter per YM (pin 21 YM2203, pin 1 YM3014B).\n"
           "No 5 V LDO from the 5 V bus - filters only.", 25.4, 142.24, 1.0)
    s.block_end()

    s.arrange(relieve=False)
    return s


def main():
    d = Design(HERE, "addon", title="ZX Trinity - addon", rev="A", company="ZX Trinity")
    build(d)
    d.write(force="--force" in sys.argv, relieve=False)
    ok = d.verify(g.NETS, g.NO_CONNECT)
    d.erc()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
