# Dedicated server netcode: plan

A plan for client/server multiplayer in the port, in the style of Quake 3 and CS 1.6:
- an authoritative server;
- client-side prediction (CSP) of the local player;
- delta compression against acked state;
- every packet within a 1400-byte MTU.

Nothing here is built yet. It builds on the port (`docs/port.md`): the headless build, the state log and its pointer canonicalisation.

## Decisions

| Topic | Choice |
|---|---|
| Authority | The server simulates everything and clients send inputs. Clients are no longer deterministic: they apply state. |
| Ambient world (traffic, peds, cops) | Server-owned. Each client gets the entities in its **interest area** (around its camera). |
| Prediction | **Whole-world rollback** on the client, plus a declared **net schema** per entity type (only the fields that matter go over the wire). A full-world state compare checks that the schema is complete. |
| Transport | **ENet**: unreliable snapshots and inputs, reliable events. |
| Remote entities | **Configurable**: interpolation, dead reckoning, or interpolation that extrapolates on loss. |
| Projectiles | **Configurable**: rewind on spawn, or predicted cosmetic plus server spawn. |
| Players | **Up to 16** (4-bit player ids on the wire). |
| Rates | Sim fixed at **30 Hz** (33 ms, as the game is built). Snapshot rate configurable per client (10-30 Hz). |

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

## Wire format

- **Encoding.** Little-endian bitstream, never `memcpy` of structs. Clients are 32- and 64-bit, and struct padding differs between them.
- **Snapshot packet** (server to client, unreliable, ENet channel 0):

  ```
  server tick S | delta-from tick B (0 = from scratch) | client's last applied cmd tick | input buffer health
  entity count, then per entity: NetId (+ type on create) | flags (create / destroy / update)
                                 | field mask | changed fields at their bit widths
  per-player block, events since the acked tick (unreliable copies of reliable ones, for latency)
  ```

- **Size cap.** Payload of at most **~1350 bytes**, so that ENet's header and the UDP/IP headers fit in 1400 and ENet never fragments an unreliable packet. Check `ENET_HOST_DEFAULT_MTU` for the version we vendor and set the host MTU to 1400.
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
| N1 | Local rollback, no network: save every tick; every K ticks restore K back and resimulate with the same inputs. | The state log hash after resim equals the hash without it. Proves the restore set is complete and the resim is deterministic within one binary. |
| N2 | Server main, ENet loopback, full snapshots (no delta), schema v1, client mode (spawners off). | The correctness check: client world at S equals server world at S, apart from `IGNORE`. |
| N3 | Bitstream, field masks, per-entity acked baselines, priority and the 1350-byte cap, link conditioner (delay, jitter, loss, reordering in our ENet wrapper). | Bytes per snapshot logged; same check as N2 at 10% loss. |
| N4 | CSP: tick-stamped commands with redundancy, the server input buffer, clock sync, rollback on snapshot. | Prediction error 0 for the local player when it moves alone, at 150 ms RTT. |
| N5 | Remote view modes, projectile modes, lag compensation history. | Tuning by playing; switch with cvars. |
| N6 | Game flow: join and leave mid-game, spawn, scores, chat, script events, map change, `MAX_PLAYERS` 16 (`#if GTA2_PORT`; affects player arrays, HUD, scores, remaps, spawn points), server browser or direct connect. | 16 bot clients (a headless client with scripted input) on one map. |
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

## Defaults I chose (say if you want them otherwise)

- **The server is Linux, 64-bit**, from the port's headless build. Its floats are SSE rather than x87, which doesn't matter for an authoritative server.
- **No compatibility with the original game's multiplayer.** The DirectPlay `NetPlay` stays as decompiled, unused in the port.
- **No NAT punch-through or relay.** Dedicated servers have a public port; a master server or browser comes later.
- **No anti-cheat beyond server authority.** Clients can't set state, only send inputs.
