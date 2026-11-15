#!/usr/bin/env python3
# Метки моих старых блоков (NARROW STEREO, ANALOG POWER) - в стиль образца: шрифт 1 мм, текст над проводом.
# Метки разъёма ZX-BUS (x > 500, от MultiSound) не трогаем - их переделает блок CPLD.
#   python norm_labels.py <in.kicad_sch> <out.kicad_sch>
import re
import sys


def fix(m):
    t = m.group(0)
    if float(re.search(r"\(at ([\d.]+)", t).group(1)) > 500:
        return t
    t = t.replace("(size 1.27 1.27)", "(size 1 1)")
    return re.sub(r"\(justify (left|right)\)", r"(justify \1 bottom)", t)


s = open(sys.argv[1], encoding="utf-8").read()
s = re.sub(r"\n\t\(label .*?\n\t\)", fix, s, flags=re.S)
open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
print("labels ok")
