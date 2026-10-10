#ifndef D64_GORE_H
#define D64_GORE_H
/* doomdef.h supplies mobj_t. Cosmetic state is discarded at level/load boundaries. */
void I_PCGoreReset(void);
void I_PCGoreDamage(mobj_t *target, mobj_t *inflictor, int damage);
void I_PCGoreTick(void);
void I_PCGoreDraw(float fraction);
#endif
