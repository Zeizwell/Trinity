#!/usr/bin/env python3
# Вставить блоки сгенерированного листа (mixer.kicad_sch) в корневой лист trinity.kicad_sch (один лист, как
# MultiSound): библиотечные символы - чего нет, всё содержимое - со сдвигом (dx, dy), экземпляры символов - под
# проект и корень trinity, ссылки #PWR - свои номера. Опции: --gnda (GNDA -> GND: одна земля), --paper A1.
#   python merge_into_root.py <src.kicad_sch> <root.kicad_sch> <out.kicad_sch> --dx 25.4 --dy 340.36 [--gnda] [--paper A1]
import argparse
import re


def top_items(body):
    """Разбить текст на верхние s-выражения (с отступом одна табуляция). -> [(head, text)]"""
    items, i, n = [], 0, len(body)
    while i < n:
        j = body.find("\n\t(", i)
        if j < 0:
            break
        k, depth, inq = j + 2, 0, False
        while k < n:
            c = body[k]
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
                    break
            k += 1
        text = body[j + 1:k + 1]
        head = re.match(r"\t\((\w+)", text).group(1)
        items.append((head, text))
        i = k + 1
    return items


def shift(text, dx, dy):
    def f(m):
        x, y = float(m.group(2)) + dx, float(m.group(3)) + dy
        return f"({m.group(1)} {round(x, 4):g} {round(y, 4):g}"
    return re.sub(r"\((at|xy|start|end) (-?[\d.]+) (-?[\d.]+)", f, text)


def bus_frame(rect):
    """Прямоугольник блока -> 4 отрезка шины (стиль MultiSound / ZXEVO), углы - на сетку 1.27 наружу."""
    import math
    import uuid as _uuid
    (x1, y1), (x2, y2) = [tuple(map(float, re.search(rf"\({k} ([\d.-]+) ([\d.-]+)\)", rect).groups()))
                          for k in ("start", "end")]
    g = 1.27
    x1, y1 = math.floor(x1 / g) * g, math.floor(y1 / g) * g
    x2, y2 = math.ceil(x2 / g) * g, math.ceil(y2 / g) * g
    pts = [(x1, y1), (x2, y1), (x2, y2), (x1, y2), (x1, y1)]
    seed = re.search(r'\(uuid "([^"]+)"\)', rect).group(1)
    return [f"\t(bus\n\t\t(pts\n\t\t\t(xy {round(a[0], 2):g} {round(a[1], 2):g}) (xy {round(b[0], 2):g} {round(b[1], 2):g})\n"
            f"\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type solid)\n\t\t)\n"
            f'\t\t(uuid "{_uuid.uuid5(_uuid.NAMESPACE_URL, seed + str(i))}")\n\t)'
            for i, (a, b) in enumerate(zip(pts, pts[1:]))]


def lib_names(text):
    i = text.index("(lib_symbols")
    return set(re.findall(r'\n\t\t\(symbol "([^"]+)"', text[i:text.index("\n\t)", i)])), i


