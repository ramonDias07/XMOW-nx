#!/bin/bash
# usage: scan_round.sh N  -> find_gaps, content filter (zero padding ignored), pointer_scan, out in gaps/*N.*
cd "$(dirname "$0")/.."; N=$1
PY="${PYTHON:-python}"
ME="${MASSEFFECT_NX:?set MASSEFFECT_NX to a masseffect-nx checkout (tools/find_gaps.py)}"
[ -f tmp/image.bin ] || "$PY" -I tools/xex_image.py "${XEX:?set XEX to your default.xex}" tmp/image.bin
"$PY" -I "$ME/tools/find_gaps.py" --gen generated --report gaps/report$N.txt --toml gaps/candidates$N.toml > gaps/find$N.log 2>&1
"$PY" -I tools/filter_gaps.py gaps/report$N.txt tmp/image.bin gaps/real$N.txt gaps/cand$N.toml
"$PY" -I tools/pointer_scan.py tmp/image.bin gaps/cand$N.toml > gaps/ptr$N.txt
