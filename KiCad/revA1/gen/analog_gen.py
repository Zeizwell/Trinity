#!/usr/bin/env python3
# Лист MIXER платы Тринити (mixer.kicad_sch): входы SSG / FM / GS, узкое стерео, сумматоры L / R, линейный выход,
# питание аналога. Спецификация - doc/trinity-board-analog.md. Генератор - schlib (kicad-claude-skills).
# Рисуем как MultiSound: блоки, внутри детали соединены проводами; метки - только между блоками.
# Обозначения с 101 (не пересекаются с корнем). Блоки копируются в корневой лист (один лист, как MultiSound);
# связь с другими блоками - метки с теми же именами:
#   AY_A1..AY_C2 (выходы SSG, нагрузка 3.3к - в листе RP-AY + чипы), FM1 / FM2 (буферы ЦАП FM, DNP),
#   GS_L / GS_R (модуль PCM5102 - в листе RP-GS), +5VA_1 / +5VA_2 (питание YM - фильтры здесь).
#   python analog_gen.py   -> mixer.kicad_sch, сверка netlist KiCad со спецификацией, ERC
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C08 = "Capacitor_SMD:C_0805_2012Metric"
FP_CP = "Capacitor_SMD:CP_Elec_5x5.4"
FP_FB = "Inductor_SMD:L_0805_2012Metric"
FP_OA = "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"

NETS, NO_CONNECT = {}, []
_n = {"R": 101, "C": 101}


def ref(k):
    r = f"{k}{_n[k]}"
    _n[k] += 1
    return r


def net(name, r, pin):
    NETS.setdefault(name, []).append((r, str(pin)))


def to_global(s, name):
    k = s.labels[-1]
    assert k[1] == name, (k, name)
    s.labels[-1] = ("global_label",) + tuple(k[1:6]) + ("passive",)


def lab(s, name, x, y, angle, glob=False):
    """Метка. Один лист (как MultiSound): одинаковые имена соединяются сами, глобальные не нужны."""
    s.label(name, x, y, angle=angle)


def hpart(s, r, lib, val, cx, y, fp, dnp=False):
    """Горизонтальная двухвыводная: ножка 1 слева, 2 справа. -> (p1, p2)"""
    s.place(r, lib, val, cx, y, rot=90, props={"Footprint": fp}, dnp=dnp)
    return s.pin(r, 1), s.pin(r, 2)


def vpart(s, r, lib, val, x, top, fp, dnp=False):
    """Вертикальная двухвыводная, ножка 1 вверху в точке (x, top). -> (p1, p2)"""
    s.place(r, lib, val, x, round(top + 3.81, 2), rot=0, props={"Footprint": fp}, dnp=dnp)
    return s.pin(r, 1), s.pin(r, 2)


def row(s, x, y, a, parts, b, a_glob=False, b_glob=False):
    """Строка слева направо: метка a -> детали подряд -> метка b. parts: (prefix, lib, val, fp, dnp).
    Цепи: a, промежуточные, b. -> список обозначений"""
    lx = x
    s.wire((lx, y), (lx + 3.81, y))
    lab(s, a, lx, y, 180, a_glob)
    cur, prev_net, refs = lx + 3.81, a, []
    for i, (k, lib, val, fp, dnp) in enumerate(parts):
        r = ref(k)
        p1, p2 = hpart(s, r, lib, val, cur + 3.81, y, fp, dnp)
        net(prev_net, r, 1)
        nxt = b if i == len(parts) - 1 else f"{a}_{r}"
        net(nxt, r, 2)
        prev_net, refs = nxt, refs + [r]
        if i < len(parts) - 1:
            s.wire(p2, (p2[0] + 2.54, y))
            cur = p2[0] + 2.54
        else:
            s.wire(p2, (p2[0] + 3.81, y))
            lab(s, b, p2[0] + 3.81, y, 0, b_glob)
    return refs


