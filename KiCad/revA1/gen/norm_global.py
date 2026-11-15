#!/usr/bin/env python3
# Глобальные метки - как у разъёма J4 (MultiSound): шрифт 1 мм, выравнивание без «bottom» (его ставит общая нормализация
# меток при вставке блоков).   python norm_global.py <in.kicad_sch> <out.kicad_sch>
import re
import sys

s = open(sys.argv[1], encoding="utf-8").read()


def fix(m):
    t = m.group(0).replace("(size 1.27 1.27)", "(size 1 1)")
    return re.sub(r"\(justify (left|right) bottom\)", r"(justify \1)", t)


s = re.sub(r"\n\t\(global_label .*?\n\t\)", fix, s, flags=re.S)
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
print("ok")
