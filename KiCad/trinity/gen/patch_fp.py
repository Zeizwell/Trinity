#!/usr/bin/env python3
# Посадочные места из библиотек, которых нет в проекте, -> наша trinity.pretty: разъём шины J4 (вытащен из платы
# MultiSound, ZX_NEMO_60_EDGE) и CPLD U1 (QFP100 из EPM3128ATC100-10N.zip).
#   python patch_fp.py <in.kicad_sch> <out.kicad_sch>
import sys

MAP = {'"my:ZX_NEMO_60_EDGE"': '"trinity:ZX_NEMO_60_EDGE"',
       '"EPM3128ATC100-10N:QFP50P1600X1600X120-100N"': '"trinity:QFP50P1600X1600X120-100N"'}
s = open(sys.argv[1], encoding="utf-8").read()
for a, b in MAP.items():
    if a not in s:
        raise SystemExit(f"not found: {a}")
    s = s.replace(a, b)
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
print("ok")
