#!/usr/bin/env python3
# Посадочные места из библиотек, которых нет в проекте, -> наша trinity.pretty: разъём шины J4 (вытащен из платы
# MultiSound, ZX_NEMO_60_EDGE) и CPLD U1 (QFP100 из EPM3128ATC100-10N.zip).
#   python patch_fp.py <in.kicad_sch> <out.kicad_sch>
import sys

MAP = {'"my:ZX_NEMO_60_EDGE"': '"trinity:ZX_NEMO_60_EDGE"',
       '"EPM3128ATC100-10N:QFP50P1600X1600X120-100N"': '"trinity:QFP50P1600X1600X120-100N"',
       # узкое стерео: SMD DIP-4 шаг 1.27 (Omron A6H-4101) вместо сквозного 9.8x12.3 - в 4 раза меньше, низ свободен
       '"Button_Switch_THT:SW_DIP_SPSTx04_Slide_9.78x12.34mm_W7.62mm_P2.54mm"':
           '"Button_Switch_SMD:SW_DIP_SPSTx04_Slide_Omron_A6H-4101_W6.15mm_P1.27mm"'}
s = open(sys.argv[1], encoding="utf-8").read()
for a, b in MAP.items():
    if a not in s:
        raise SystemExit(f"not found: {a}")
    s = s.replace(a, b)
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
print("ok")
