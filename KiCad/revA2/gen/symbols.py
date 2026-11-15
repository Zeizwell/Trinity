#!/usr/bin/env python3
# Символы рев A2, которых нет в библиотеках KiCad, - в Trinity.kicad_sym проекта (заменяет одноимённые):
#   TPA6120A2 (DWP, HSOP-20 PowerPAD): ножки по даташиту (ref/datasheets/tpa6120a2.txt, Pin Functions), 6-15 NC -
#     в символе нет; площадка снизу - ножка 21 (на землю, «must be soldered down»).
#   LM2662 (SOIC-8): ножки по даташиту (ref/datasheets/lm2662.txt): 1 FC, 2 CAP+, 3 GND, 4 CAP-, 5 OUT, 6 LV, 7 OSC, 8 V+.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" gen/symbols.py   (из KiCad/revA2)
import os
import re

LIB = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Trinity.kicad_sym")


def prop(name, value, x, y, hide=False):
    h = "\n\t\t\t(hide yes)" if hide else ""
    return (f'\t\t(property "{name}" "{value}"\n\t\t\t(at {x} {y} 0){h}\n'
            f'\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n')


def pin(etype, x, y, ang, name, num):
    return (f'\t\t\t(pin {etype} line\n\t\t\t\t(at {x} {y} {ang})\n\t\t\t\t(length 2.54)\n'
            f'\t\t\t\t(name "{name}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t(size 1.27 1.27)\n'
            f'\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n\t\t\t\t(number "{num}"\n\t\t\t\t\t(effects\n'
            f'\t\t\t\t\t\t(font\n\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n\t\t\t)\n')


def symbol(name, ref, fp, desc, w, h, pins):
    """Прямоугольник 2w × 2h (мм, центр в 0); pins: (тип, сторона L/R/T/B, позиция по стороне, имя, номер)."""
    out = [f'\t(symbol "{name}"\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n',
           prop("Reference", ref, 0, h + 2.54), prop("Value", name, 0, -h - 2.54),
           prop("Footprint", fp, 0, 0, True), prop("Datasheet", "", 0, 0, True),
           prop("Description", desc, 0, 0, True),
           f'\t\t(symbol "{name}_0_1"\n\t\t\t(rectangle\n\t\t\t\t(start {-w} {h})\n\t\t\t\t(end {w} {-h})\n'
           f'\t\t\t\t(stroke\n\t\t\t\t\t(width 0.254)\n\t\t\t\t\t(type default)\n\t\t\t\t)\n'
           f'\t\t\t\t(fill\n\t\t\t\t\t(type background)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n',
           f'\t\t(symbol "{name}_1_1"\n']
    for etype, side, pos, pname, num in pins:
        if side == "L":
            out.append(pin(etype, -w - 2.54, pos, 0, pname, num))
        elif side == "R":
            out.append(pin(etype, w + 2.54, pos, 180, pname, num))
        elif side == "T":
            out.append(pin(etype, pos, h + 2.54, 270, pname, num))
        else:
            out.append(pin(etype, pos, -h - 2.54, 90, pname, num))
    out.append("\t\t)\n\t\t(embedded_fonts no)\n\t)\n")
    return "".join(out)


TPA = symbol("TPA6120A2", "U", "trinity:TPA6120A2_DWP", "Headphone amplifier, current feedback, +-5..+-15 V (TI)",
             7.62, 10.16, [
                 ("input", "L", 7.62, "LIN+", "4"), ("input", "L", 5.08, "LIN-", "5"),
                 ("input", "L", -5.08, "RIN+", "17"), ("input", "L", -7.62, "RIN-", "16"),
                 ("output", "R", 5.08, "LOUT", "2"), ("output", "R", -5.08, "ROUT", "19"),
                 ("power_in", "T", -2.54, "LVCC+", "3"), ("power_in", "T", 2.54, "RVCC+", "18"),
                 ("power_in", "B", -2.54, "LVCC-", "1"), ("power_in", "B", 2.54, "RVCC-", "20"),
                 ("passive", "B", 5.08, "PAD", "21")])
LM = symbol("LM2662", "U", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "Switched capacitor inverter 200 mA (TI)",
            7.62, 5.08, [
                ("power_in", "T", 0, "V+", "8"), ("input", "L", 2.54, "FC", "1"), ("input", "L", 0, "LV", "6"),
                ("input", "L", -2.54, "OSC", "7"), ("passive", "R", 2.54, "CAP+", "2"),
                ("passive", "R", 0, "CAP-", "4"), ("power_out", "R", -2.54, "OUT", "5"),
                ("power_in", "B", 0, "GND", "3")])


def main():
    t = open(LIB, encoding="utf-8").read()
    for name, text in (("TPA6120A2", TPA), ("LM2662", LM)):
        t = re.sub(rf'\t\(symbol "{name}"\n.*?\n\t\)\n', "", t, flags=re.S)   # старая версия - долой
        i = t.rindex("\n)")
        t = t[:i + 1] + text + t[i + 1:]
    open(LIB, "w", encoding="utf-8", newline="").write(t)
    print("ok", LIB)


if __name__ == "__main__":
    main()
