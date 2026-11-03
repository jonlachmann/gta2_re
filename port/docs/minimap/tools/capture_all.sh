#!/bin/sh
# capture_all.sh <game dir copy>: the four captures make_all.py expects (noon/dusk, 640x480 and 1280x720).
T=$(dirname "$0")
"$T/mmrun.sh" "$1" day43 640 480 0 600,900,1200 1210
"$T/mmrun.sh" "$1" dusk43 640 480 1 600,900,1200 1210
"$T/mmrun.sh" "$1" day169 1280 720 0 600,900,1200 1210
"$T/mmrun.sh" "$1" dusk169 1280 720 1 600,900,1200 1210
