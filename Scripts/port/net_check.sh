#!/bin/bash
# The netcode's checks in one go (docs/netcode.md, "Checks"), on the macOS build (Scripts/port/build_mac.sh):
#   arena   bot matches on the server alone, several maps, brains and seeds: none may crash
#   live    a server and two bot clients over a bad link (100 ms, jitter, loss): no crash, no CHECK line
#   churn   two bot clients staying while two others join and leave, four times (stopped: SIGTERM, Ctrl-C): no
#           crash, no CHECK line, nobody turned away as the server being full
#   rounds  the round flow: two bot clients ready up, play a round, vote for the next map and follow; a third joins late
#   maps    a map rotation, a round of 15 s each (the clock 4 times as fast): two bot clients follow every map change,
#           no crash, no CHECK line
#   soak    a server and three bot clients, the clock 10 times as fast: 10 minutes of play in 60 s, no crash
#   replay  recorded client sessions replayed against their saved logs: the same, or nondeterministic (it differs
#           from itself too), else a regression
# usage: Scripts/port/net_check.sh [--quick] [--record] [--save] [arena|live|replay ...]
#   --record  new fixtures (recorded sessions) and their logs; --save: the current build's replay logs as the baseline
# Fixtures and logs: build_port/net_check/. Exit 1 if anything failed.
cd "$(dirname "$0")/../.."
ROOT=$PWD
GAME=$ROOT/build_port/mac/game
BIN=$ROOT/build_port/mac/gta2_port
OUT=$ROOT/build_port/net_check
QUICK= RECORD= SAVE= STEPS=()
for a in "$@"; do
  case $a in
    --quick) QUICK=1 ;;
    --record) RECORD=1 ;;
    --save) SAVE=1 ;;
    *) STEPS+=("$a") ;;
  esac
