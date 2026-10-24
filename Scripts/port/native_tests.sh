#!/bin/bash
# The Port/ tests that run on the host (Port/tests/native): the 2-byte wide strings and the POSIX file layer, built
# with the host's clang. The others (Port/tests/*.cpp) are Windows exes from Scripts/port/build.sh, run under wine.
# usage: Scripts/port/native_tests.sh
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=build_port/native_tests
rm -rf $OUT && mkdir -p $OUT/files
clang++ -std=c++17 -g -fshort-wchar -IPort -o $OUT/port_wchar_test Port/tests/native/port_wchar_test.cpp Port/port_wchar.cpp
clang++ -std=c++17 -g -IPort -o $OUT/posix_files_test Port/tests/native/posix_files_test.cpp Port/posix_files.cpp
$OUT/port_wchar_test
(cd $OUT/files && ../posix_files_test) # creates its files in the current folder
