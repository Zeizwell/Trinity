#!/usr/bin/env python3
# Блок GS платы Тринити по образцу TURBO SOUND FM (MultiSound) на том же листе: блок обведён шиной, всё, что идёт в
# другие блоки, выходит на неё вводами; метка - у ввода, текст к детали (шрифт и выравнивание - как у образца, их
# приводит merge_into_root.py); внутри (RP-GS -> PCM5102, RP-GS -> microSD) - прямые провода.
# RP-GS - Spotpear RP2350B-MINI-A; ножки (переложены 2026-10-25 под расстановку на плате; в прошивке - переменные):
#   слева (смотрит на RP-AY, низ - к CPLD) -> шина блока: GP0-5 GS_C0-5, GP6 GS_TX / GP7 GS_RX (UART к RP-AY - ножки
#     напротив её GP45 RX / GP44 TX со сдвигом на одну: точно напротив всегда одно направление; GP6 / GP7 - UART1
#     через доп. функцию UART RP2350 - сверить по даташиту до прошивки), GP8 GS_WR, GP9-16 GS_D0-7 (подряд, для PIO),
#     GP17 GS_RD, GP18 GS_RDY, GP21 CPLD_CLK, GP25 AY_RUN (у площадки R RP-AY); GP19 (PSRAM), GP24 (VBUS), GP22-23 - нет;
#   справа: I2S GP40 BCK / GP41 LRCK / GP42 DIN (33 Ом), GP43 XSMT (1к); SD GP32 CS / GP31 MOSI / GP30 SCK / GP28 MISO
#     (33 Ом на SCK / MOSI - линия до SD длинная); JTAG CPLD GP44 TCK / GP45 TMS / GP46 TDI / GP47 TDO; остальное - J6.
#   python gs_gen.py -> gs.kicad_sch + сверка netlist; вставка в корень - merge_into_root.py
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C08 = "Capacitor_SMD:C_0805_2012Metric"
U = "U5"
RX, RY = 101.6, 76.2                                      # RP-GS
BL, BR, BT, BB = 73.66, 284.48, 58.42, 172.72            # шина вокруг блока: левый, правый край, верх, низ
NETS, NO_CONNECT = {}, []
_n = {"R": 201, "C": 201}


def ref(k):
    r = f"{k}{_n[k]}"
    _n[k] += 1
    return r


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def nc(s, r, p):
    s.nc(r, p)
    NO_CONNECT.append((r, str(p)))


def lab_stub(s, r, p, n, length=7.62):
    s.stub(r, p, length, label=n)
    net(n, r, p)


def to_left_bus(s, p, n):
    """Ножка левого края RP -> провод -> ввод на левую шину; метка у ввода, текст к ножке (как Y1D0 у образца)."""
    px, py = s.pin(U, p)
    s.wire((px, py), (BL + 2.54, py))
    s.bus_entry(BL + 2.54, py, -2.54, -2.54)
    s.label(n, BL + 2.54, py)
    net(n, U, p)


def to_right_bus(s, r, p, n):
    """Ножка -> провод -> ввод на правую шину; метка у ввода, текст к ножке."""
    px, py = s.pin(r, p)
    s.wire((px, py), (BR - 2.54, py))
    s.bus_entry(BR - 2.54, py, 2.54, -2.54)
    s.label(n, BR - 2.54, py, angle=180)
    net(n, r, p)


def route(s, a, b, xj):
    """Провод из a в b: вправо до xj, по вертикали, вправо до b."""
    pts = [a]
    if abs(a[1] - b[1]) > 0.01:
        pts += [(xj, a[1]), (xj, b[1])]
    pts.append(b)
    s.wire(*pts)


