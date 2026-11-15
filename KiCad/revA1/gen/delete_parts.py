#!/usr/bin/env python3
# Удалить детали из листа вместе с тем, что после них повиснет (провода, метки, крестики NC, узлы).
# Висящее ищет ERC самого KiCad; убираем только НОВОЕ (то, что висело до удаления, не трогаем) - по кругу, пока есть.
#   python delete_parts.py <sch> REF [REF ...]      (KiCad должен быть закрыт)
import json
import os
import re
import subprocess
import sys

CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
KINDS = ("unconnected_wire_endpoint", "label_dangling", "no_connect_dangling", "isolated_pin_label")


def erc(path):
    out = path + ".erc.json"
    subprocess.run([CLI, "sch", "erc", "--format", "json", "--severity-all", "-o", out, path],
                   capture_output=True)
    data = json.load(open(out, encoding="utf-8"))
    os.remove(out)
    res = set()
    for sh in data.get("sheets", []):
        for v in sh.get("violations", []):
            if v.get("type") in KINDS:
                for it in v.get("items", []):
                    p = it.get("pos", {})
                    res.add((v["type"], round(p.get("x", 0) * 100, 2), round(p.get("y", 0) * 100, 2)))
    return res


def netsets(path):
    """Цепи как множества (деталь, ножка) - для проверки, что удаление не порвало оставшееся."""
    out = path + ".net"
    subprocess.run([CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out, path], capture_output=True)
    t = open(out, encoding="utf-8").read()
    os.remove(out)
    i = t.index("(nets")
    return {frozenset(re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', b))
            for b in re.split(r"\n\t\t\(net\b", t[i:])[1:]}


def items(s, head):
    return list(re.finditer(rf"\n\t\({head}\b.*?\n\t\)", s, re.S))


def cut(s, spans):
    for a, b in sorted(spans, reverse=True):
        s = s[:a] + s[b:]
    return s


def near(p, q):
    return abs(p[0] - q[0]) < 0.02 and abs(p[1] - q[1]) < 0.02


def main():
    path, refs = sys.argv[1], set(sys.argv[2:])
    s = open(path, encoding="utf-8").read()
    base = erc(path)
    spans, gone = [], set()
    for m in items(s, "symbol"):
        r = re.search(r'\(property "Reference" "([^"]*)"', m.group(0))
        if r and r.group(1) in refs:
            spans.append(m.span())
            gone.add(r.group(1))
    if gone != refs:
        raise SystemExit(f"not found: {sorted(refs - gone)}")
    s = cut(s, spans)
    open(path, "w", encoding="utf-8", newline="").write(s)
    good = netsets(path)                                # цепи оставшихся деталей - их не трогаем
    for rnd in range(30):
        new = erc(path) - base
        if not new:
            break
        pts = [(x, y) for _, x, y in new]
        spans = []
        for head in ("wire", "label", "no_connect", "junction", "bus_entry"):
            for m in items(s, head):
                t = m.group(0)
                if head == "bus_entry":                 # ввод шины: два конца - (at) и (at)+(size)
                    a = re.search(r"\(at ([\d.-]+) ([\d.-]+)", t)
                    z = re.search(r"\(size ([\d.-]+) ([\d.-]+)", t)
                    x0, y0 = float(a.group(1)), float(a.group(2))
                    ends = [(x0, y0), (x0 + float(z.group(1)), y0 + float(z.group(2)))]
                elif head == "wire":
                    ends = [tuple(map(float, e)) for e in re.findall(r"\(xy ([\d.-]+) ([\d.-]+)\)", t)]
                else:
                    a = re.search(r"\(at ([\d.-]+) ([\d.-]+)", t)
                    ends = [(float(a.group(1)), float(a.group(2)))]
                if any(near(e, p) for e in ends for p in pts):
                    spans.append(m.span())
        if not spans:
            print("left (not removable automatically):", sorted(new))
            break
        trial = cut(s, spans)
        open(path, "w", encoding="utf-8", newline="").write(trial)
        if netsets(path) == good:
            s = trial
            print(f"round {rnd + 1}: removed {len(spans)}")
            continue
        kept = 0                                        # порвало цепь - по одному, каждый с проверкой
        for sp in sorted(spans, reverse=True):
            trial = s[:sp[0]] + s[sp[1]:]
            open(path, "w", encoding="utf-8", newline="").write(trial)
            if netsets(path) == good:
                s = trial
            else:
                kept += 1
        open(path, "w", encoding="utf-8", newline="").write(s)
        print(f"round {rnd + 1}: one by one, kept {kept} that would break a net")
        if kept == len(spans):
            break
    # узлы, на которых осталось меньше трёх концов проводов - лишние
    ends = {}
    for m in items(s, "wire"):
        for e in re.findall(r"\(xy ([\d.-]+) ([\d.-]+)\)", m.group(0)):
            k = (round(float(e[0]), 2), round(float(e[1]), 2))
            ends[k] = ends.get(k, 0) + 1
    n = 0
    for m in reversed(items(s, "junction")):            # узел без концов проводов - лишний (с проверкой цепей)
        a = re.search(r"\(at ([\d.-]+) ([\d.-]+)", m.group(0))
        if ends.get((round(float(a.group(1)), 2), round(float(a.group(2)), 2)), 0) == 0:
            trial = s[:m.start()] + s[m.end():]
            open(path, "w", encoding="utf-8", newline="").write(trial)
            if netsets(path) == good:
                s, n = trial, n + 1
    open(path, "w", encoding="utf-8", newline="").write(s)
    print("deleted:", sorted(gone), "orphan junctions:", n, "nets intact:", netsets(path) == good)


if __name__ == "__main__":
    main()
