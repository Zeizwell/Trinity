#!/usr/bin/env python3
# Плата: в посадочных местах trinity:rp2350b убрать импортированный чертёж модуля (Dwgs.User, ~37 тыс. элементов -
# KiCad виснет) и группу, переименовать площадки 35..39 -> GP35..GP39, G -> GND (как в библиотеке).
#   python pcb_rp_fix.py <board.kicad_pcb>      (KiCad с платой должен быть закрыт)
import re
import sys


def block_end(s, i):
    d, q, k = 0, False, i
    while True:
        c = s[k]
        if q:
            if c == "\\":
                k += 1
            elif c == '"':
                q = False
        elif c == '"':
            q = True
        elif c == "(":
            d += 1
        elif c == ")":
            d -= 1
            if d == 0:
                return k + 1
        k += 1


def drop_drawing(m):
    return "" if '(layer "Dwgs.User")' in m.group(0) else m.group(0)


p = sys.argv[1]
s = open(p, encoding="utf-8").read()
out, pos = [], 0
for m in re.finditer(r'\n\t\(footprint "trinity:rp2350b"', s):
    a = m.start() + 1
    b = block_end(s, a)
    fp = s[a:b]
    ref = re.search(r'\(property "Reference" "([^"]*)"', fp).group(1)
    n0 = len(fp)
    fp = re.sub(r"\n\t\t\((?:fp_line|fp_arc|fp_rect|fp_text|fp_poly|fp_circle)\b.*?\n\t\t\)", drop_drawing, fp, flags=re.S)
    fp = re.sub(r"\n\t\t\(group\b.*?\n\t\t\)", "", fp, flags=re.S)
    for n in range(35, 40):
        fp = fp.replace(f'(pad "{n}" ', f'(pad "GP{n}" ')
    fp = fp.replace('(pad "G" ', '(pad "GND" ')
    print(ref, n0, "->", len(fp))
    out.append(s[pos:a] + fp)
    pos = b
out.append(s[pos:])
s2 = "".join(out)
open(p, "w", encoding="utf-8", newline="").write(s2)
print("pcb", len(s), "->", len(s2))
