#ifndef D64_SOUND_PACK_H
#define D64_SOUND_PACK_H
#include <stdint.h>
enum { PC_SOUND_KICK = 1000, PC_SOUND_SMG, PC_SOUND_RIFLE };
void PCSound_Init(int rate);
void PCSound_Shutdown(void);
/* Called under the same audio lock as WESS; no per-event allocations. */
int PCSound_Play(const void *origin, int id, int volume, int pan, int reverb);
void PCSound_Stop(const void *origin, int id);
void PCSound_StopAll(void);
void PCSound_Pause(int paused);
int PCSound_Active(int id);
void PCSound_Mix(int16_t *stereo, int frames);
#endif
