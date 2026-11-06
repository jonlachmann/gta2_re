# Texture uploads and preloading

Status: measured on PC, nothing changed. A preload of tiles and base sprites and a cache of moved textures
were tried and dropped: they did not change whether a frame is late. This note records what a texture costs
the first time it is drawn, whether loading textures ahead of time is worth it, and what else would help,
for the PC and the 3DS.

## How textures reach the GPU

At a city load `TextureCache::LoadStyleTextures` registers every texture the game may draw
(`gbh_RegisterTexture`): the tiles, each sprite with its base palette, and each sprite again with every remap
palette of its kind. The gbh_* layer only records them; `Resolve` (`renderer/gbh_api.cpp`) converts a
texture the first time it is drawn, in the variant the draw needs (opaque, or alpha for blended and
colour-keyed draws): the 8-bit pixels through the palette into RGBA8, padded to the original's square slot.
`GLRenderer::UploadTexture` then creates the GL texture and, with TEXTURE FILTER on trilinear or
anisotropic, makes the mip levels on the CPU (`Downsample`) and uploads each.

A few textures are uploaded again whenever the game changes them: the HUD's rolling digits
(`TextureCache::GetDigitTexture`, `SetPal`), car damage masks (`SetCarDamageTextureSizeAndPalette`) and the
shared texture of shrunk sprites (`SetSharedTextureData`). Preloading cannot help those.

## What a city registers

The three cities' styles have the same sprite counts, so these figures hold for wil, ste and bil. Sizes are
per variant, RGBA8, without mip levels (mip levels add a third).

| Set | Textures | Size |
|---|---|---|
| Tiles | 1024 | 16 MB |
| Sprites, base palette | 2089 | 18 MB |
| Car remaps | 2849 | 95 MB |
| Ped remaps | 25122 | 95 MB |
| Code object remaps | 3312 | 23 MB |
| Font remaps | 6507 | 56 MB |
| User remaps | 2595 | 50 MB |
| Map object remaps, car damage | 138 | 2 MB |
| All | 43636 | 355 MB |

The maps use 893 (bil), 843 (ste) and 819 (wil) distinct tiles.

## Measurements

Ryzen 7 5700X3D, Radeon RX 6700 (Mesa radeonsi, OpenGL ES 3.2), the port headless under gamescope at
1920x1080 with the user's settings (true colour, bloom, per-pixel lighting, MSAA 4x, soft shadows, zoom 0,
tilt 8), FRAME LIMIT 60, no vsync, sound off (on with SDL's disk driver: the same figures). A temporary
autopilot drove a cop car along the long roads of bil (52 s, about 200 blocks, traffic included) and ste
(43 s, about 310 blocks), with TEXTURE FILTER on anisotropic (mip levels) and on bilinear (none).
Times are CPU times on the main thread. "Work" is a frame's time between presents minus the frame
limiter's sleep (logic step, drawing, uploads, swap, wait for the GPU).

| | bil, mips | bil, no mips | ste, mips | ste, no mips |
|---|---|---|---|---|
| First uploads | 628 (12 per s) | 628 | 897 (21 per s) | 897 |
| Frames with a first upload | 15 % | 15 % | 23 % | 23 % |
| Most first uploads in a frame | 8 | 8 | 9 | 9 |
| Time per upload | 0.25 ms | 0.13 ms | 0.25 ms | 0.13 ms |
| Upload time in a frame with uploads, median / p99 / max | 0.20 / 1.56 / 2.35 ms | 0.13 / 0.46 / 0.57 ms | 0.29 / 1.53 / 2.30 ms | 0.14 / 0.53 / 0.69 ms |
| Work per frame, median / p99 / max | 1.25 / 2.57 / 5.58 ms | 1.26 / 2.16 / 11.6 ms | 1.48 / 2.65 / 5.39 ms | 1.43 / 2.13 / 3.43 ms |
| Work in frames without uploads, p99 / max | 1.77 / 2.43 ms | 2.00 / 2.48 ms | 1.88 / 2.57 ms | 1.85 / 2.32 ms |
| Time between presents, p50 / p99 / p99.9 / max | 16.7 / 17.8 / 18.5 / 20.5 ms | 16.7 / 17.1 / 17.9 / 26.7 ms | 16.7 / 17.8 / 18.9 / 19.8 ms | 16.7 / 17.1 / 18.0 / 18.2 ms |
| Frames with more than 16.7 ms of work | 0 | 0 | 0 | 0 |

The one 11.6 ms frame (bil, no mips) had 2 uploads taking 0.4 ms; the rest of its time was elsewhere.

Where an upload's time goes, with mip levels: the GL call that creates the texture 0.18 ms (the driver
allocating it), the mip levels' `glTexImage2D` calls 0.05 ms, `Downsample` 0.02 ms, the palette conversion
0.002 ms. Without mip levels only the first call remains.

