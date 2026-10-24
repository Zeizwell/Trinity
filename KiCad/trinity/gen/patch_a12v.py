#!/usr/bin/env python3
# ANALOG POWER после вставки a12v.kicad_sch: ферриты FB101..103 -> катушки L101..103 27 мкГн (LC как у NeoGS: L1 27u,
# сопротивление <= 1 Ом), символы +5V на входах фильтров - вон (вход теперь с JP101), узлы на вертикали входов,
# временная метка AN_IN - вон, заголовок и заметка блока - под новую схему.
#   python patch_a12v.py <in.kicad_sch> <out.kicad_sch>
import re
import sys
import uuid

LIB = r"C:\Program Files\KiCad\10.0\share\kicad\symbols\Device.kicad_sym"
X = 125.73
FP_L = "Inductor_SMD:L_1210_3225Metric"


def item_span(s, pat):
    m = re.search(pat, s, re.S)
    if not m:
        raise SystemExit(f"not found: {pat[:60]}")
    return m.span()


def lib_symbol_l():
    """Символ L из библиотеки KiCad -> "Device:L" для lib_symbols листа."""
    t = open(LIB, encoding="utf-8").read()
    i = t.index('\n\t(symbol "L"\n')
    j = t.index('\n\t(symbol "', i + 5)
    body = t[i:j].replace('\n\t(symbol "L"\n', '\n\t(symbol "Device:L"\n', 1)
    return body.replace("\n\t", "\n\t\t")


def main():
    s = open(sys.argv[1], encoding="utf-8").read()
    # 1. символы +5V на входах фильтров
    for y in (349.25, 374.65, 400.05):
        a, b = item_span(s, rf'\n\t\(symbol\n\t\t\(lib_id "power:\+5V"\)\n\t\t\(at {X} {y:g} 0\).*?\n\t\)')
        s = s[:a] + s[b:]
    # 2. ферриты -> катушки
    n = 0
    for m in reversed(list(re.finditer(r"\n\t\(symbol\n\t\t\(lib_id \"Device:FerriteBead\"\).*?\n\t\)", s, re.S))):
        t = m.group(0).replace('(lib_id "Device:FerriteBead")', '(lib_id "Device:L")')
        t = re.sub(r'"FB(10[123])"', r'"L\1"', t).replace('"600R@100MHz"', '"27u"')
        t = t.replace('"Inductor_SMD:L_0805_2012Metric"', f'"{FP_L}"')
        s = s[:m.start()] + t + s[m.end():]
        n += 1
    if n != 3:
        raise SystemExit(f"ferrites: {n}")
    if '\n\t\t(symbol "Device:L"\n' not in s:
        i = s.index("(lib_symbols")
        e = s.index("\n\t)", i)
        s = s[:e] + lib_symbol_l() + s[e:]
    # 3. узлы на вертикали входов (там сходятся три провода)
    add = ""
    for y in (374.65, 400.05):
        u = uuid.uuid5(uuid.NAMESPACE_URL, f"a12v-junction-{y}")
        add += f'\n\t(junction\n\t\t(at {X} {y:g})\n\t\t(diameter 0)\n\t\t(color 0 0 0 0)\n\t\t(uuid "{u}")\n\t)'
    i = s.index("\n\t(sheet_instances")
    s = s[:i] + add + s[i:]
    # 4. временная метка
    a, b = item_span(s, rf'\n\t\(label "AN_IN"\n\t\t\(at {X} 349.25 0\).*?\n\t\)')
    s = s[:a] + s[b:]
    # 5. заголовок и заметка
    s = s.replace('(text "ANALOG POWER (from +5V bus, no 12 V)"',
                  '(text "ANALOG POWER (+5V bus via LC; option: +12V -> 78M05, JP101 2-3)"')
    s = s.replace("No 5 V LDO from the 5 V bus - filters only.",
                  "L101-L103 27u, DCR <= 1 Ohm (LC as NeoGS). JP101 1-2 (default): +5V bus;\\n"
                  "2-3: U104 78M05 from +12V (DNP, as MultiSound) - only if the bus has +12V.")
    open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
    print("ok")


if __name__ == "__main__":
    main()
