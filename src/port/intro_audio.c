/* Optional native title sting. One preloaded PCM lane, controlled by music volume. */
#include <SDL3/SDL.h>
#include "config.h"
#include "respack.h"
#include "intro_audio.h"
extern int MusVolume;
static Sint16 *pcm;
static int length,position,playing;
void PCIntroAudio_Stop(void) { playing=0;position=0; }
void PCIntroAudio_Shutdown(void) { PCIntroAudio_Stop();SDL_free(pcm);pcm=NULL;length=0; }
void PCIntroAudio_Init(int rate)
{
    SDL_AudioSpec from,to={SDL_AUDIO_S16,2,rate};Uint8 *raw=NULL,*converted=NULL;
    Uint32 bytes=0;int converted_bytes=0;size_t size=0;char path[1024];
    PCIntroAudio_Shutdown();
    void *file=ResPack_File("music/title.wav",&size);
    if(file) { SDL_IOStream *io=SDL_IOFromConstMem(file,size);if(io) SDL_LoadWAV_IO(io,true,&from,&raw,&bytes);SDL_free(file); }
    else { SDL_snprintf(path,sizeof(path),"%sassets/title/title.wav",SDL_GetBasePath());SDL_LoadWAV(path,&from,&raw,&bytes); }
    if(raw && bytes<=16*1024*1024 && SDL_ConvertAudioSamples(&from,raw,(int)bytes,&to,&converted,&converted_bytes)) {
        pcm=(Sint16 *)converted;length=converted_bytes/4;
        SDL_Log("native title sting: %d frames preloaded at %d Hz",length,rate);
    }
    SDL_free(raw);
}
int PCIntroAudio_Start(void) { position=0;playing=pcm && length>0 && pc_config.sound && pc_config.sound_upgrades;return playing; }
void PCIntroAudio_Mix(int16_t *stereo,int frames)
{
    if(!playing) return;
    if(!pc_config.title_intro || !pc_config.native_addons || !pc_config.respacks || !pc_config.sound_upgrades) { PCIntroAudio_Stop();return; }
    float gain=SDL_clamp(MusVolume,0,100)/100.f * .5f;
    for(int i=0;i<frames && playing;i++) {
        stereo[i*2]=(Sint16)SDL_clamp(stereo[i*2]+pcm[position*2]*gain,-32768,32767);
        stereo[i*2+1]=(Sint16)SDL_clamp(stereo[i*2+1]+pcm[position*2+1]*gain,-32768,32767);
        if(++position>=length) playing=0;
    }
}
