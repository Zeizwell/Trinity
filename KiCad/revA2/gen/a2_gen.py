#!/usr/bin/env python3
# Схема рев A2 (один лист, блоки): решения - doc/trinity-config.md п.66-77, ножки - doc/trinity-a2-layout.md.
# Сначала список цепей (NETS - правда), рисунок - по нему, в конце сверка с netlist самого KiCad (verify).
# Микросхемы: каждая ножка - коротким проводом на метку или символ питания; мелочь - столбиками рядом, по меткам.
# Пассив - 0805 (п.75).
#   "C:\Program Files\KiCad\10.0\bin\python.exe" gen/a2_gen.py   (из KiCad/revA2; KiCad со схемой - закрыт)
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from schlib import Design  # noqa: E402

PROJ = os.path.dirname(HERE)
FP_R = "Resistor_SMD:R_0805_2012Metric"
FP_C = "Capacitor_SMD:C_0805_2012Metric"
FP_C12 = "Capacitor_SMD:C_1210_3225Metric"          # 47 мкФ керамика (LM2662: низкое ESR)
FP_CP = "Capacitor_SMD:CP_Elec_5x5.4"                # 47 мкФ электролит
FP_CP6 = "Capacitor_SMD:CP_Elec_6.3x7.7"             # 100 мкФ
FP_L = "Inductor_SMD:L_Bourns_SRN6045TA"            # 27 мкГн, как рев A
POWER = {"+5V", "GND", "+3V3", "+5VA", "-5VA"}      # настоящие символы питания; прочие шины - метками
NETS, NO_CONNECT = {}, []
_n = {"R": 1, "C": 1, "L": 1}


def ref(k):
    r = f"{k}{_n[k]}"
    _n[k] += 1
    return r


def net(n, r, p):
    NETS.setdefault(n, []).append((r, str(p)))


def term(s, r, p, n, length=2.54):
    """Ножка -> провод -> метка / символ питания / NC."""
    if n is None:
        s.nc(r, p)
        NO_CONNECT.append((r, str(p)))
        return
    if n in POWER:
        s.stub(r, p, length, power=n)
    else:
        s.stub(r, p, length, label=n)
    net(n, r, p)


def ic(s, r, lib, val, x, y, fp, conns, rot=0, unit=None):
    """Микросхема: все ножки из conns (номер -> цепь или None = NC)."""
    s.place(r, lib, val, x, y, rot=rot, props={"Footprint": fp}, unit=unit or 1)
    for p, n in conns.items():
        term(s, r, p, n, 5.08 if n and n not in POWER else 2.54)


def two(s, kind, val, x, y, a, b, fp=None, dnp=False):
    """Двухвыводная деталь лёжа: ножка 1 слева - цепь a, ножка 2 справа - цепь b."""
    lib = {"R": "Device:R", "C": "Device:C", "CP": "Device:C_Polarized", "L": "Device:L"}[kind]
    r = ref("C" if kind == "CP" else kind)
    if fp is None:
        fp = {"R": FP_R, "C": FP_C, "CP": FP_CP, "L": FP_L}[kind]
    s.place(r, lib, val, x, y, rot=90, props={"Footprint": fp}, dnp=dnp)
    term(s, r, 1, a, 5.08)
    term(s, r, 2, b, 5.08)
    return r


