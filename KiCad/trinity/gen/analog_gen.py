#!/usr/bin/env python3
# Аналоговая часть платы Тринити - лист analog.kicad_sch (генератор, schlib из kicad-claude-skills).
# Спецификация - FT2 doc/trinity-board-analog.md. Обозначения с 101 - не пересекаются с корневым листом.
# Связь с корнем - глобальными метками: AY_A1..AY_C2 (выходы SSG с нагрузкой 3.3к - в блоке чипов),
# FM1 / FM2 (буферы ЦАП FM, DNP), I2S_BCK / I2S_LCK / I2S_DIN / DAC_XSMT (от RP-GS), +5VA_1 / +5VA_2 (питание YM).
#   python analog_gen.py        -> gen/analog.kicad_sch, проверка netlist (KiCad) и ERC
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

NETS = {}            # net -> [(ref, pin)] - что должно получиться (сверка с netlist KiCad)
NO_CONNECT = []


def net(name, ref, pin):
    NETS.setdefault(name, []).append((ref, str(pin)))


def glob(s, name):
    """Последняя метка листа - глобальная (связь с корнем по имени)."""
    k = s.labels[-1]
    assert k[1] == name, (k, name)
    s.labels[-1] = ("global_label",) + tuple(k[1:6]) + ("passive",)


def st(s, ref, pin, name, unit=None, length=2.54, glob_label=False, power=False):
    """Ножка -> короткий провод -> метка / глобальная метка / порт питания; в NETS."""
    if power:
        s.stub(ref, pin, length, power=name, unit=unit)
    else:
        s.stub(ref, pin, length, label=name, unit=unit)
        if glob_label:
            glob(s, name)
    net(name, ref, pin)


def two(s, ref, lib, value, x, y, a, b, fp, dnp=False, ga=False, gb=False, pa=False, pb=False):
    """Двухвыводная деталь вертикально: ножка 1 вверх -> a, ножка 2 вниз -> b."""
    s.place(ref, lib, value, x, y, props={"Footprint": fp}, dnp=dnp)
    st(s, ref, 1, a, glob_label=ga, power=pa)
    st(s, ref, 2, b, glob_label=gb, power=pb)


