/*
 * savegame.h - PC save/load of the level state. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_SAVEGAME_H
#define D64_SAVEGAME_H

#define PC_SAVE_SLOTS 8
#define PC_QUICK_SLOT (PC_SAVE_SLOTS - 1) /* F5 / F9 */

int  G_PCSaveGame(int slot);                       /* 1 on success */
int  G_PCSaveSlotInfo(int slot, char *desc, int len); /* 0 if empty */
int  G_PCLoadGame(int slot);                       /* sets the map; 1 if ok */
int  G_PCLoadPending(void);
void G_PCApplyPendingLoad(void);                   /* called at the end of P_Start */

#endif
