# ЦАП FM U12 сдвинут пользователем - его C121 (сверху) и C22 / C23 (снизу) к ножкам
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_rest as R
R.TODO = {"C121": ("U12", False), "C22": ("U12", True), "C23": ("U12", True)}
R.main()
