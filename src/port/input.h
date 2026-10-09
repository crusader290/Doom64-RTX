/*
 * input.h - emulated N64 pad. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_INPUT_H
#define D64_INPUT_H

#include <SDL3/SDL.h>

void IN_Init(void);
void IN_Shutdown(void);
void IN_HandleEvent(const SDL_Event *ev);
void IN_SetGrab(SDL_Window *window, int grab);
int  IN_ReadPad(void);

#ifndef PCACT_JUMP
#define PCACT_JUMP 1
#define PCACT_ADS  2
#define PCACT_KICK 4
#endif

#endif
