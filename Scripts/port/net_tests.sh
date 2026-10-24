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
# ENet as the port builds compile it (Scripts/port/build_*.sh), then the loopback transport test
. Scripts/port/build_common.sh # ENET_DEFS
for c in Port/net/enet/*.c; do clang -O1 -g -w -IPort/net/enet/include $ENET_DEFS -c $c -o $OUT/$(basename $c .c).o; done
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=${SAN:-undefined} -fno-sanitize-recover=undefined -IPort/net/enet/include \
    -o $OUT/test_transport Port/net/tests/test_transport.cpp Port/net/transport/*.cpp Port/net/core/*.cpp $OUT/*.o
$OUT/test_transport
# The bots' navigation (no game)
clang++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=${SAN:-undefined} -fno-sanitize-recover=undefined \
    -o $OUT/test_bot Port/net/tests/test_bot.cpp Port/net/session/bot_nav.cpp
$OUT/test_bot
