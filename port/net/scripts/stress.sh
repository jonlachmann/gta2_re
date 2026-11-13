#!/bin/bash
# Stress the netcode until something breaks (docs/netcode.md, "Checks"): <servers> servers side by side, each with four
# bot clients (carhop, rammer, soldier, simple), every round a fresh game on the same map, the clock <speed> times as
# fast. Everything that helps find a bug is on: the clients check the collision grid (GTA2_NET_CHECKGRID) and the
# server's snapshots (GTA2_NET_SNAPCHECK), and record their sessions. A crash report names the ped and car last updated.
# usage: port/net/scripts/stress.sh [-m minutes (10)] [-n servers (3)] [-s speed (4)] [-p map (mp1-6p)] [-g] [-c seconds]
#   -g  the servers and clients under Guard Malloc (libgmalloc: each allocation on its own pages, freed ones unmapped), so a write
#       to freed memory crashes where it is made; slower (give it -s 1 or 2)
#   -c  churn: every <seconds> a random bot is killed (SIGKILL, as a crash leaves) and joins again; its log and
#       recording are the last run's
# Logs and recordings: $BUILD/net_stress/<n>/. Exit 1 on a crash or a CHECK line.
cd "$(dirname "$0")/../../.."
ROOT=$PWD
BUILD=${BUILD:-$ROOT/build_mac} # BUILD=<cmake build folder>: another build
BIN=${BIN:-$BUILD/reg2.app/Contents/MacOS/reg2}
[ -x "$BIN" ] || BIN=$BUILD/reg2
GAME=${GAME:-$BUILD/game} # GAME=<a game folder>: one with data/
export GTA2_DIR=$GAME
OUT=$BUILD/net_stress
MINUTES=10 SERVERS=3 SPEED=4 MAP=mp1-6p GUARD= CHURN=
while getopts "m:n:s:p:gc:" o; do
  case $o in
    m) MINUTES=$OPTARG ;;
    n) SERVERS=$OPTARG ;;
    s) SPEED=$OPTARG ;;
    p) MAP=$OPTARG ;;
    g) GUARD=/usr/lib/libgmalloc.dylib ;;
    c) CHURN=$OPTARG ;;
    *) exit 1 ;;
  esac
done
[ -x "$BIN" ] && [ -d "$GAME/data" ] || { echo "no build or game (see net_check.sh)"; exit 1; }
rm -rf "$OUT" && mkdir -p "$OUT"
cp "$BIN" "$OUT/reg2" # a copy: building meanwhile doesn't touch it
BIN=$OUT/reg2
trap 'kill $(jobs -p) 2>/dev/null' EXIT
BASE=$((34000 + RANDOM % 1000))
BOTS=(carhop rammer soldier simple)
cd "$GAME"
for n in $(seq 1 "$SERVERS"); do
  d=$OUT/$n p=$((BASE + n))
  mkdir -p "$d"
  DYLD_INSERT_LIBRARIES=$GUARD GTA2_HEADLESS=1 GTA2_SPEED=$SPEED GTA2_NET_CHECKGRID=1 GTA2_NET_SNAPCHECK=1 "$BIN" --server --port $p --map "$MAP" --players 6 \
    --police 6 --timelimit 1 --results 5 --warmup 5 --votes none > "$d/server.log" 2>&1 &
done
sleep 2
PIDS=()
bot() { # <server> <brain's index>: one bot client, its pid in PIDS (bash 3: no associative arrays)
  local d=$OUT/$1 b=${BOTS[$2]}
  DYLD_INSERT_LIBRARIES=$GUARD GTA2_HEADLESS=1 GTA2_SPEED=$SPEED GTA2_NET_CHECKGRID=1 GTA2_NET_RECORD=$d/$b.rec "$BIN" --connect 127.0.0.1:$((BASE + $1)) \
    --name s_$b --bot $b > "$d/$b.log" 2>&1 &
  PIDS[$1 * 10 + $2]=$!
}
for n in $(seq 1 "$SERVERS"); do
  for k in "${!BOTS[@]}"; do
    bot $n $k
  done
done
echo "stress: $SERVERS servers, $((SERVERS * ${#BOTS[@]})) bots, $MINUTES minutes at $SPEED times the speed${CHURN:+, a bot killed every $CHURN s} ($OUT)"
END=$((SECONDS + MINUTES * 60)) KILLS=0
while [ $SECONDS -lt $END ]; do
  if [ -n "$CHURN" ]; then
    sleep "$CHURN"
    n=$((1 + RANDOM % SERVERS)) k=$((RANDOM % ${#BOTS[@]}))
    if ! grep -q "crashed" "$OUT/$n/${BOTS[$k]}.log"; then # a crash's log stays
      kill -9 "${PIDS[$n * 10 + $k]}" 2>/dev/null
      wait "${PIDS[$n * 10 + $k]}" 2>/dev/null
      KILLS=$((KILLS + 1))
      bot $n $k
    fi
  else
    sleep 5
  fi
done
[ -n "$CHURN" ] && echo "stress: $KILLS bots killed and joined again"
kill $(jobs -p) 2>/dev/null
wait 2>/dev/null
BAD=0
for n in $(seq 1 "$SERVERS"); do
  d=$OUT/$n
  echo "server $n: $(grep -c 'round flow: round' "$d/server.log") rounds"
  for f in "$d"/*.log; do
    name=$(basename "$f" .log) checks=$(grep -c "net: CHECK" "$f")
    if grep -q "crashed" "$f"; then
      BAD=1
      echo "  $name CRASHED: $(grep -m1 crashed "$f" | cut -c1-100)"
      grep -A12 "crashed" "$f" | grep -E "^ *[0-9]+ +gta2_port" | sed -n '2,5p' | sed -E 's/ +/ /g; s/^/      /' | cut -c1-120
      grep "net: crash state: the last updated" "$f" | sed 's/^/      /' | cut -c1-240
    fi
    if [ "$checks" != 0 ]; then
      BAD=1
      echo "  $name: $checks CHECK lines, the first: $(grep -m1 "net: CHECK" "$f" | cut -c1-200)"
    fi
  done
done
[ $BAD = 0 ] && echo "stress: nothing broke"
exit $BAD
