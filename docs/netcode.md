# The network game

Client/server multiplayer for the port (`docs/port.md`), in the style of Quake 3 and its successors:

- an **authoritative server** runs the game and takes only the players' inputs;
- each **client predicts** its game ahead of the server, and on every snapshot rolls the whole world back, applies the
  server's state and runs the ticks since again (**whole-world rollback**);
- **snapshots** are deltas against what each client has acknowledged, per entity, cut by priority to a packet budget;
- **ENet** carries them: unreliable snapshots and commands, reliable messages for everything else.

It replaces the original's DirectPlay lockstep game. The game's own code runs unchanged: the netcode sits where
DirectPlay sat (`NetPlay`), and the few changes to `Source/` are port-only hooks ("The game's code").

What works: 1-6 players on the game's multiplayer maps, frags, points and tag games, police, traffic and ambient peds,
joining and leaving mid-game, rounds with warmup, results and votes, map rotations, chat with commands, reconnecting,
a frontend page to join, bots (scripted and trained), recordings that replay without a server. Protocol 16.

## Running

Build the port (`docs/port.md`, "Building"), then from the game folder:

```bash
./gta2_port --server --map downtown-6p --players 4         # a dedicated server, headless (--window to watch it)
./gta2_port --connect 127.0.0.1 --name me                   # play with the keyboard or a pad
./gta2_port --connect 127.0.0.1 --name bot1 --bot hunter    # a bot client (GTA2_HEADLESS=1: no window)
```

`Scripts/port/run/run_mac.sh` passes `GTA2_ARGS` on. Without `--server` or `--connect` the game starts at its frontend,
where **Network game** joins a server (below). The command line (`Port/net/cmdline.cpp` lists every option):

| | Options |
|---|---|
| Server | `--port` (27920), `--map <data/*.mmp>` (downtown-6p), `--maps a,b,c` (a rotation), `--players 1-6`, `--gametype 1-3` (frags, points, tag), `--fraglimit`, `--timelimit` (minutes), `--tickms`, `--police [stars]` (off by default, as the lobby; up to the game's 1 star, or 1-6), `--traffic N` (ambient cars per player, 16), `--packets N` (a snapshot's parts at most, 4), `--interest blocks` (24) |
| Round flow | `--minplayers N` (2), `--warmup s` (60), `--results s` (15), `--votes start,map,kick` (or `none`), `--rounds on\|off` |
| Arena | `--arena brain,...` (bots on the server itself), `--ticks N` (the result printed and the server ended at tick N), `--seed N` |
| Client | `--connect host[:port]`, `--name`, `--bot [brain]` (simple, carhop, rammer, soldier, hunter, `nn:<file>`), `--replay <file>` |
| Either end | `--lag ms`, `--jitter ms`, `--loss %`: a link conditioner on this end's sends |

## Playing

**Joining from the frontend.** The main menu has **Network game** (`Port/net/menu.cpp`): an address (`host[:port]`), a
name and **Join game**, kept in gta2.ini (`[network]`). The join runs in the background with "Connecting..." and any
error on the page; welcomed, the frontend boots the network game as it boots a level. `--name` and the link
conditioner's options apply to a join from the page too.

**Leaving.** Escape in a network game is the client's: "Leave the game? Y / N" over the frame. Yes leaves the server
and goes back to the network page as a single player again. So does a client that lost the server for good, or was
kicked, with why on the page.

**Connection.** The ping shows at the bottom right: the network's round trip alone (`kMsgPing`, answered at once,
both polled between frames). With no snapshot for 0.7 s it shows **Connection interrupted** and plays on with the
others' last inputs. When ENet drops the connection (after 5-10 s) it shows **Lost the server, reconnecting** and
connects again every 3 s under its name; the server gives a name its old slot back if it is free, at a restart zone.
After 30 s, or back as another player or on another map, it goes back to the network page.

**Chat** is the game's own: F10 starts a line, each key types into it, Enter ends it, and every machine shows
"name: line_" while it is typed. The key events reach the server in the commands, so the server's player types the
same line, and the player entity carries it to the other clients. A key types the character the system's keyboard
layout gives with shift (`Port_TypedChar`): the game's own tables (`data/keyboard`) have one character per key and no
shift. The server's copy of a line, which has no keyboard, types with the game's table (US, no shift).

