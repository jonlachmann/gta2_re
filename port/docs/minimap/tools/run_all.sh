#!/bin/sh
# run_all.sh <data dir> <captures dir> <out dir>: regenerates every image from the user's data and captures.
set -e
T=$(dirname "$0"); D=$1; C=$2; O=$3
mkdir -p "$O"
for L in bil wil; do python3 -I "$T/render_map.py" "$D/$L.gmp" "$D/$L.sty" "$O/work_map_$L.png" 8; done
python3 -I "$T/make_all.py" "$D" "$C" "$O"
python3 -I "$T/variants.py" "$D" "$C" "$O"
python3 -I "$T/placement.py" "$C" "$O"
for L in wil bil; do python3 -I "$T/fullmap.py" "$D" $L "$O" "$O/fullmap/$L"; done
python3 -I "$T/minimap_pois.py" "$D" "$C" "$O" "$O/fullmap/bil"
mkdir -p "$O/hud_ref" && python3 -I "$T/extract_hud.py" "$D/bil.sty" "$O/hud_ref"
