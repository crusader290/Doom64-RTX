/*
 * gbi.h - N64 display list interpreter.
 * Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_GBI_H
#define D64_GBI_H

#include <ultra64.h>
#include "d64gfx.h"

/* Interpret one complete frame display list and render it. */
void GBI_RunFrame(Gfx *dl);

/* Output size in pixels, used to size expanded lines. */
void GBI_SetOutputSize(int w, int h);

/* Display aspect (4/3 or 16/9). 2D stays 4:3 and centred; 3D widens. */
void GBI_SetAspect(float aspect);
float GBI_VirtualWidth(void);

/* Drop every cached texture (renderer reset). */
void GBI_FlushTextureCache(void);

/* Light sources for the RT path, filled by the game side before
 * GBI_RunFrame (src/port/rtlights.c). */
void GBI_SetLights(const D64GfxLight *lights, uint32_t count);
void GBI_SetWorldFog(const float color[4], float fog_near);

/* Statistics of the last frame (debug overlay / logging). */
typedef struct {
    uint32_t cmds, vertices, tex_uploads, tex_cached, dl_words;
} gbistats_t;
const gbistats_t *GBI_Stats(void);
/* PC text atlas: resolved overlay geometry without disturbing N64 RDP state. */
void GBI_OverlayQuad(uint32_t tex, float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1, unsigned color);

#endif
