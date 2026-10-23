#!/bin/bash
# The netcode's core tests (docs/netcode.md, "Code layout"): Port/net/core without the game, built with the host's
# clang and run, with UBSan. usage: Scripts/port/net_tests.sh    env: SAN (default undefined; address,undefined for
# ASan too, which hangs at startup in some sandboxes)
set -eo pipefail
cd "$(dirname "$0")/../.."
OUT=build_port/net_tests
mkdir -p $OUT
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=${SAN:-undefined} -fno-sanitize-recover=undefined -o $OUT/test_core \
    Port/net/tests/test_core.cpp Port/net/core/*.cpp
$OUT/test_core
