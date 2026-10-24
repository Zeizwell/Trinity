#!/bin/sh
# Сборка корневого листа из сгенерированных блоков (от корня 4ce7fe2 - до вставки GS / RP-AY).
# Результат - gen/test/root.kicad_sch; в trinity.kicad_sch копировать руками при закрытом KiCad.
#   sh gen/build_root.sh   (из KiCad/trinity)
set -e
T=gen/test
mkdir -p $T
git show 4ce7fe2:KiCad/trinity/trinity.kicad_sch > $T/p0.kicad_sch
python gen/delete_parts.py $T/p0.kicad_sch RP2350B-MINI-A2 PCM5102 | tail -1
python gen/merge_into_root.py gen/gs.kicad_sch $T/p0.kicad_sch $T/p1.kicad_sch --dx 156.21 --dy 281.94
python gen/patch_rpay_left.py $T/p1.kicad_sch $T/p2.kicad_sch
python gen/merge_into_root.py gen/rpay.kicad_sch $T/p2.kicad_sch $T/p3.kicad_sch --skip-ref RPTMP --skip-bus
python gen/merge_into_root.py gen/rpay_pu.kicad_sch $T/p3.kicad_sch $T/p4.kicad_sch --dx 486.41 --dy 311.15
python gen/norm_labels.py $T/p4.kicad_sch $T/p5.kicad_sch
python gen/merge_into_root.py gen/a12v.kicad_sch $T/p5.kicad_sch $T/p6.kicad_sch
python gen/patch_a12v.py $T/p6.kicad_sch $T/root.kicad_sch
cp $T/root.kicad_sch $T/p7.kicad_sch
python gen/delete_parts.py $T/p7.kicad_sch C3 R6 C17 R30 | tail -1
python gen/patch_out.py $T/p7.kicad_sch $T/root.kicad_sch
cp $T/root.kicad_sch $T/p8.kicad_sch
python gen/merge_into_root.py gen/bulk.kicad_sch $T/p8.kicad_sch $T/root.kicad_sch
cp $T/root.kicad_sch $T/p9.kicad_sch
python gen/merge_into_root.py gen/dec.kicad_sch $T/p9.kicad_sch $T/root.kicad_sch
cp $T/root.kicad_sch $T/p10.kicad_sch
python gen/patch_fp.py $T/p10.kicad_sch $T/root.kicad_sch