First uploads per kind over the whole run (both variants; bil / ste): tiles 267 / 399, base sprites
122 / 130, ped remaps 387 / 427 (each walk frame of each ped colour is its own texture), car remaps
33 / 31, others 82 / 92. Re-uploads: the HUD's digits (five textures of 11x20 and 9x13) 235 / 310
(0.29 ms each, 67 / 94 ms in all), car damage masks 60 / 48.

So on this PC uploads are the main source of frame time variance (of the frames more than 18.5 ms apart
while driving, all but one had 3 to 9 first uploads), but even those frames do 3 to 6 ms of work against a
budget of 16.7 ms; none came near it. An upload frame could only show as a hitch when its present lands
close to a vblank, with vsync on a real display; gamescope's headless output does not reproduce that (the
bil run with vsync on stayed at 16.7 ms between presents, max 19.5).

## Fast straight drive

The fastest car found (model 19), full throttle along the longest straights: ste eastward and westward on
the road at y 152 to 155 (about 170 blocks each), bil eastward at y 58 (55 blocks, traffic) and southward
at x 220 (88 blocks); 14.5 s each, anisotropic filtering, the rest as above. Per frame: the uploads by
kind and the work split into the logic step, the scene (drawing, uploads included), the present (post
effects, swap, wait for the GPU) and the rest. The map is loaded whole at the city load: there is no
streaming, and spawning happens in the logic step.

| | ste east | ste west | bil east | bil south |
|---|---|---|---|---|
| First uploads: tiles and base sprites / remaps, damage, HUD | 184 / 279 | 196 / 258 | 75 / 183 | 288 / 324 |
| Upload time, same split (re-uploads included), ms | 58 / 103 | 64 / 104 | 21 / 69 | 89 / 127 |
| Work p50 / p99 / max, ms | 1.6 / 3.4 / 5.6 | 1.7 / 3.0 / 3.7 | 1.3 / 2.7 / 4.3 | 1.7 / 3.5 / 5.1 |
| Time between presents p99.9 / max, ms | 20.8 / 20.8 | 18.5 / 18.5 | 19.0 / 19.0 | 19.7 / 19.7 |
| Of the 9 slowest frames (work p99): with tile or sprite uploads, other uploads only, none | 4, 2, 3 | 6, 3, 0 | 7, 2, 0 | 7, 1, 1 |
| With the preload and cache: work p99 / max, ms | 2.7 / 3.4 | 2.8 / 5.1 | 2.4 / 3.3 | 2.9 / 3.2 |
| With the preload and cache: time between presents p99.9 / max, ms | 18.3 / 18.3 | 20.9 / 20.9 | 18.5 / 18.5 | 18.4 / 18.4 |

