/*
 * n64synth.h - PC software synth behind the libaudio.h alSyn* API.
 * Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_N64SYNTH_H
#define D64_N64SYNTH_H

#include "ultra64.h"

void Synth_SetOutputRate(s32 rate);
s32  Synth_OutputRate(void);
/* Renders interleaved stereo s16 frames, running player callbacks. */
void Synth_Render(s16 *out, int frames);
/* Debug: decodes a wave without loops; returns the sample count. */
int  Synth_DecodeWave(const void *wave, s16 *out, int max);

#endif
