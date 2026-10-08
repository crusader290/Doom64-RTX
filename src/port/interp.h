/*
 * interp.h - frame interpolation for 60/120 fps. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_INTERP_H
#define D64_INTERP_H

extern void (*I_PCCurrentDrawer)(void); /* set by MiniLoop */

int    I_PCInterpEnabled(void);
void   I_PCInterpSnapshot(void);          /* before each game tic */
double I_PCInterpTicFraction(void);       /* time since the tic, 0..1 */
void   I_PCInterpBegin(double frac);      /* blend into the live state */
void   I_PCInterpEnd(void);               /* restore the live state */

#endif
