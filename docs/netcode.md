# Dedicated server netcode: plan

A plan for client/server multiplayer in the port, in the style of Quake 3 and CS 1.6:
- an authoritative server;
- client-side prediction (CSP) of the local player;
- delta compression against acked state;
- every packet within a 1400-byte MTU.

It builds on the port (`docs/port.md`): the headless build, the state log and its pointer canonicalisation.

## Status and how to run it

**Built:**
- milestone **S** (sound out of the rollback, its own random stream);
- `core/` with tests (`Scripts/port/net_tests.sh`);
- the ENet `transport/` with a loopback test;
- the engine bridge, with the game's network layer as the frame hook;
- server, client and a test bot.

A client starts the server's network game. The server takes every player's input from its client, and sends each client delta snapshots of its area: all players, cars and peds within 24 blocks, cut to 1372 bytes. A client puts the players, cars and peds into its own game: it adopts what the map script made on both sides, creates the rest, moves them every snapshot and removes what the server drops.

**Prediction (N4).** The client:
- saves the game after every tick (`engine/rollback.cpp`);
- on a snapshot of tick S: restores S, applies the server's state, and resimulates up to its own tick with its own inputs (other players: their last input);
- then moves the cars and peds it doesn't drive on by their speed (dead reckoning).

Commands carry the server tick they are for. The server buffers them per tick and repeats the last one when one is late. It reports the newest command tick it got, and the client keeps two ticks of lead by that: a jump when far off, a 10% faster or slower tick otherwise.

