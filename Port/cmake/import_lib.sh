#!/bin/sh
# import_lib.sh <dll> <lib.a>: a MinGW import library for a DLL of the GTA2 install (binkw32.dll), from its exports.
# They are decorated stdcall names (_BinkOpen@8), which the "name == export" form of a .def maps to.
set -e
name=$(basename "$1" .dll)
def="${2%.a}.def"
{ echo "LIBRARY $name.dll"; echo EXPORTS
  i686-w64-mingw32-objdump -p "$1" | sed -n '/Ordinal\/Name Pointer/,/^$/p' | awk '/^[[:space:]]+\[/ {print $NF}' |
    sed -E 's/^_([A-Za-z0-9_]+@[0-9]+)$/\1 == _\1/'; } > "$def"
i686-w64-mingw32-dlltool -d "$def" -l "$2"
