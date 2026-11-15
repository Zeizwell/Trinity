#!/usr/bin/env python3
# Блок CPLD на плате (черново, пользователь двигает): буферы - у контактов a13 / a15 ламелей, 3.3 В - у CPLD,
# развязка - снизу к каждой ножке питания CPLD, JTAG и DIP-5 - рядом с CPLD, их подтяжки - снизу у их ножек,
# 33 Ом SD - у RP-GS.   "C:\Program Files\KiCad\10.0\bin\python.exe" cpld_place.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_rest as R  # noqa: E402

VCC = ("3", "18", "34", "39", "51", "66", "82", "91")
R.TODO = {"U21": ("J4", False), "U22": ("J4", False), "C133": ("U21", False, "5"), "C134": ("U22", False, "5"),
          "U23": ("U1", False), "C122": ("U23", False, "3"), "C123": ("U23", False, "2"),
          **{f"C{124 + k}": ("U1", True, p) for k, p in enumerate(VCC)}, "C132": ("U1", True, "39"),
          "J7": ("U1", "top"), "SW102": ("U1", "top"),
          "R226": ("J7", True, "1"), "R227": ("J7", True, "5"), "R228": ("J7", True, "9"),
          **{f"R{229 + k}": ("SW102", True, str(10 - k)) for k in range(5)},
          "R212": ("U5", True, "GP31"), "R213": ("U5", True, "GP30")}
R.main()
