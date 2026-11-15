#!/usr/bin/env python3
# Пары вплотную: цепь ровно из двух выводов двух пассивных деталей (конденсатор -> резистор, резистор -> резистор) -
# мелкую деталь к крупной (электролит > керамика / резистор), сверху, если влезет. Плюс R210 / R211 -> C82 / C83.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" pair_up.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_rest as R  # noqa: E402


def size(f):
    _, _, w, h = R.P.bbox(f)
    return w * h


def main():
    b = pcbnew.LoadBoard(sys.argv[1])
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    nets = {}
    for f in fps.values():
        for p in f.Pads():
            n = p.GetNetname()
            if n and not n.startswith("unconnected"):
                nets.setdefault(n, []).append((f.GetReference(), p.GetNumber()))
    todo, used = {}, set()
    for n, pads in sorted(nets.items()):
        refs = {r for r, _ in pads}
        if n in R.POWER or len(pads) != 2 or len(refs) != 2 or not all(r[0] in "RC" for r in refs):
            continue
        a, c = sorted(pads, key=lambda q: -size(fps[q[0]]))     # a - крупная (якорь), c - мелкая
        if c[0] in used or c[0] in todo:
            continue
        todo[c[0]] = (a[0], False, a[1])
        used.add(a[0])
    todo["R210"] = ("C82", False)
    todo["R211"] = ("C83", False)
    print(len(todo), "pairs:", ", ".join(f"{k}->{v[0]}" for k, v in todo.items()))
    R.TODO = todo
    R.main()


if __name__ == "__main__":
    main()