**Commands** are chat lines starting with `/`, sent to the server when typed to their end: `/ready`, `/start`,
`/map N` (or a name), `/kick NAME`, `/yes`, `/no`. Tab completes the command, then a map from the welcome's list or a
player's name, as far as all that fit agree; when several fit they show over the frame (`CompleteCommand`).

**Rounds** (`session/round_flow.cpp`): a match is rounds in phases, run by the server and shown on every client:

1. **Warmup**: the map is played as usual. It ends when `--minplayers` are in and all are ready (F1 or `/ready`), or
   `--warmup` seconds after the minimum was reached (0: only when all are ready).
2. **Countdown**, 5 s. Too few left: back to the warmup. At its end everyone is at a restart zone with no score,
   weapons or power-ups.
3. **The round**, to the frag limit (points in a points game; tag: time only) or the time limit. The leader wins;
   a tie at the time limit is a draw.
4. **Results** (`--results` s): the winner, frags and deaths, and the maps with their votes (`/map N`). Then the most
   chosen map, else the rotation's next, else the same map.

**Votes**: `/start` (end the warmup), `/map N` (change now; a round ends without a winner), `/kick NAME` (out, and the
name turned away for 10 minutes). One at a time, open 20 s, decided by a majority of the people in the game (not
voting counts as no; the caller votes yes); Y or N while it shows. `--votes` sets which are allowed.

With `--rounds off` (the default with `--arena`) the round starts at once and the next follows its end directly.
The game's own end of a network game (its frag and time limits) can't be come back from on a dedicated server, so the
game gets neither and the server ends rounds itself. A map change ends the game on the server and every client and
starts the next at once: `winmain.cpp` asks the netcode (`gPortNextNetworkGame`) instead of going to the frontend.

## How it works

### Why the engine shapes the design

- **No per-entity update.** `Game::UpdateGame_4B9410` runs every subsystem once per tick, so neither end can simulate
  one player on its own. The client predicts by rolling back and resimulating the whole world; the server applies
  each player's input at a given tick, so a client runs ahead of the server (the Overwatch and Rocket League model,
  not Quake 3's) and the server keeps a small input buffer per player.
- **State is fixed-point.** Positions are `Fix16` (14 fractional bits), angles `Ang16` (0-1439): a position and
  rotation go over the wire losslessly, so predicted and server state agree to the bit when nothing else interferes.
- **State is pools allocated once**, about 3.5 MB in all, already listed by the port's state log (every global and
  what it points to). The same registry gives the rollback set.
- **A tick is the game's unit.** Speeds, timers and animations count ticks, so the tick stays the game's (33 ms
  by default, `--tickms`).

### The game loop

Every network frame `RunGameFrame_4DA850` runs the tick, then `NetPlay::SendToAll_521B20`, where the original sent
its inputs. The port's `NetPlay` (`engine/netplay.cpp`) makes that the netcode's frame hook: after tick T, before the
frame is drawn. There the server reads its clients and sets every player's input for T+1; the client sends its
command, applies snapshots and rolls back. Between frames both poll the network about every millisecond
(`gPortIdleHook`) for pings, so neither the tick nor the frame adds to the ping.

### The server

- **Commands** carry the server tick they are for and repeat the last few, so a lost packet loses no input. The server
  buffers them per tick and repeats a player's last input when one is late (its controls only: a repeated key would
  type twice). It reports the newest command tick it has, and the client keeps its commands about two ticks early.
