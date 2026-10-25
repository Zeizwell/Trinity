#!/usr/bin/env python3
# Файлы монтажа JLCPCB из файла позиций KiCad: BOM (Comment, Designator, Footprint, LCSC) и CPL (позиции).
# Коды LCSC подобраны 2026-10-25 по складу JLC (base - без доплаты за тип). Не ставят: CPLD и слот microSD (паяет
# пользователь, слота у JLC нет), сквозные детали и модули, DNP.
#   python jlc_bom.py <pos.csv> <bom.csv> <cpl.csv>
import csv
import sys

LCSC = {  # (значение, корпус) -> код
    ("100n", "C_0603_1608Metric"): "C14663",
    ("1u", "C_0603_1608Metric"): "C15849",
    ("2.2u", "C_0603_1608Metric"): "C23630",
    ("10u", "C_0805_2012Metric"): "C15850",
    ("100n", "C_0805_2012Metric_Pad1.18x1.45mm_HandSolder"): "C49678",
    ("2.2n", "C_0805_2012Metric_Pad1.18x1.45mm_HandSolder"): "C28260",      # C0G - звуковой тракт
    ("10u/16V X5R", "C_1206_3216Metric"): "C13585",                          # 10u 50V X5R: меньше просадка ёмкости
    ("10u", "CP_Elec_4x5.4"): "C213487",
    ("22u", "CP_Elec_4x5.4"): "C72502",
    ("47u", "CP_Elec_5x5.4"): "C72515",
    ("100u/16V", "CP_Elec_6.3x7.7"): "C72476",
    ("82k", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17840",
    ("3.3k", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C26010",
    ("0", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17477",
    ("24k", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17575",
    ("10k", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17414",
    ("10K", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17414",
    ("47k", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17713",
    ("1k", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17513",
    ("470", "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"): "C17710",
    ("10", "R_0603_1608Metric"): "C22859",
    ("33", "R_0603_1608Metric"): "C23140",
    ("1k", "R_0603_1608Metric"): "C21190",
    ("10k", "R_0603_1608Metric"): "C25804",
    ("SS14", "D_SMA"): "C2480",
    ("LM358D", "SOIC-8_3.9x4.9mm_P1.27mm"): "C7950",
    ("NJM4580", "SOIC-8_3.9x4.9mm_P1.27mm"): "C3000516",                     # NJM4580E-TE1 (SOP-8)
    ("74LVC1G125", "SOT-23-5"): "C23654",
    ("AMS1117-3.3", "SOT-223-3_TabPin2"): "C6186",
    ("TPS60400", "SOT-23-5"): "C9839",
    ("27u", "L_Bourns_SRN6045TA"): "C168081",                                # FNR6045S270MT 133 мОм, 6x6 как SRN6045TA
    ("NARROW", "SW_DIP_SPSTx04_Slide_Omron_A6H-4101_W6.15mm_P1.27mm"): "C397291",
}


def main():
    rows = list(csv.DictReader(open(sys.argv[1], encoding="utf-8")))
    groups, cpl, skip = {}, [], []
    for r in rows:
        code = LCSC.get((r["Val"], r["Package"]))
        if not code:
            skip.append(f'{r["Ref"]} ({r["Val"]}, {r["Package"]})')
            continue
        groups.setdefault((r["Val"], r["Package"], code), []).append(r["Ref"])
        cpl.append([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm", "Top" if r["Side"] == "top" else "Bottom", r["Rot"]])
    with open(sys.argv[2], "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
        for (v, p, c), refs in sorted(groups.items(), key=lambda g: g[1][0]):
            w.writerow([v, ",".join(refs), p, c])
    with open(sys.argv[3], "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writerows(cpl)
    print("parts", len(cpl), "lines", len(groups))
    print("not by JLC:", ", ".join(skip))


if __name__ == "__main__":
    main()
