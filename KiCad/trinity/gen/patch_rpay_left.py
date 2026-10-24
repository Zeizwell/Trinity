#!/usr/bin/env python3
# Левый край RP-AY на корневом листе: новая раскладка (верхний чип - верхние ножки, нижний - нижние; данные чипа
# 8 подряд под PIO; GP19 - PSRAM модуля, GP24 - контроль VBUS - свободны). Меняем только метки у ножек RP
# (метки стоят на x = 111.76 на высоте ножки), у GP19 / GP24 убираем метку и провод, ставим крестик.
#   python patch_rpay_left.py <in.kicad_sch> <out.kicad_sch>
import re
import sys
import uuid

Y0 = 119.38                                   # RP-AY: GPn на y = Y0 - 2.54 + 2.54 * n (GP0 на 116.84), GP21.. - на 2.54 выше
PIN_Y = {f"GP{n}": round(116.84 + 2.54 * n, 2) for n in range(20)}
PIN_Y.update({"GP21": 167.64, "GP22": 170.18, "GP23": 172.72, "GP24": 175.26, "GP25": 177.8})
NEW = {**{f"GP{n}": f"Y1D{n}" for n in range(8)},
       "GP8": "Y1A0", "GP9": "~{Y1CS}", "GP10": "~{Y1WR}",
       **{f"GP{11 + n}": f"Y2D{n}" for n in range(8)},
       "GP21": "Y2A0", "GP22": "~{Y2CS}", "GP23": "~{Y2WR}", "GP25": "Y_M"}
FREE = ("GP19", "GP24")
LX, PX = 111.76, 116.84                        # x метки и кончика ножки


def main():
    s = open(sys.argv[1], encoding="utf-8").read()
    for gp, y in PIN_Y.items():
        pat = re.compile(rf'(\n\t\(label )"([^"]*)"(\n\t\t\(at {LX} {y:g} )')
        m = pat.search(s)
        if not m:
            raise SystemExit(f"no label at {gp} ({LX}, {y})")
        if gp in FREE:                           # метка и провод ножка -> метка - вон, крестик на ножку
            a = s.rfind("\n\t(label", 0, m.end())
            b = s.index("\n\t)", m.end()) + 3
            s = s[:a] + s[b:]
            w = re.search(rf"\n\t\(wire\n\t\t\(pts\n\t\t\t\(xy ({PX}|{LX}) {y:g}\) \(xy ({PX}|{LX}) {y:g}\).*?\n\t\)",
                          s, re.S)
            if not w:
                raise SystemExit(f"no wire at {gp}")
            s = s[:w.start()] + s[w.end():]
            e = re.search(rf"\n\t\(bus_entry\n\t\t\(at 109.22 {round(y - 2.54, 2):g}\).*?\n\t\)", s, re.S)
            if e:                                        # ввод шины к этой строке (из шины вниз-вправо)
                s = s[:e.start()] + s[e.end():]
            u = uuid.uuid5(uuid.NAMESPACE_URL, f"rpay-nc-{gp}")
            nc = f'\n\t(no_connect\n\t\t(at {PX} {y:g})\n\t\t(uuid "{u}")\n\t)'
            i = s.index("\n\t(sheet_instances")
            s = s[:i] + nc + s[i:]
        elif gp in NEW and m.group(2) != NEW[gp]:
            s = s[:m.start()] + m.group(1) + f'"{NEW[gp]}"' + m.group(3) + s[m.end():]
    open(sys.argv[2], "w", encoding="utf-8", newline="").write(s)
    print("ok")


if __name__ == "__main__":
    main()