def summer(s, xn, y0, side, inputs, unit):
    """Инвертирующий сумматор: входные резисторы проводами в узел xn, ОУ, ОС 10к || 470п снизу, выход
    100 Ом -> 10 мкФ -> JACK_x (47к на землю). inputs: (метка, номинал, dnp)"""
    sum_n, jack = f"SUM_{side}", f"JACK_{side}"
    ys = [y0 + i * 7.62 for i in range(len(inputs))]
    s.wire((xn, y0 - 5.08), (xn, y0))
    s.wire((xn, y0), (xn, ys[-1]))                      # узел: все входы на одну вертикаль
    lab(s, sum_n, xn, y0 - 5.08, 90)                    # узел - и для узкого стерео (лист: блок NARROW)
    for (name, val, dnp), y in zip(inputs, ys):
        r = ref("R")
        p1, p2 = hpart(s, r, "Device:R", val, xn - 7.62, y, FP_R, dnp)
        s.wire((p1[0] - 3.81, y), p1)
        lab(s, name, p1[0] - 3.81, y, 180, glob=name.startswith(("GS_",)))
        s.wire(p2, (xn, y))
        net(name, r, 1)
        net(sum_n, r, 2)
    yo = ys[-1] + 10.16                                 # ОУ: «-» на yo + 2.54
    xo = xn + 15.24
    s.place("U101", "Amplifier_Operational:NJM4580", "NJM4580", xo, yo, unit=unit, props={"Footprint": FP_OA})
    inn, inp, out = (2, 3, 1) if unit == 1 else (6, 5, 7)
    pm, pp, po = s.pin("U101", inn, unit), s.pin("U101", inp, unit), s.pin("U101", out, unit)
    s.stub("U101", inp, 2.54, power="GND", unit=unit)
    net("GND", "U101", inp)
    net(sum_n, "U101", inn)
    yfb = yo + 12.7
    s.wire((xn, ys[-1]), (xn, pm[1]))
    s.wire((xn, pm[1]), pm)
    s.wire((xn, pm[1]), (xn, yfb))
    s.wire((xn, yfb), (xn, yfb + 7.62))
    xout = po[0] + 2.54
    s.wire(po, (xout, po[1]))
    s.wire((xout, po[1]), (xout, yfb))
    s.wire((xout, yfb), (xout, yfb + 7.62))
    mix = f"MIX_{side}"
    for k, (lib, val, fp) in enumerate((("Device:R", "10k", FP_R), ("Device:C", "470p", FP_C))):
        y = yfb + k * 7.62
        r = ref("R" if lib == "Device:R" else "C")
        p1, p2 = hpart(s, r, lib, val, round((xn + xout) / 2, 2), y, fp)
        s.wire((xn, y), p1)
        s.wire(p2, (xout, y))
        net(sum_n, r, 1)
        net(mix, r, 2)
    net(mix, "U101", out)
    # выход: 100 Ом -> 10 мкФ -> узел гнезда, 47к на землю
    r100 = ref("R")
    a1, a2 = hpart(s, r100, "Device:R", "100", xout + 7.62, po[1], FP_R)
    s.wire((xout, po[1]), a1)
    cout = ref("C")
    b1, b2 = hpart(s, cout, "Device:C_Polarized", "10u", a2[0] + 6.35, po[1], FP_CP)
    s.wire(a2, b1)
    xj = b2[0] + 5.08
    s.wire(b2, (xj, po[1]))
    rb = ref("R")
    c1, c2 = vpart(s, rb, "Device:R", "47k", xj, po[1], FP_R)
    s.stub(rb, 2, 2.54, power="GND")
    s.wire((xj, po[1]), (xj + 5.08, po[1]))
    lab(s, jack, xj + 5.08, po[1], 0)
    net(mix, r100, 1); net(f"OUT_{side}", r100, 2)
    net(f"OUT_{side}", cout, 1); net(jack, cout, 2)
    net(jack, rb, 1); net("GND", rb, 2)


def rail(s, x, y, src, fbval, dst, caps, dst_glob=False, flag=False):
    """Шина питания: порт src -> феррит -> шина -> конденсаторы вниз на землю -> dst (порт или глоб. метка)."""
    s.power(src, x, y)
    fb = f"FB{_n.setdefault('FB', 101)}"
    _n["FB"] += 1
    p1, p2 = hpart(s, fb, "Device:FerriteBead", fbval, x + 6.35, y, FP_FB)
    s.wire((x, y), p1)
    net(src, fb, 1); net(dst, fb, 2)
    cur = p2
    xc = p2[0] + 5.08
    for (lib, val, fp) in caps:
        r = ref("C")
        s.wire(cur, (xc, y))
        vpart(s, r, lib, val, xc, y, fp)
        s.stub(r, 2, 2.54, power="GND")
        net(dst, r, 1); net("GND", r, 2)
        cur = (xc, y)
        xc += 10.16
    xe = xc
    s.wire(cur, (xe, y))
    if dst_glob:
        lab(s, dst, xe, y, 0, glob=True)
    else:
        s.power(dst, xe, y)
    if flag:
        s.wire((xe - 2.54, y), (xe - 2.54, y - 5.08))
        s.power("PWR_FLAG", xe - 2.54, y - 5.08)


