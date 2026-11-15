#!/usr/bin/env python3
# Электролиты снизу (обвязка FM под панельками YM) - наверх, как MultiSound: конденсаторы сверху, резисторы снизу.
# Сверху место - верх полосы под нижним YM, над ламелями (низ полосы - под дорожки шины). Остальное не трогаем.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" fm_caps_top.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_analog as P  # noqa: E402

ZONES = ((59.1, 72.1, 100.0, 92.0),                   # у SD сверху (пользователь: там место)
         (126.0, 111.8, 185.0, 119.5))                 # остальное - под нижним YM, над коридором шины


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    move = [f for f in b.GetFootprints()
            if f.IsFlipped() and str(f.GetFPID().GetLibItemName()).startswith("CP_Elec")]
    for f in b.GetFootprints():
        if f not in move:
            P.block(f)
    for f in sorted(move, key=lambda f: f.GetReference()):
        f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        f.SetOrientationDegrees(90)                     # стоя: уже по ширине, ряд короче
        print(f.GetReference(), "top" if any(P.put(f, z) for z in ZONES) else "NO ROOM")
    b.Save(sys.argv[2])
    print("saved")


if __name__ == "__main__":
    main()
