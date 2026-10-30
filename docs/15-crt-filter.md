# CRT filter (prototype)

## What it is
Settings → **CRT** → **Filter** runs the [Lola Retro](https://github.com/zoran123456/lola-retro) presets
on the game picture: a simulation of the whole analog chain (the Mega Drive's video encoder, the cable, the
TV's decoder, the electron beam, the phosphor mask and the glass). Eight of its fifteen presets fit a
Genesis: Consumer TV (Composite), Consumer TV (RGB SCART), Trinitron TV (S-Video), Old TV (RF Antenna),
Studio Monitor (Composite), Studio Monitor (RGB), Commodore 1084S Monitor and CRT Projector. **Screen**
picks curved or flat glass, **Quality** the full or the super lite passes (all in the CRT tab).

## How it works
- The shaders are Lola Retro's GLSL port for Kodi (`third_party/lola-retro`, unchanged, MIT), embedded in the
  program at build time by `cmake/embed_files.cmake`.
- `src/host/crt.c` is a small RetroArch-style `.glslp` runner: passes, scale types, aliases, float
  framebuffers, `#pragma parameter` defaults and the preset's values. Each pass draws a quad into a
  framebuffer; the last pass draws into an SDL target texture that SDL then puts on screen, so the HUD,
  minimap and menus are drawn on top, unfiltered, as before.
- It needs the OpenGL renderer: the GL calls go between SDL's (`SDL_FlushRenderer`, available since SDL
  3.2.0). With Display → Renderer on Automatic, OpenGL is chosen at startup while the filter is on (on
  Windows, Automatic is otherwise Direct3D). OpenGL 3.3 is the minimum.
- The input is the 1× picture, also when an HD pack is on: the tube makes its own detail.
- Two parameters are set by the game over the preset's: Console timing (`LR_CONSOLE`) 2, Mega Drive H40
  (3 for H32 screens), because Auto guesses from the frame size and a widescreen frame isn't 320 wide; and
  Picture height (`LR_FILL`) 1, so the 224 lines fill the picture height and the picture covers the same
  area as without the filter (the HUD and minimap are placed on that area).

## Options over the presets
The presets stay as Lola Retro made them; three menu options change one or two parameters each:

| Option | Default | Parameters |
|---|---|---|
| CRT Glass Glow | On | Off: `LR_GLOW` 0 (the halo the glass spreads around bright things) and `LR_REFLECT` 0 (the room's reflection) |
| CRT Damper Wires | Off | On keeps `LR_DAMPER` as in the preset: the two wires that hold a Trinitron's aperture grille, at about 1/3 and 2/3 of the height (Trinitron and studio monitor presets; lola-retro issue #16). Off: 0 |
| CRT Anti-Moire | On | `LR_MASK_PX` 1 (the mask on whole screen pixels, lola-retro issue #11) and `LR_SMOOTH` 1 (each screen pixel shows the light averaged over its height): no moire bands or dark patches where the mask or the lines beat against the screen's pixels. Off: the mask at the tube's size |

| CRT Dither Blend | On | `LR_DBLEND_AUTO` 1: on the RGB and S-Video presets, 1-pixel dither patterns (water, sand, shadows) are blended as composite would have, without its rainbow. Off: as in the preset (0) |
| CRT Mask | As the Tube | Half: `LR_MASK_STR` × 0.5; Off: 0. Below 4K the mask is finer than the screen's pixels and shimmers on moving detail (water) |

With the filter on, Scaling: Integer Only is ignored (the tube resamples the picture anyway), so the
picture fills the same area as with the other scaling modes.

The first tester (Batocera, RTX) saw a horizontal line at about 3/5 of the height (the lower damper wire),
the glass halo, and dark patches on some landscapes (most likely moire), hence these defaults.

## Widescreen
Kodi's port draws the tube the shape of the area it is given (it doesn't define `LR_FIT_43`), and the TV
model spreads the source line across the tube's width. So a widescreen frame (432 pixels at 16:9) becomes a
16:9 tube, like the widescreen CRT TVs of the late 1990s. Screens that stay 4:3 (menus, maps, cutscenes) are
already pillarboxed inside the frame by the host, so the tube keeps its shape.

The presets' absolute widths are meant for a 320-pixel line (the encoder's 1280 samples, 4 a pixel; the
beam's 8192): a wider frame gets them in proportion, so it keeps as many samples per pixel.

## Performance
Measured with `DS_CRT_TIME=1` (GPU time a frame; `=2` also each pass).

Intel UHD Graphics 630 (i5-10500T, Mesa 26 iris, GPU at its 1150 MHz maximum), ms of GPU time a frame,
curved; 16:9 is 1920×1067 (widescreen 16:9), 4:3 is 1440×1080:

| Preset | Full 16:9 | Super lite 16:9 | Full 4:3 | Super lite 4:3 | Super lite 1280×711 |
|---|---|---|---|---|---|
| Consumer TV (Composite) | 148 | 65 | 111 | 48 | 43 |
| Consumer TV (RGB SCART) | 99 | 44 | 74 | 34 | 23 |
| Trinitron TV (S-Video) | 137 | 55 | 99 | 39 | |
| Old TV (RF Antenna) | 199 | 76 | 158 | 53 | |
| Studio Monitor (Composite) | 121 | 54 | 90 | 40 | |
| Studio Monitor (RGB) | 95 | 41 | 72 | 31 | |
| Commodore 1084S Monitor | 100 | 47 | 75 | 36 | |
| CRT Projector | 87 | 38 | 66 | 29 | 20 |

None reaches the 16.7 ms of a 60 Hz frame on this GPU. Per pass (full composite, 16:9): encode 16 ms,
decode 42, beam 51, crt 36, present 4. Intel's compiler spills registers on two of the large passes
(`INTEL_DEBUG=perf`: "fragment shader triggered register spilling", SIMD16 failed); forcing SIMD16
(`INTEL_SIMD_DEBUG=fs16`) gains under 10%, half-float targets nothing (the passes are ALU-bound). For
comparison, Lola Retro's README gives 5.4–6.6 ms (full) and 1.7–2.5 ms (super lite) on an Intel Core
Ultra 5 225U with Vulkan at 1600×1200, so recent integrated and discrete GPUs should have room to spare.

At most one filtered frame is in flight (a fence waited for before the next one): with a GPU slower than
the game, frames would otherwise pile up (headless, where nothing else waits for the GPU, this showed as a
blank window). A GPU that slow therefore slows the game down.

The shaders compile once per preset and are cached by the driver: the first start took 16 s on Mesa for
the full composite preset, later ones 0.3 to 1.6 s.

## Tested on
- Batocera 44-dev, X11 (openbox + picom), NVIDIA RTX 3090 (driver 610.57), 1920×1080 at 60 Hz: the full
  RGB SCART preset takes 5–6 ms of GPU time. In fullscreen a torn band stayed at a fixed height (seen only
  in motion; gone in a window and with the super lite passes). VSync was on and honoured; the cause was
  picom, Batocera's compositor, copying the window on its own clock: with picom stopped the band is gone.
  A Batocera launcher therefore stops picom while the game runs and starts it again afterwards.
- Intel UHD 630 (Mesa): too slow, see Performance.
