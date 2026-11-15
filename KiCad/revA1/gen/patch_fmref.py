#!/usr/bin/env python3
# Опорные ЦАП FM (YM3014B Mp / Rb, U2A+): постоянка ~2.5 В, сигнала нет - электролит не нужен. C13, C14, C22, C23 ->
# керамика 10u 16 В X5R 1206 (плоская, снизу под ЦАП FM; 16 В - чтобы под 2.5 В осталась ёмкость).
#   python patch_fmref.py <in.kicad_sch> <out.kicad_sch>
import re
import sys

REFS = ("C13", "C14", "C22", "C23")
s = open(sys.argv[1], encoding="utf-8").read()
n = 0
for m in reversed(list(re.finditer(r"\n\t\(symbol\n.*?\n\t\)", s, re.S))):
    t = m.group(0)
    r = re.search(r'\(property "Reference" "([^"]*)"', t)
    if r and r.group(1) in REFS:
        t = t.replace('(lib_id "Device:C_Polarized")', '(lib_id "Device:C")')
        t = t.replace('(property "Value" "10u"', '(property "Value" "10u/16V X5R"')
        t = t.replace('"Capacitor_SMD:CP_Elec_4x5.4"', '"Capacitor_SMD:C_1206_3216Metric"')
        s = s[:m.start()] + t + s[m.end():]
        n += 1
if n != len(REFS):
    raise SystemExit(f"found {n}")
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
print("ok")
