#!/usr/bin/env python3
# Символ EPM3128ATC100 (скачан у поставщика): 14 ножек помечены N.C., а у TQFP-100 это обычные I/O (даташит MAX 3000A:
# 80 I/O; символ ZXEVO 1010 с той же CPLD - I/O). N.C._x -> IO_NCx, тип no_connect -> bidirectional.
# Правит и библиотеку (EPM/...kicad_sym), и кэш символа в листе.   python fix_epm.py <file> [<out>]
import re
import sys


def fix(s):
    n = 0

    def pin(m):
        nonlocal n
        t = m.group(0)
        if '(name "N.C.' not in t:
            return t
        n += 1
        t = t.replace("(pin no_connect ", "(pin bidirectional ", 1)
        return re.sub(r'\(name "N\.C\.(_\d+)?"', lambda k: f'(name "IO_NC{k.group(1) or "_1"}"', t)
    s = re.sub(r"\(pin \w+ \w+.*?\(number \"[^\"]*\"", pin, s, flags=re.S)
    return s, n


src = sys.argv[1]
s, n = fix(open(src, encoding="utf-8").read())
if n not in (14, 0):
    raise SystemExit(f"pins fixed: {n}")
open(sys.argv[2] if len(sys.argv) > 2 else src, "w", encoding="utf-8", newline="").write(s)
print("fixed", n)
