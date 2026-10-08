/*
 * rtlights.c - light sources for the ray traced renderer.
 * Doom64-RTX PC port, GPLv3.
 */
#include "doomdef.h"
#include "gbi.h"
#include "rtlights.h"

void RT_CollectLights(void)
{
    GBI_SetLights(NULL, 0);
}