def row(s, x, y, items, per_col=8, dx=55.88, dy=7.62):
    """Мелочь сеткой: столбики по per_col сверху вниз, столбики вправо. items = [(kind, val, a, b[, fp[, dnp]])]."""
    out = []
    for i, it in enumerate(items):
        kind, val, a, b = it[:4]
        fp = it[4] if len(it) > 4 else None
        dnp = it[5] if len(it) > 5 else False
        out.append(two(s, kind, val, x + (i // per_col) * dx, y + (i % per_col) * dy, a, b, fp, dnp))
    return out


def flag_label(s, rail, x, y):
    """PWR_FLAG на шину-метку (за диодом / дросселем): флаг, провод вниз, метка."""
    s.place(f"#FLG{_n.setdefault('F', 1)}", "power:PWR_FLAG", "PWR_FLAG", x, y)
    _n["F"] += 1
    s.wire((x, y), (x, y + 5.08))
    s.label(rail, x, y + 5.08, angle=270)


def flag(s, rail, x, y):
    """PWR_FLAG на шину: флаг и символ питания - в одной точке."""
    s.place(f"#FLG{_n.setdefault('F', 1)}", "power:PWR_FLAG", "PWR_FLAG", x, y)
    _n["F"] += 1
    s.power(rail, x, y, rot=180 if rail != "GND" and not rail.startswith("-") else 0)


# ------------------------------------------------------------------ шина ZX и питание +5V
# Ламели (как рев A, `KiCad/revA1`): контакт -> цепь; остальные - не подключены.
ZX_PINS = {
    "b1": "A15", "a1": "A14", "b2": "A13", "a2": "A12", "b3": "D7", "a3": "+5V", "a4": "~{DOS}",
    "b6": "D0", "a6": "GND", "b7": "D1", "a7": "GND", "b8": "D2", "b9": "D6", "a9": "A0", "b10": "D5",
    "a10": "A1", "b11": "D3", "a11": "A2", "b12": "D4", "a12": "A3", "a13": "IORQGE", "a14": "GND",
    "a15": "CSROMCE", "b16": "~{MREQ}", "b17": "~{IORQ}", "b18": "~{RD}", "b19": "~{WR}", "b20": "~{IODOS}",
    "a20": "~{RST}", "b21": "~{WAIT}", "a21": "A7", "a22": "A6", "a23": "A5", "b24": "~{M1}", "a24": "A4",
    "a25": "CSROM", "b26": "A8", "b27": "A10", "a27": "A9", "b28": "+5V", "a28": "A11", "a29": "+5V",
    "b30": "GND", "a30": "GND"}                     # b29 +12V - на рев A2 не используется (п.69)


def zxbus(s):
    with s.block("ZX-BUS"):
        conns = {f"{r}{i}": None for r in "ab" for i in range(1, 31)}
        conns.update(ZX_PINS)
        ic(s, "J1", "Connector_Generic:Conn_02x30_Row_Letter_First", "ZX-BUS", 50.8, 101.6,
           "trinity:ZX_NEMO_60_EDGE", conns)
        # выходы на шину через буферы на 5 В (как рев A / MultiSound): CPLD держит ~OE в 0 -> на шине 5 В
        for u, oe, out, y in (("U13", "~{IORQGE_OE}", "IORQGE", 76.2), ("U14", "~{CSROMCE_OE}", "CSROMCE", 111.76)):
            ic(s, u, "74xGxx:74LVC1G125", "74LVC1G125", 121.92, y, "Package_TO_SOT_SMD:SOT-23-5",
               {1: oe, 2: "+5V", 3: "GND", 4: out, 5: "+5V"})
        # 100n у буферов; +5V шины: общая ёмкость (как MultiSound C75 / C33)
        row(s, 177.8, 66.04, [("C", "100n", "+5V", "GND"), ("C", "100n", "+5V", "GND"),
                              ("CP", "100u", "+5V", "GND", FP_CP6), ("C", "10u", "+5V", "GND")])
        # разъём питания (п.69)
        ic(s, "J5", "Connector_Generic:Conn_01x02", "+5V / GND", 190.5, 109.22,
           "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical", {1: "+5V", 2: "GND"})
        flag(s, "+5V", 213.36, 76.2)
        flag(s, "GND", 223.52, 76.2)


# ------------------------------------------------------------------ CPLD
# Раскладка под рев A2 (doc/trinity-a2-layout.md): лево - RP-GS, низ + низ права - ZX по порядку ламелей,
# верх права + право вверху - RP-AY, верх - DIP-4 и такт, верх слева - RP-GS.
ZX_ORDER = ["A14", "A15", "A12", "A13", "D7", "~{DOS}", "D0", "D1", "D2", "A0", "D6", "A1", "D5", "A2", "D3", "A3",
            "D4", "~{IORQGE_OE}", "~{CSROMCE_OE}", "~{MREQ}", "~{IORQ}", "~{RD}", "~{WR}", "~{RST}", "~{IODOS}",
            "A7", "~{WAIT}", "A6", "A5", "A4", "~{M1}", "CSROM", "A8", "A9", "A10", "A11"]
ZX_CPLD = [19, 20, 21, 22, 23, 24, 25,                                         # лево внизу
           27, 28, 29, 30, 31, 32, 35, 36, 37, 40, 41, 42, 44, 45, 46, 47, 48, 49, 50,   # низ
           52, 54, 55, 56, 57, 58, 60, 61, 63, 64]                             # право внизу
GS_CPLD = {97: "GS_RDY", 98: "GS_RD", 99: "GS_WR", 100: "GS_C5", 1: "GS_C4", 2: "GS_C3", 5: "GS_C2", 6: "GS_C1",
           7: "GS_C0", 8: "GS_D7", 9: "GS_D6", 10: "GS_D5", 12: "GS_D4", 13: "GS_D3", 14: "GS_D2", 16: "GS_D1",
           17: "GS_D0"}
AY_CPLD = {84: "AY_D0", 83: "AY_D1", 81: "AY_D2", 80: "AY_D3", 79: "AY_D4", 77: "AY_D5", 76: "AY_D6", 75: "AY_D7",
           72: "AY_C0", 71: "AY_C1", 70: "AY_WR", 69: "AY_RD", 68: "AY_RDY", 67: "AY_RST"}
MISC_CPLD = {90: "CPLD_CLK", 85: "DIP1", 87: "DIP2", 88: "DIP3", 89: "DIP4",
             4: "JTAG_TDI", 15: "JTAG_TMS", 62: "JTAG_TCK", 73: "JTAG_TDO",
             92: None, 93: None, 94: None, 96: None}                          # запас I/O
VCC = (3, 18, 34, 39, 51, 66, 82, 91)
GND = (11, 26, 33, 38, 43, 53, 59, 65, 74, 78, 86, 95)


def cpld(s):
    with s.block("CPLD (ZX-BUS decoder), 3.3 V, JTAG, DIP-4"):
        conns = dict(zip(ZX_CPLD, ZX_ORDER))
        conns.update(GS_CPLD)
        conns.update(AY_CPLD)
        conns.update(MISC_CPLD)
        conns.update({p: "+3V3" for p in VCC})
        conns.update({p: "GND" for p in GND})
        assert len(conns) == 100, len(conns)
        ic(s, "U1", "EPM:EPM3128ATC100-10N", "EPM3128ATC100-10N", 190.5, 101.6,
           "trinity:QFP50P1600X1600X120-100N", conns)
        # 3.3 В CPLD: AMS1117-3.3 (10u вход, 22u выход - как рев A), 100n у каждой из 8 ножек VCC;
        # JTAG (шьёт RP-GS): TCK 1к вниз, TMS / TDI 10к вверх (как рев A, рекомендация Altera);
        # DIP-4 (п.47): включён = 0, подтяжки 10к к +3V3; что делает - прошивка CPLD
        ic(s, "U11", "Regulator_Linear:AMS1117-3.3", "AMS1117-3.3", 284.48, 40.64,
           "Package_TO_SOT_SMD:SOT-223-3_TabPin2", {1: "GND", 2: "+3V3", 3: "+5V"})
        row(s, 335.28, 40.64, [("C", "10u", "+5V", "GND"), ("C", "22u", "+3V3", "GND")] +
            [("C", "100n", "+3V3", "GND")] * 8 +
            [("R", "1k", "JTAG_TCK", "GND"), ("R", "10k", "+3V3", "JTAG_TMS"), ("R", "10k", "+3V3", "JTAG_TDI")] +
            [("R", "10k", "+3V3", f"DIP{i}") for i in range(1, 5)], per_col=9)
        ic(s, "SW1", "Switch:SW_DIP_x04", "DIP-4", 284.48, 142.24,
           "Button_Switch_THT:SW_DIP_SPSTx04_Slide_9.78x12.34mm_W7.62mm_P2.54mm",
           {1: "GND", 2: "GND", 3: "GND", 4: "GND", 8: "DIP1", 7: "DIP2", 6: "DIP3", 5: "DIP4"})


# ------------------------------------------------------------------ RP-GS
def rpgs(s):
    with s.block("RP-GS: bus GP2-18 (one read), DAC, microSD, JTAG, link"):
        c = {f"GP{i}": f"GS_D{i - 2}" for i in range(2, 10)}
        c.update({f"GP{i}": f"GS_C{i - 10}" for i in range(10, 16)})
        c.update({"GP16": "GS_WR", "GP17": "GS_RD", "GP18": "GS_RDY", "GP19": None, "GP24": None,
                  "GP0": "GS_GP0", "GP1": "JTAG_TDO", "GP20": "AY_RUN", "GP21": "CPLD_CLK",
                  "GP22": "JTAG_TCK", "GP23": "JTAG_TMS", "GP25": "JTAG_TDI", "GP26": "GS_TX", "GP27": "GS_RX",
                  "GP28": "GS_BCK", "GP29": "GS_LRCK", "GP30": "GS_DIN", "GP31": "DAC_XSMT",
                  "GP32": "MIX_DATA", "GP33": "MIX_ON", "GP34": "LINK1", "GP35": "LINK2",
                  "GP43": "SD_CD", "GP44": "SD_MISO", "GP45": "SD_CS", "GP46": "GS_SCK", "GP47": "GS_MOSI",
                  "5V": "GS_5V", "3V3": "3V3_GS", "GND": "GND", "G1": "GND", "G2": "GND", "EN": None, "R": None})
        c.update({f"GP{i}": f"GS_GP{i}" for i in range(36, 43)})
        ic(s, "U2", "Trinity:RP2350B-MINI-A", "RP-GS", 76.2, 210.82, "trinity:rp2350b", c)
        # 5V модуля - через Шоттки (USB модуля не питает шину)
        ic(s, "D1", "Device:D_Schottky", "SS14", 154.94, 205.74, "Diode_SMD:D_SMA", {1: "GS_5V", 2: "+5V"})
        # 33 Ом последовательно: I2S (линии до ЦАП и до RP-AY), SD SCK / MOSI (как рев A);
        # microSD (п.67): SPI, подтяжки 10к CS, MISO, DAT1, DAT2, детект; 100n + 10u у питания
        row(s, 154.94, 220.98, [("R", "33", "GS_BCK", "I2S_BCK"), ("R", "33", "GS_LRCK", "I2S_LRCK"),
                                ("R", "33", "GS_DIN", "DAC_DIN"), ("R", "33", "GS_SCK", "SD_SCK"),
                                ("R", "33", "GS_MOSI", "SD_MOSI"), ("C", "100n", "3V3_GS", "GND")] +
            [("R", "10k", "3V3_GS", n) for n in ("SD_CS", "SD_MISO", "SD_DAT1", "SD_DAT2", "SD_CD")] +
            [("C", "10u", "3V3_GS", "GND")], per_col=6)
        ic(s, "J3", "Connector:Micro_SD_Card_Det_Hirose_DM3AT", "microSD DM3AT", 292.1, 228.6,
           "Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5",
           {1: "SD_DAT2", 2: "SD_CS", 3: "SD_MOSI", 4: "3V3_GS", 5: "SD_SCK", 6: "GND", 7: "SD_MISO",
            8: "SD_DAT1", 9: "SD_CD", 10: "GND", "SH": "GND"})
        # запас RP-GS - гребёнка J6
        ic(s, "J6", "Connector_Generic:Conn_01x10", "GS SPARE", 292.1, 279.4,
           "Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical",
           {1: "GS_GP0", 2: "GS_GP36", 3: "GS_GP37", 4: "GS_GP38", 5: "GS_GP39", 6: "GS_GP40", 7: "GS_GP41",
            8: "GS_GP42", 9: "3V3_GS", 10: "GND"})


# ------------------------------------------------------------------ RP-AY + AY ×2
def rpay(s):
    with s.block("RP-AY: bus GP34-47 (one read), AY x2, link"):
        c = {f"GP{i}": f"AY_D{i - 34}" for i in range(34, 42)}
        c.update({"GP42": "AY_C0", "GP43": "AY_C1", "GP44": "AY_WR", "GP45": "AY_RD", "GP46": "AY_RDY",
                  "GP47": "AY_RST"})
        c.update({f"GP{i}": f"PSG_D{i - 11}" for i in range(11, 19)})
        c.update({"GP9": "NARROW", "GP10": "~{PSG_RST}", "GP19": None, "GP20": "PSG1_BC1", "GP21": "PSG1_BDIR",
                  "GP22": "PSG2_BC1", "GP23": "PSG2_BDIR", "GP24": None, "GP25": "PSG_CLK",
                  "GP26": "LINK1", "GP27": "LINK2", "GP28": "I2S_BCK", "GP29": "I2S_LRCK", "GP30": "MIX_DATA",
                  "GP31": "MIX_ON", "GP32": "GS_RX", "GP33": "GS_TX",
                  "5V": "AY_5V", "3V3": None, "GND": "GND", "G1": "GND", "G2": "GND", "EN": None, "R": "AY_RUN"})
        c.update({f"GP{i}": f"AY_GP{i}" for i in range(0, 9)})
        ic(s, "U3", "Trinity:RP2350B-MINI-A", "RP-AY", 76.2, 330.2, "trinity:rp2350b", c)
        ic(s, "D2", "Device:D_Schottky", "SS14", 154.94, 325.12, "Diode_SMD:D_SMA", {1: "AY_5V", 2: "+5V"})
        # пока RP грузится: стробы AY в 0 (неактивны), сброс AY в 0 (молчат), narrow выключен
        row(s, 154.94, 340.36, [("R", "10k", n, "GND") for n in ("PSG1_BC1", "PSG1_BDIR", "PSG2_BC1",
                                                                 "PSG2_BDIR", "~{PSG_RST}", "NARROW")])
        ic(s, "J7", "Connector_Generic:Conn_01x10", "AY SPARE", 226.06, 345.44,
           "Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical",
           {**{i + 1: f"AY_GP{i}" for i in range(9)}, 10: "GND"})
        # AY-3-8910 / YM2149F (символ YM2149 - та же DIP-40): BC2, A8 - +5, ~A9 - земля, ~SEL / TEST - NC,
        # порты IO не используются. Шина данных - общая, стробы - свои. Питание - своя ветка +5VA_x.
        # Нагрузка выходов 3.3к (как рев A / MultiSound), 100n у ножки 40.
        for k, (u, x) in enumerate((("U4", 284.48), ("U5", 373.38)), 1):
            pw = f"+5VA_{k}"
            con = {1: "GND", 2: None, 5: None, 39: None, 26: None, 3: f"AY{k}_B", 4: f"AY{k}_A", 38: f"AY{k}_C",
                   22: "PSG_CLK", 23: "~{PSG_RST}", 24: "GND", 25: pw, 27: f"PSG{k}_BDIR", 28: pw,
                   29: f"PSG{k}_BC1", 40: pw}
            con.update({30 + i: f"PSG_D{7 - i}" for i in range(8)})
            con.update({p: None for p in range(6, 22)})
            ic(s, u, "Audio:YM2149", "AY-3-8910 / YM2149F", x, 355.6, "Package_DIP:DIP-40_W15.24mm_Socket", con)
            row(s, x - 10.16, 406.4, [("R", "3.3k", f"AY{k}_{ch}", "GND") for ch in "ABC"] +
                [("C", "100n", pw, "GND")])


# ------------------------------------------------------------------ аналог: питание, сумматор, narrow, ЦАП, выход
def analog_power(s):
    with s.block("ANALOG POWER: +5VA, +5VA_1/2, -5VA (LM2662), 3.3 V DAC"):
        # +5V -> 27 мкГн -> 47u + 100n (как рев A): на ОУ / ключ / TPA и на каждый AY.
        # -5VA: LM2662 (200 мА; TPS60400 - 60 мА, мало для TPA6120) от +5V, FC = V+ (150 кГц, выше звука), LV на землю,
        # OSC свободен; 47u низкое ESR (как в тесте даташита); дальше 27 мкГн -> 47u + 100n.
        # 3.3 В ЦАП (+3V3A): AMS1117-3.3 от +5V (10u / 22u, как у CPLD).
        ic(s, "U10", "Trinity:LM2662", "LM2662", 345.44, 50.8, "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
           {8: "+5V", 1: "+5V", 6: "GND", 7: None, 2: "LM_CP", 4: "LM_CM", 5: "-5V_RAW", 3: "GND"})
        ic(s, "U12", "Regulator_Linear:AMS1117-3.3", "AMS1117-3.3", 345.44, 88.9,
           "Package_TO_SOT_SMD:SOT-223-3_TabPin2", {1: "GND", 2: "+3V3A", 3: "+5V"})
        items = []
        for rail in ("+5VA", "+5VA_1", "+5VA_2"):
            items += [("L", "27u", "+5V", rail), ("CP", "47u", rail, "GND"), ("C", "100n", rail, "GND")]
        items += [("C", "47u", "LM_CP", "LM_CM", FP_C12), ("C", "47u", "GND", "-5V_RAW", FP_C12),
                  ("C", "10u", "+5V", "GND"), ("L", "27u", "-5V_RAW", "-5VA"), ("CP", "47u", "GND", "-5VA"),
                  ("C", "100n", "GND", "-5VA"), ("C", "10u", "+5V", "GND"), ("C", "22u", "+3V3A", "GND")]
        row(s, 398.78, 40.64, items, per_col=9)
        flag(s, "+5VA", 520.7, 40.64)
        flag(s, "-5VA", 530.86, 40.64)
        for i, rail in enumerate(("+5VA_1", "+5VA_2", "GS_5V", "AY_5V", "3V3_GS")):
            flag_label(s, rail, 520.7 + i * 10.16, 66.04)


def mixer(s):
    with s.block("MIXER: AY -> summing amps (NJM4580 +-5VA), narrow (74HCT4053), output 1k"):
        # сумматоры: «+» на землю, обратная связь 10к || 470p (срез ~34 кГц), как рев A. Усиление под уровень AY -
        # пересчитать (AY на ~20 дБ ниже полной шкалы ЦАП).
        ic(s, "U7", "Amplifier_Operational:NJM4580", "NJM4580", 350.52, 177.8, "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
           {1: "MIX_L", 2: "SUM_L", 3: "GND"}, unit=1)
        ic(s, "U7", "Amplifier_Operational:NJM4580", "NJM4580", 396.24, 177.8, "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
           {7: "MIX_R", 6: "SUM_R", 5: "GND"}, unit=2)
        ic(s, "U7", "Amplifier_Operational:NJM4580", "NJM4580", 441.96, 177.8, "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
           {8: "+5VA", 4: "-5VA"}, unit=3)
        # narrow (п.71): ключ на стороне AY (0..5 В, до конденсатора). A обоих чипов через 82к -> ключ X -> 10 мкФ ->
        # правый сумматор; C -> ключ Y -> левый. NARROW = 1 - узко. Третий ключ не нужен - на землю.
        # 74HCT: порог входа 2 В - управление 3.3 В от RP-AY напрямую.
        ic(s, "U9", "4xxx:4053", "74HCT4053", 487.68, 177.8, "Package_DIP:DIP-16_W7.62mm",
           {14: "NAR_A", 13: "NAR_A_ON", 12: None, 15: "NAR_C", 1: "NAR_C_ON", 2: None, 4: "GND", 3: "GND",
            5: "GND", 11: "NARROW", 10: "NARROW", 9: "GND", 6: "GND", 7: "GND", 8: "GND", 16: "+5VA"})
        items = []
        # вход: 10 мкФ последовательно; A, C - 24к на свою сторону, B - 47к на обе
        for k in (1, 2):
            items += [("C", "10u", f"AY{k}_{ch}", f"AY{k}_{ch}_AC") for ch in "ABC"]
        for k in (1, 2):
            items += [("R", "24k", f"AY{k}_A_AC", "SUM_L"), ("R", "47k", f"AY{k}_B_AC", "SUM_L"),
                      ("R", "47k", f"AY{k}_B_AC", "SUM_R"), ("R", "24k", f"AY{k}_C_AC", "SUM_R")]
        items += [("R", "10k", "SUM_L", "MIX_L"), ("C", "470p", "SUM_L", "MIX_L"),
                  ("R", "10k", "SUM_R", "MIX_R"), ("C", "470p", "SUM_R", "MIX_R"),
                  ("C", "100n", "+5VA", "GND"), ("C", "100n", "GND", "-5VA")]
        # выход: 10 мкФ -> 47к на землю -> 1к на точку смешения (гнездо); 10к между L / R - как рев A (MultiSound)
        items += [("C", "10u", "MIX_L", "AYL"), ("R", "47k", "AYL", "GND"), ("R", "1k", "AYL", "OUT_L"),
                  ("C", "10u", "MIX_R", "AYR"), ("R", "47k", "AYR", "GND"), ("R", "1k", "AYR", "OUT_R"),
                  ("R", "10k", "AYL", "AYR")]
        items += [("R", "82k", "AY1_A", "NAR_A"), ("R", "82k", "AY2_A", "NAR_A"),
                  ("R", "82k", "AY1_C", "NAR_C"), ("R", "82k", "AY2_C", "NAR_C"),
                  ("C", "10u", "NAR_A_ON", "SUM_R"), ("C", "10u", "NAR_C_ON", "SUM_L"), ("C", "100n", "+5VA", "GND")]
        row(s, 345.44, 218.44, items, per_col=9)


def dac(s):
    with s.block("DAC PCM5102A (one config: I2S, normal filter, no de-emphasis, PLL from BCK)"):
        # даташит рис.33: 2.2u CAPP-CAPM, 2.2u VNEG, 0.1u + 10u у AVDD / CPVDD / DVDD; SCK, FMT, FLT, DEMP - на землю;
        # LDOO - 1u. XSMT - от RP-GS (10к вниз: молчит, пока RP грузится).
        # Выход: 470 Ом -> точка смешения; 2.2 нФ там на землю (даташит: 470 Ом + 2.2 нФ).
        ic(s, "U6", "Audio:PCM5102A", "PCM5102A", 360.68, 381.0, "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm",
           {1: "+3V3A", 2: "DAC_CAPP", 3: "GND", 4: "DAC_CAPM", 5: "DAC_VNEG", 6: "DAC_L", 7: "DAC_R", 8: "+3V3A",
            9: "GND", 10: "GND", 11: "GND", 12: "GND", 13: "I2S_BCK", 14: "DAC_DIN", 15: "I2S_LRCK", 16: "GND",
            17: "DAC_XSMT", 18: "DAC_LDOO", 19: "GND", 20: "+3V3A"})
        row(s, 414.02, 360.68, [("C", "2.2u", "DAC_CAPP", "DAC_CAPM"), ("C", "2.2u", "GND", "DAC_VNEG"),
                                ("C", "1u", "DAC_LDOO", "GND"), ("R", "10k", "DAC_XSMT", "GND")] +
            [("C", "100n", "+3V3A", "GND"), ("C", "10u", "+3V3A", "GND")] * 3 +
            [("R", "470", "DAC_L", "OUT_L"), ("R", "470", "DAC_R", "OUT_R"),
             ("C", "2.2n", "OUT_L", "GND"), ("C", "2.2n", "OUT_R", "GND")], per_col=7)


def output(s):
    with s.block("LINE OUT (PJ-324M) + HEADPHONES (TPA6120A2 -> HD Audio)"):
        ic(s, "J2", "Connector_Audio:AudioJack3_SwitchTR", "PJ-324M", 495.3, 330.2, "trinity:PJ-324M_placeholder",
           {"T": "OUT_L", "R": "OUT_R", "S": "GND", "TN": None, "RN": None})
        # TPA6120A2 по даташиту: неинвертирующий, RF 1к (токовая ОС: >= 800 Ом, без конденсатора), RI - DNP
        # (усиление 1; с RI 1к - 2), RO 39 Ом последовательно на выходе (10-100 Ом), 100n у каждой из 4 ножек питания
        ic(s, "U8", "Trinity:TPA6120A2", "TPA6120A2", 495.3, 381.0, "trinity:TPA6120A2_DWP",
           {4: "OUT_L", 5: "HP_LFB", 17: "OUT_R", 16: "HP_RFB", 2: "HP_LO", 19: "HP_RO", 3: "+5VA", 18: "+5VA",
            1: "-5VA", 20: "-5VA", 21: "GND"})
        row(s, 548.64, 335.28, [("R", "1k", "HP_LO", "HP_LFB"), ("R", "1k", "HP_RO", "HP_RFB"),
                                ("R", "39", "HP_LO", "HP_L"), ("R", "39", "HP_RO", "HP_R"),
                                ("R", "1k", "HP_LFB", "GND", None, True), ("R", "1k", "HP_RFB", "GND", None, True),
                                ("C", "100n", "+5VA", "GND"), ("C", "100n", "+5VA", "GND"),
                                ("C", "100n", "GND", "-5VA"), ("C", "100n", "GND", "-5VA"),
                                ("C", "10u", "+5VA", "GND"), ("C", "10u", "GND", "-5VA")], per_col=6)
        # HD Audio (п.70): 9 L, 5 R, 2 и 4 - земля (как X14 Евы); 1, 3, 6, 7, 10 - нет; 8 - ключ (ножку удалить)
        ic(s, "J4", "Connector_Generic:Conn_02x05_Odd_Even", "HD Audio", 495.3, 431.8,
           "Connector_PinHeader_2.54mm:PinHeader_2x05_P2.54mm_Horizontal",
           {9: "HP_L", 5: "HP_R", 2: "GND", 4: "GND", 1: None, 3: None, 6: None, 7: None, 8: None, 10: None})


def build():
    d = Design(PROJ, "trinity_a2", title="ZX Trinity rev A2", rev="A2", company="")
    s = d.sheet("trinity_a2.kicad_sch", "ZX Trinity rev A2", paper="A0")
    zxbus(s)
    cpld(s)
    rpgs(s)
    rpay(s)
    analog_power(s)
    mixer(s)
    dac(s)
    output(s)
    s.arrange()
    d.write()
    d.verify(NETS, NO_CONNECT)
    d.erc()


if __name__ == "__main__":
    build()
