#!/usr/bin/env python3
# Блок GS платы Тринити: RP-GS (Spotpear RP2350B-MINI-A) + PCM5102 + microSD (GCT MEM2055) + JTAG CPLD + связь с RP-AY.
# Ножки - таблица RP-GS (согласовано 2026-10-25; при разводке платы можно переставить). Стиль - как TURBO SOUND FM:
# шины к CPLD (GS_D[0..7], GS_C[0..5]) с вводами и метками, одиночные сигналы - метками.
#   python gs_gen.py -> gs.kicad_sch + сверка netlist; вставка в корень - merge_into_root.py
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_C08 = "Capacitor_SMD:C_0805_2012Metric"
U = "U5"                                                  # RP-GS
NETS, NO_CONNECT = {}, []
_n = {"R": 201, "C": 201}


def ref(k):
    r = f"{k}{_n[k]}"
    _n[k] += 1
    return r


def net(name, r, pin):
    NETS.setdefault(name, []).append((r, str(pin)))


def st(s, r, pin, name, length=5.08, power=False):
    if power:
        s.stub(r, pin, length, power=name)
    else:
        s.stub(r, pin, length, label=name)
    net(name, r, pin)


def nc(s, r, pin):
    s.nc(r, pin)
    NO_CONNECT.append((r, str(pin)))


def series(s, r_pin, r, val, out_name, fp=FP_R, stagger=0):
    """Ножка RP (правый край) -> резистор последовательно -> метка. stagger 1 - резистор дальше (соседние ножки)."""
    px, py = s.pin(U, r_pin)
    s.place(r, "Device:R", val, px + 7.62 + stagger * 12.7, py, rot=90, props={"Footprint": fp})
    a, b = s.pin(r, 1), s.pin(r, 2)
    s.wire((px, py), a)
    s.wire(b, (b[0] + 5.08, py))
    s.label(out_name, b[0] + 5.08, py)
    net(f"{r_pin}_{r}", U, r_pin); net(f"{r_pin}_{r}", r, 1); net(out_name, r, 2)


