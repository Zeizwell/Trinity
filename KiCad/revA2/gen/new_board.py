#!/usr/bin/env python3
# Чистая плата рев A2: контур и ламели шины - с рев A (без дорожек, заливок и деталей), запрет меди на ламелях
# (рев A испорчена землёй между ламелями), посадочные места основных деталей - рядом с платой, под расстановку.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" gen/new_board.py   (из KiCad/revA2)
import os
import pcbnew

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(HERE, "..", "revA1", "trinity.kicad_pcb")      # рев A
OUT = os.path.join(HERE, "trinity_a2.kicad_pcb")
STD = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
OWN = os.path.join(HERE, "trinity.pretty")

# ссылка, библиотека, посадочное, значение (порядок - как выкладываем справа от платы)
PARTS = [
    ("U2", OWN, "rp2350b", "RP-GS (RP2350B-MINI-A)"),
    ("U3", OWN, "rp2350b", "RP-AY (RP2350B-MINI-A)"),
    ("U1", OWN, "QFP50P1600X1600X120-100N", "EPM3128ATC100"),
    ("U4", STD + r"\Package_DIP.pretty", "DIP-40_W15.24mm_Socket", "AY-3-8910 / YM2149F"),
    ("U5", STD + r"\Package_DIP.pretty", "DIP-40_W15.24mm_Socket", "AY-3-8910 / YM2149F"),
    ("U6", STD + r"\Package_SO.pretty", "TSSOP-20_4.4x6.5mm_P0.65mm", "PCM5102A"),
    ("U7", STD + r"\Package_SO.pretty", "SOIC-8_3.9x4.9mm_P1.27mm", "NJM4580"),
    ("U8", STD + r"\Package_SO.pretty", "SOIC-20W_7.5x12.8mm_P1.27mm", "TPA6120A2 (DWP: + площадка снизу)"),
    ("U9", STD + r"\Package_DIP.pretty", "DIP-16_W7.62mm", "74HC4053"),
    ("J2", OWN, "PJ-324M_placeholder", "PJ-324M (линия)"),
    ("J3", STD + r"\Connector_Card.pretty", "microSD_HC_Hirose_DM3AT-SF-PEJM5", "microSD DM3AT"),
    ("J4", STD + r"\Connector_PinHeader_2.54mm.pretty", "PinHeader_2x05_P2.54mm_Vertical", "HD Audio (без 8)"),
    ("J5", OWN, "Molex_8981_4pin_placeholder", "Molex 4-pin"),
]

mm = pcbnew.FromMM
src = pcbnew.LoadBoard(SRC)
b = pcbnew.LoadBoard(SRC)                                   # копия рев A, дальше всё лишнее - долой
for t in list(b.GetTracks()):
    b.Delete(t)
for z in list(b.Zones()):
    b.Delete(z)
for d in list(b.GetDrawings()):
    if b.GetLayerName(d.GetLayer()) != "Edge.Cuts":
        b.Delete(d)
edge = None
for f in list(b.GetFootprints()):
    if f.GetReference() == "J4":                             # ламели шины - оставить, назвать J1
        edge = f
        f.SetReference("J1")
    else:
        b.Delete(f)

# запрет меди на ламелях: от верха ламелей до края платы, оба слоя
bb = edge.GetBoundingBox(False)
area = pcbnew.ZONE(b)
area.SetIsRuleArea(True)
area.SetDoNotAllowZoneFills(True)
area.SetDoNotAllowTracks(True)
area.SetDoNotAllowVias(True)
area.SetDoNotAllowPads(False)
area.SetDoNotAllowFootprints(False)
ls = pcbnew.LSET()
ls.AddLayer(pcbnew.F_Cu)
ls.AddLayer(pcbnew.B_Cu)
area.SetLayerSet(ls)
area.SetZoneName("ламели: без меди")
x0, y0, x1, y1 = bb.GetX(), bb.GetY(), bb.GetRight(), bb.GetBottom() + mm(1)
o = area.Outline()
o.NewOutline()
for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
    o.Append(x, y)
b.Add(area)

# детали - справа от платы, в два ряда
ebox = b.GetBoardEdgesBoundingBox()
x, y, row_h = ebox.GetRight() + mm(15), ebox.GetY(), 0
for ref, lib, name, val in PARTS:
    f = pcbnew.FootprintLoad(lib, name)
    f.SetReference(ref)
    f.SetValue(val)
    fb = f.GetBoundingBox(False)
    if x + fb.GetWidth() > ebox.GetRight() + mm(15 + 120):
        x, y, row_h = ebox.GetRight() + mm(15), y + row_h + mm(6), 0
    f.SetPosition(pcbnew.VECTOR2I(x - fb.GetX(), y - fb.GetY()))
    b.Add(f)
    x += fb.GetWidth() + mm(6)
    row_h = max(row_h, fb.GetHeight())

b.SetCopperLayerCount(2)
pcbnew.SaveBoard(OUT, b)
print("сохранено", OUT, "деталей", len(b.GetFootprints()))