def build(d):
    s = d.sheet("mixer.kicad_sch", "MIXER: SSG / FM / GS inputs, narrow stereo, summing amps, line out, analog power",
                paper="auto", comment="doc/trinity-board-analog.md; MultiSound rev.A1 values; refs from 101")

    # ---- Развязка входов: 10 мкФ (+ к YM: у выхода SSG постоянка выше нуля)
    s.block_start("INPUT COUPLING (SSG load 3.3k is at the chips)")
    for i, n in enumerate(("AY_A1", "AY_B1", "AY_C1", "AY_A2", "AY_B2", "AY_C2")):
        row(s, 25.4, 30.48 + i * 10.16, n, [("C", "Device:C_Polarized", "10u", FP_CP, False)], f"{n}_AC", a_glob=True)
    for i, n in enumerate(("FM1", "FM2")):
        row(s, 25.4, 96.52 + i * 10.16, n, [("C", "Device:C_Polarized", "10u", FP_CP, True)], f"{n}_AC", a_glob=True)
    s.note("FM path DNP on the first board.", 25.4, 119.38, 1.0)
    s.block_end()

    # ---- Сумматоры
    s.block_start("MIXER LEFT (inverting summer, MultiSound values)")
    summer(s, 127.0, 30.48, "L", [("AY_A1_AC", "24k", False), ("AY_A2_AC", "24k", False),
                                  ("AY_B1_AC", "47k", False), ("AY_B2_AC", "47k", False),
                                  ("GS_L", "22k", False), ("FM1_AC", "10k", True), ("FM2_AC", "10k", True)], 1)
    s.block_end()
    s.block_start("MIXER RIGHT")
    summer(s, 254.0, 30.48, "R", [("AY_C1_AC", "24k", False), ("AY_C2_AC", "24k", False),
                                  ("AY_B1_AC", "47k", False), ("AY_B2_AC", "47k", False),
                                  ("GS_R", "22k", False), ("FM1_AC", "10k", True), ("FM2_AC", "10k", True)], 2)
    s.block_end()

    # ---- Узкое стерео: A -> правый, C -> левый через 82к; DIP-4 включает все четыре сразу
    s.block_start("NARROW STEREO (DIP ON = narrow ~30%, OFF = plain ABC)")
    xd, yd = 76.2, 203.2
    s.place("SW101", "Switch:SW_DIP_x04", "NARROW", xd, yd,
            props={"Footprint": "Button_Switch_THT:SW_DIP_SPSTx04_Slide_9.78x12.34mm_W7.62mm_P2.54mm"})
    for k, (src, dst) in enumerate((("AY_A1_AC", "SUM_R"), ("AY_A2_AC", "SUM_R"),
                                    ("AY_C1_AC", "SUM_L"), ("AY_C2_AC", "SUM_L")), 1):
        nar = f"NAR{k}"
        row(s, 25.4, yd - 20.32 + (k - 1) * 10.16, src, [("R", "Device:R", "82k", FP_R, False)], nar)
        s.stub("SW101", k, 2.54, label=nar)
        s.stub("SW101", 9 - k, 2.54, label=dst)
        net(nar, "SW101", k)
        net(dst, "SW101", 9 - k)
    s.note("Width by value: 100k ~25%, 82k ~30%, 68k ~35%.", 25.4, yd + 22.86, 1.0)
    s.block_end()

    # ---- Выход
    s.block_start("LINE OUT")
    s.place("J101", "Connector_Audio:AudioJack3", "LINE OUT (PJ307?)", 177.8, 203.2, mirror="y",
            props={"Footprint": ""})
    for p, n in (("T", "JACK_L"), ("R", "JACK_R")):
        s.stub("J101", p, 5.08, label=n)
        net(n, "J101", p)
    s.stub("J101", "S", 5.08, power="GND")
    net("GND", "J101", "S")
    s.block_end()

    # ---- Питание аналога
    s.block_start("ANALOG POWER (from +5V bus, no 12 V) + OP-AMP SUPPLY")
    rail(s, 381.0, 30.48, "+5V", "600R@100MHz", "+5VA",
         [("Device:C_Polarized", "47u", FP_CP), ("Device:C", "100n", FP_C)], flag=True)
    rail(s, 381.0, 55.88, "+5V", "600R@100MHz", "+5VA_1",
         [("Device:C_Polarized", "47u", FP_CP), ("Device:C", "100n", FP_C)], dst_glob=True, flag=True)
    rail(s, 381.0, 81.28, "+5V", "600R@100MHz", "+5VA_2",
         [("Device:C_Polarized", "47u", FP_CP), ("Device:C", "100n", FP_C)], dst_glob=True, flag=True)
    # -5VA: TPS60400 (datasheet: Cin 2.2u, Cfly 1u, Cout 10u), затем RC 10 Ом + 10u
    xt, yt = 406.4, 121.92
    s.place("U103", "Regulator_SwitchedCapacitor:TPS60400DBV", "TPS60400", xt, yt,
            props={"Footprint": "Package_TO_SOT_SMD:SOT-23-5"})
    pin_in, pin_out = s.pin("U103", 2), s.pin("U103", 1)
    pfp, pfn = s.pin("U103", 5), s.pin("U103", 3)
    s.stub("U103", 4, 2.54, power="GND")
    net("GND", "U103", 4)
    xin = pin_in[0] - 10.16
    s.power("+5VA", xin, pin_in[1])
    cin = ref("C")
    s.wire((xin, pin_in[1]), (pin_in[0] - 5.08, pin_in[1]))
    vpart(s, cin, "Device:C", "2.2u", pin_in[0] - 5.08, pin_in[1], FP_C)
    s.stub(cin, 2, 2.54, power="GND")
    s.wire((pin_in[0] - 5.08, pin_in[1]), pin_in)
    net("+5VA", "U103", 2); net("+5VA", cin, 1); net("GND", cin, 2)
    cf = ref("C")                                         # летающий: ножка 1 - на CFLY+, ножка 2 - на CFLY-
    xc = pfp[0] + 7.62
    s.place(cf, "Device:C", "1u", xc, round(pfp[1] + 1.27, 2), rot=0, props={"Footprint": FP_C})
    q1, q2 = s.pin(cf, 1), s.pin(cf, 2)
    s.wire(pfp, (xc - 2.54, pfp[1]), (xc - 2.54, q1[1]), q1)
    s.wire(pfn, q2)
    net("CFLY_P", "U103", 5); net("CFLY_P", cf, 1)
    net("CFLY_N", "U103", 3); net("CFLY_N", cf, 2)
    x1 = pin_out[0] + 15.24
    s.wire(pin_out, (x1, pin_out[1]))
    co = ref("C")
    vpart(s, co, "Device:C", "10u", x1, pin_out[1], FP_C08)
    s.stub(co, 2, 2.54, power="GND")
    rr = ref("R")
    r1, r2 = hpart(s, rr, "Device:R", "10", x1 + 7.62, pin_out[1], FP_R)
    s.wire((x1, pin_out[1]), r1)
    x2 = r2[0] + 3.81
    s.wire(r2, (x2, pin_out[1]))
    c2 = ref("C")
    vpart(s, c2, "Device:C", "10u", x2, pin_out[1], FP_C08)
    s.stub(c2, 2, 2.54, power="GND")
    s.wire((x2, pin_out[1]), (x2 + 7.62, pin_out[1]))
    s.power("-5VA", x2 + 7.62, pin_out[1])
    s.wire((x2 + 5.08, pin_out[1]), (x2 + 5.08, pin_out[1] - 5.08))
    s.power("PWR_FLAG", x2 + 5.08, pin_out[1] - 5.08)
    net("NEG_RAW", "U103", 1); net("NEG_RAW", co, 1); net("GND", co, 2)
    net("NEG_RAW", rr, 1); net("-5VA", rr, 2); net("-5VA", c2, 1); net("GND", c2, 2)
    s.note("+5VA_1 / +5VA_2: own filter per YM (pin 21 YM2203, pin 1 YM3014B).\n"
           "No 5 V LDO possible from the 5 V bus - filters only; spare pads for a 2nd RC stage.", 381.0, 142.24, 1.0)
    # ---- Питание ОУ (тот же блок): 100n у каждой ножки
    s.place("U101", "Amplifier_Operational:NJM4580", "NJM4580", 393.7, 165.1, unit=3, props={"Footprint": FP_OA})
    s.stub("U101", 8, 2.54, power="+5VA", unit=3)
    s.stub("U101", 4, 2.54, power="-5VA", unit=3)
    net("+5VA", "U101", 8); net("-5VA", "U101", 4)
    for k, (top, bot) in enumerate((("+5VA", "GND"), ("GND", "-5VA"))):
        r = ref("C")
        vpart(s, r, "Device:C", "100n", 408.94 + k * 10.16, 160.02, FP_C)
        s.stub(r, 1, 2.54, power=top)
        s.stub(r, 2, 2.54, power=bot)
        net(top, r, 1); net(bot, r, 2)
    s.note("Op-amp NJM4580 (or NE5532, same pinout) on +-5 V, 100n at V+ and V-.", 381.0, 180.34, 1.0)
    s.block_end()

    s.arrange(relieve=False)                          # координаты - как нарисованы (relief двигал строки)
    return s


def main():
    d = Design(HERE, "mixer", title="ZX Trinity - mixer", rev="A", company="ZX Trinity")
    build(d)
    d.write(force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    d.erc()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
