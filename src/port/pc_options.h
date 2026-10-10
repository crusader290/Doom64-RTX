/*
 * pc_options.h - PC option pages (Graphics / Gameplay / Debug) for the
 * game's menu code. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_PC_OPTIONS_H
#define D64_PC_OPTIONS_H

enum { PCPAGE_GRAPHICS, PCPAGE_GAMEPLAY, PCPAGE_DEBUG, PCPAGE_SAVE, PCPAGE_LOAD,
       PCPAGE_AUDIO, PCPAGE_CONTROLS, PCPAGE_COUNT };

int         PCOpt_Count(int page);
const char *PCOpt_Title(int page);
const char *PCOpt_Label(int page, int i);
/* Value text, or NULL for items without one. buf is scratch space. */
const char *PCOpt_Value(int page, int i, char *buf, int len);
/* dir: -1 left, +1 right, 0 confirm. Returns a game action (ga_*) or 0. */
int         PCOpt_Change(int page, int i, int dir);
int         PCOpt_IsAction(int page, int i);
int         PCOpt_ValueX(int page);  /* x of the value column */
const char *PCOpt_Help(int page, int item);

#endif