- **Snapshots**, per client, every tick (`Server::SendSnapshot`): every player and the car each sits in, wherever they
  are, and everything within the client's interest area (24 blocks around its player; a camera sees 6 to 8). Each
  entity is a delta against the state that client acknowledged for it (per-entity baselines, so a snapshot can carry
  part of the world and nothing waits for a lost packet). What doesn't fit the budget goes with the state the client
  already has and its age, a few bits. New state goes by priority: the age plus one over one plus a sixth of the
  distance; an entity new to the client counts as 8 ticks old. A busy area's snapshot goes in up to `--packets` parts
  of 1372 bytes (the largest unreliable ENet packet that isn't fragmented).
- **Spawning** (`engine/spawn.cpp`): the game's limits on ambient traffic (16 cars) and peds (50) are for the whole
  game, and the first players' cameras took them all. A server allows the limits times its players, up to 3 players'
  worth (its traffic has drivers of its own: more ran the ped pool out). Before anything spawns, each client's camera
  bounds grow ahead along its motion by that client's lead, so nothing appears inside what it already shows.
- **Joining and leaving** (`engine/slots.cpp`): a slot without a client is out of the game the way the game handles a
  player who left (its ped walks on as a pedestrian). A joining client gets a new player ped at the next restart zone
  and an empty score. Snapshots carry the slots in the game, and clients mirror them.

### The client

- **Prediction** (`engine/rollback.cpp`): the client saves the game after every tick. On a snapshot of tick S it
  restores S, applies the server's state, and runs the ticks up to its own with its own inputs; other players repeat
  their last input. Everything else moves on by the game's own simulation in those ticks.
- **The blend**: what is shown moves a part of the way to the corrected state each frame (`GTA2_NET_BLEND`, 0.2);
  more than 2 blocks off (a respawn, a car entered) jumps.
- **What a client runs** (`engine/client_mode.cpp`): a client simulates what moves, not what decides. The game's own
  debug switches turn the rest off, set again every frame:

  | Service | Client | |
  |---|---|---|
  | players, cars, peds, objects (movement, physics, collisions, weapons) | runs | the prediction |
  | traffic and ambient ped spawning, despawning | off | only the server creates and removes entities |
  | scripts, police, ambulance, firefighters, emergency crews, generators, garages | off | they create entities; the results arrive as state |
  | doors, cranes, crushers, trains, traffic lights | runs | predicted, corrected by snapshots (the lights' phase is in the world entity) |
  | particles, tile animations, lights, HUD, sound | runs | cosmetic; sound is muted while resimulating |

  Player peds take no damage on a client, and everything counts as seen there, so nothing is removed off screen.
- **Replicas** (`engine/replica.cpp`): the client adopts what the map script made on both sides, makes the rest, moves
  them every snapshot and removes what the server drops. A car carries its driver: none, the traffic driver (all
  traffic cars share CarManager's spare, which is never sent as a ped), or a ped. A car's seated passengers come as
  peds at their car and are seated as the game seats them.
- **Players in cars** (`engine/car_entry.cpp`): a client gets into and out of cars by its own predicted input. When
  both sides have settled and the server has the player elsewhere, the client puts the ped into the server's car, or
  out of its own, at once, ending the way the game's own entering and leaving end. Another player's enter key reaches
  a client after its tick, so when the server's player is on its way into a car and the client's isn't, the client
  starts it as the key does.
- **Guards** (`engine/guards.cpp`, on the server too): what a network game breaks in the game's own logic is made
  safe: peds given on-foot orders while in a car, the HUD arrows to players who joined after the start, objects
  attached to cars the client moved (a tank's turret).

### Entities

The **net schema** (`engine/schema.cpp`) declares each entity type's fields, by name and width; `Capture` reads them
from the game and `Apply` writes them through the game's own functions wherever it keeps derived state (moving a
sprite relinks it in the collision grid; seating a ped sets both sides of the link). References to other entities go
as net ids. Structures go member by member by name (`engine/members.hpp`), never by position: the 64-bit layout isn't
the original's.

| Entity | Carries |
|---|---|
| World (one per snapshot, `engine/world_entity.cpp`) | the game's tick counter (timers are stamped with it), the random seed and stream, the restart zones' turn, the round's state (`core/round_state.hpp`), the game's generation (a map change), the traffic lights, the tag game's fugitive and times |
| Player (`engine/players.cpp`) | place, input, health, death count and last killer, weapons and the ammo of all 28, frags and points, power-up timers, its car, the chat line |
| Car (`engine/cars.cpp`, `car_ai.cpp`) | place, its whole `CarPhysics` (but its two pointers), damage, lights, horn, colour, its driver, and its AI (`CarAI`: route, speed, the cars it chases) |
| Ped (`engine/ped_state.cpp`) | place, and the plain values its AI runs on (objectives, targets, speeds, timers, occupation, flags, health, death cause), its references (targets, group) and its car and seat |
| Pickup (`engine/pickups.cpp`) | model and place; roadblock barriers too (they are off the object pool's list) |

Health, deaths, weapons and scores are the server's. A death the server had is carried out on the client with the
game's own `Ped::Kill_46F9D0`, credited to the killer, and the deaths carried out are part of the rollback set, so a
resimulation never repeats one. Scores, power-up timers and the tag game's "it" are set after the resimulation.

Not synced: objects other than pickups (fires, explosions, projectiles); the client's own simulation makes them, and
rolls them back with the world, but nothing corrects them.

### Random draws

The game has one random stream (`rand()`, behind `gRng_6F6784`). Its original netcode was lockstep, every machine
simulating the whole map. A client here simulates its own area only, so every draw the server makes for anything else
would move every later draw of the client's. In a network game the draws have owners (`Port/rand/owner.hpp`): each
ped's update (with its char's), each car's, and each service only the server runs draws a hash of the game's seed, the
owner (its net id, or the service), the tick and how many it drew in this update. An entity then decides the same way
wherever it is simulated. Draws without an owner (a player's controls, objects, explosions) stay the one stream, which
the world entity carries. Sound and particles draw from streams of their own (`Port/rand/sound.hpp`, `particle.hpp`),
so muting sound in a resimulation, or the particles there are, don't move the game's draws. Outside a network game
none of this is on, and the port draws exactly as `10.5.exe`.

### The rollback set

`engine/world.cpp` lists the world as memory blocks: every registered global and what it points to, the `Player`
objects, the light grid, and the netcode's own (the deaths carried out, the random streams, the sound entries'
pool). `SoundPosEntry` comes from a pool in the set (`engine/sound_pool.cpp`) instead of the heap, so a restore can't
bring back freed sound entries. The set is made again for every game (a new map's pools are elsewhere), and leaves
out what the game's scratch pointers point to (they point into a freed map after a map change). The whole world is
about 3.5 MB; saving or restoring it takes 0.06 ms, a busy tick about 30 µs.

### Wire format

A little-endian bitstream (`core/bitstream.cpp`), never a copy of a struct: clients are 32- and 64-bit. Messages
(`core/protocol.hpp`):

| Message | Channel | |
|---|---|---|
| `kMsgHello`, `kMsgWelcome`, `kMsgReject` | reliable | the join: the protocol version and name; the slot, the game's settings and map, the maps a vote chooses from |
| `kMsgCommands` | unreliable | the last few commands (tick and input bits, each delta-coded against the one before) and the newest snapshot the client has |
| `kMsgSnapshot`, `kMsgSnapshotPart` | unreliable | server tick, the baseline it is made against, entities with field masks and changed fields (`core/snapshot.cpp`) |
| `kMsgMapChange` | reliable | the next map, after a round |
| `kMsgPing`, `kMsgPong` | unreliable | the client's clock, sent back at once |
| `kMsgCommand`, `kMsgNotice` | reliable | a player's command; a line to show (a vote, a player ready) |

ENet runs with `mtu` 1400, two channels (never reliable data on the snapshot channel: under loss it delayed the
snapshots), its throttle off (it dropped snapshots when the round trip rose) and a flush after every tick.

## Recordings

Every client session is recorded (`session/recording.cpp`): the welcome, then per frame the packets, the round trip
and the input, to `recordings/session-<n>.rec` (the newest 10; `GTA2_NET_RECORD=<file>` elsewhere, `off` none). A
client does nothing but react to what arrives and to its own input, so `--replay <file>` plays the session again
without a server, tick for tick, as fast as the game runs: 8 minutes in 10-15 s, the same every time. **F12, or the key
under Esc, marks a moment** in play: the client and its replay print `net: mark <n> at frame <f>`, so a bug seen in
play is "the last session, mark 2".

## Bots

Bots are players whose input comes from a brain (`session/bot.hpp`), on the same path as a person's. A brain sees a
`BotView` built from what its client knows (its own body as predicted, the snapshot's players, cars and peds, and the
map as plain data: `engine::NavMap`, the walking height and the ways between cells) and returns the input bits a
person would press. It uses no game logic: no route finder, no ped AI. Navigation is A* (`session/bot_nav.cpp`).

| Brain | |
|---|---|
| `simple` | walks its route to the nearest player and fights when close |
| `carhop` | gets into the nearest car, drives, gets out, again |
| `rammer` | drives at players |
| `hunter` | the rammer when a car is near and nobody is, the fighter otherwise |
| `soldier` | a weapon, a car, then to the nearest player: runs it over when coming at it head on, or gets out and shoots |
| `nn:<file>` | a trained network (`session/bot_nn.cpp`): 62 inputs (`BotFeatures`), a choice in six groups of outputs |

In the round flow bots ready up, vote for map 2 in the results and yes on any vote but their own kick.

**Training** (`Scripts/port/bots/rl_bots.py`, PPO with torch): an **arena** (`--server --arena ... --ticks N`, bots
on the server itself, `Port/net/dev/arena.cpp`) plays the trainer's `remote:<port>` slots over a local socket, a
decision every 3 ticks with the reward since; `GTA2_SPEED=1000` headless runs a 150 s match in under half a second.
Each remote slot also runs a scripted teacher on the same view, and an imitation loss on what it would press, fading
over the first updates, gets the policy started (from scratch PPO learned nothing). `rl_bots.py --watch [file.nn]`
plays a match in a window. Out: `build_port/rl/` (`latest.nn`, `policy.pt`, `pool/`, `log.csv`). Head to head on Tiny
Town, the trained policy makes more frags than the simple bot and fewer than the hunter.

## Code layout

`Port/net/`. **Only `engine/` and the frontend page (`menu.cpp`) include the game's headers**; everything else works
on plain data, so the decomp can move on (renames, struct fixes) while the netcode follows it in one place.
Dependencies go one way: `session` uses `core`, `transport` and `engine/engine.hpp` (plain types only); `engine` uses
the game and `core`'s types; `dev` and `eval` use whatever they check.

```
Port/net/
  net_main.cpp    the session's lifetime: a server, a client, a replay, or a join from the frontend
  cmdline.cpp     the command line            maps.cpp   the .mmp files, the rotation, what a vote chooses from
  menu.cpp        the frontend's network page (game UI, so it includes the game's headers)
  core/           no game, unit-tested: the bitstream, the protocol, entity states and their delta coding, the
                  round's state
  transport/      the network: an interface, its ENet implementation, a link conditioner for tests
  session/        server.cpp, client.cpp, round_flow.cpp (no game: tested on its own), the bots (bot*.cpp),
                  recording.cpp, env.cpp (the GTA2_NET_* switches)
  engine/         the bridge to the game (engine.hpp: what the session sees; internal.hpp: what its files share):
                    netplay.cpp       the game's NetPlay/NetUi for the port: the frame hook, the network game's start
                    snapshot.cpp      a snapshot captured and applied, the game's controls, shared helpers
                    schema.cpp        every entity type's fields       world_entity.cpp  the world entity
                    players.cpp, cars.cpp, car_ai.cpp, ped_state.cpp, pickups.cpp   the entities (members.hpp)
                    replica.cpp       a client's cars and peds          car_entry.cpp     into and out of cars
                    slots.cpp         joining and leaving               spawn.cpp         the server's spawn budget
                    client_mode.cpp   what a client doesn't run         guards.cpp        the game made safe
                    world.cpp, rollback.cpp, sound_pool.cpp             the rollback set, save and resimulate
                    rounds.cpp        new rounds, map changes           overlay.cpp       what a client draws over the frame
                    stats.cpp         the periodic log's statistics     navmap.cpp        the map for bots
                    dev/              checks, logs and test cheats (dev.hpp), off unless switched on
  dev/            the session's checks and test hooks (client_probe.cpp, server_probe.cpp), the arena, the jerk meter
  eval/           measurements behind the design (sim_bench.cpp, neteval.cpp)
  tests/          the unit tests (ctest)
  enet/           ENet 1.3.18, vendored (MIT)
```

The server and the client call their probes (`dev/server_probe.hpp`, `dev/client_probe.hpp`) at fixed places in
their frame; the engine's dev functions are `engine/dev/dev.hpp`. Everything in `dev/` is linked in and off unless
its switch is set.

### The game's code

The netcode's hooks in `Source/`, all under `GTA2_PORT` and declared in `Port/port_hooks.hpp`:

| Where | Hook | |
|---|---|---|
| `Game.cpp` | `gPortAfterPlayersHook` | after the players' tick, before anything spawns: the server's camera lead |
| `Game.cpp` | `gPortDrawOverlay` | drawn last: the status, the round's panel, chat completion |
| `Car.cpp`, `Char_Pool.cpp` | `PortTrafficLimit()`, `gPortSpawnPlayers` | the spawn limits times the players |
| `Frontend.cpp` | `gPortFrontendSetup`, `gPortFrontendInput` | the network page |
| `winmain.cpp` | `gPortNextNetworkGame`, `gPortLeaveNetworkGame` | a map change goes to the next game; a client that left goes to the network page |
| `map_0x370.cpp` | `gPortZoneRoundRobin` | the restart zones' turn, a function's static in the original: rolled back and sent |
| `Ped.cpp` | `gPortKeepWantedPoints` | a client keeps the server's wanted points with its police off |
| `Hud.cpp` | `Port_TypedChar` | the chat types the keyboard layout's characters |
| `Pool.hpp`, `Car.cpp`, `Char_Pool.cpp`, `Game.cpp` | `PortRandScope` (`Port/rand/owner.hpp`) | who a random draw belongs to |
| `sound_obj.*`, `ParticleManager.cpp`, `Particle_4C.cpp` | `Port/rand/sound.hpp`, `particle.hpp` | their own random streams; `SoundPosEntry` from the pool; `sound_obj`'s state public for the rollback |
| `Pool.hpp` and the pools | `GTA2_POOL(n)` | pool sizes, `-DGTA2_POOL_SCALE=<k>` for benchmarks |

## Checks

| Check | | |
|---|---|---|
| Unit tests | `ctest --test-dir build_port/mac -L net`, or with UBSan: `cmake --preset net-tests -S Port && cmake --build build_port/net_tests --target net_tests && ctest --test-dir build_port/net_tests` | the core's coding, the transport over loopback, the bots' navigation, the round flow |
| The network check | `Scripts/port/net/net_check.sh [--quick] [step...]` (`BIN=<program>`: another build), about 6 minutes | PASS or FAIL per step, below |

`net_check.sh`'s steps, on the macOS build; logs and fixtures in `build_port/net_check/`:

- **arena**: 24 bot matches of 5 minutes on the server alone (three maps, every brain, random seeds): none may crash.
- **live**: a server and two bot clients at 100 ms, jitter and 2% loss, police on, with the grid check: no crash and
  no `net: CHECK` line. Prints the corrections and the comparison check's worst fields.
- **churn**: two bot clients stay while two others join and leave four times (stopped by SIGTERM and Ctrl-C): no
  crash, no `CHECK` line, nobody turned away as the server being full.
- **maps**: a three-map rotation, 15 s rounds at 4 times the clock: both clients follow every change.
- **rounds**: two bot clients ready up, play a round, vote for map 2 and follow the server there (a check of where
  the players stand: on the new map); a third joins during the results and plays round 2.
- **soak**: a server and three bot clients at 10 times the clock: 10 minutes of play in 60 s, no crash.
- **replay**: recorded sessions replayed against their saved logs: the same, or nondeterministic (a second replay
  differs too), else a regression. `--record` makes new ones; `--save` takes the current build's logs as the baseline
  after an intended change.

**The comparison check** (`GTA2_NET_COMPARE=1` on a client): on each snapshot, with the game restored to the
snapshot's tick and before it is applied, every mapped entity is captured here and compared with the server's, field
by field; every 300 frames the fields that differ most and how often. A measure, not a pass: remote players and AI
peds are predicted, so they differ.

**The divergence check** (`GTA2_NET_DIVERGE=<server tick>[:<ticks>]`, 300 by default): the client reconciles up to that
tick, then runs on its own; each later snapshot is compared with its save of that tick, and at the end it prints per
field after how many ticks each entity first differed. Below the fields, `GTA2_NET_DUMPPEDS` dumps every ped's bytes
on both ends (`Scripts/port/net/ped_diff.py` names the members that differ) and `GTA2_NET_RANDLOG` each `rand()` draw's
caller (`Scripts/port/net/rand_diff.py` counts them per tick and function).

### Environment variables

The session's (`session/env.cpp`) and the engine's (`engine/dev/checks.cpp`), all off by default:

| Variable | Where | Effect |
|---|---|---|
| `GTA2_NET_RECORD=<file>\|off` | client | the recording's file (default `recordings/session-<n>.rec`) |
| `GTA2_NET_BLEND=<0..1>` | client | how far a correction is blended in each frame (0.2; 1: at once) |
| `GTA2_NET_COMPARE=1` | client | the comparison check |
| `GTA2_NET_DIVERGE=<tick>[:<ticks>]` | client | the divergence check |
| `GTA2_NET_CORRLOG=1` | client | corrections over half a block or 45 degrees |
| `GTA2_NET_JERKLOG=1\|2` | both | how jerky what is shown moves (2: and each jump) |
| `GTA2_NET_MARKER=1` | client | a square where the newest snapshot has the local player |
| `GTA2_NET_INPUTLOG=1`, `_ANIMLOG=1`, `_SELFLOG=1`, `_SOUNDLOG=1` | client | the input, the other players' animation, the local player around a death, the sound's channels |
| `GTA2_NET_CARDIFF=1` | client | where the prediction of the local player's car differs from the snapshot |
| `GTA2_NET_GANGLOG=1` | both | player 0's group every 15 ticks, by the server's ids |
| `GTA2_NET_PICKUPLOG=1` | client | pickups made and removed |
| `GTA2_NET_CHECKGRID=1` | both | the collision grid checked after every move (`net: CHECK` lines) |
| `GTA2_NET_DUMPPEDS=<file>[:<from>:<to>]`, `GTA2_NET_RANDLOG=<file>:<from>:<to>` | both | the divergence check's dumps |
| `GTA2_NET_NAVDUMP=<file.pgm>` | client bot | the bots' map as a picture |
| `GTA2_NET_TESTKILL=<tick>:<victim>:<killer>,...` | server | a player killed by the game's own death |
| `GTA2_NET_TESTPOWERUP=<tick>:<player>:<power-up>,...` | server | a power-up (12: an instant gang) |
| `GTA2_NET_TESTCAR=<tick>:<player>:<model>,...` | server | a car beside the player (11: a bus) |
| `GTA2_NET_TESTARM=<weapon>:<ammo>`, `GTA2_NET_TESTWANTED=<points>` | server | every player kept armed, or at least that wanted (600: a star) |
| `GTA2_NET_SAY=<frame>:<command>;...` | client | commands sent at those frames |
| `GTA2_SIMBENCH=...`, `GTA2_SIMBENCH_FILL=<cars>`, `GTA2_NETEVAL=...`, `GTA2_OFFLINE_MP=...` | game | the measurements below (`Port/net/eval`, `engine/netplay.cpp`) |

`GTA2_SPEED`, `GTA2_HEADLESS` and the port's others: `docs/port.md`, "Environment variables".

## Measurements behind the design

Native macOS, arm64, `-O2`. `GTA2_SIMBENCH=<flip>[:<ticks>]` (`Port/net/eval/sim_bench.cpp`) runs that many ticks back
to back without drawing, prints the cost of each service, the world's size and its save and restore time, and checks
the rollback; `GTA2_SIMBENCH_FILL=<cars>` fills the map with traffic first, by the game's own spawner with its caps
lifted. `GTA2_NETEVAL` (`Port/net/eval/neteval.cpp`) measures what changes per tick (`churn`), how fast two runs
drift apart (`diverge`), the random draws (`rand`), sound in a rollback (`soundrollback`), a projectile advanced on its
own (`single`) and rollback in scenes with explosions (`explode`).

| | |
|---|---|
| The world | 1,763 blocks, 3.5 MB; save 0.06 ms, restore 0.055 ms; the rollback reproduces it exactly |
| A busy tick (73 cars, 39 peds, 176 objects) | about 30 µs: cars 52%, peds 28%, objects 13%; a 10-tick resimulation is about 2% of a 60 fps frame |
| A full map (4 times the pools, 983 cars, 661 peds, no cleanup) | 0.9 ms a tick, slightly worse than linear; the collision grid's link pool is the first to run out |
| What changes per tick | about 680 bytes for the whole attract demo's world, half of it in one interest area: about 500 bytes a snapshot |
| Drift of a client's own simulation | within 330 ms nearly everything stays within a pixel; peds drift first (46% by a pixel after 1 s) |
| ENet 1.3.18 | the largest unfragmented unreliable packet at `mtu` 1400 is 1372 bytes; a larger one goes as reliable fragments (seconds of delay under loss) |
| A link at 150 ms, ±10 ms, 5% loss | commands 2 ticks early, none late; about 9 ticks resimulated per snapshot; the local player's prediction error 0.00 blocks |

## Open

- **Other players at car doors**: most of the remaining large corrections of other players are with the server at a
  door. Next: a remote player the server has at a door put there at once instead of walked there by its predicted
  inputs, measured on recorded sessions (`GTA2_NET_COMPARE`, `GTA2_NET_JERKLOG`).
- **Cops** differ the most of any entity (health from hits a client predicts, animation and aim on foot): corrected by
  the next snapshot; worth a look if players see cops jump.
- **An instant gang doesn't board a bus** (on the server, so the game's own AI): the members walk to the doors and stay
  there; normal cars work. To check against a traffic bus, then the original under wine.
- **The game's results page** instead of the overlay's text; a minimum of players that counts people only.
- **More than 6 players**: about 45-50 places in the game depend on the count (arrays sized `MAX_PLAYERS`, a few
  literal 6s, a `u8` bitmask, the results screen, colours, the scripts' six `PLAYER_PED`s). A port-only cap with a
  runtime count covers most; spawn points beyond the scripts' from the restart zones.
- **Modes** beyond the game's frags, points and tag (team deathmatch, CTF) as a mode module, reusing the game's assets.
- **Hardening**: the data files' hashes and the protocol version checked at the join, rate limits, fuzzed input.
- **Tick rates above 30 Hz** would need every per-tick constant and timer scaled (a project of its own, port-only).
- **Whole-map simulation**: a server could keep everything everywhere, with a cleanup policy as a server setting
  (none, wrecks only, the game's own). Every cleanup and spawn decision goes through `Game::IsSpriteInPlayerBounds_4B9950`
  and five places (car despawn, ped and corpse removal, the object caps, the traffic and ped caps); the pools would
  need allocation that can fail or eviction before every spawn.

### Setting up games (plan)

Both ways players know: **classic** (start a server, open a port, tell friends the address) and **central** (a master
server lists them). One list, several views: the master's list is a public JSON API that a web app, the game and
anyone's tools read.

1. **Hosting from the menu**: a "Host game" page that starts a server in the background and joins it; the LAN and
   public address to share; UPnP / NAT-PMP to open the port, and a reachability test; an optional password;
   `server.ini` for a server's settings.
2. **The data check**: the client's protocol version and a hash of `data/` at the join, a mismatch turned away with a
   clear message.
3. **The master**: a heartbeat from `--public` servers over their game socket (so the master sees the address after
   NAT), verified before listing; the web app's list with join links (`gta2://host:port`).
4. **In the game**: an info query every server answers outside ENet (name, map, players, version: real pings), LAN
   games by broadcast, recent servers.
5. **Connectivity**: NAT punch-through through the master, a relay as the last resort.
6. Admin (kick, ban, map, say), quick play, a server package (a static Linux build, a Dockerfile).