In the slowest frames, tile and sprite uploads take 0.3 to 0.8 ms on average and the other uploads 0.9 to
1.3 ms (a car damage mask is converted again whenever its car takes damage, 0.4 to 0.6 ms each). The
logic step takes 0.1 to 0.25 ms. The slowest frames without uploads spent 3.2 ms in the scene or 2 to
3.4 ms in the present. So tile and sprite uploads are among what makes the slowest 1 % of frames slower,
but no frame of these drives came near 16.7 ms of work: on this PC, at 60 frames per second, there is no
dropped frame for the preload to remove. With it the slowest frames are the remaps, damage masks and HUD
uploads; the city's first frame takes 1.0 s instead of 0.07 s and the GPU holds about 106 MB of textures
instead of 15. The cache of moved textures only saves uploads once the money counter's positions repeat:
over the 43 s ste drive, the uploads while driving went from 768 to 624.

## Options

| Option | PC cost | Gain | 3DS |
|---|---|---|---|
| Everything at the city load | 710 MB of GPU memory (945 MB with mips), 2.5 s (12.5 s with mips) per city load | No first uploads | Impossible (64 MB in all) |
| Tiles at the city load, both variants | 32 MB (43 MB with mips), 0.06 s (0.32 s with mips) | About a third of the first uploads | 893 tiles at 8 KB (RGB565) are 7 MB, 9.3 MB with mips, about 0.75 s at Azahar's 0.84 ms each: with mips more than the Old 3DS's linear heap has left (16 MB, 6.8 MB used in bil); fits on a New 3DS with a larger linear heap |
| Tiles and base sprites at the city load | 68 MB (90 MB with mips), 0.17 s (0.94 s with mips) | About 45 % | As above, worse |
| Prefetch with a per-frame budget: tiles of the blocks just outside the view (from the map), a ped's or car's sprites when it spawns (the game spawns them off screen); at most N uploads or 1 ms per frame, a draw still resolves at once what is missing | A queue in the gbh_* layer, a hook at spawn | Spreads the spikes; nothing preloaded that is never drawn | Yes, and the most useful there (uploads of 0.84 to 3.1 ms, 7 to 10 ms in the worst frame of a long drive) |
| Cache re-uploads by content (pixels, size, palette) | A small map in `Resolve` | The HUD digits stop re-uploading (0.3 ms each when the money counter rolls) | Yes |
| Cheaper uploads: immutable storage (`glTexStorage2D`) from a pool, or a texture array of 64x64 layers for the tiles | Renderer work; the array also needs the tiles' UVs per layer | The driver allocation (most of the 0.25 ms) goes away; an array also allows fewer draw calls | No texture arrays on the PICA200; an atlas instead |
| Mip levels with `glGenerateMipmap` | Premultiplied alpha first, so filtering still takes no colour from transparent texels | 0.07 of the 0.25 ms | The PICA200 has no mipmap generation |
| PBOs, async upload | Little: the cost is the allocation, not the copy (16 to 64 KB a texture) | Small | No |
| 8-bit textures and palettes on the GPU, the lookup in the fragment shader | Large renderer change; filtering (bilinear, mips, anisotropic) by hand in the shader, mip levels of indices are meaningless | One upload per sprite page (tiles 4 MB), no per-palette textures | No fragment shader on the PICA200 |

## Recommendation

- **PC: the stutters are not texture uploads.** In the worst case measured (the fastest car on the longest
  straights) a frame does at most 6 ms of work against 16.7 ms at 60 frames per second, and at most 4.3 ms
  against 8.3 ms at 120, uploads included. A preload of tiles and base sprites with a cache of moved textures
  cut the slowest frames' work by about 0.5 ms, at 1 s more city load and about 90 MB more GPU memory, and
  did not change whether a frame is late; it was dropped.
- **The late frames come from pacing:** at 120 frames per second without vsync, 1 % of frames arrive 0.8 to
  1.1 ms late with or without uploads, the frame limiter's sleep waking late. With vsync, a frame limit
  timed apart from the display's refresh shows frames for 1 then 3 refreshes as the two clocks drift; pacing
  frames on the refresh avoids it.
- **3DS: prefetch with a per-frame budget, and cache the re-uploads.** Uploads cost 0.84 to 3.1 ms there,
  7 to 10 ms in the worst frame of a long drive. Preloading the tiles only fits on a New 3DS.