def lib_block(text, name):
    i = text.index(f'\n\t\t(symbol "{name}"')
    k, depth, inq = i + 3, 0, False
    while True:
        c = text[k]
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
                return text[i:k + 1]
        k += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"), ap.add_argument("root"), ap.add_argument("out")
    ap.add_argument("--dx", type=float, default=0.0), ap.add_argument("--dy", type=float, default=0.0)
    ap.add_argument("--gnda", action="store_true"), ap.add_argument("--paper")
    ap.add_argument("--skip-ref"), ap.add_argument("--skip-bus", action="store_true")
    a = ap.parse_args()
    src = open(a.src, encoding="utf-8").read()
    root = open(a.root, encoding="utf-8").read()
    root_uuid = re.search(r'\n\t\(uuid "([^"]+)"\)', root).group(1)
    project = re.search(r'\(instances\s*\(project "([^"]*)"', root).group(1)

    # библиотечные символы: чего нет в корне - добавить
    have, li = lib_names(root)
    want, _ = lib_names(src)
    add = "".join(lib_block(src, n) for n in sorted(want - have))
    end = root.index("\n\t)", li)
    root = root[:end] + add + root[end:]

    # #PWR корня - чтобы не пересечься
    pwr = [int(x) for x in re.findall(r'\(reference "#PWR0*(\d+)"', root)] + [0]
    pnext = max(pwr) + 1
    pflag = [int(x) for x in re.findall(r'\(reference "#FLG0*(\d+)"', root)] + [0]
    fnext = max(pflag) + 1

    body = src[src.index("\n\t)", src.index("(lib_symbols")) + 3:]
    out = []
    for head, text in top_items(body):
        if head in ("sheet_instances", "embedded_fonts", "title_block", "paper", "uuid"):
            continue
        if head == "bus" and a.skip_bus:                 # шина-заглушка: в корне она уже есть
            continue
        if head == "symbol" and a.skip_ref and re.search(rf'\(property "Reference" "{a.skip_ref}"', text):
            continue                                     # временный символ (в корне уже стоит настоящий)
        t = shift(text, a.dx, a.dy)
        if head == "rectangle":                          # рамки нет: у MultiSound синяя линия - настоящая шина с
            continue                                     #   вводами сигналов, её рисуем только там, где она есть
        if head in ("label", "global_label", "hierarchical_label"):   # метки - как у образца (TURBO SOUND FM):
            t = t.replace("(size 1.27 1.27)", "(size 1 1)")             #   шрифт 1 мм, текст над проводом
            t = re.sub(r"\(justify (left|right)\)", r"(justify \1 bottom)", t)
        if head == "text" and "(bold yes)" in t:         # заголовок блока - как у MultiSound: 2 мм, не жирный
            t = t.replace("(size 1.6 1.6)", "(size 2.0066 2.0066)").replace("\n\t\t\t\t(bold yes)", "")
            t = t.replace("(justify left)", "(justify left bottom)")
        if head == "symbol":
            t = re.sub(r'\(project "[^"]*"', f'(project "{project}"', t)
            t = re.sub(r'\(path "[^"]*"', f'(path "/{root_uuid}"', t)
            m = re.search(r'\(property "Reference" "(#PWR|#FLG)\d+"', t)
            if m:
                new = f"{m.group(1)}0{pnext if m.group(1) == '#PWR' else fnext}"
                if m.group(1) == "#PWR":
                    pnext += 1
                else:
                    fnext += 1
                t = re.sub(r'\(property "Reference" "(#PWR|#FLG)\d+"', f'(property "Reference" "{new}"', t)
                t = re.sub(r'\(reference "(#PWR|#FLG)\d+"', f'(reference "{new}"', t)
        out.append(t)
    i = root.index("\n\t(sheet_instances")
    root = root[:i] + "\n" + "\n".join(out) + root[i:]

    if a.gnda:                                           # одна земля: GNDA -> GND у экземпляров
        def g(m):
            t = re.sub(r'\t\t\(lib_name "GNDA[^"]*"\)\n', "", m.group(0))    # варианты GNDA_2 и т.п.
            return t.replace('(lib_id "power:GNDA")', '(lib_id "power:GND")') \
                    .replace('(property "Value" "GNDA"', '(property "Value" "GND"')
        root = re.sub(r'\n\t\(symbol\n(?:\t\t\(lib_name "GNDA[^"]*"\)\n)?\t\t\(lib_id "power:GNDA"\).*?\n\t\)',
                      g, root, flags=re.S)
        if '\n\t\t(symbol "power:GND"' not in root:
            raise SystemExit("power:GND lib symbol missing in root")
    if a.paper:
        root = re.sub(r'\(paper "[^"]*"\)', f'(paper "{a.paper}")', root, count=1)
    open(a.out, "w", encoding="utf-8", newline="").write(root)
    print(f"merged {len(out)} items, added lib symbols: {sorted(want - have)}")


if __name__ == "__main__":
    main()