def build(d):
    s = d.sheet("gs.kicad_sch", "GS: RP-GS + PCM5102 + microSD", paper="A3",
                comment="TURBO SOUND FM style; refs U5, U8, J5, J6, D1, R/C from 201")
    s.place(U, "Trinity:RP2350B-MINI-A", "RP-GS", RX, RY, props={"Footprint": "trinity:rp2350b"})

    # ---- левый край -> шина блока (к CPLD)
    left = [(f"GP{i}", f"GS_C{i}") for i in range(6)] + [("GP6", "GS_TX"), ("GP7", "GS_RX"), ("GP8", "GS_WR")] + \
           [(f"GP{9 + i}", f"GS_D{i}") for i in range(8)] + \
           [("GP17", "GS_RD"), ("GP18", "GS_RDY"), ("GP21", "CPLD_CLK"), ("GP25", "AY_RUN")]
    for p, n in left:
        to_left_bus(s, p, n)
    for p in ("GP19", "GP24", "GP22", "GP23"):
        nc(s, U, p)

    # ---- низ: земли модуля; RUN своей RP, светодиод, торец без дыр - не используем
    g1, g2 = s.pin(U, "G1"), s.pin(U, "G2")              # обе земли - скобой вниз, один символ
    s.wire(g1, (g1[0], g1[1] + 2.54), (g2[0], g2[1] + 2.54), g2)
    s.power("GND", g1[0], g1[1] + 2.54)
    net("GND", U, "G1"); net("GND", U, "G2")
    for p in ("R", "GP20", "GP26", "GP27"):
        nc(s, U, p)

    # ---- правый край, верх: 5V через Шоттки, земля; 3V3 модуля - питание SD (3V3_GS); EN - нет
    p5 = s.pin(U, "5V")
    s.place("D1", "Device:D_Schottky", "SS14", p5[0] + 5.08, p5[1], rot=0, props={"Footprint": "Diode_SMD:D_SMA"})
    k, a = s.pin("D1", 1), s.pin("D1", 2)
    s.wire(p5, k)
    s.wire(a, (a[0] + 1.27, a[1]))
    s.power("+5V", a[0] + 1.27, a[1])
    net("GS_5V", U, "5V"); net("GS_5V", "D1", 1); net("+5V", "D1", 2)
    g = s.pin(U, "GND")
    s.wire(g, (g[0] + 15.24, g[1]))                      # земля - правее метки 3V3_GS (символ вниз)
    s.power("GND", g[0] + 15.24, g[1])
    net("GND", U, "GND")
    nc(s, U, "EN")
    v3 = s.pin(U, "3V3")
    s.wire(v3, (v3[0] + 5.08, v3[1]))
    s.label("3V3_GS", v3[0] + 5.08, v3[1])
    net("3V3_GS", U, "3V3")

    # ---- запасные справа -> гребёнка J6 (метки); связь с RP-AY - метками
    spare = ["GP39", "GP38", "GP37", "GP36", "GP35", "GP34", "GP33", "GP29"]   # как идут по краю модуля сверху вниз
    for p in spare:
        lab_stub(s, U, p, f"GS_{p}", 5.08)
    for p, n in (("GP44", "JTAG_TCK"), ("GP45", "JTAG_TMS"), ("GP46", "JTAG_TDI"), ("GP47", "JTAG_TDO")):
        lab_stub(s, U, p, n, 5.08)

    # ---- I2S: RP -> 33 Ом -> PCM5102 прямыми проводами; резисторы - рядами через 7.62 (надписи не на проводах)
    XP, YP = 228.6, 101.6                                # PCM: LRCK на высоте GP41
    s.place("U8", "Trinity:PCM5102", "GY-PCM5102", XP, YP, props={"Footprint": "trinity:PCM5102"})
    RC = 175.26                                          # колонка резисторов I2S
    i2s = [("GP42", "I2S_DIN", 4, 91.44, 147.32, 200.66),     # ножка, сигнал, ножка PCM, ряд, излом у RP, у PCM
           ("GP41", "I2S_LCK", 3, 101.6, 0, 0),
           ("GP40", "I2S_BCK", 5, 111.76, 152.4, 205.74)]
    for p, n, pp, ry, xa, xb in i2s:
        r = ref("R")
        s.place(r, "Device:R", "33", RC, ry, rot=90, props={"Footprint": FP_R})
        route(s, s.pin(U, p), s.pin(r, 1), xa)
        route(s, s.pin(r, 2), s.pin("U8", pp), xb)
        net(f"{p}_{r}", U, p); net(f"{p}_{r}", r, 1); net(n, r, 2); net(n, "U8", pp)
    lab_stub(s, U, "GP43", "GS_XSMT", 5.08)              # XSMT: 1к - у PCM
    # PCM: VIN вверх к +5VA, GND слева - символом, GND справа - перемычкой вниз, выходы - на правую шину
    v1 = s.pin("U8", 1)
    s.wire(v1, (v1[0] - 2.54, v1[1]), (v1[0] - 2.54, v1[1] - 5.08))
    s.power("+5VA", v1[0] - 2.54, v1[1] - 5.08)
    net("+5VA", "U8", 1)
    s.stub("U8", 2, 5.08, power="GND")
    net("GND", "U8", 2)
    g16, g18 = s.pin("U8", 16), s.pin("U8", 18)
    gx = g16[0] + 2.54
    s.wire(g16, (gx, g16[1]))
    s.wire((gx, g16[1]), (gx, g18[1]))
    s.wire(g18, (gx, g18[1]))
    s.wire((gx, g18[1]), (gx, YP + 25.4))
    s.power("GND", gx, YP + 25.4)
    net("GND", "U8", 16); net("GND", "U8", 18)
    to_right_bus(s, "U8", 19, "GS_L")                    # L / R гнезда модуля: выход карты (и гребёнка J2)
    to_right_bus(s, "U8", 17, "GS_R")
    xs = s.pin("U8", 13)                                  # XSMT: ножка -> 1к -> метка GS_XSMT (ножка RP)
    r = ref("R")
    s.place(r, "Device:R", "1k", xs[0] + 17.78, xs[1], rot=90, props={"Footprint": FP_R})
    s.wire(xs, s.pin(r, 1))
    r2 = s.pin(r, 2)
    s.wire(r2, (r2[0] + 10.16, r2[1]))
    s.label("GS_XSMT", r2[0] + 10.16, r2[1], angle=180)
    net("DAC_XSMT", "U8", 13); net("DAC_XSMT", r, 1); net("GS_XSMT", r, 2)
    r = ref("R")                                          # 10к на землю (DNP), висит на линии XSMT у ножки
    s.place(r, "Device:R", "10k", xs[0] + 5.08, xs[1] - 3.81, props={"Footprint": FP_R}, dnp=True)
    s.stub(r, 1, 2.54, power="GND")
    net("GND", r, 1); net("DAC_XSMT", r, 2)
    for pn in (6, 11, 12, 14, 15):
        nc(s, "U8", pn)

    # ---- microSD (GCT MEM2055, как ZXEVO 1010), SPI: прямые провода; подтяжки 10к к 3V3_GS на своих линиях
    XJ, YJ = 213.36, 139.7                                # ниже PCM; линии - лесенкой (нижняя поворачивает первой)
    s.place("J5", "Connector:Micro_SD_Card_Det2", "MEM2055-00-190-01-A", XJ, YJ,
            props={"Footprint": "trinity:GCT-MEM2055-00-190-01-A"})
    for p, n, pp, xj in (("GP32", "SD_CS", 2, 162.56), ("GP28", "SD_MISO", 7, 142.24)):
        route(s, s.pin(U, p), s.pin("J5", pp), xj)
        net(n, U, p); net(n, "J5", pp)
    for r, p, n, pp, xj in (("R212", "GP31", "SD_MOSI", 3, 157.48), ("R213", "GP30", "SD_SCK", 5, 152.4)):
        a, b = s.pin(U, p), s.pin("J5", pp)               # 33 Ом у RP - на вертикальном участке лесенки
        s.place(r, "Device:R", "33", xj, (a[1] + b[1]) / 2, props={"Footprint": FP_R})
        s.wire(a, (xj, a[1]), s.pin(r, 1))
        s.wire(s.pin(r, 2), (xj, b[1]), b)
        net(f"{n}_RP", U, p); net(f"{n}_RP", r, 1); net(n, r, 2); net(n, "J5", pp)
    pulls = [(1, "SD_DAT2", 182.88, -1), (2, "SD_CS", 175.26, -1), (7, "SD_MISO", 165.1, 1), (8, "SD_DAT1", 182.88, 1)]
    for pn, n, x, up in pulls:                            # up -1 - резистор вверх, 1 - вниз
        jx, jy = s.pin("J5", pn)
        if pn in (1, 8):                                  # DAT1 / DAT2 - только подтяжка
            s.wire((x, jy), (jx, jy))
            net(n, "J5", pn)
        r = ref("R")
        s.place(r, "Device:R", "10k", x, jy + up * 3.81, props={"Footprint": FP_R})
        q1, q2 = s.pin(r, 1), s.pin(r, 2)
        far = q1 if up < 0 else q2
        s.wire(far, (far[0], far[1] + up * 2.54))
        s.label("3V3_GS", far[0], far[1] + up * 2.54, angle=90 if up < 0 else 270)
        net("3V3_GS", r, 1 if up < 0 else 2); net(n, r, 2 if up < 0 else 1)
    v4 = s.pin("J5", 4)
    s.wire(v4, (v4[0] - 2.54, v4[1]))
    s.label("3V3_GS", v4[0] - 2.54, v4[1], angle=180)
    net("3V3_GS", "J5", 4)
    s.stub("J5", 6, 5.08, power="GND")
    net("GND", "J5", 6)
    s.stub("J5", "SH", 2.54, power="GND")
    net("GND", "J5", "SH")
    for pn in (9, 10):                                    # датчик карты не используем (карту видно по инициализации)
        nc(s, "J5", pn)
    for k2, (v, fp) in enumerate((("100n", FP_C), ("10u", FP_C08))):   # развязка SD
        c = ref("C")
        s.place(c, "Device:C", v, 248.92 + k2 * 12.7, 129.54, props={"Footprint": fp})
        s.stub(c, 1, 2.54, label="3V3_GS")
        s.stub(c, 2, 2.54, power="GND")
        net("3V3_GS", c, 1); net("GND", c, 2)

    # ---- гребёнка J6: запасные ножки RP-GS
    s.place("J6", "Connector_Generic:Conn_01x10", "SPARE", 271.78, 76.2,
            props={"Footprint": "Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical"})
    # порядок контактов - как ножки идут по правому краю модуля сверху вниз (линии на плате не перекрещиваются)
    for k2, p in enumerate(spare, 1):
        lab_stub(s, "J6", k2, f"GS_{p}", 5.08)
    lab_stub(s, "J6", 9, "AY_GP47", 5.08)                # единственная свободная ножка RP-AY
    j = s.pin("J6", 10)                                   # земля гребёнки - уголком вниз, мимо меток
    s.wire(j, (j[0] - 2.54, j[1]), (j[0] - 2.54, j[1] + 2.54))
    s.power("GND", j[0] - 2.54, j[1] + 2.54)
    net("GND", "J6", 10)

    # AY + FM с микшера (SND_L / SND_R) -> 1к -> гнездо модуля: там уже GS (ЦАП через 470 Ом модуля);
    # нагрузка ЦАП 470 + 1к = 1.47 кОм (даташит PCM5102A: не меньше 1 кОм)
    for r, pp, n, dy in (("R210", 19, "SND_L", 1), ("R211", 17, "SND_R", -1)):   # номера - после старых R201..R209
        x = 271.78
        y = s.pin("U8", pp)[1]
        s.place(r, "Device:R", "1k", x, y + dy * 3.81, props={"Footprint": FP_R})
        far = s.pin(r, 2) if dy > 0 else s.pin(r, 1)
        s.wire(far, (BR - 2.54, far[1]))
        s.bus_entry(BR - 2.54, far[1], 2.54, -2.54)
        s.label(n, BR - 2.54, far[1], angle=180)
        g_ = "GS_L" if pp == 19 else "GS_R"
        net(g_, r, 1 if dy > 0 else 2); net(n, r, 2 if dy > 0 else 1)
    # ---- шина вокруг блока и заголовок (как TURBO SOUND FM)
    s.bus((BL, BT), (BR, BT), (BR, BB), (BL, BB), (BL, BT))
    s.texts.append(("GS", BL + 1.27, BT - 0.635, 2.0066, "left bottom", False))
    return s


def main():
    d = Design(HERE, "gs", title="ZX Trinity - GS", rev="A", company="ZX Trinity")
    build(d)
    d.write(center=False, force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    if "--text" in sys.argv:
        d.check_text(show=40)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