done
[ ${#STEPS[@]} -eq 0 ] && STEPS=(arena live churn maps rounds soak replay)
[ -x "$BIN" ] && [ -d "$GAME/data" ] || { echo "no build or game: Scripts/port/build_mac.sh, then Scripts/port/run_mac.sh once"; exit 1; }
mkdir -p "$OUT"
trap 'kill $(jobs -p) 2>/dev/null' EXIT # our games end with us (each job execs its game: killing it kills the game)
FAILED=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; FAILED=1; }
BASE=$((33000 + RANDOM % 1000)) # the servers' ports: BASE + 1.. (arena), BASE + 200.. (the rest)

arena() {
  local maps=(mp1-6p mp5-6p downtown-6p) seeds=8 crashed=0 n=0
  [ -n "$QUICK" ] && seeds=3
  rm -f "$OUT"/arena_*.log
  for m in "${maps[@]}"; do
    for s in $(seq 1 $seeds); do
      n=$((n + 1))
      (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=1000 exec "$BIN" --server --port $((BASE + n)) --map $m --players 6 --seed $s \
        --arena rammer,hunter,soldier,simple,carhop,rammer --ticks 9000 > "$OUT/arena_${m}_$s.log" 2>&1
       echo $? > "$OUT/arena_${m}_$s.exit") &
    done
  done
  wait
  for f in "$OUT"/arena_*.exit; do
    if [ "$(cat "$f")" != 0 ]; then
      crashed=$((crashed + 1))
      echo "  ${f%.exit}.log: $(grep -m1 -A3 crashed "${f%.exit}.log" | tail -1 | sed 's/^ *//' | cut -c1-120)"
    fi
  done
  [ $crashed = 0 ] && pass "arena: $n matches of 5 minutes" || fail "arena: $crashed of $n matches crashed"
}

live() {
  local secs=45 p=$((BASE + 200))
  [ -n "$QUICK" ] && secs=20
  rm -f "$OUT"/live_*.log
  (cd "$GAME" && GTA2_HEADLESS=1 GTA2_NET_CHECKGRID=1 exec "$BIN" --server --port $p --map mp1-6p --players 6 --police \
    --arena hunter,soldier,rammer,simple > "$OUT/live_server.log" 2>&1) &
  sleep 3
  for b in rammer soldier; do
    (cd "$GAME" && GTA2_HEADLESS=1 GTA2_NET_CHECKGRID=1 GTA2_NET_JERKLOG=1 GTA2_NET_COMPARE=1 GTA2_NET_CORRLOG=1 exec "$BIN" --connect 127.0.0.1:$p --name c_$b --bot $b \
      --lag 100 --jitter 10 --loss 2 > "$OUT/live_$b.log" 2>&1) &
  done
  sleep $secs
  kill $(jobs -p) 2>/dev/null
  wait 2>/dev/null
  local crashes checks joined
  crashes=$(grep -l "crashed" "$OUT"/live_*.log | wc -l | tr -d ' ')
  checks=$(cat "$OUT"/live_*.log | grep -c "net: CHECK")
  joined=$(grep -c "joined as player" "$OUT/live_server.log")
  echo "  $(grep "players' corrections" "$OUT/live_rammer.log" | tail -1 | sed 's/^net: //')"
  echo "  $(grep "prediction against the server" "$OUT/live_rammer.log" | tail -1 | sed 's/^net: //' | cut -d';' -f1-4)"
  if [ "$crashes" != 0 ] || [ "$checks" != 0 ] || [ "$joined" != 2 ]; then
    fail "live: $joined of 2 clients joined, $crashes crashed, $checks CHECK lines (build_port/net_check/live_*.log)"
  else
    pass "live: 2 bot clients for ${secs} s at 100 ms, 2% loss, police on"
  fi
}

churn() {
  local p=$((BASE + 203)) r
  rm -f "$OUT"/churn_*.log
  (cd "$GAME" && GTA2_HEADLESS=1 exec "$BIN" --server --port $p --map mp1-6p --players 6 --arena hunter,soldier \
    > "$OUT/churn_server.log" 2>&1) &
  sleep 2
  for b in rammer soldier; do
    (cd "$GAME" && GTA2_HEADLESS=1 GTA2_NET_CHECKGRID=1 exec "$BIN" --connect 127.0.0.1:$p --name stay_$b --bot $b \
      > "$OUT/churn_stay_$b.log" 2>&1) &
  done
  for r in 1 2 3 4; do
    (cd "$GAME" && GTA2_HEADLESS=1 exec "$BIN" --connect 127.0.0.1:$p --name in_a$r --bot carhop > "$OUT/churn_a$r.log" 2>&1) &
    local a=$!
    (cd "$GAME" && GTA2_HEADLESS=1 exec "$BIN" --connect 127.0.0.1:$p --name in_b$r --bot simple > "$OUT/churn_b$r.log" 2>&1) &
    local b=$!
    sleep 10
    kill -TERM $a
    kill -INT $b
    sleep 1
  done
  kill $(jobs -p) 2>/dev/null
  wait 2>/dev/null
  local crashes checks full joined
  crashes=$(grep -l "crashed" "$OUT"/churn_*.log | wc -l | tr -d ' ')
  checks=$(cat "$OUT"/churn_*.log | grep -c "net: CHECK")
  full=$(grep -l "is full" "$OUT"/churn_*.log | wc -l | tr -d ' ')
  joined=$(grep -c "joined as" "$OUT/churn_server.log")
  if [ "$crashes" != 0 ] || [ "$checks" != 0 ] || [ "$full" != 0 ] || [ "$joined" != 10 ]; then
    fail "churn: $joined of 10 joins, $full turned away, $crashes crashed, $checks CHECK lines (build_port/net_check/churn_*.log)"
  else
    pass "churn: 8 joins and leaves beside 2 clients that stayed"
  fi
}

maps() {
  local secs=120 p=$((BASE + 204))
  [ -n "$QUICK" ] && secs=60
  rm -f "$OUT"/maps_*.log
  (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=4 exec "$BIN" --server --port $p --maps mp1-6p,mp2-6p,downtown-6p --players 6 --timelimit 1 \
    --arena simple,hunter,soldier > "$OUT/maps_server.log" 2>&1) &
  sleep 2
  for b in rammer soldier; do
    (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=4 GTA2_NET_CHECKGRID=1 exec "$BIN" --connect 127.0.0.1:$p --name m_$b --bot $b \
      > "$OUT/maps_$b.log" 2>&1) &
  done
  sleep $secs
  kill $(jobs -p) 2>/dev/null
  wait 2>/dev/null
  local changes crashes checks followed=1 b
  changes=$(grep -c "the next map" "$OUT/maps_server.log")
  crashes=$(grep -l "crashed" "$OUT"/maps_*.log | wc -l | tr -d ' ')
  checks=$(cat "$OUT"/maps_*.log | grep -c "net: CHECK")
  for b in rammer soldier; do
    # every change but perhaps the last, still under way at the end
    [ $(grep -c "a new game" "$OUT/maps_$b.log") -ge $((changes - 1)) ] || followed=0
  done
  if [ "$changes" -lt 3 ] || [ "$crashes" != 0 ] || [ "$checks" != 0 ] || [ $followed = 0 ]; then
    fail "maps: $changes map changes, clients followed: $followed, $crashes crashed, $checks CHECK lines (build_port/net_check/maps_*.log)"
  else
    pass "maps: $changes map changes, both clients followed"
  fi
}

# The round flow (session/round_flow.cpp), the clock 4 times as fast: two bot clients ready up, play a 1-minute round,
# vote for map 2 in the results and follow the server there, where the next round starts; a third joins in the results
rounds() {
  local p=$((BASE + 205))
  rm -f "$OUT"/rounds_*.log
  (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=4 exec "$BIN" --server --port $p --map mp1-6p --players 6 --timelimit 1 --results 5 \
    > "$OUT/rounds_server.log" 2>&1) &
  sleep 2
  for b in rammer soldier; do
    (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=4 GTA2_NET_CHECKGRID=1 exec "$BIN" --connect 127.0.0.1:$p --name r_$b --bot $b \
      > "$OUT/rounds_$b.log" 2>&1) &
  done
  sleep 18
  (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=4 GTA2_NET_CHECKGRID=1 exec "$BIN" --connect 127.0.0.1:$p --name r_late --bot simple \
    > "$OUT/rounds_late.log" 2>&1) &
  sleep 17
  kill $(jobs -p) 2>/dev/null
  wait 2>/dev/null
  local S="$OUT/rounds_server.log" crashes checks ok=1 b
  crashes=$(grep -l "crashed" "$OUT"/rounds_*.log | wc -l | tr -d ' ')
  checks=$(cat "$OUT"/rounds_*.log | grep -c "net: CHECK")
  for line in "countdown (round 1)" "round (round 1)" "round 1 over" "results (round 2)" "votes for mp2-6p" "the next map, mp2-6p" \
    "countdown (round 2)" "round (round 2)"; do
    grep -q "$line" "$S" || { ok=0; echo "  rounds: no \"$line\" on the server"; }
  done
  for b in rammer soldier; do
    grep -q "a new game" "$OUT/rounds_$b.log" || { ok=0; echo "  rounds: r_$b didn't follow to the next map"; }
  done
  grep -q "notice: Round 2: go" "$OUT/rounds_late.log" || { ok=0; echo "  rounds: the late client wasn't in round 2"; }
  # The game on the new map, not the old one again: Tiny Town (mp1) is within x 50..130, mp2's restart zones east of 140
  sed -n '/the next map, mp2-6p/,$p' "$S" | grep "frags/deaths" | tail -1 | grep -Eq "@(1[4-9][0-9]|2[0-9][0-9])," ||
    { ok=0; echo "  rounds: the server's players aren't on mp2-6p after the map change"; }
  if [ "$crashes" != 0 ] || [ "$checks" != 0 ] || [ $ok = 0 ]; then
    fail "rounds: $crashes crashed, $checks CHECK lines (build_port/net_check/rounds_*.log)"
  else
    pass "rounds: warmup, ready, a round, results with a map vote, the voted map, a player joining in the results"
  fi
}

soak() {
  local secs=60 p=$((BASE + 202))
  [ -n "$QUICK" ] && secs=30
  rm -f "$OUT"/soak_*.log
  (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=10 exec "$BIN" --server --port $p --map mp1-6p --players 6 --police \
    --arena hunter,soldier,rammer > "$OUT/soak_server.log" 2>&1) &
  sleep 2
  for b in rammer soldier hunter; do
    (cd "$GAME" && GTA2_HEADLESS=1 GTA2_SPEED=10 GTA2_NET_CHECKGRID=1 exec "$BIN" --connect 127.0.0.1:$p --name s_$b --bot $b \
      > "$OUT/soak_$b.log" 2>&1) &
  done
  sleep $secs
  kill $(jobs -p) 2>/dev/null
  wait 2>/dev/null
  local crashes checks
  crashes=$(grep -l "crashed" "$OUT"/soak_*.log | wc -l | tr -d ' ')
  checks=$(cat "$OUT"/soak_*.log | grep -c "net: CHECK")
  if [ "$crashes" != 0 ] || [ "$checks" != 0 ]; then
    fail "soak: $crashes crashed, $checks CHECK lines (build_port/net_check/soak_*.log)"
  else
    pass "soak: 3 bot clients, $((secs * 10 / 60)) minutes of play, $(grep -c 'joined as' "$OUT/soak_server.log") joined"
  fi
}

record() {
  local p=$((BASE + 201))
  rm -f "$OUT"/fixture_*.rec
  (cd "$GAME" && GTA2_HEADLESS=1 exec "$BIN" --server --port $p --map mp1-6p --players 6 --police --arena hunter,soldier,rammer \
    > "$OUT/record_server.log" 2>&1) &
  sleep 3
  for b in rammer soldier carhop; do
    (cd "$GAME" && GTA2_HEADLESS=1 GTA2_NET_RECORD="$OUT/fixture_$b.rec" exec "$BIN" --connect 127.0.0.1:$p --name f_$b --bot $b \
      --lag 100 --jitter 10 > /dev/null 2>&1) &
  done
  sleep 60
  kill $(jobs -p) 2>/dev/null
  wait 2>/dev/null
  echo "  recorded $(ls "$OUT"/fixture_*.rec | wc -l | tr -d ' ') sessions of 60 s"
  SAVE=1
}

# A replay's log without what changes from run to run by itself (pointers)
replay_log() {
  (cd "$GAME" && GTA2_HEADLESS=1 GTA2_NET_CHECKGRID=1 GTA2_NET_CORRLOG=1 exec "$BIN" --replay "$1" 2>&1) | grep "^net:" | sed -E 's/0x[0-9a-f]+/PTR/g'
}

replay() {
  [ -n "$RECORD" ] && record
  local fixtures=("$OUT"/fixture_*.rec) bad=0 flaky=0
  [ -e "${fixtures[0]}" ] || { fail "replay: no fixtures (--record makes them)"; return; }
  for r in "${fixtures[@]}"; do
    local name=$(basename "$r" .rec)
    replay_log "$r" > "$OUT/$name.now"
    if [ -n "$SAVE" ] || [ ! -e "$OUT/$name.base" ]; then
      cp "$OUT/$name.now" "$OUT/$name.base"
      echo "  $name: baseline saved ($(wc -l < "$OUT/$name.base" | tr -d ' ') lines)"
      continue
    fi
    if grep -q "net: CHECK" "$OUT/$name.now"; then
      echo "  $name: CHECK lines: $(grep -m1 "net: CHECK" "$OUT/$name.now" | cut -c1-120)"
      bad=$((bad + 1))
    elif ! cmp -s "$OUT/$name.now" "$OUT/$name.base"; then
      replay_log "$r" > "$OUT/$name.again"
      if cmp -s "$OUT/$name.now" "$OUT/$name.again"; then
        echo "  $name: differs from the baseline in $(diff "$OUT/$name.base" "$OUT/$name.now" | grep -c '^[<>]') lines (diff build_port/net_check/$name.base $name.now)"
        bad=$((bad + 1))
      else
        echo "  $name: nondeterministic (two replays differ in $(diff "$OUT/$name.now" "$OUT/$name.again" | grep -c '^[<>]') lines)"
        flaky=$((flaky + 1))
      fi
    fi
  done
  local note=""
  [ $flaky != 0 ] && note=", $flaky nondeterministic"
  [ $bad = 0 ] && pass "replay: ${#fixtures[@]} sessions$note" || fail "replay: $bad of ${#fixtures[@]} sessions differ"
}

for s in "${STEPS[@]}"; do
  case $s in
    arena | live | churn | maps | rounds | soak | replay) $s ;;
    *) echo "no step $s (arena, live, churn, maps, rounds, soak, replay)"; FAILED=1 ;;
  esac
done
exit $FAILED
