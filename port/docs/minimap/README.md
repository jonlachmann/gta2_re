# A minimap for the port

Status: design study, nothing in the game yet. This note records the frame designs, the map
treatments, where every map layer comes from, and how it would be built.

The goal is a minimap that looks like part of GTA2's own HUD rather than a modern overlay.

![All eight frames in the same corner of the same capture](demo/overview_all.png)

## What makes the HUD look the way it does

The HUD sprites (`user` sprites in the level `.sty`) are 3D renders reduced to an 8-bit palette:

- soft gradients and specular highlights, lit from the **top left**;
- about 130 colours per sprite, binary transparency, no hard black outline (edges fade through
  a dark rim, like ambient occlusion);
- short bevels (2 to 5 px) on small objects: a respect meter is 71x16, the pager 56x48;
- a few materials: bronze/khaki rims (`#463624` to `#978f75`), ribbed coloured glass tubes with a
  lit core, dark green-black plastic, a green LCD (`#629128`), coloured rubber caps, small lamps.

## Frame rendering

`tools/mm_lib.py` and `tools/designs.py` draw every frame from code; no game pixel is reused.

| Step | What it does |
|------|--------------|
| Shapes | Signed distance fields in HUD pixels: circle, rounded rect, chamfered rect, polygon |
| Height | Profiles along the distance: half-round bead, bevel, plateau; ribs as a cosine along the path |
| Materials | Albedo, specular strength and exponent, emission (lamps, tube cores) per region |
| Light | Normals from the height gradient, Blinn-Phong, light from the top left, ambient occlusion from the blurred height deficit, a slight key-light falloff |
| Supersample | Everything at 16 samples per HUD pixel, box-downsampled to 1x, 1.5x, 2x |
| Palette | Median cut to 128 colours with k-means refinement (keeps small saturated details), alpha cut at 50% |
| Glass | Separate soft overlay: inner shadow of the bezel and a sheen, drawn over the map |

Each frame ships as `frame` (screen hole transparent), `glass` and `screen_mask`, at 1x and 2x
(`assets/frames/`).

## The eight frames

![Frames A to D: frame alone x4, in game, corner x3](demo/overview_a_d.png)
![Frames E to H](demo/overview_e_h.png)

| | Frame | Size (1x) | Idea | Map | Build |
|-|-------|-----------|------|-----|-------|
| A | Respect ring | 96x101 | A respect meter bent into a ring: bronze lips, ribbed glass tube, bright N cap, three lamps | any | fixed image, map in a disc |
| B | Pager handset | 104x95 | Same plastic and khaki bezel as the pager, orange button, LED | green LCD | 9-slice |
| C | Steel plate | 98x102 | Gunmetal plate, cut corners, screws, N tab, hazard stripe | any | 9-slice |
| D | Compass dial | 104x100 | Polished ring with ticks and NESW, socket holding the zone's gang icon | any | fixed image |
| E | Tube frame | 104x92 | Four straight respect-meter tubes, rubber corner caps | block kind | 9-slice (the tube repeats) |
| F | Police scanner | 104x100 | Navy housing, red and blue light bar, curved CRT | phosphor + sweep | fixed image |
| G | Dashboard gauge | 100x104 | Hood, chrome cone bezel, black dial with ticks, N pointer | night | fixed image, map in a disc |
| H | 90s handheld GPS | 96x110 | Yellow rubber bumper (frontend colours), antenna, keys | grey LCD | 9-slice |

Ranking: **A** (with the E map), **E**, F, B, H, G, D, C.

- A and E are the closest to the respect meters; A is round, so it also suits the rotating camera
  option, E stretches to wide screens.
- F has the most character, and its light bar could flash with the wanted level.
- D's gang socket is worth keeping as an extra on A or E.

## Map treatments

The minimap is legible because of the map, more than the frame. Two sources:

| Source | How | Result |
|--------|-----|--------|
| Lid textures | Top-most lid tile of each column, box-filtered to a few pixels, shaded by height | Faithful, but beige and low contrast at 3 px per block |
| Block kind | Ground type (`slope_type & 3`) and height of the top block: water, road, pavement, field, building | Clear streets and blocks |

