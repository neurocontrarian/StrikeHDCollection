/*
 * CRT filter: runs the Lola Retro presets (third_party/lola-retro, the
 * RetroArch shader's GLSL port) on the game picture. OpenGL renderer only:
 * the passes are drawn with GL calls next to SDL's own (SDL_FlushRenderer).
 */
#ifndef DS_CRT_H
#define DS_CRT_H

#include <SDL3/SDL.h>
#include <stdint.h>

#define CRT_PRESET_COUNT 9              /* "Off" and eight presets */
extern const char *const crt_names[CRT_PRESET_COUNT];

/* After the renderer is created: 1 if it can run the filter (OpenGL 3.3). */
int  crt_init(SDL_Renderer *ren);
/* Why the filter can't run, for the user ("" when it can). */
const char *crt_why_not(void);

/* What the player picked. preset: 1 .. CRT_PRESET_COUNT - 1; flat: no
 * curvature; lite: the super lite passes; glow: the glass glow and the room's
 * reflection; wires: a Trinitron's damper wires; antimoire: the mask on whole
 * screen pixels and smoothed scanlines; dither: 1-pixel dither patterns
 * blended on RGB and S-Video as composite would have; mask: the phosphor
 * mask as in the preset (0), at half strength (1) or off (2). */
typedef struct { int preset, flat, lite, glow, wires, antimoire, dither, mask; } crt_look;

/* Draw a w x h XRGB8888 frame (pitch in pixels) through a preset into dst.
 * console: Lola Retro's console timing id (2 Mega Drive H40, 3 H32).
 * Returns 0 if the preset can't run (the caller draws the plain picture
 * instead). */
int  crt_draw(const crt_look *look, const uint32_t *px, int pitch, int w, int h,
              int console, SDL_FRect dst, uint64_t frame);

#endif