def build(d):
    s = d.sheet("gs.kicad_sch", "GS: RP-GS + PCM5102 + microSD + CPLD JTAG", paper="auto",
                comment="RP-GS pin table 2026-10-25; refs U5, U8, J5, J6, D1, R/C from 201")

    s.block_start("GS: RP-GS (RP2350B-MINI-A) + PCM5102 + microSD")
    s.place(U, "Trinity:RP2350B-MINI-A", "RP-GS", 101.6, 76.2, props={"Footprint": "trinity:rp2350b"})
    # ---- левый край -> CPLD: шины данных и кода, стробы, такт, JTAG
    s.pins_to_bus(U, [f"GP{i}" for i in range(8)], "GS_D[0..7]", toward="up")
    for i in range(8):
        net(f"GS_D{i}", U, f"GP{i}")
    s.pins_to_bus(U, [f"GP{i}" for i in range(10, 16)], "GS_C[0..5]", toward="down")
    for i in range(6):
        net(f"GS_C{i}", U, f"GP{10 + i}")
    for p, n in (("GP8", "GS_WR"), ("GP9", "GS_RD"), ("GP16", "GS_RDY"), ("GP17", "JTAG_TCK"),
                 ("GP18", "JTAG_TMS"), ("GP21", "CPLD_CLK"), ("GP22", "JTAG_TDI"), ("GP23", "JTAG_TDO")):
        st(s, U, p, n, 10.16)
    for p in ("GP19", "GP24"):                            # PSRAM CS / контроль VBUS на модуле
        nc(s, U, p)
    # ---- правый край: SD, RUN и UART к RP-AY, I2S к PCM5102
    for p, n in (("GP28", "SD_MISO"), ("GP29", "SD_CS"), ("GP30", "SD_SCK"), ("GP31", "SD_MOSI"),
                 ("GP32", "SD_DET"), ("GP33", "AY_RUN"), ("GP36", "GS_TX"), ("GP37", "GS_RX")):
        st(s, U, p, n, 10.16)
    for k, (p, n) in enumerate((("GP40", "I2S_BCK"), ("GP41", "I2S_LCK"), ("GP42", "I2S_DIN"))):
        series(s, p, ref("R"), "33", n, stagger=k % 2)    # 33 Ом у источника (чек-лист: I2S)
    series(s, "GP43", ref("R"), "1k", "DAC_XSMT", stagger=1)
    # ---- запасные ножки -> гребёнка J6
    spare = ["GP25", "GP34", "GP35", "GP38", "GP39", "GP44", "GP45", "GP46", "GP47"]
    for p in spare:
        st(s, U, p, f"GS_{p}", 7.62)
    # ---- не используем: светодиод модуля, торец без дыр, EN модуля, RUN самой RP-GS
    for p in ("GP20", "GP26", "GP27", "EN", "R"):
        nc(s, U, p)
    # ---- питание модуля: 5V через Шоттки (USB модуля не замыкается с 5 В Евы), земли
    s.place("D1", "Device:D_Schottky", "SS14", 152.4, 63.5, rot=180,
            props={"Footprint": "Diode_SMD:D_SMA"})
    pk, pa = s.pin("D1", 1), s.pin("D1", 2)               # 1 - катод, 2 - анод
    p5 = s.pin(U, "5V")
    s.wire(p5, (pk[0], p5[1]), pk)
    s.stub("D1", 2, 5.08, power="+5V")
    net("GS_5V", U, "5V"); net("GS_5V", "D1", 1); net("+5V", "D1", 2)
    g1 = s.pin(U, "GND")                                  # GND: у символа две ножки с одним номером - для KiCad
    s.wire(g1, (g1[0] + 5.08, g1[1]))                     #   это один вывод; провод - к первой
    s.power("GND", g1[0] + 5.08, g1[1])
    net("GND", U, "GND")
    st(s, U, "G1", "GND", 2.54, power=True)
    st(s, U, "G2", "GND", 2.54, power=True)
    st(s, U, "3V3", "3V3_GS", 10.16)
    s.note("D1: anode - digital +5V of the board, cathode - module 5V (its USB-C VBUS is tied to 5V).\n"
           "3V3_GS: module LDO output - SD card supply and pull-ups.\n"
           "Pins may be reassigned at PCB layout; firmware pins are variables (config.h).", 60.96, 165.1, 1.0)

    # ---- PCM5102 (модуль GY-PCM5102)
    s.place("U8", "Trinity:PCM5102", "GY-PCM5102", 203.2, 76.2, props={"Footprint": "trinity:PCM5102"})
    st(s, "U8", 1, "+5VA", 2.54, power=True)
    for p in (2, 16, 18):
        st(s, "U8", p, "GND", 2.54, power=True)
    st(s, "U8", 5, "I2S_BCK"); st(s, "U8", 3, "I2S_LCK"); st(s, "U8", 4, "I2S_DIN")
    st(s, "U8", 13, "DAC_XSMT")
    st(s, "U8", 19, "GS_L"); st(s, "U8", 17, "GS_R")
    for p in (6, 11, 12, 14, 15):                         # SCK / FLT / DEMP / FMT - перемычками модуля
        nc(s, "U8", p)
    r = ref("R")
    s.place(r, "Device:R", "10k", 228.6, 101.6, props={"Footprint": FP_R}, dnp=True)
    st(s, r, 1, "DAC_XSMT", 2.54); st(s, r, 2, "GND", 2.54, power=True)
    s.note("Module jumpers: SCK->GND, FLT/DEMP/FMT->L; XSMT jumper OPEN (driven by RP-GS).\n"
           "10k DNP: XSMT pull-down - muted until RP-GS is up.", 182.88, 111.76, 1.0)

    # ---- microSD (GCT MEM2055, как у ZXEVO 1010), режим SPI; подтяжки 10к к 3V3_GS
    s.place("J5", "Connector:Micro_SD_Card_Det2", "MEM2055-00-190-01-A", 203.2, 152.4,
            props={"Footprint": "trinity:GCT-MEM2055-00-190-01-A"})
    st(s, "J5", 2, "SD_CS"); st(s, "J5", 3, "SD_MOSI"); st(s, "J5", 5, "SD_SCK"); st(s, "J5", 7, "SD_MISO")
    st(s, "J5", 1, "SD_DAT2"); st(s, "J5", 8, "SD_DAT1"); st(s, "J5", 10, "SD_DET")
    st(s, "J5", 4, "3V3_GS")
    for p in (6, 9, "SH"):
        st(s, "J5", p, "GND", 2.54, power=True)
    for k, n in enumerate(("SD_CS", "SD_MISO", "SD_DAT1", "SD_DAT2", "SD_DET")):
        r = ref("R")
        s.place(r, "Device:R", "10k", 241.3 + k * 7.62, 134.62, props={"Footprint": FP_R})
        st(s, r, 1, "3V3_GS", 2.54); st(s, r, 2, n, 2.54)
    for k, (v, fp) in enumerate((("100n", FP_C), ("10u", FP_C08))):
        c = ref("C")
        s.place(c, "Device:C", v, 241.3 + k * 7.62, 170.18, props={"Footprint": fp})
        st(s, c, 1, "3V3_GS", 2.54); st(s, c, 2, "GND", 2.54, power=True)

    # ---- JTAG CPLD: 1к на TCK к земле (MAX3000A), гребёнка для бластера (запас)
    r = ref("R")
    s.place(r, "Device:R", "1k", 40.64, 172.72, props={"Footprint": FP_R})
    st(s, r, 1, "JTAG_TCK", 2.54); st(s, r, 2, "GND", 2.54, power=True)
    s.place("J6", "Connector_Generic:Conn_01x10", "GS SPARE", 279.4, 76.2,
            props={"Footprint": "Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical"})
    for k, p in enumerate(spare, 1):
        st(s, "J6", k, f"GS_{p}", 5.08)
    st(s, "J6", 10, "GND", 2.54, power=True)
    s.note("CPLD firmware without a blaster: RP-GS plays an SVF file (Quartus -> SVF) on JTAG_* from SD or USB.\n"
           "1k TCK pull-down per MAX3000A datasheet.", 30.48, 182.88, 1.0)
    s.block_end()
    s.arrange(relieve=False)
    return s


def main():
    d = Design(HERE, "gs", title="ZX Trinity - GS", rev="A", company="ZX Trinity")
    build(d)
    d.write(force="--force" in sys.argv, relieve=False)
    ok = d.verify(NETS, NO_CONNECT)
    d.erc()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