Block-kind maps get a dark rim where a building meets a lower block, and a cast shadow toward
the bottom right. Palettes:

| Style | Used by | Colours |
|-------|---------|---------|
| lid colours | A, C, D | Lid tiles, slight contrast boost |
| green LCD | B | Lid luminance in 4 greens (`#243814` to `#7ea83c`), cell grid at 2x |
| block kind | E | Light roads, mid pavement, green fields, slate buildings lighter with height, 18% lid texture |
| phosphor | F | Luminance in green, glow, range rings, sweep with a trail, scanlines |
| night | G | Dark buildings, amber roads |
| grey LCD | H | Block kinds in 4 grey-greens (`#2a3124` to `#b9c3a3`) |

![A and D with the block-kind map](demo/variants_schematic.png)

## Map layers and their sources

![Downtown, territories and points of interest](demo/map_downtown.png)
![Legend](demo/legend_downtown.png)

| Layer | File and record | Icon |
|-------|-----------------|------|
| Ground | `.gmp` DMAP: lids, heights, ground types | |
| Gang territory | `.gmp` ZONE type 14 (`Gang_14`), linked by name to the gang (`yakuzagang`, `krisgang`...) | 30% tint, edge where gangs meet |
| Gang colour | `.scr` SET_GANG_INFO (opcode 223/322) `mArrowId`, then the respect meter pip, user sprite `70 + arrow` | |
| Gang icon | user sprite `63 + arrow` (the respect meter icon) | game sprite |
| Info phone | SET_GANG_INFO position, also an OBJ_DATA of model 163 | code_obj 510 |
| Mission phones | OBJ_DATA (opcodes 15 to 20) models 180 green/easy, 178 yellow/medium, 176 red/hard; THREAD_DECLARE4 (213) ties each to its mission | code_obj 510, code_obj remap 0 / 2 / 1 |
| Church (save) | The THREAD_DECLARE5 (214) area trigger whose thread starts with DO_SAVE_GAME (438) | our pin |
| Restart | ZONE type 16 (`Restart_16`) | our pin, white outline |
| Arrest restart | ZONE type 20 (`Arrest_Restart_20`) | our pin, blue outline |
| Garages | OBJ_DATA model 130 (`car_shop_130`), bays grouped; value 250 car gun, 251 oil, 252 mines, 253 plates, 254 car bomb, other values respray | our pin |
| Train stations | ZONE type 6 (platforms) | our pin |
| Police stations | ZONE type 10 (`Information_10`) named `police*` (Downtown only) | our pin |
| Kill frenzies | OBJ_DATA model 286 | code_obj 286 |
| Secret tokens | OBJ_DATA model 266 | code_obj 9 |
| Pickups | OBJ_DATA models 200 to 239 | first frame of their code_obj |
| Player start | PLAYER_PED (opcode 5) | our arrow |

Formats, from the decompiled loaders:

- `.scr` (`ScriptManager::Load`): `u16[6000]` base pointers, 64 KB of commands (header
  `s16 this, u16 type, s16 next, s16 ret`), `u16` string table size, then entries
  `s16 id, s16 zone, s16 type, s16 pad, u8 len, name`. Positions are `Fix16` (14 fractional
  bits); a z of 255 means ground level.
- `.gmp` ZONE: `u8 type, x, y, w, h, name length`, then the name.
- Code object sprites: `CreateCodeObjectDefinitions` gives each model in `gCodeObjectTemplates`
  the next `mNumSprites` sprites, so a model's first sprite is the running sum (phone 163: 510,
  token 266: 9, frenzy 286: 286, health 230: 167). The 126 templates add up to the 552 code_obj
  sprites of the style, which confirms it.
- Sprite palettes: sprite `n` uses virtual palette `PALB.tile + n`; code object remaps start at
  `tile + sprite + car_remap + ped_remap` (`gtx_0x106C::GetTruePalette`).

