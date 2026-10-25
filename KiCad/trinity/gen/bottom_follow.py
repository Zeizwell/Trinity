#!/usr/bin/env python3
# Нижняя мелочь - вслед за верхом, который двигал пользователь: каждая - к ножке, с которой связана, снизу.
#   "C:\Program Files\KiCad\10.0\bin\python.exe" bottom_follow.py <in.kicad_pcb> <out.kicad_pcb>
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_rest as R  # noqa: E402

R.TODO = {"U7": ("U12", True),                                     # буфер ЦАП FM2 - под свою панельку
          "C24": ("U7", True), "C27": ("U7", True), "R40": ("U7", True), "R39": ("U12", True),
          "R8": ("U4", True), "R9": ("U4", True),                   # нагрузки каналов AY - у выходов YM
          "R35": ("U10", True), "R36": ("U10", True), "R37": ("U10", True),
          "C15": ("C14", True), "C112": ("U104", True)}
R.main()
