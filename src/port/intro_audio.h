#ifndef D64_INTRO_AUDIO_H
#define D64_INTRO_AUDIO_H
#include <stdint.h>
void PCIntroAudio_Init(int rate);
void PCIntroAudio_Shutdown(void);
int PCIntroAudio_Start(void); /* caller holds audio lock */
void PCIntroAudio_Stop(void);
void PCIntroAudio_Mix(int16_t *stereo,int frames);
int I_PCIntroMusic(int start); /* game-thread locking wrapper */
#endif