**Reliable from the files:** zones, gang info, phones and their colours, the church, garages,
frenzies, tokens, pickups, player start. Per level: 3 gangs, 3 info phones, 15 mission phones, 1
church, 4 to 6 restarts, 4 to 6 garages, 15 or 16 frenzies, about 50 tokens. Industrial also has
`islagang`, a gang with arrow 0 and no respect: not a real gang, left untinted. The police
stations come from zone names, an educated guess.

**From runtime state in the port, not the files:**

| What | Where |
|------|-------|
| Gang colour, info phone | `gGangPool` (`mArrowColour`, `mInfoPhoneX/Y/Z`) |
| Phones still active, dead after a mission | `gScriptManager->mPhoneIds` and the object pool (`dead_phone_174`) |
| Objects spawned by mission scripts (`.mis`) | object pool |
| Zones, ground, heights | `gMap_0x370` |
| Mission targets | `Hud_Arrow` targets |
| Tokens taken, frenzies done, pickups respawned | game state |

## Placement

![HUD zones and the minimap slot](demo/placement.png)

- Everything else sits at the top (respect meters, pagers, wanted heads, zone and car names,
  lives, score, hearts, weapon) or bottom left (brief portrait). The free corner is the **bottom
  right**, 6 HUD px from the edges, scaled like the hearts by HUD SCALE and SAFE AREA.
- At 16:9 the corner is always free: briefs stay 4:3 wide and left anchored.
- At 4:3 the last brief lines run under it: fade the minimap out while a brief shows, or narrow
  the brief wrap width.

## Building it in the port

- **Map texture**, built once per level load from `gMap_0x370` and the tile data already in
  memory: one colour per block (block kind, or the lid tile's average colour), 1 texel per block
  (256x256) with nearest sampling, or 4 per block (1024x1024) with the rims and shadows. Territory
  tint baked in, since zones do not move.
- **Draw order:** map (a triangle fan for round screens, a scissored quad otherwise, UVs centred
  on the player, rotated with the camera if it rotates), then pins and icons, the player arrow
  (rotated to the ped heading), the glass overlay, the frame, and the extras (A's N cap orbiting
  when the camera turns, D's gang icon from the `.sty`).
- **Sizes:** frames about 100x100 at 1x; the map at 2 to 3 px per block (22 to 33 blocks
  across); icons about 8 to 13 px at 1x.
- **Assets:** the PNGs in `assets/` are original art, so they can ship; pick 1x or 2x by HUD
  scale and filter in between. Game icons (phones, gang icons, frenzies, tokens) are read from the
  user's `.sty` at run time and never stored.
- **Cost:** a fixed-image frame plus the map texture is a day or two; the animated F sweep and a
  rotating map add a little.

![Minimap A with the block-kind map and points of interest](demo/minimap_pois.png)

## Tools

`tools/` holds the generators and readers. They run with `python3 -I`, need Pillow and numpy,
and read the game data without writing to it.

```sh
tools/capture_all.sh <copy of the game dir>                # headless captures (gamescope, Linux port)
tools/run_all.sh <game data dir> <game dir copy> <out dir>  # every image above, about 30 s
```

| Script | Role |
|--------|------|
| `sty.py`, `scr.py`, `gmpzones.py` | Readers for `.sty` sprites and palettes, `.scr` commands and strings, `.gmp` zones |
| `render_map.py` | Top-down render from lid tiles, plus height and ground type grids |
| `mm_lib.py`, `designs.py` | Frame renderer and the eight frames |
| `make_all.py` | Frames, glass, masks, markers, composites, overviews |
| `variants.py`, `placement.py` | Map treatment variants, HUD zone diagram |
| `fullmap.py`, `minimap_pois.py` | Full maps with territories and points of interest, minimap with them |
| `extract_hud.py`, `zoomref.py` | HUD sprite contact sheet and zooms, for study |
| `mmrun.sh`, `capture_all.sh` | Headless captures |

`demo/` images contain game graphics (captures and sprites from the user's data).
