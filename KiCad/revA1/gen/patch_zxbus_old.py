#!/usr/bin/env python3
# У разъёма J4 - остатки буфера IORQGE от MultiSound (символ U21 удаляет delete_parts.py, а провода, символы питания и
# метка ~{IORQGE} висели и до того - их убираем здесь) и крестики «не подключено» на a15 / a25 (теперь CSROMCE / CSROM).
# Блок CPLD и буферы рисуют cpld_gen.py / zxbus_gen.py.   python patch_zxbus_old.py <in.kicad_sch> <out.kicad_sch>
import re
import sys

WIRES = {((528.32, 271.145), (528.32, 275.59)), ((527.05, 271.145), (527.05, 271.78)),
         ((528.32, 278.13), (528.32, 280.035)), ((518.795, 271.145), (527.05, 271.145)),
         ((533.4, 276.86), (543.56, 276.86))}
POWER = {"#PWR0123", "#PWR0124", "#PWR0125"}
NC_AT = {(543.56, 281.94), (543.56, 307.34)}


def main():
    s = open(sys.argv[1], encoding="utf-8").read()
    gone = 0

    def drop(m):
        nonlocal gone
        t = m.group(0)
        head = re.match(r"\n\t\((\w+)", t).group(1)
        if head == "wire":
            p = tuple(tuple(map(float, e)) for e in re.findall(r"\(xy ([\d.-]+) ([\d.-]+)\)", t))
            if p in WIRES or p[::-1] in WIRES:
                gone += 1
                return ""
        elif head == "symbol":
            r = re.search(r'\(property "Reference" "([^"]*)"', t)
            if r and r.group(1) in POWER:
                gone += 1
                return ""
        elif head == "global_label" and '"~{IORQGE}"' in t and "(at 518.795 271.145" in t:
            gone += 1
            return ""
        elif head == "no_connect":
            a = tuple(map(float, re.search(r"\(at ([\d.-]+) ([\d.-]+)", t).groups()))
            if a in NC_AT:
                gone += 1
                return ""
        return t
    s = re.sub(r"\n\t\((?:wire|symbol|global_label|no_connect)\b.*?\n\t\)", drop, s, flags=re.S)
    if gone != len(WIRES) + len(POWER) + 1 + len(NC_AT):
        raise SystemExit(f"removed {gone}")
    open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
    print("removed", gone)


if __name__ == "__main__":
    main()
