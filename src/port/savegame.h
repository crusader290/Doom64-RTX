/*
 * savegame.h - PC save/load of the level state. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_SAVEGAME_H
#define D64_SAVEGAME_H

#define PC_SAVE_SLOTS 9
#define PC_QUICK_SLOT 7 /* F5 / F9 */
#define PC_AUTO_SLOT  8 /* written at each level start */

int  G_PCSaveGame(int slot);                       /* 1 on success */
int  G_PCSaveSlotInfo(int slot, char *desc, int len); /* 0 if empty */
int  G_PCLoadGame(int slot);                       /* sets the map; 1 if ok */
int  G_PCLoadPending(void);
void G_PCApplyPendingLoad(void);                   /* called at the end of P_Start */

#endif
