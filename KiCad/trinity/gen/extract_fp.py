#!/usr/bin/env python3
# Вытащить футпринт из готовой платы (.kicad_pcb) в нашу библиотеку (.pretty).
#   python extract_fp.py <board.kicad_pcb> <lib:name в плате> <out.kicad_mod>
import re
import sys


def block(s, i):
    k, depth, inq = i, 0, False
    while True:
        c = s[k]
        if inq:
            if c == "\\":
                k += 1
            elif c == '"':
                inq = False
        elif c == '"':
            inq = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return s[i:k + 1]
        k += 1


def main():
    board, name, out = sys.argv[1:4]
    s = open(board, encoding="utf-8").read()
    fp = block(s, s.index(f'(footprint "{name}"'))
    short = name.split(":", 1)[-1]
    fp = fp.replace(f'(footprint "{name}"', f'(footprint "{short}"', 1)
    fp = re.sub(r"\n(\s*)\(at [\d.\- ]+\)", r"\n\1(at 0 0)", fp, count=1)      # место на плате -> начало координат
    open(out, "w", encoding="utf-8").write(fp + "\n")
    print(short, "pads:", sorted(set(re.findall(r'\(pad "([^"]*)"', fp))))


if __name__ == "__main__":
    main()