**Health, deaths, weapons, scores.** These are the server's:
- The player entity carries health, a death count and the last killer, the selected weapon, the ammo of the 15 personal weapons, frags and points.
- On a client, player peds take no damage of their own (the game's invulnerability counter, every tick).
- A death the server had is carried out with the game's own `Ped::Kill_46F9D0`, credited to the killer. `Player::Wasted_567130` then leaves the corpse and respawns. The deaths carried out are part of the rollback set, so a resimulation never repeats one.
- Scores are applied after the resimulation.
- So are the power-up timers (electrofingers and invisibility switched on the ped as the game does), the ammo of all 28 weapons (the car weapons too) and the tag game's "it" with each player's time as it (`SetNewFugitive_516590` when it changes).
- `GTA2_NET_TESTKILL=<tick>:<victim>:<killer>,...` kills players on the server for tests.

`--lag ms --jitter ms --loss %` puts a link conditioner on either end.

**Joining and leaving** (`engine/slots.cpp`):
- A slot without a client is out of the game the way the game handles a player who left (`Player::Disconnect_568730`): its ped walks on as a pedestrian.
- A joining client gets a new player ped at the next restart zone and an empty score. The ped is set up as the map script's `PLAYER_PED` and `Player::InitPlayerPed_565490` do, but keeps the slot's weapons.
- The server takes empty slots out at the start, puts joiners in and takes leavers out.
- Snapshots carry only the slots in the game, and clients mirror them.

**Players in cars** (`engine/vehicles.cpp`):
- A client gets into and out of cars by its own predicted input.
- When both sides have settled and the server has the player elsewhere, the client puts the ped into the server's car, or out of its own, at once. This ends the way the game's own entering does (the char back to its pool, in car and driving, `Car::AssignDriver_4406E0`) and leaving does (`Car::ClearDriver_4407F0`, `Ped::StartPedWalking_470200`).
- Another player's enter or exit key reaches a client after the tick it was pressed on, so its resimulations never press it. When the server's player is on its way into or out of a car and the client's isn't, the client starts it the way the key does, from the server's position. The walk to the door is corrected like any walking; only at the door itself is a ped left to the game.
- The cars players sit in, or are getting into, are never removed, and replicas never remove a car any ped points to (a corpse cloned in a car).

**Drivers** (`engine/replica.cpp`). A car carries its driver: none, the traffic driver, or a ped. All traffic cars share one driver, CarManager's spare (`Car::SpawnDriverPed`, `CarManager::GetSpareDriver_421960`): a ped that points to no car and is never on the street. So that one isn't sent as a ped. A car marked "traffic driver" gets the client's own spare (`Car::SpawnDriverPed`), and loses it when the server's car has none. Any other driver is a ped entity with the car it sits in. The client adopts the driver its car already has, or makes one (`Ped::SpawnDriver_470B00`), and once settled puts the ped into that car (`ForceIntoCar`). The spare never leaves a car the normal way: putting a ped into a traffic car just clears the spare.

**Cars carry their whole physics** (`engine/access.cpp`, `kPhysics`): every `CarPhysics` member but its two pointers, by name (the 64-bit layout isn't the original's), so a predicted car goes on from exactly the server's state, whatever acted on it. A **world** entity, always sent, carries the game's tick counter (`gpRng_67AB34`'s `field_0_rng`). Timers are stamped with it (a car's "damaged until", a ped's voice...), and before it was sent the client counted from elsewhere, which made its car diverge on 237 of 237 desyncs in a test drive. `GTA2_NET_CARDIFF=1` prints each member of the local car the client predicted differently from the server. A car-hopping bot, 150 s: 6 differences, all small (about 0.02 blocks), against 237 before. Getting into and out of a car still shows up to about 1.2 blocks for a moment.

**Peds carry their AI state** (`engine/ped_state.cpp`). Snapshots of peds used to carry only a position and heading; the client's AI then made its own plans and every snapshot pulled the ped back. Now a ped entity also carries the plain values the AI runs on (objectives and their status, targets, speeds, timers, occupation, type, class, flags, health, death cause) and its references (the peds and cars it is after, its gang) as net ids. A client sets them while the ped is plainly on foot on both sides; states only from a whitelist that stands on its own (walking, running, standing); all or nothing when a reference can't be resolved here (out of the area); a death the server had is carried out, one of the client's own is remade. Patrol route and pathfinder list stay local. In a soak, ped corrections of over 45 degrees fell by a third. **Not synced at all:** objects other than pickups (fires, explosions, projectiles, doors, cranes, trains, traffic lights); the client's own simulation makes them, they are rolled back with the world but never corrected.

**Filling a snapshot** (`session/server.cpp`, `SendSnapshot`). A snapshot is one packet of at most 1372 bytes. Every player, and the car each sits in, is in every snapshot wherever it is. Of the rest of the client's area (24 blocks; a camera sees 6 to 8, more at speed): what the client has stays in every snapshot, either with this tick's state or, when there is no room, with the state the client already has and its age (ticks since it was new, the entity type's last field) - a few bits. A client applies only new state and predicts the rest on. New state goes by priority, as in Quake 3's successors: the age plus one, over one plus a sixth of the distance; an entity new to the client counts as 8 ticks old. Before, the snapshot was cut at a distance: entities beyond it were removed on the client and made again later (cars vanished at speed, a bot's car didn't come and its player showed as a ped). On Tiny Town with 48 traffic cars and four players, about 50 to 100% of the area gets new state each tick.

**Test fixture** (`session/replay.cpp`): `GTA2_NET_RECORD=<file>` records a client session (the welcome, then per frame the packets, the round trip and the input). `--replay <file>` plays it back without a server, as fast as the game runs: 8 minutes of game in 10-15 s, the same every time, so a crash can be reproduced and run down. `GTA2_NET_CHECKGRID=1` checks after every step that each link in the collision grid has a live owner whose sprite it is.

With the fixture, a client crash after 1-4 minutes of car hopping turned out to be the replicas' own doing:
- **What happened:** a car's sprite stayed linked in the collision grid after the car was freed. The sprite was reused, as a roof light, a skid or a particle, and a collision query read through the dead link.
- **Why:** the grid keeps a sprite in the rows of the collision rect it was added with. The replicas turned cars after re-adding them, and moved cars that aren't in the grid: despawning ones, and ones a carrier holds (`Car::Deactivate_43AA60`, grid 3).
- **The fix:** moves now change position and rotation between removal and re-adding, and leave such cars alone. Four car-hopping clients ran 6 and 8 minutes with no failed check and no crash.

**CPU.** The game's loop spins between frames, as the original does. The port's `PeekMessageA` now sleeps 1 ms while the next frame is more than 1 ms away: a server takes about 6% of a core, a predicting client about 16%. Lockstep runs and replays don't sleep.

**Bots** (`session/bot.hpp`) are players that perceive and decide on their own:
- **What a bot sees:** a `BotView` built from what its client knows. That is its own body as predicted, the snapshot's players, cars and peds, and the static map as plain data (`engine::NavMap`: per cell the walking height and the ways to its neighbours, from the map's wall faces and slopes).
- **What it does:** it returns the input bits a person would press. It uses no game logic: no route finder, no ped AI.
- **Other brains:** smarter brains, or one that sends the view to another process and reads its answer, implement the same `BotBrain` interface.
- **Navigation:** `session/bot_nav.cpp` (A*, tested on made-up maps).
- **The simple brain:** walks its route to the nearest player and fights when close, with presses for fists and holding for guns. Walking while facing `rot` moves along (sin, cos) (measured).

Usage and test switches:
- `--bot [brain]` (`simple`, the default; `carhop`, which gets into the nearest car, drives, gets out, again);
- `GTA2_NET_NAVDUMP=<file.pgm>` writes the map as a picture;
- `GTA2_NET_TESTARM=<weapon>:<ammo>` keeps every player armed on the server.

**Trained bots.** `--bot nn:<file>` is a multilayer perceptron (`session/bot_nn.cpp`). Its 59 inputs (`BotFeatures`) are its health, car, speed, weapon and ammo; the nearest 3 players, 2 cars, a ped, 2 weapon pickups and a power-up relative to where it faces; how far it can walk in 8 directions; a compass, the way along its own route (`BotRoute`) to the nearest player and to the nearest weapon; and its last buttons. Its 14 outputs are a choice in each of six groups (turn, move, attack, enter/exit, jump, next weapon), the best one or a sample. Two trainers make the files:
- **The arena:** `--server --arena <brain>,<brain>,... --ticks N [--seed S]` plays bots on the server itself, in the first slots, with no clients (`session/arena.cpp`). At tick N it prints a result line per bot (frags, deaths, damage dealt, cars entered, pickups taken) and ends. The other slots stay open, so people can join and play against the bots. The game is deterministic: without another seed every match from the same start is the same.
- **Fast:** `GTA2_SPEED=<k>` speeds up the clock on macOS and Linux too (`Port/posix_win32.cpp`); headless at k = 1000 a match runs at about 11,000 ticks/s, a 150 s match in under half a second.
- **PPO** (`Scripts/port/rl_bots.py`, torch): `remote:<port>` slots are played by the trainer over a local TCP connection, a decision every 3 ticks, with the reward since the last one (a frag +1, a death -1, 0.01 per point of damage dealt, 0.3 per new kind of weapon, a little for being near someone and for getting nearer). One policy plays five slots of each match; in half the matches two of them are earlier copies of it; the scripted bot plays the sixth as a yardstick. No free weapons: they come from the map's pickups (`--armed`: pistols for all). The trainer, not the games, is the limit: 32 games at once keep its batches full.
- **Kickstarting:** from scratch, PPO learned nothing in 2 million decisions (the scripted bot farmed it). Each remote slot also runs the scripted bot on the same view and sends what it would press; an imitation loss on that, fading over the first 150 updates (`--teach`, `--teach-updates`), got the policy fighting, and once it was gone the policy went on to beat its teacher (armed, after 400 updates: 4 frags and 3 deaths a match against the scripted bot's 1.3 and 5.4).
- **Weapons and cars:** the scripted bot fetches the nearest weapon when it has none and nobody is within 2 blocks, so the teacher shows that too. Without the compass the policy never learned it (Tiny Town's pickups are behind buildings). The teacher is now "hunter": the rammer when a car is within 10 blocks and nobody within 8 (and once it drives), the fighter otherwise. Two teachers on different slots (rammer and fighter) taught nothing: they showed opposite things on the same view. With the hunter the policy took about one car a match while the imitation lasted, and gave cars up once it was gone (0.03 a match), with more frags on foot (2.2 against the scripted bot's 1.3): to the rewards, driving doesn't pay at its level. The pool keeps the driving versions (`pool/`). Out: `build_port/rl/` (`latest.nn`, `policy.pt` to continue, `pool/`, `log.csv`).
- **Evolution** (`Scripts/port/train_bots.py`, no dependencies): five networks and the scripted bot per match, pistols kept loaded (`GTA2_NET_TESTARM`), the best 8 of 40 kept and the rest their mutated copies. It levelled off below the scripted bot after about 150 generations.
- **Watching:** `rl_bots.py --watch [file.nn]` (or `train_bots.py --watch`) plays a match in a window at normal speed, following player 0. `--spectate K` opens one every K updates (generations) during training.

**Measured** (bots on Downtown, 60 s):

| Link | Result |
|---|---|
| Loopback | 28-107 bytes per snapshot; both clients see the same positions; up to 13 cars and 11 peds mapped; prediction error 0.00 blocks |
| Four slots, two bots, a third joining after 40 s, one leaving after 80 s | The joiner spawns at a restart zone and everyone sees it; the leaver disappears everywhere; no crash. |
| 80 ms round trip, three armed bots on Downtown, 5 minutes | The bots find each other and fight: frags and deaths on the server (for example 1/1, 2/3, 2/1), every client shows them and carries out each death once; no crash, nobody drops; prediction error 0.00 blocks. |
| 100 ms round trip, 2% loss, test kills (4 deaths, one without a killer) | Every client shows the server's frags and deaths and carries out each death exactly once (2, 1, 1). |
| 80 ms round trip, two car-hopping bots and a fighting one, 5 minutes, grid check on | Every traffic car in a snapshot has the traffic driver on the client too (up to 16 at once); a ped driver shows in its car; no crash, no failed check. |
| 150 ms round trip, ±10 ms jitter, 5% loss | Commands 2 ticks early, none late; about 9 ticks resimulated per snapshot; **prediction error 0.00 blocks**. Cars and peds are shown 0.04-0.05 blocks from the server's on average, at most about 0.6 (0.07-0.10 without dead reckoning). |

**Not yet:**
- passengers;
- bots that fight well (they find each other, but often stand next to each other without hitting);
- ped state beyond position (cars have their whole physics now);
- interpolation;
- the comparison check;
- modes beyond the game's own frags;
- the player cap above 6.

```
Scripts/port/build_mac.sh                                    # or build_linux.sh / build.sh
GTA2_HEADLESS=1 GTA2_ARGS="--server --map downtown-6p --players 4" Scripts/port/run_mac.sh
GTA2_ARGS="--connect 127.0.0.1 --name me" Scripts/port/run_mac.sh                  # play with the keyboard
GTA2_HEADLESS=1 GTA2_ARGS="--connect 127.0.0.1 --bot --name bot1" Scripts/port/run_mac.sh   # a test bot
```

Server options: `--port` (27920), `--map <data/*.mmp>`, `--players 1-6`, `--gametype 1-3`, `--fraglimit`, `--timelimit`, `--tickms`, `--police` (the lobby's police box, off by default there too), `--traffic N`, `--packets N`.

**Jerkiness** (`GTA2_NET_JERKLOG=1`, 2 also prints each jump): per entity and frame, the change of its step, on each client (what is shown) and on the server (the game's own motion). Tiny Town, police on, two rammers, a fighter and a car hopper, 100 ms ±10 ms, 200 s: cars move as smoothly on the clients as on the server, peds have twice the server's small changes but as few large ones, remote players three times (prediction from their last input, eased by the blend, `GTA2_NET_BLEND`, 0.2 by default). Most steps over 0.4 blocks were a player getting into or out of a car. Starting other players' entries from the snapshot, and correcting the walk to the door (including the walk after a car that still moves, states 3/4, which isn't at the door yet), took them from 28 to 4 in four recorded sessions. What is left is a ped reaching the door about half a block from where the server's did.

Client options: `--connect host[:port]`, `--name`, `--bot`.

Either end: `--lag`, `--jitter`, `--loss` (the link conditioner).

## Decisions

| Topic | Choice |
|---|---|
| Authority | The server simulates everything and clients send inputs. Clients are no longer deterministic: they apply state. |
| Ambient world (traffic, peds, cops) | Server-owned. Each client gets the entities in its **interest area** (around its camera). |
| Prediction | **Whole-world rollback** on the client, plus a declared **net schema** per entity type (only the fields that matter go over the wire). A full-world state compare checks that the schema is complete. |
| Transport | **ENet**: unreliable snapshots and inputs, reliable events. |
| Remote entities | **Configurable**: interpolation, dead reckoning, or interpolation that extrapolates on loss. |
| Projectiles | **Configurable**: rewind on spawn, or predicted cosmetic plus server spawn. |
| Players | **Any number**: no fixed count in the protocol (player ids are a varint, byte-sized for up to 255). In the game, `MAX_PLAYERS` (28 uses, ~10 loops written as `< 6`) becomes a port setting. Tested at 16. |
| Game modes | **Frags first.** Team deathmatch, CTF, points and tag later, so the rules live in one mode module (scoring, spawning, teams, win condition) rather than in the netcode, and teams are part of the player state from the start. |
| Joining | **Mid-game join**: a full state of the interest area over the reliable channel (~20 KB, about 15 messages), then deltas (see Wire format). For now a joiner **spawns at once** at a restart zone with an empty score. The join is a step of the mode module (`OnJoin`: where, when, with what score, which team), so a mode can later add a respawn delay, spawn protection, team balance or a spectator start. |
| Spawn points | The map's **Restart zones** (`GetNextZoneOfTypeRoundRobin_4DF0F0(0x10)`, already used for multiplayer respawns), plus an **optional per-map file** next to the `.mmp` with extra points. The scripts' 6 `PLAYER_PED` points stay for the first 6 players. |
| Program | **One binary** for client, server and bots: `--server`, `--connect <host[:port]>`, `--bot`. A separate server target can be split off later. |
| Connecting | **Command line** (`--connect`). A frontend page and a server browser come later. |
| Other players on the HUD | GTA2 shows arrows to every player. Whether every player's position and score go to every client, or only those in the interest area, is a **mode option** (`sv_players_always_visible`). |
| Network-game rules | The original multiplayer rules (police at most wanted level 1, gangs with basic weapons, car weapons at a tenth, all car models, endless respawns) become **server settings**, each one on by default as in the original. |
| Player colours | **New remaps** for players beyond the 7 palettes (`Player::ColorScoreFromRemap_569A10`, `Hud_Arrow::SetPlayerArrowColour_5D0DC0`). If that turns out to be complicated (style data), it is deferred and the extra players share colours. Team modes colour by team. |
| Team modes, CTF | New mode code that **reuses the game's assets**: a flag is a pickup object, its carrier gets the HUD arrow, scores use the existing HUD digits and the results screen. |
| Cleanup | **Stock GTA2** cleanup at first. It still goes through the cleanup-policy hooks (see "Whole-map simulation"), so modes such as `none` or `wrecks` are a setting later, not a rewrite. |
| Bots | **Yes**: a headless client with scripted or AI input, over the real network path; also how 16 players are tested. **A simple bot first**: chase the nearest player, shoot when facing them, take a car, follow road routes. Better bots later. |
| Code | `Port/net/` (protocol, ENet wrapper, schema, rollback), `Port/server_main.cpp`. |
| Platforms | Server mainly **Linux**, cross-platform (Windows, macOS); ENet 1.3.18. |
| Rates | **Server settings**: `sv_tickrate`, plus a snapshot rate per client (up to the tick rate). See "Tick rate" below: the game's logic is written per tick, so rates above 30 Hz need the sim scaled. |

### Tick rate

The rate is a server setting because it has to be tuned; fast games run at 64 or 128 ticks per second. In GTA2, though, the simulation is written **per tick**, not per second:
- speeds, accelerations and turn rates are added once per tick;
- timers count ticks (for example 130 ticks until a car despawns, 40 for a ped);
- animations advance a frame per tick.

The original's game speed setting only changes the wall time of a tick (50, 40 or 33 ms), so a faster tick makes the whole game run faster.

| Rate | What it takes |
|---|---|
| 20-30 Hz (50-33 ms) | Nothing: the original's range. The first version offers this. |
| Above 30 Hz | Either run the 30 Hz simulation and raise only the *network* rates (inputs sampled and sent faster, snapshots each tick; this lowers input delay a little but not the tick), or scale the simulation: every per-tick constant and timer for a tick of 1/N s (a separate project through Car, CarPhysics, Ped, Char, Object, Weapon and the timers; it has to stay port-only to keep the match). |

The netcode keeps the tick length a variable from the start: in the clock sync, the input buffer, the interpolation delay and the snapshot timing, so a higher rate later needs no protocol change.

## Why the engine shapes the design

- **No per-entity update.** `Game::UpdateGame_4B9410` runs every subsystem once per tick: peds, cars, objects, police, particles, doors, trains, and so on. Q3 runs one player's usercmd on its own, on arrival. Here neither the client nor the server can simulate just one player. Two things follow:
  - The **client** predicts by rolling back the whole world and resimulating it.
  - The **server** applies each player's input at a given server tick. So the client has to run *ahead* of the server (the Overwatch/Rocket League model, not Q3's), and the server keeps a small input buffer per player.
- **The original multiplayer is deterministic lockstep.** `NetPlay` sends one `u32` of inputs per player per tick (`Network_InputData`) plus sync checks. That whole layer is replaced. The game keeps its input entry points (`Player::SetInputs_565740`), and the server feeds them from the network.
- **State is fixed-point.** Positions are `Fix16` (14 fractional bits) and angles are `Ang16` (0-1439). A position plus rotation fits **losslessly** in 72 bits: x and y take 22 bits each (256 blocks), z 17, angle 11. The client gets exactly the server's numbers, so predicted and server state agree to the bit when nothing else interferes. No quantisation error feeds back into the prediction.
- **State is pools, allocated once.** About 3.4 MB in total:
  - cars 306, peds 200, chars 400, objects 3825, weapons 255, sprites 5031;
  - plus doors, cranes, trains, explosions and so on.

  The state log already lists every global and heap block (its "entries"), and it maps pointers to `(entry, offset)`. The same registry gives the rollback set and the entity ids.

## Code layout

The netcode lives in `Port/net/`. **Only `engine/` includes decomp headers.** Everything else works on plain data, so the decomp can keep moving (renames, struct fixes, new matches) while the netcode only follows it in one folder.

```
Port/net/
  core/        engine-agnostic, unit-tested without the game: bitstream, protocol constants and messages,
               entity states and their delta coding, input commands
  transport/   the network: an interface (hosts, peers, channels, events) and its ENet implementation,
               plus a link conditioner (delay, jitter, loss) for tests
  engine/      the bridge to the decomp, the only code that knows its types (engine.hpp: what the session sees;
               internal.hpp: what the engine's files share):
               world.cpp     the rollback set (every global, what it points to, players, light grid, sound pool)
               rollback.cpp  saving, restoring and resimulating a tick (the sound's mailboxes emptied)
               netplay.cpp   the game's network layer (NetPlay/NetUi) for the port: how the game loop and the
                             netcode meet, and the offline multiplayer start
               schema.cpp    the net schema: every entity type's fields
               access.cpp    a snapshot captured and applied as a whole, the game's queries, shared helpers
               players.cpp   players as entities: health, deaths, weapons, power-ups, names, their cars
               cars.cpp      cars as entities: place, physics, damage, lights, colour
               car_ai.cpp, ped_state.cpp  a car's and a ped's AI state (members.hpp: member tables by name)
               replica.cpp   a client's cars and peds: adopted, made, moved, removed
               vehicles.cpp  getting into and out of cars at once, or started as the enter key starts it
               pickups.cpp, slots.cpp  pickups; slots joining and leaving a running game
               client_mode.cpp  what a client doesn't simulate (spawning, despawning, scripts, police)
               sound_pool.cpp   SoundPosEntry from a pool in the rollback set (milestone S)
               navmap.cpp    the static map for bots
               debug.cpp     checks, logs and statistics (the GTA2_NET_* switches)
  session/     server, client, bots, the recording and replay, the jerk measure (env.cpp: the switches)
  enet/        ENet 1.3.18, vendored (MIT)
  tests/       core tests (Scripts/port/net_tests.sh)
  net_main.cpp the command line (--server, --connect, --bot) and the session's lifetime
```

Dependencies only go one way: `session` uses `core`, `transport` and the engine interface (`engine/engine.hpp`, plain types only); `engine` uses the decomp and `core`'s data types.

**How the game and the netcode meet.** The game's network game loop already has the boundary we need: the DirectPlay layer, `NetPlay`. Every network frame, `RunGameFrame_4DA850` runs:
1. the tick (`ExecuteGame_4DA780`);
2. `do_network_and_local_inputs_4DAF30`, which samples the local input, sends it with `NetPlay::SendToAll_521B20`, draws, receives the other players' inputs, and sets every player's input for the next tick.

The port's `NetPlay` (`engine/netplay.cpp`) makes `SendToAll` the netcode's per-frame hook: after tick T, before the draw. There the server reads its clients and sets every player's input for tick T+1; the client sends its command, applies snapshots, and later rolls back. The netcode needs no frame-loop code in `Source/`.

**What it does need in `Source/`:** a short list of `#if GTA2_PORT` lines, and nothing more:

| Where | Why |
|---|---|
| `sound_obj.cpp`: the random table on its own stream (`Port/port_rand_sound.h`) | Muting sound during a resim mustn't change the sim's random numbers. The stream is shared by default, so the replay checks against 10.5.exe stay exact; the netcode switches it on. |
| `sound_obj.hpp`: `SoundPosEntry::operator new/delete` | From a pool that is part of the rollback set, so a restore can't bring back freed sound entries. |
| (later, N6) `types.hpp`: `MAX_PLAYERS` | A port-only player cap. |

Each one stays a single include or declaration, so it moves along with the decomp without merge pain.

## Architecture

```
server (headless port build, Linux)          client (port build, any platform)
  ENet host                                     ENet peer
  per tick (30 Hz):                             per frame:
    pop each player's input for this tick         sample input -> cmd(tick T+ahead), send last N cmds
    Game::UpdateGame (full sim, scripts)          on snapshot(S): restore world@S, apply snapshot,
    capture net state of every entity               resim S+1..now with own cmds (rollback)
    per client, at its snapshot rate:             predict tick now+1 (local sim)
      interest set -> delta vs acked baseline     draw: local = predicted,
      -> priority-sorted, cut at budget             remote = interp / dead reckoning (cvar)
      -> send unreliable
```

### Server
- **The headless build plus a server main.** `Port/headless` has null video and a fast clock; replace the clock with a 33 ms fixed-tick loop. There is no local player: every player slot is network-driven.
- **Scripts (`.scr`) run only on the server.** Their visible results reach clients as replicated state (doors, generators, cranes, wanted level, scores) or as reliable events (HUD text, arrows, timers, phone messages, kill frenzy).
- **Interest area.** The camera rectangle of each player plus a margin; the camera zooms out with speed, so the area is derived from the replicated camera height. Traffic already spawns around each player's view in the original MP (`ReduceTrafficSpawnCounterOfOtherViewers_4B9D60`), so the server's world already contains what every client needs.
- **Validation.** The server checks the data-file hashes (`.gmp`, `.sty`, `.scr`) and the protocol version during the handshake.

### Client
- **Never spawns or destroys replicated entities on its own.** Ambient spawners, AI decisions with lasting effects (spawning, deleting) and scripts are off in client mode.
- **Still runs the full simulation.** Every replicated entity moves by the engine's own physics between snapshots, so dead reckoning comes free.
- **Creates only cosmetic things itself:** particles, decals, sounds, camera shake.
- **Exception, predicted spawns:** the player's own projectiles in the cosmetic-prediction mode (see Projectiles).

### Rollback
- **History.** Keep a ring of full-world states, one per tick for the last ~0.5-1 s: 15-30 × 3.4 MB, about 50-100 MB.
- **On a snapshot for server tick S:**
  1. Restore world@S.
  2. Apply the snapshot through the schema.
  3. Create and destroy entities as the snapshot says.
  4. Resimulate S+1 .. now with the client's stored commands.
- **Remote players' inputs during resim:** each player's last input is replicated (a few bits), and the resim repeats it. This is dead reckoning that uses the real game logic.
- **No side effects during resim.** A `resim` flag mutes sounds, particles, HUD messages and camera shake. These hooks live in `Source/` under `#if GTA2_PORT`, so VC6 is unaffected.
- **The RNG state** (`port_rand`'s `sState`) is in the snapshot set, so a resim draws the same numbers.

### Net schema: "set what matters"

One table per entity type, like Q3's `netField_t`. Each field has an accessor, a bit width and a kind: int, `Fix16`, angle, enum, or **ref**. A ref is a pointer to another entity, sent as a NetId. Two operations:
- `Capture(entity) -> NetState`: plain data, compared and delta-encoded by field.
- `Apply(entity, NetState)`: writes the fields **through engine functions** wherever the engine keeps derived state. Examples: moving a sprite relinks it in the collision grid; seating a ped in a car sets both sides of the link. Raw writes leave the grid and back-links stale.

A starting set, refined by the correctness check below:

| Type | Fields |
|---|---|
| Car | position, angle, velocity, angular velocity, model, remap, damage areas, health, doors, siren/horn, driver ref, trailer ref, engine/burning state |
| Ped / Char | position, angle, state/sub-state, health, armour, weapon, occupation, car ref + seat, target ref, remap |
| Object | position, angle, type, state/timer |
| Player | ped ref, inputs (last), weapons and ammo, money, score, lives, wanted level, multiplier, power-up timers |
| World | doors, cranes, crushers, trains and buses, traffic-light phase, generators, explosions, fires |

**NetId** = `(pool, index, generation)`, about 16 bits. The generation is bumped on every pool allocation, so a stale id never points at a reused slot.

**Correctness check.** This is the full-state comparison you asked for, using the existing state log tooling:
1. Run the server and a client on loopback with no latency and no loss.
2. After the client applies snapshot S, and before it resimulates, hash the client's world with `state_log`'s masked 64-byte chunks.
3. Compare those hashes with the server's world at S, restricted to the interest set.
4. Any chunk that differs is either a field the schema is missing, or something cosmetic that belongs in an `IGNORE` list (particles, render counters).

This is the same loop as the attract-demo replay, with the server as the reference instead of 10.5.exe. The same check measures prediction error under simulated latency: the local player's state predicted for tick S against the server's state at S.

## N0 measurements (native macOS, arm64, `-O2`)

`GTA2_SIMBENCH=<flip>[:<ticks>]` or `GTA2_SIMBENCH=p<peds>[:<ticks>]` (`Port/headless/sim_bench.cpp`, every port build) runs that many sim ticks back to back once the game reaches that flip, or once that many peds are alive. It runs them without drawing, prints the cost of each service, the full-world size and save/restore time, and a rollback check, then exits. The busy scene below is an attract demo through Downtown traffic:

```
GTA2_HEADLESS=1 GTA2_SIMBENCH=p40:3000 Scripts/port/run_mac.sh
```

| | Result |
|---|---|
| World (registered globals and what they point to, players, light grid) | 1,763 blocks, **3.53 MB**; save **0.06 ms**, restore **0.055 ms** |
| One sim tick, busy scene (73 cars, 39 peds, 176 objects) | **~30 µs** (30,000-43,000 ticks/s); an idle player in a quiet area is 9 µs |
| A 10-tick resim (300 ms RTT) | ~0.3 ms plus one restore: ~2% of a 60 fps frame |
| Rollback: save, 30 ticks, restore, the same 30 ticks | **Reproduces the world exactly** with `skip_audio`. With audio on, one byte of `gSound_obj` differs (the mixer's real-time sample status); nothing in the gameplay state does. |

Cost by service:

| Service | Share of a busy tick |
|---|---|
| cars (physics, AI, traffic) | 52% |
| peds | 28% |
| objects (projectiles, pickups) | 13% |
| everything else together (HUD 2%, players 1.4%, ...) | ~7% |

Whole-world rollback holds with a wide margin, so speed gives no reason to cut anything out of the client. The server simulates up to 16 interest areas, so its world is a few times larger, still well under a millisecond per tick.

The check found two pieces of state outside the registry:
- the `Player` objects (`Game::field_4_players`);
- `gLightGrid_6F5400`, which points to an array of which the registry knows only one element.

The bench adds both. To make the rollback set complete by construction, the client could allocate all game memory from one arena and snapshot the arena together with the globals.

### What the client runs (client mode)

Every service is one call in `Game::UpdateGame_4B9410`, and several already have a debug skip flag (`bSkip_mission`, `bSkip_police`, `bSkip_ambulance`, `bSkip_particles`, `bSkip_traffic_lights`). Cutting whole services is a flag each. The finer cuts are single calls inside the services.

| Service | Client | Why |
|---|---|---|
| players, cars, peds, objects (movement, physics, collisions, weapons) | **run** | This is the prediction, for the local player and for dead reckoning of the rest |
| traffic spawn (`TrafficSpawner::GenerateTraffic_583670`, in the car service) and dummy peds (`PedManager::Dummies_470330`) | **off** | Only the server creates entities |
| despawning of far entities (inside the per-entity updates) | **off** | Only the server destroys; these checks still have to be found |
| scripts, police, ambulance, firefighters, emergency crews, generators, garages | **off** | They create entities or change game state; the results arrive replicated |
| doors, cranes, crushers, trains/buses, traffic lights | run or off | Deterministic from their state, cheap; running them predicts them, and the snapshot corrects them |
| particles, tile animations, lights, HUD, sound | **run, cosmetic** | Client only, and muted during resim |

## Whole-map simulation and tunable cleanup

The server can keep everything that exists anywhere on the map, simulated and kept until a cleanup policy removes it. With many players the map then builds up its own history: wrecks, bodies, police chases nobody is watching. How much cleanup a server runs becomes a game mode:
- **none**: the chaos builds over time;
- **wrecks only**;
- **stock GTA2**: clean up whatever no player sees.

### Where GTA2 decides

Every cleanup and spawn decision goes through one predicate: `Game::IsSpriteInPlayerBounds_4B9950`, the camera rectangles of the players. The `IsSpriteInAnyPlayerBounds_4B97E0` family, `Char::IsOnScreen_545700` and `Car::IsOnScreenForAnyPlayer_43B730` / `IsVisibleToAnyPlayer_43B750` all build on it. A headless server already has the cameras; drawing doesn't feed any of it. Every live entity is updated every tick, wherever it is.

| What | Where | Rule now |
|---|---|---|
| Car despawn | `Car::ManageDespawning_442310`, `CanDespawn_442200`, `UpdateCarDespawnStatus_4424C0`, `IsCarInConditionsToDespawn_442170` | Marked after 130 ticks unseen (emergency vehicles 300), then removed once no player sees it. Never: trains, the 3 cars in a player's history, mission cars, special peds' cars. |
| Wrecks | `HandleCarExplosion_43D840` → `StartWreckCountdown_441380` (50 ticks) → `TurnToWreck_4436A0` | No own timer: a wreck is a car at maximum damage and goes the normal 130-tick way. |
| Ambient peds | `Ped::StateMachineTick_4626B0`, `field_20E_offscreen_counter` (reset in `UpdateOnScreenCounter_462620`) | Dummies after `gPedOffscreenDespawnTime_678656` = 40 ticks unseen (fleeing: 200); occupation and special peds after 100. Player and script peds never. |
| Corpses | `Ped::PoolUpdate` (`Ped.cpp:4453`) | Any non-script ped more than 200 ticks off screen. |
| Ped removal | `Ped::Deallocate_45EB60`, `DeallocateWithGroupCleanup_45EA00`, `PedPool::UpdatePool_445960` | |
| Objects | `Object::PoolUpdate` (`Object_5C.cpp:1945`), `field_25_removal_state` | Timers and animations (debris, fire, explosions, pickups on touch); no camera except `RequestRemovalWhenOffScreen_5290B0`, used only by mission cleanup. Caps: 360 objects of kind 2, a FIFO of 88 of kind 3 (`TrimSpriteList_529300`). |
| Particles | `Particle_4C::PoolUpdate` | Timers only; allocation checks for free particles and sprites. |
| Traffic spawn | `TrafficSpawner::GenerateTraffic_583670` → `MakeTrafficForCurrCamera_5832C0` → `SpawnTrafficCar_582480` | At the edges of each player's camera, gated by: **16 ambient cars for the whole game** (`recycled + proto_recycled`, `CarManager::CanAllocateCarOfKind_446930`, which also caps parked 200, police 6, mission 24...), a cooldown (`CarManager::field_54_spawn_block`), the per-player pacing `Player::field_680_traffic_spawn_counter` (`SelectTrafficCarModel_444AB0`), and zone car density. `skip_recycling` turns generation off altogether. |
| Ped spawn | `PedManager::Dummies_470330` → `SpawnDummies_46EB60` | At most `field_0_max_dummy_chars` = 50 dummies (halved above wanted level 3), at the camera edges, while the on-screen count is under the zone's ped density / 25. |

**Pools.** Every capacity is now `GTA2_POOL(n)` (`Pool.hpp`). It is the literal `n` except in a port build with `-DGTA2_POOL_SCALE=<k>`.
- **They have to grow together.** Sprites (5031) and sprite shapes are the sum of cars 306 + chars 400 + objects 3825 + particles 500. CarAI and CarPhysics match the car pool. The collision grid's links (4096) and cells (6000) run out first: at 282 cars in the stock pools, `GridSpriteLink` was the pool that crashed.
- **A full pool crashes.** Every `Allocate` dereferences an empty free list; only particles check for space first. The game is safe only because of its logical caps (16 traffic cars, 50 dummies, 360/88 objects).
- **Size-dependent fields:**
  - `PedRefTable[255]` with `u8` indices; it returns "no ped" when full.
  - `PedManager`'s `char_type` counters (they overflow above 127).
  - `Object::SaveObjects_52A500`'s literal 3825.
  - `PedListNode` (100), shared by the threat list and every car's passengers.

**Linear scans that grow with the world:**
- `Ped_List::FindClosestPedInViewCone_4713C0` and the threat-list add/remove (peds × threats);
- `PedManager::PedById` (a full ped-pool scan, ~20 callers);
- `CarManager::DoGetNearestCarFromCoord_444FC0`;
- `Object::PoolTake_522360`;
- the sorted per-row lists of the sprite grid.

### Benchmark: no cleanup, 4× pools (`build_port/mac_x4`)

```
OUT=build_port/mac_x4 Scripts/port/build_mac.sh -DGTA2_POOL_SCALE=4
GTA2_BIN=../../mac_x4/gta2_port GTA2_SIMBENCH_FILL=600 GTA2_SIMBENCH_FILL_PEDS=1 GTA2_LOCKSTEP=1 \
  GTA2_DEBUG=skip_audio GTA2_HEADLESS=1 GTA2_SIMBENCH=300:3000 Scripts/port/run_mac.sh wil
```

The fill moves the player's camera to random places in Wil and calls the game's own traffic and ped spawners there, with the caps, the cooldown and the pacing lifted. The traffic gets drivers, AI and routes. During the 3,000 measured ticks nothing is cleaned up: car recycling is off, and every ped's off-screen counter is reset each tick.

| Cars | Peds | Tick | Cars per car | Peds per ped |
|---|---|---|---|---|
| 52 (start, idle) | 2 | 8 µs | | |
| 149 | 58 | 72 µs | 0.34 µs | 0.30 µs |
| 299 | 136 | 181 µs | 0.43 µs | 0.34 µs |
| 599 | 329 | 447 µs | 0.52 µs | 0.38 µs |
| 983 | 661 | **891 µs** | 0.61 µs | 0.42 µs |

- **Scaling.** Slightly worse than linear: about n^1.3 for cars, n^1.2 for peds. The grid lists and the full-pool scans above are the likely causes.
- **Budget.** A thousand cars and 660 peds take 2.7% of the 33 ms tick. Extrapolated, 4,000 cars and 2,600 peds are ~5 ms, still a sixth of the budget. Cars and peds are 99% of the cost.
- **The client's rollback copy** with 4× pools is 9.3 MB; save 0.16 ms, restore 0.16 ms. The rollback is still exact.
- **Grid links.** At 983 cars, 11,000 of 16,384 links were free. The pools at 4× hold ~1,200 cars (the fill stopped at the ped pool: every traffic car needs a driver).

The bench also found a 64-bit bug that crashed the Mac build whenever a ped touched a save point. `CarThreadTriggerCmd` (script data) had `ScriptThread* field_8_thread` in a union, so its later fields moved on 64-bit; it is now `PTR32`. Separately, attract mode with `skip_audio` desyncs the demo replay, probably because sound code shares the game's `rand()` (not confirmed). Use the `wil` start for benches.

### Effort

| Step | Work | Size |
|---|---|---|
| 1. Pool sizes | Done for the benchmark (`GTA2_POOL`). For the server: a size per pool rather than one factor, and check the size-dependent fields above (`PedRefTable`, the `char_type` counters, `PedListNode`). | small |
| 2. Allocation that can fail | `Allocate` returns NULL on an empty pool, in port builds; the ~40 unchecked callers (listed by the survey) back off; or simpler, **eviction under pressure** before every spawn, so the pools never fill. | medium: one central check plus eviction, or many call sites |
| 3. Cleanup policy | One `Port/cleanup` module with the categories (traffic, ambient peds, wrecks, corpses, emergency, dropped items, burning objects), their "unseen for" timers, caps and oldest-first eviction, and the `sv_cleanup` modes. Hooks in the five decision points: car despawn, ped off-screen removal, corpse removal, object kind caps, traffic/ped caps. Each hook is a `#if GTA2_PORT` call at the existing check. | medium |
| 4. Spawn by density per player | Replace the global 16-car and 50-dummy budgets with "cars/peds within N tiles of this camera" (the sprite grid can count them); keep the cooldown and pacing per player. | small to medium |
| 5. Interest areas | The server's "seen" predicate becomes "inside any player's interest area", the same areas the snapshots use; `IsSpriteInPlayerBounds_4B9950` is the one place. | small |
| 6. Hot spots | Only if the larger maps need it: `PedById` by a table, the threat list as a set, `DoGetNearestCarFromCoord` through the grid. | small each |
| 7. Check | The bench in `GTA2_SIMBENCH_FILL` form per mode, plus a long soak (an hour of game time at 16 bot players), watching the pool headroom that `Counts` prints. | small |

Steps 1, 3 and 5 make the modes work; 2 makes "none" safe; 4 keeps the traffic around players right when the rest of the map is full.

## Evaluations (before building)

Measured with `Port/headless/neteval.cpp` (`GTA2_NETEVAL=heap,rand,churn,diverge,single,explode`), the offline multiplayer experiment in `Port/net/engine/netplay.cpp` (`GTA2_OFFLINE_MP`) and an ENet probe. The probe was built in a scratch folder and is not in the repo. None of this is netcode; it is what the design rests on. Native macOS, busy attract demo or Wil unless noted.

### 1. What changes per tick (schema and snapshot size)

`GTA2_NETEVAL=churn` diffs every pool's slots tick by tick for 300 ticks. The fields come from clang's 64-bit record layouts (`-fdump-record-layouts`).

| Type | Size | Fields that change | Bytes changed per changed entity per tick | The biggest changers |
|---|---|---|---|---|
| Car | 264 | 25 | 1.1 | `last_seen_timer` (local), doors, flash phase, damage |
| CarPhysics | 192 | 33 | 6.2 | `cm1`/`cp1` (centre of mass points), velocity, linear velocity, gas, skids |
| CarAI | 144 | 19 | 1.6 | timers (local), angle, speed |
| Ped | 776 | 65 | 5.0 | position (`field_1AC_cam`), off-screen counter (local), move target, facing and aim angles |
| Char | 208 | 42 | 3.7 | animation frame and timers, velocity, rotation, state |
| Sprite | 80 | 14 | 2.7 | position, sprite index (animation), z, rotation |
| Object | 64 | 10 | 15.6, but only 1.3 objects per tick | sprite, `pAny`, id: objects mostly appear and disappear |
| Player | 2400 | 7 | 68 | the two cameras, scores |

- **Mostly a few dynamic values.** The state that really moves per entity: position (x, y, z), rotation, the car's two physics points and two velocities, gas, the ped's targets and angles, the char's velocity, state and animation frame.
- **A large share is local counters** that the client can run itself: off-screen counters, last-seen timers, animation frame timers, AI tick counters.
- **Peds have the widest state** (65 fields in 5 minutes). That is the part of the schema to build first and check with the comparison oracle.
- **Snapshot estimate.** The whole demo world changes ~830 bytes per tick, ~680 without the local counters, at byte granularity. The interest area holds about half of it: 15 cars, 35 peds on foot, 72 objects in 26 × 20 blocks. With an id and field mask per entity, that is **~500 bytes per snapshot at 30 Hz**, ~600 at 15 Hz. It fits the 1372-byte cap (see 7) with room to spare. Busier scenes rely on the priority cut.
- **Join estimate.** A full state of the interest area, from the fields that ever change plus the static ones (model, remap, type), is **~20 KB** uncompressed: about 15 reliable messages of 1372 bytes.

### 2. How fast a client's own simulation drifts (`diverge`)

The same state is simulated K ticks twice, the second time with a different random stream: the worst case for a client simulating other entities itself.

| After | Cars off by > 1 px | Cars > 1 block | Peds off by > 1 px | Peds > 1/4 block | Peds > 1 block |
|---|---|---|---|---|---|
| 1 tick (33 ms) | 0% | 0% | 0% | 0% | 0% |
| 3 ticks (100 ms) | 0% | 0% | 0-3% | 0% | 0% |
| 10 ticks (330 ms) | 0-0.3% | 0% | 0-17% | 0-1% | 0% |
| 30 ticks (1 s) | 0.3-1.2% | 0-0.3% | 46% | 5-14% | 0% |
| 90 ticks (3 s) | 2-5% | 2-4% | 55-70% | 18-42% | 0-27% |

The local player doesn't drift at all (its input doesn't depend on randomness).
- **Running the full sim on the client works well** for everything at normal latencies. Within 330 ms nearly everything stays within a pixel. So `cl_remote_mode extrap` (dead reckoning by the game's own sim) is a good default.
- **Interpolation matters mostly for peds** past ~300 ms and for lossy links.
- **Traffic cars hardly drift** (they follow the road arrows).

### 3. Random numbers and sound (`rand`, `heap`)

- **The sim draws 67 random numbers per tick; the sound service draws 5,** all from the game's one `rand()` stream. The 5 come from `sound_obj::GenerateIntegerRandomNumberTable_41BA90`, which refills a 5-entry table every service. A client that mutes sound during a resim draws differently and mispredicts on its own. **Sound needs its own random stream** (a port-only `rand` for that table).
- **During ticks, only sound uses the heap:** `sound_obj::AddSoundObject_419FA0` allocates a `SoundPosEntry` per sound entity, and `FreeSoundEntry_41A090` frees it. The sim itself allocates nothing.
- **Sound breaks a rollback.** A restore brings back pointers to sound entries that were freed in between: heap corruption, and one "pointer being freed was not allocated" abort. Network games clear `skip_audio` (`ClearDebugFlags_4DB170`), so they hit it. **The client keeps sound out of the rollback set:** the sound entities as a separate, non-rolled-back layer, or a pool instead of `new`.

### 4. A headless multiplayer game (`GTA2_OFFLINE_MP`)

The offline stubs in `Port/netplay_offline.cpp` answer "connected, N players, settings, map" instead of "no network". With `-N` (`GTA2_ARGS=-N`) the game then starts a real network game without DirectPlay. This is the path a server takes.

```
GTA2_ARGS=-N GTA2_OFFLINE_MP=6:wil-multi.gmp:wil.sty:downtown-6P.scr:1 GTA2_LOCKSTEP=1 GTA2_HEADLESS=1 \
  GTA2_SIMBENCH=300:3000 Scripts/port/run_mac.sh
```

- **Every multiplayer map boots with 6 players and spawns all six peds** at the script's positions, in all three game types: downtown, industrial, res, mp1, mp2 and mp5, frags/points/tag.
- **Tick cost** with six idle players: 16-31 µs, against 13 µs for one.
- **Stability.** 100 s of game time on every map; one abort (`res-6p`) was the sound rollback problem of 3.
- **No debug path exists:** `-N` is the only trigger, and the stubs decide the rest (about nine return values, listed by the survey below).
- **Above 6 players.** About 45-50 sites depend on the count:
  - 13-14 arrays sized `MAX_PLAYERS`, several in structures with size asserts;
  - about 13 loops, and 7 literal `6`s (for example `Car::ManageDespawning_442310`, winmain's give-up loops, `gCurrentInputsBufferSize = 48`);
  - a `u8` disconnect bitmask (a cap of 8);
  - the six-row results screen;
  - colours from the ped remap (7 palettes);
  - the 17-slot HUD arrow pool;
  - **the number of `PLAYER_PED` declarations in each map's script** (6 in all of them): more players need more spawn points.

  Nothing in the save data depends on it. A compile-time cap (say 32, port only) plus a runtime count covers most of it. That leaves fewer than 10 places with real logic: the bitmask, the results screen, colours, spawn points, and the two `< 6` loops.

**Game rules.** Network games change some rules, all gated by `bStartNetworkGame_7081F0`:
- police at most wanted level 1;
- gangs use their basic weapon;
- car weapons cost a tenth;
- all car models spawn;
- endless respawns at the Restart zones, round robin (`Player::RespawnPlayer_5670B0`).

**Scoring.** Frags go through `GameSession::UpdateFrags_4C5CD0`, points through `PlayerScoreTracker::AddCash_592620` and tag through `TagGame`. The win check (frag or points limit) is at `PlayerScoreTracker.cpp:226`. Team modes and CTF don't exist and would be new mode code.

### 5. Advancing one object on its own (`single`)

Rockets are fired around the player. Then each moving object is advanced either with one full tick or with its own `Object::PoolUpdate` only, from the same state.
- `PoolUpdate` alone gives the full tick's position for **15 of 15** moving objects, and the same bytes for 14.
- So **projectile rewind-on-spawn can advance a projectile by itself.** Collisions with other entities then test against their current, not their rewound, positions unless a position history is added.

The run afterwards ran the collision grid's link pool (4096) dry in `Particle_4C::UpdateObjectBeamLink_state_38_538AC0`, from the rockets' explosions and fire. That's one more reason the grid pools need headroom.

### 6. Rollback in harder scenes (`explode`)

| Scene | Result |
|---|---|
| Wil, Industrial, Residential, each with 250 traffic cars, 100-126 peds and 20 explosions (fire, debris) | Rollback of 30 ticks exact in all three (with `skip_audio`) |
| Six-player network games | Exact, except while sound is on (see 3) |

Residential was down to **46 free collision-grid links** with 250 cars: the grid pools are the first limit for a fuller world. Not covered yet: a running mission, cranes and trains moving during the window.

### 7. ENet 1.3.18 (probe in a scratch folder, not in the repo)

**Limits and overhead:**
- The largest unreliable packet that is not fragmented at `host->mtu = 1400` is **1372 bytes**: `mtu - 28`, because ENet fragments anything above `mtu - 4 - 24`. With CRC32 it is 1368.
- ENet's MTU is the UDP payload: 1400 is a 1428-byte IPv4 packet. If 1400 has to be the IP packet, use `mtu = 1372` (payload 1344).
- A snapshot-only datagram has 10 bytes of overhead. ENet bundles up to 32 commands per datagram (acks, reliables, pings).
- There is no MTU setter: set `host->mtu` on both sides before connecting. The default is 1392, and there is no path-MTU discovery.

**Larger packets:** an unreliable packet larger than the cap is **sent as reliable fragments** by default. Under 10% loss its latency went to a 207 ms median, 637 ms p95 and 820 ms max; at 2000 bytes, seconds, and the backlog grows. It also delays the separate reliable channel (p95 1.4 s), since all reliable data shares one send window. `ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT` keeps the fragments unreliable: losing one loses the whole packet.

**Measured delivery** (proxy with 50 ms each way, ±10 ms jitter, reordering):
- Capped snapshots arrive as often as the raw loss allows (97% at 5% loss, 89-92% at 10%), with p95 latency 60 ms.
- Reliable 40-byte messages: median 52 ms. p95 85-180 ms at 5% loss and 190-310 ms at 10%. Worst case 0.5 s at 5% and 2 s at 10%, because the retransmit timeout doubles and there is no fast retransmit.

**Throttle, RTT and compression:**
- The packet throttle drops unreliable packets when the RTT rises: 3-8 of 900 snapshots in an RTT spike. Turning it off with `enet_peer_throttle_configure(peer, 5000, 32, 0)` lost nothing.
- `peer->roundTripTime` is accurate (99-107 ms against 101-107 ms) but excludes send-queue delay.
- The built-in range coder compresses whole datagrams after the fragmentation decision, so it doesn't raise the cap. Compress our own payload instead.

**Settings for us:**
- `host->mtu = 1400`; 2 channels; throttle off; `enet_host_flush` after each tick.
- Channel 0 only unreliable (snapshots capped at **1372**, inputs with the last few commands repeated).
- Channel 1 only reliable (events, join state in messages under the cap, chat).
- Never mix reliable sends into the snapshot channel: at 10% loss that cost 6% of the snapshots and up to 450 ms of delay.
- Lower `timeoutMaximum` (`enet_peer_timeout`).

### 8. Bots

**Bots are players whose inputs come from a bot brain.** That's the same path a network client's inputs take (`Player::SetInputs_565740`, then `ControlInputs_566820` and `Service_5687F0`). Everything downstream stays the game's own: frags, respawn, car entry, weapons.

The ped objective AI can't simply drive a player ped:
- walking reads `Player::field_8_turn_speed`/`field_C_move_direction` for `player_2` peds (`Char::state_0_54DDF0`);
- `HandleControls_5668D0` clears attacking unless the attack bit is set;
- `Car::AssignDriver_4406E0` removes the car AI when a player drives;
- about 70 `player_2` checks in `Ped.cpp`/`char.cpp`.

A bot brain can reuse:
- auto-aim (`Ped::ComputeAimAngle_45C9D0`);
- target searches (`FindBestTargetPed_466BF0`, the threat list);
- the nearest car (`GetNearestFrontVehicle_445210`);
- road routes (`RouteFinder::StartRoute_58A190`);
- foot paths (`PathFinder::ComputePath_554AB0`, tied to a ped id);
- the steering logic in `Ped::UpdateMovementTowardsTarget_4672E0` and `GotoAreaInCar_468310`, as a reference.

The input bits are in `Source/input_bits.hpp`: forward, back, left, right, attack, enter/exit, handbrake/jump, the weapons, three specials, plus a key code for chat.

### What this changes in the plan

- **Size cap:** 1372 bytes of payload, not ~1350 (`host->mtu = 1400`, throttle off, snapshots and reliables on separate channels).
- **Sound:** its own random stream, and out of the rollback set, before client-side prediction.
- **Remote entities:** extrapolation by the full sim as the default, interpolation as the option for peds and lossy links.
- **The server's start path:** the offline stubs (as in the experiment) plus a port-only player cap. Spawn points beyond a script's `PLAYER_PED`s become part of the mode module.
- **Projectiles:** rewind-on-spawn is possible with `Object::PoolUpdate` per object.
- **The schema starts with peds:** the widest state.
- **Collision-grid pools need headroom** on the server, even at 1×.

## Wire format

- **Encoding.** Little-endian bitstream, never `memcpy` of structs. Clients are 32- and 64-bit, and struct padding differs between them.
- **Snapshot packet** (server to client, unreliable, ENet channel 0):

  ```
  server tick S | delta-from tick B (0 = from scratch) | client's last applied cmd tick | input buffer health
  entity count, then per entity: NetId (+ type on create) | flags (create / destroy / update)
                                 | field mask | changed fields at their bit widths
  per-player block, events since the acked tick (unreliable copies of reliable ones, for latency)
  ```

- **Size cap.** Payload of at most **1372 bytes** with `host->mtu = 1400`, the largest unreliable packet ENet 1.3.18 doesn't fragment (see "Evaluations", 7). An oversized unreliable packet would silently go out as reliable fragments.
- **Per-entity acked baselines** instead of Q3's single acked snapshot:
  - For each client, the server remembers which entity states each sent snapshot carried.
  - When the client acks that snapshot, those states become the client's baselines for those entities.
  - This way a snapshot can carry **part of the world**: entities that don't fit within the budget are sent later, measured against their own baseline. An entity is never stuck on a lost packet.
- **Priority.** Entities are sorted by a priority accumulator: distance to the player's camera, the player's own ped and car first, and the time since the entity was last sent.
- **Join.** A full state of the interest area goes over the **reliable** channel (ENet fragments it, a few KB). Deltas then build on it.
- **Budget check.** 16 players and a busy interest area of about 40 cars, 60 peds and 30 objects:
  - A typical moving entity's delta is position plus angle (72 bits) plus mask and id, roughly 12 bytes.
  - 130 entities, half moving, is about 800 bytes, which fits.
  - In worse scenes the priority cut takes over. The N0 measurement replaces these guesses.
- **Input packet** (client to server, unreliable):
  - Carries the last **N commands** (tick and input bits), so losing one packet loses no input.
  - Carries the client's ack of the last snapshot.
  - Inputs are 32 bits, as `Network_InputData` uses now. Each command after the first is delta-coded against the one before.
- **Reliable channel** (ENet channel 1): chat, join/leave, map change, script HUD events, kick, and the data-hash handshake.

## Timing

- **Clock sync.** The client estimates the server tick from RTT (an EWMA, as openGTA2 does). It runs its own commands at `server tick + RTT/2 + jitter margin`, so that they reach the server just before they're needed.
- **Server input buffer.** The server reports per client how full that client's input buffer is. The client nudges its tick rate by ±2-5% to keep the buffer at 1-2 ticks.
- **Late commands.** If a command arrives late, the server repeats the player's last input and says so. The client's resim then corrects for it.
- **Remote view.** The modes are cvars, so the feel can be tuned:
  - `cl_interp_ms`: render remote entities this far in the past, between two snapshots. This sets only the draw position, never sim state.
  - `cl_remote_mode`: `interp` | `extrap` (show the predicted world as is: dead reckoning) | `interp_extrap` (interpolate, extrapolate when snapshots are late, up to `cl_extrap_max_ms`).
  - The local player and its car always come from prediction.
- **Projectiles.** Selected by `sv_projectile_mode`:
  - `rewind`: the server spawns the projectile at the shooter's view time (the command's tick minus their interp), then fast-forwards it to the current tick. Rewind is capped by `sv_maxunlag_ms`, as in CS. During the fast-forward, hits are tested against a ~1 s history of entity positions.
  - `predict`: the client spawns a cosmetic projectile at once, tagged with `(command tick, slot)`. The server spawns the real one when the command is processed. The snapshot names the command tick, so the client swaps its cosmetic copy for the real one or deletes it.

## Milestones (each one verifiable on its own)

| | Goal | Check |
|---|---|---|
| N0 | **Done**, see "N0 measurements". | Whole-world rollback holds: ~30 µs per busy tick, 0.06 ms save/restore, rollback exact. |
| S | **Done.** Sound fixes before prediction: its own random stream for `sound_obj::GenerateIntegerRandomNumberTable_41BA90`, and sound kept out of the rollback set (`SoundPosEntry` from a pool, or the sound layer not rolled back at all). | The rollback check exact with sound on; the sim's draws unchanged by muting sound. |
| N1 | Local rollback, no network: save every tick; every K ticks restore K back and resimulate with the same inputs. | The state log hash after resim equals the hash without it. Proves the restore set is complete and the resim is deterministic within one binary. |
| N2 | **Mostly done** (see "Status"; the comparison check is still to do). One binary with `--server`/`--connect`; the server starts its network game through the offline stubs (as `GTA2_OFFLINE_MP` does); ENet loopback, full snapshots (no delta), schema v1 starting with peds, client mode (spawners off). | The correctness check: client world at S equals server world at S, apart from `IGNORE`. |
| N3 | Bitstream, field masks, per-entity acked baselines, priority and the 1372-byte cap, link conditioner (delay, jitter, loss, reordering in our ENet wrapper). | Bytes per snapshot logged; same check as N2 at 10% loss. |
| N4 | **Done.** CSP: tick-stamped commands with redundancy, the server input buffer, clock sync, rollback on snapshot. | Prediction error 0 for the local player when it moves alone, at 150 ms RTT. |
| N5 | Remote view modes, projectile modes, lag compensation history. | Tuning by playing; switch with cvars. |
| N6 | Game flow: the mode module (frags first: join, spawn, score, win), join and leave mid-game, chat, script events, map change; a port-only player cap with a runtime count (~45-50 sites, see "Evaluations", 4); spawn points from restart zones and the per-map file; the network-game rules as server settings; new remaps if simple; the simple bot (`--bot`). | 16 bot clients on one map for an hour. |
| N8 | Later modes: points, tag (both in the game), team deathmatch and CTF (new, with the game's assets). | One bot match per mode. |
| N7 | Hardening: the server trusts only inputs; rate limits; protocol version; timeouts. | Fuzz input packets. |

## From openGTA2 (`openGTA2/network`)

Keep from it:
- the delta against the acked snapshot with a per-client ring;
- entities keyed by id;
- the 1400-byte MTU constant;
- the RTT EWMA;
- the handshake retries with the map and tickrate in the server info.

Fix:
- **Inputs are sent only when they change, and unreliably.** A lost packet leaves a key stuck down, or never pressed. Send the last N commands every packet.
- **Commands carry no tick.** Without one the client can't reconcile, which is a prerequisite for CSP.
- **No destroy message.** An entity missing from the delta means "unchanged", so a deleted entity never goes away on the client.
- **The client merges each delta into its latest state.** The packet doesn't say which baseline it was made against, which breaks once destroys or field masks exist. Put the baseline tick in the header.
- **No size cap.** The vectors go out however big they get. Cut at the byte budget, by priority.
- **Structs are sent with `memcpy`.** `ped_state` has padding after its `int16_t id`, and the layout differs between compilers and architectures. Use a bitstream.
- **Old acks.** An ack more than 127 snapshots old silently uses a ring slot that has been overwritten. Fall back to a full state.
- **`Server::sendGameState` has `return` instead of `continue`.** One unconnected client stops the sends to every client after it.

## Assumed, and deferred

Assumed:
- **The server is Linux, 64-bit**, from the port's headless build, and builds on Windows and macOS too. Its floats are SSE rather than x87, which doesn't matter for an authoritative server.
- **No compatibility with the original game's multiplayer.** The DirectPlay `NetPlay` stays as decompiled, unused in the port.

Wanted, but deferred:
- a frontend page for connecting, then a master server and server browser;
- NAT punch-through or a relay;
- anti-cheat beyond server authority (clients can't set state, only send inputs);
- protocol versioning for mods (data-file hashes at connect come first, in N7);
- better bots;
- simulation tick rates above 30 Hz (see "Tick rate");
- cleanup modes other than stock (prepared by the hooks).
