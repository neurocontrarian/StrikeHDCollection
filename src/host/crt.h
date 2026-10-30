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

/* Draw a w x h XRGB8888 frame (pitch in pixels) through a preset into dst.
 * preset: 1 .. CRT_PRESET_COUNT - 1; flat: no curvature; lite: the super
 * lite passes; console: Lola Retro's console timing id (2 Mega Drive H40,
 * 3 H32). Returns 0 if the preset can't run (the caller draws the plain
 * picture instead). */
int  crt_draw(int preset, int flat, int lite, const uint32_t *px, int pitch, int w, int h,
              int console, SDL_FRect dst, uint64_t frame);

#endif
