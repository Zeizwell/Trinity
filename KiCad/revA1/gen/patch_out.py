#!/usr/bin/env python3
# Выход карты - гнездо модуля PCM5102 (решено 2026-10-25): GS в микшер больше не идёт (C3 / R6, C17 / R30 удаляет
# delete_parts.py до этого шага), гребёнка J2 (1 R, 2-3 GND, 4 L, как MultiSound) - на L / R гнезда (GS_L / GS_R),
# а не на выход микшера SND_L / SND_R (тот идёт в гнездо через 1к - блок GS).
#   python patch_out.py <in.kicad_sch> <out.kicad_sch>
import re
import sys

s = open(sys.argv[1], encoding="utf-8").read()
for old, new, y in (("SND_L", "GS_L", 49.53), ("SND_R", "GS_R", 41.91)):    # метки у J2 (x 411.48)
    pat = rf'(\n\t\(label )"{old}"(\n\t\t\(at 411.48 {y:g} )'
    if not re.search(pat, s):
        raise SystemExit(f"no J2 label {old} at y {y}")
    s = re.sub(pat, rf'\1"{new}"\2', s, count=1)
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
print("ok")
