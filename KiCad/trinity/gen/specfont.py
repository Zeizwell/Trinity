#!/usr/bin/env python3
# Спектрумовский шрифт 8x8 (.SpecCHR, 256 знаков по 8 байт, CP866) -> TTF для шелкографии в KiCad.
# Пиксели - квадраты, горизонтальные отрезки строки сливаются в один прямоугольник.
#   python specfont.py <in.SpecCHR> <out.ttf> <имя шрифта>
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

PX = 128                                             # пиксель в единицах шрифта (8 x 128 = 1024 на em)


def glyph(rows):
    """8 байт знака -> контуры: в каждой строке подряд идущие единицы - один прямоугольник."""
    pen = TTGlyphPen(None)
    for y, b in enumerate(rows):
        top = (7 - y) * PX - PX                      # строка 0 - верхняя; базовая линия под 7-й строкой
        x = 0
        while x < 8:
            if b & (0x80 >> x):
                x0 = x
                while x < 8 and b & (0x80 >> x):
                    x += 1
                pen.moveTo((x0 * PX, top)); pen.lineTo((x0 * PX, top + PX))     # по часовой - заливка в TTF
                pen.lineTo((x * PX, top + PX)); pen.lineTo((x * PX, top)); pen.closePath()
            else:
                x += 1
    return pen.glyph()


def main():
    src, out, name = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(src, "rb").read()
    names, glyphs, cmap = [".notdef", "space"], {".notdef": TTGlyphPen(None).glyph(), "space": TTGlyphPen(None).glyph()}, {32: "space"}
    for code in range(33, 256):
        u = ord(bytes([code]).decode("cp866"))
        g = f"g{code:02X}"
        names.append(g)
        glyphs[g] = glyph(data[code * 8:code * 8 + 8])
        cmap[u] = g
    fb = FontBuilder(8 * PX, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (8 * PX, 0) for n in names})
    fb.setupHorizontalHeader(ascent=7 * PX, descent=-PX)
    ps = name.replace(" ", "")
    fb.setupNameTable({"familyName": name, "styleName": "Regular", "fullName": name, "psName": ps,
                       "uniqueFontIdentifier": ps + "-1.0", "version": "Version 1.0"})
    fb.setupOS2(sTypoAscender=7 * PX, sTypoDescender=-PX, usWinAscent=7 * PX, usWinDescent=PX)
    fb.setupPost()
    fb.save(out)
    print(out, len(cmap), "glyphs")


if __name__ == "__main__":
    main()
