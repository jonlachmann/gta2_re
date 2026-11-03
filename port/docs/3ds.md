# A Nintendo 3DS port

Status: not started. This note records what a first proof of concept would take.

## What helps

- The 3DS is 32-bit (ARM11), so the game's structures keep their original sizes and the data
  files load as they are. It does not depend on the 64-bit work (`64bit.md`); that work keeps
  i686-style 32-bit targets building.
- SDL3 supports the 3DS through devkitPro: buttons, audio, threads and files.
- GTA2 is light: the PC version targeted a Pentium 200 with 32 MB of RAM.

## What needs work

- **Renderer.** The PICA200 has no OpenGL ES 3 and no programmable fragment stage (fixed texture
  combiners only). It needs a citro3d backend next to `port/renderer`'s GL one, without the
  port's per-pixel lighting, shadows or bloom.
- **Textures.** The PICA200 has no paletted formats: tiles and sprites are converted at load time
  (RGB565/RGBA4/RGBA5551), and must fit in 64 MB of RAM (Old 3DS) or 124 MB (New 3DS).
- **Screen.** 400x240 top screen: a suited zoom, and later the bottom screen for a map or the HUD.
- **CPU.** 268 MHz on the Old 3DS, 804 MHz on the New 3DS: target the New 3DS first.

## Proof of concept scope

The game builds with devkitARM and shows a level in an emulator, with a simple citro3d renderer,
the buttons, and sound if it comes easily. A few days of work; a playable port is weeks.

## Tools (not downloaded yet, need the user's approval)

- devkitPro: the official `devkitpro/devkitarm` Docker image (devkitARM, libctru, citro3d, SDL3
  for 3DS), about 1 to 2 GB.
- Azahar (the Citra continuation), AppImage from its GitHub releases, for headless tests.
- Real hardware testing needs a 3DS or New 3DS with custom firmware.
