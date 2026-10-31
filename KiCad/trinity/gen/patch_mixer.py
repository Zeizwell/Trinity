#!/usr/bin/env python3
# Довести блок MIXER (копия MultiSound в корневом листе) до Тринити, НЕ меняя рисунка: номиналы, DNP, имена меток,
# символ питания на V- ОУ. Решения - doc/trinity-board-analog.md.
#   python patch_mixer.py <in.kicad_sch> <out.kicad_sch>
import re
import sys

VALUES = {
    "U3": "NJM4580",                      # ОУ микшера: на +-5 В, вместо LM358
    "R11": "0",                           # вход «+» - на землю (виртуальная земля не нужна: питание +-5 В)
    "R2": "82k", "R4": "82k",             # узкое стерео: C1, C2 -> левый (бывшие MIDI_L, SAA_L)
    "R32": "82k", "R33": "82k",           #               A2, A1 -> правый (бывшие SAA_R, MIDI_R)
    "R6": "22k", "R30": "22k",            # GS L / R (бывшие DACCH0, DACCH2)
}
DNP = ["R5",                              # делитель виртуальной земли
       "C5", "R12", "C18", "R31",         # лишние каналы ЦАП GS MultiSound (DACCH1, DACCH3)
       "J3", "R1", "R34", "R7", "R19"]    # внешний аудиовход - не в первой плате
LABELS = {"MIDI_R": "NAR1", "SAA_R": "NAR2", "MIDI_L": "NAR3", "SAA_L": "NAR4",
          "DACCH0": "GS_L", "DACCH2": "GS_R"}
NEG = (374.65, 102.87)                    # U3 ножка 4 (V-): символ GNDA здесь -> -5VA
KICAD_POWER = r"C:\Program Files\KiCad\10.0\share\kicad\symbols\power.kicad_sym"


def symbols(s):
    """Экземпляры символов: (start, end, text)"""
    for m in re.finditer(r"\n\t\(symbol\n.*?\n\t\)", s, re.S):
        yield m.start(), m.end(), m.group(0)


def lib_symbol(path, name):
    t = open(path, encoding="utf-8").read()
    i = t.index(f'\n\t(symbol "{name}"')
    j = t.index("\n\t(symbol ", i + 5)
    return t[i:j].replace(f'(symbol "{name}"', f'(symbol "power:{name}"', 1).replace("\n\t", "\n\t\t")


def main():
    s = open(sys.argv[1], encoding="utf-8").read()
    out, last, done = [], 0, set()
    for a, b, t in symbols(s):
        ref = re.search(r'\(property "Reference" "([^"]*)"', t)
        ref = ref.group(1) if ref else ""
        at = re.search(r"\n\t\t\(at ([\d.-]+) ([\d.-]+)", t)
        if ref in VALUES:
            t = re.sub(r'\(property "Value" "[^"]*"', f'(property "Value" "{VALUES[ref]}"', t, count=1)
            done.add(ref)
        if ref in DNP:
            t = re.sub(r"\n\t\t\(dnp \w+\)", "\n\t\t(dnp yes)", t, count=1)
            done.add(ref)
        if 'lib_id "power:GNDA"' in t and at and abs(float(at.group(1)) - NEG[0]) < 0.01 \
                and abs(float(at.group(2)) - NEG[1]) < 0.01:
            t = re.sub(r'\t\t\(lib_name "[^"]*"\)\n', "", t)
            t = t.replace('(lib_id "power:GNDA")', '(lib_id "power:-5VA")')
            t = re.sub(r'\(property "Value" "GNDA"', '(property "Value" "-5VA"', t)
            done.add("NEG")
        out.append(s[last:a] + t)
        last = b
    s = "".join(out) + s[last:]
    missing = (set(VALUES) | set(DNP) | {"NEG"}) - done
    if missing:
        raise SystemExit(f"not found: {sorted(missing)}")
    for old, new in LABELS.items():                       # имена меток
        n = len(re.findall(rf'\n\t\(label "{old}"', s))
        if not n:
            raise SystemExit(f"label {old} not found")
        s = re.sub(rf'(\n\t\(label )"{old}"', rf'\1"{new}"', s)
    # библиотечный символ -5VA
    if '\n\t\t(symbol "power:-5VA"' not in s:
        i = s.index("(lib_symbols")
        j = s.index("\n\t)", i)
        s = s[:j] + lib_symbol(KICAD_POWER, "-5VA") + s[j:]
    open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
    print("patched:", sorted(done))


if __name__ == "__main__":
    main()