def build(d):
    s = d.sheet("analog.kicad_sch", "Analog: mixer, narrow stereo, GS DAC, analog power", paper="auto",
                comment="FT2 doc/trinity-board-analog.md; refs from 101")

    # ---------------- Питание аналога ----------------
    s.block_start("ANALOG POWER (from +5V bus, no 12 V)")
    x = 20.32
    # +5VA: феррит + 47u + 100n
    two(s, "FB101", "Device:FerriteBead", "600R@100MHz", x, 30.48, "+5V", "+5VA", FP_FB, pa=True, pb=True)
    two(s, "C101", "Device:C_Polarized", "47u", x + 10.16, 30.48, "+5VA", "GND", FP_CP, pa=True, pb=True)
    two(s, "C102", "Device:C", "100n", x + 20.32, 30.48, "+5VA", "GND", FP_C, pa=True, pb=True)
    s.power("PWR_FLAG", x + 30.48, 25.4)
    s.wire((x + 30.48, 25.4), (x + 30.48, 30.48))
    s.power("+5VA", x + 30.48, 30.48)
    # -5VA: TPS60400 (инвертор), 1u летающий, 2.2u вход, 10u выход, затем RC 10 Ом + 10u
    s.place("U103", "Regulator_SwitchedCapacitor:TPS60400DBV", "TPS60400", x + 10.16, 60.96,
            props={"Footprint": "Package_TO_SOT_SMD:SOT-23-5"})
    st(s, "U103", 2, "+5VA", power=True)
    st(s, "U103", 4, "GND", power=True)
    st(s, "U103", 5, "CFLY_P")
    st(s, "U103", 3, "CFLY_N")
    st(s, "U103", 1, "NEG_RAW")
    two(s, "C103", "Device:C", "1u", x + 30.48, 60.96, "CFLY_P", "CFLY_N", FP_C)
    two(s, "C104", "Device:C", "2.2u", x + 40.64, 60.96, "+5VA", "GND", FP_C, pa=True, pb=True)
    two(s, "C105", "Device:C", "10u", x + 50.8, 60.96, "GND", "NEG_RAW", FP_C08, pa=True)
    s.place("R101", "Device:R", "10", x + 60.96, 60.96, props={"Footprint": FP_R})
    st(s, "R101", 1, "NEG_RAW")
    st(s, "R101", 2, "-5VA", power=True)
    two(s, "C106", "Device:C", "10u", x + 71.12, 60.96, "GND", "-5VA", FP_C08, pa=True, pb=True)
    s.power("PWR_FLAG", x + 81.28, 66.04)
    s.wire((x + 81.28, 66.04), (x + 81.28, 71.12))
    s.power("-5VA", x + 81.28, 71.12, rot=180)
    # +5VA_1 / +5VA_2: свой фильтр каждому YM (ножка 21 YM2203, ножка 1 YM3014B - в блоке чипов)
    for i, (fb, cb, cs) in enumerate((("FB102", "C107", "C108"), ("FB103", "C109", "C110"))):
        rail = f"+5VA_{i + 1}"
        y = 91.44 + i * 22.86
        two(s, fb, "Device:FerriteBead", "600R@100MHz", x, y, "+5V", rail, FP_FB, pa=True, gb=True)
        two(s, cb, "Device:C_Polarized", "47u", x + 15.24, y, rail, "GND", FP_CP, ga=True, pb=True)
        two(s, cs, "Device:C", "100n", x + 25.4, y, rail, "GND", FP_C, ga=True, pb=True)
    s.note("TPS60400: Cfly 1u, Cin 2.2u, Cout 10u (datasheet); -5VA after RC 10R + 10u.\n"
           "+5VA_1 / +5VA_2 - one filter per YM (MultiSound Max idea, without 12 V).\n"
           "Op-amp decoupling: 100n at V+ and V- pins (mixer block).", x, 139.7, 1.0)
    s.block_end()

    # ---------------- Входы SSG, узкое стерео, FM, GS ----------------
    s.block_start("MIXER INPUTS: SSG (MultiSound values) + NARROW STEREO DIP + FM (DNP) + GS")
    srcs = [("AY_A1", "L", "R"), ("AY_B1", "B", None), ("AY_C1", "R", "L"),
            ("AY_A2", "L", "R"), ("AY_B2", "B", None), ("AY_C2", "R", "L")]
    rn, cn = 102, 111
    sw_pin = 1                                           # DIP-4: контакт k = ножки k и 9-k
    x0 = 139.7
    for i, (name, side, narrow) in enumerate(srcs):
        xx = x0 + i * 25.4
        ac = f"{name}_AC"
        # 10u: + к YM (у выхода SSG постоянка выше нуля), - к сумматору
        two(s, f"C{cn}", "Device:C_Polarized", "10u", xx, 30.48, name, ac, FP_CP, ga=True)
        cn += 1
        if side == "B":
            two(s, f"R{rn}", "Device:R", "47k", xx, 50.8, ac, "SUM_L", FP_R); rn += 1
            two(s, f"R{rn}", "Device:R", "47k", xx + 10.16, 50.8, ac, "SUM_R", FP_R); rn += 1
            continue
        main = "SUM_L" if side == "L" else "SUM_R"
        cross = "SUM_R" if side == "L" else "SUM_L"
        two(s, f"R{rn}", "Device:R", "24k", xx, 50.8, ac, main, FP_R); rn += 1
        nar = f"NAR{sw_pin}"
        two(s, f"R{rn}", "Device:R", "82k", xx + 10.16, 50.8, ac, nar, FP_R); rn += 1
        net(nar, "SW101", sw_pin)
        net(cross, "SW101", 9 - sw_pin)
        sw_pin += 1
    s.place("SW101", "Switch:SW_DIP_x04", "NARROW", x0 + 60.96, 76.2,
            props={"Footprint": "Button_Switch_THT:SW_DIP_SPSTx04_Slide_9.78x12.34mm_W7.62mm_P2.54mm"})
    for k in range(1, 5):
        s.stub("SW101", k, 2.54, label=f"NAR{k}")
        s.stub("SW101", 9 - k, 2.54, label=("SUM_R" if k in (1, 3) else "SUM_L"))
    # FM (DNP): буфер ЦАП FM -> 10u -> 10k в оба сумматора
    for i, fm in enumerate(("FM1", "FM2")):
        xx = x0 + i * 30.48
        two(s, f"C{cn}", "Device:C_Polarized", "10u", xx, 101.6, fm, f"{fm}_AC", FP_CP, dnp=True, ga=True)
        cn += 1
        two(s, f"R{rn}", "Device:R", "10k", xx + 10.16, 101.6, f"{fm}_AC", "SUM_L", FP_R, dnp=True); rn += 1
        two(s, f"R{rn}", "Device:R", "10k", xx + 20.32, 101.6, f"{fm}_AC", "SUM_R", FP_R, dnp=True); rn += 1
    # GS: выходы PCM5102 (около земли) -> 22k
    two(s, f"R{rn}", "Device:R", "22k", x0 + 71.12, 101.6, "GS_L", "SUM_L", FP_R); rn += 1
    two(s, f"R{rn}", "Device:R", "22k", x0 + 81.28, 101.6, "GS_R", "SUM_R", FP_R); rn += 1
    s.note("SSG: load 3.3k + YM in chip block; here 10u + 24k (A, C) / 47k (B) as MultiSound rev.A1.\n"
           "NARROW DIP ON: A -> right, C -> left through 82k (~30%). OFF: plain ABC.\n"
           "Width: 100k ~25%, 82k ~30%, 68k ~35%. FM path DNP on the first board.\n"
           "GS 22k: PCM5102 full scale 2.1 Vrms -> ~1 V out; GS volume digital in RP-GS.",
           x0, 124.46, 1.0)
    s.block_end()

    # ---------------- Сумматоры и выход ----------------
    s.block_start("SUMMING AMPS L / R + LINE OUT")
    xa = 20.32
    for unit, (sum_n, mix, o1, jack, inp, inn, outp) in enumerate(
            (("SUM_L", "MIX_L", "OUTL_R", "JACK_L", 3, 2, 1), ("SUM_R", "MIX_R", "OUTR_R", "JACK_R", 5, 6, 7)), 1):
        y = 160.02 + (unit - 1) * 35.56
        s.place("U101", "Amplifier_Operational:NJM4580", "NJM4580", xa + 15.24, y, unit=unit,
                props={"Footprint": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"})
        st(s, "U101", inp, "GND", unit=unit, power=True)
        st(s, "U101", inn, sum_n, unit=unit)
        st(s, "U101", outp, mix, unit=unit)
        two(s, f"R{rn}", "Device:R", "10k", xa + 35.56, y, sum_n, mix, FP_R); rn += 1
        two(s, f"C{cn}", "Device:C", "470p", xa + 45.72, y, sum_n, mix, FP_C); cn += 1
        two(s, f"R{rn}", "Device:R", "100", xa + 55.88, y, mix, o1, FP_R); rn += 1
        two(s, f"C{cn}", "Device:C_Polarized", "10u", xa + 66.04, y, o1, jack, FP_CP); cn += 1
        two(s, f"R{rn}", "Device:R", "47k", xa + 76.2, y, jack, "GND", FP_R, pb=True); rn += 1
    s.place("U101", "Amplifier_Operational:NJM4580", "NJM4580", xa + 15.24, 241.3, unit=3,
            props={"Footprint": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"})
    st(s, "U101", 8, "+5VA", unit=3, power=True)
    st(s, "U101", 4, "-5VA", unit=3, power=True)
    two(s, f"C{cn}", "Device:C", "100n", xa + 30.48, 241.3, "+5VA", "GND", FP_C, pa=True, pb=True); cn += 1
    two(s, f"C{cn}", "Device:C", "100n", xa + 40.64, 241.3, "GND", "-5VA", FP_C, pa=True, pb=True); cn += 1
    s.place("J101", "Connector_Audio:AudioJack3", "LINE OUT", xa + 101.6, 175.26,
            props={"Footprint": ""})
    st(s, "J101", "T", "JACK_L")
    st(s, "J101", "R", "JACK_R")
    st(s, "J101", "S", "GND", power=True)
    s.note("Op-amp: NJM4580 (or NE5532, same pinout) on +-5 V; inverting summer, Rf 10k || 470p (~34 kHz).\n"
           "Out: 100R -> 10u -> 47k bleeder -> jack (PJ307 like MultiSound; footprint to choose).\n"
           "Keep '-' input nodes short; LM2664/TPS60400 away from the summing nodes.", xa, 261.62, 1.0)
    s.block_end()

    # ---------------- GS: модуль PCM5102 ----------------
    s.block_start("GS DAC: GY-PCM5102 module")
    xp = 139.7
    s.place("U102", "Trinity:PCM5102", "GY-PCM5102", xp + 20.32, 175.26, props={"Footprint": "trinity:PCM5102"})
    st(s, "U102", 1, "+5VA", power=True)
    for p in (2, 16, 18):
        st(s, "U102", p, "GND", power=True)
    st(s, "U102", 5, "I2S_BCK", glob_label=True)
    st(s, "U102", 3, "I2S_LCK", glob_label=True)
    st(s, "U102", 4, "I2S_DIN", glob_label=True)
    st(s, "U102", 13, "XSMT")
    st(s, "U102", 19, "GS_L")
    st(s, "U102", 17, "GS_R")
    for p in (6, 11, 12, 14, 15):                        # SCK, FLT, DEMP, FMT - перемычками модуля; AVDD - выход модуля
        s.nc("U102", p)
        NO_CONNECT.append(("U102", str(p)))
    two(s, f"R{rn}", "Device:R", "1k", xp + 50.8, 175.26, "DAC_XSMT", "XSMT", FP_R, ga=True); rn += 1
    two(s, f"R{rn}", "Device:R", "10k", xp + 60.96, 175.26, "XSMT", "GND", FP_R, dnp=True, pb=True); rn += 1
    s.note("Module jumpers: SCK -> GND, FLT / DEMP / FMT -> L (I2S, normal filter, no de-emphasis).\n"
           "XSMT jumper OPEN: driven by RP-GS (DAC_XSMT); 10k DNP pull-down = muted until RP is up.\n"
           "I2S: 33R series at the RP-GS end (RP-GS sheet). VIN from +5VA (module has its own LDOs).",
           xp, 210.82, 1.0)
    s.block_end()

    s.arrange()
    return s


def main():
    force = "--force" in sys.argv
    d = Design(HERE, "analog", title="ZX Trinity - analog", rev="A", company="ZX Trinity")
    build(d)
    d.write(force=force)
    ok = d.verify(NETS, NO_CONNECT)
    d.erc()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
