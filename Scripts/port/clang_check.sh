#!/bin/bash
# Port M0 check (docs/port.md): compile the game sources with clang for 32-bit x86, 2-byte wchar_t,
# against the MinGW-w64 headers. Syntax only, no link. Needs clang++ and i686-w64-mingw32-g++.
# usage: Scripts/port/clang_check.sh [Source/x.cpp ...]   (default: the gta2_lib_src list of CMakeLists.txt)
# Output: a summary of failing files and error kinds; full logs in $OUT (default build_clang/).
cd "$(dirname "$0")/../.."
OUT=${OUT:-build_clang}
rm -rf "$OUT"; mkdir -p "$OUT"
. Scripts/port/build_common.sh
INC=$(mingw_includes)
FLAGS="--target=i686-w64-mingw32 -fshort-wchar -std=c++17 -fsyntax-only -ferror-limit=0 -w -Wno-address-of-temporary -nostdinc++ -nostdlibinc $INC -ISource -I."
files=("$@")
[ ${#files[@]} -eq 0 ] && files=($(sed -n '/set *(gta2_lib_src/,/)/p' CMakeLists.txt | grep -o 'Source/[^ ]*\.cpp'))
for f in "${files[@]}"; do
  log="$OUT/$(basename "$f").log"
  ( clang++ $FLAGS "$f" > "$log" 2>&1 || touch "$log.fail" ) &
  while [ "$(jobs -r | wc -l)" -ge 8 ]; do sleep 0.1; done
done
wait
cat "$OUT"/*.log > "$OUT/all.log"
fails=$(ls "$OUT"/*.fail 2>/dev/null | wc -l | tr -d ' ')
echo "files: ${#files[@]}  ok: $((${#files[@]} - fails))  fail: $fails"
ls "$OUT"/*.fail 2>/dev/null | xargs -n1 basename 2>/dev/null | sed 's/\.log\.fail$//; s/^/  /'
echo "errors: $(grep -c 'error:' "$OUT/all.log")"
grep 'error:' "$OUT/all.log" | sort -u | sed 's/^\(Source\/[^:]*:[0-9]*\):[0-9]*: \(fatal \)\{0,1\}error: /\1  /'
[ "$fails" -eq 0 ]
