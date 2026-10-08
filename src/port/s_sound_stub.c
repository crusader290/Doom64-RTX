/*
 * s_sound_stub.c - silent sound API, used when the build is configured with
 * -DD64_WITH_AUDIO=OFF. Doom64-RTX PC port, GPLv3.
 */
#include "doomdef.h"

int  wess_init_flag;
void S_Init(void) {}
void S_SetSoundVolume(int volume) { (void)volume; }
void S_SetMusicVolume(int volume) { (void)volume; }
void S_StartMusic(int mus_seq) { (void)mus_seq; }
void S_StopMusic(void) {}
void S_PauseSound(void) {}
void S_ResumeSound(void) {}
void S_StopSound(mobj_t *origin, int seqnum) { (void)origin; (void)seqnum; }
void S_StopAll(void) {}
int  S_SoundStatus(int seqnum) { (void)seqnum; return 0; }
void S_StartSound(mobj_t *origin, int sound_id) { (void)origin; (void)sound_id; }
int  S_AdjustSoundParams(mobj_t *listener, mobj_t *origin, int *vol, int *pan) { (void)listener; (void)origin; (void)vol; (void)pan; return 0; }
void wess_enable(void) {}
void wess_disable(void) {}
void I_PCAudioUpdate(void) {}
