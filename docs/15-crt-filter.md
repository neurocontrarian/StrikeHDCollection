# CRT filter (prototype)

## What it is
Settings → Picture → **CRT Filter** runs the [Lola Retro](https://github.com/zoran123456/lola-retro) presets
on the game picture: a simulation of the whole analog chain (the Mega Drive's video encoder, the cable, the
TV's decoder, the electron beam, the phosphor mask and the glass). Eight of its fifteen presets fit a
Genesis: Consumer TV (Composite), Consumer TV (RGB SCART), Trinitron TV (S-Video), Old TV (RF Antenna),
Studio Monitor (Composite), Studio Monitor (RGB), Commodore 1084S Monitor and CRT Projector. **CRT Screen**
picks curved or flat glass, **CRT Quality** the full or the super lite passes.

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

## Widescreen
Kodi's port draws the tube the shape of the area it is given (it doesn't define `LR_FIT_43`), and the TV
model spreads the source line across the tube's width. So a widescreen frame (432 pixels at 16:9) becomes a
16:9 tube, like the widescreen CRT TVs of the late 1990s. Screens that stay 4:3 (menus, maps, cutscenes) are
already pillarboxed inside the frame by the host, so the tube keeps its shape.

The presets' absolute widths are meant for a 320-pixel line (the encoder's 1280 samples, 4 a pixel; the
beam's 8192): a wider frame gets them in proportion, so it keeps as many samples per pixel.

## Performance
Measured with `DS_CRT_TIME=1` (GPU time a frame; `=2` also each pass).
