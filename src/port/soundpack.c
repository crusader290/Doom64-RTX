/* Supplemental PCM sounds share WESS spatialization, volume and audio locking.
 * Bounded voices; preloaded/resampled assets; original synth is the fallback. */
#include <SDL3/SDL.h>
#include <math.h>
#include "doomdef.h"
#include "config.h"
#include "respack.h"
#include "soundpack.h"

#define CLIPS 6
#define VOICES 16
typedef struct { Sint16 *pcm; int frames; } clip_t;
typedef struct { int clip, pos, id; uintptr_t origin; float l, r, send; } voice_t;
static clip_t clips[CLIPS];
static voice_t voices[VOICES];
static const int ids[CLIPS] = {sfx_pistol, sfx_punch, sfx_slop, PC_SOUND_KICK, PC_SOUND_SMG, PC_SOUND_RIFLE};
static const char *names[CLIPS] = {"pistol.wav", "meat.wav", "slop.wav", "kick.wav", "smg.wav", "rifle.wav"};
static float *tail;
static int tail_size, tail_pos, paused;
static unsigned cursor;
extern int SfxVolume;

void PCSound_StopAll(void)
{
    int i;
    for (i=0; i<VOICES; i++) voices[i].clip = -1;
    if (tail) SDL_memset(tail, 0, (size_t)tail_size * 2 * sizeof(float));
    tail_pos=0;
}
void PCSound_Shutdown(void)
{
    int i;
    PCSound_StopAll();
    for (i=0; i<CLIPS; i++) { SDL_free(clips[i].pcm); clips[i]=(clip_t){0}; }
    SDL_free(tail); tail=NULL; tail_size=0;
}
void PCSound_Init(int rate)
{
    int i, count=0;
    PCSound_Shutdown();
    paused=0; cursor=0;
    tail_size=rate * 29 / 100; /* diffuse room tail, 290 ms with cross feedback */
    tail=SDL_calloc((size_t)tail_size * 2, sizeof(float));
    for (i=0; i<CLIPS; i++) {
        char name[128], path[1024];
        size_t size=0;
        SDL_AudioSpec from, to={SDL_AUDIO_S16, 2, rate};
        Uint8 *raw=NULL, *converted=NULL;
        Uint32 length=0;
        int converted_length=0;
        void *data;
        SDL_snprintf(name, sizeof(name), "sounds/immersion/%s", names[i]);
        data=ResPack_File(name, &size);
        if (data) {
            SDL_IOStream *io=SDL_IOFromConstMem(data,size);
            if (io) SDL_LoadWAV_IO(io, true, &from, &raw, &length);
            SDL_free(data);
        } else {
            SDL_snprintf(path,sizeof(path),"%sassets/sounds/%s",SDL_GetBasePath(),names[i]);
            SDL_LoadWAV(path,&from,&raw,&length);
        }
        if (raw && SDL_ConvertAudioSamples(&from,raw,(int)length,&to,&converted,&converted_length)) {
            clips[i].pcm=(Sint16 *)converted;
            clips[i].frames=converted_length/4;
            count++;
        }
        SDL_free(raw);
    }
    SDL_Log("immersion sounds: %d/%d preloaded at %d Hz",count,CLIPS,rate);
}
int PCSound_Play(const void *origin, int id, int volume, int pan, int reverb)
{
    int i, slot=-1;
    float position, gain;
    voice_t *voice;
    if (!pc_config.sound_upgrades || demoplayback || demorecording) return 0;
    for (i=0; i<CLIPS; i++) if (ids[i]==id) break;
    if (i==CLIPS || !clips[i].pcm) return 0;
    for (int v=0; v<VOICES; v++) if (voices[v].clip<0) { slot=v; break; }
    if (slot<0) slot=(int)(cursor++ % VOICES);
    position=SDL_clamp(pan,0,127)/127.0f;
    gain=SDL_clamp(volume,0,127)/127.0f * .32f;
    voice=&voices[slot];
    *voice=(voice_t){i,0,id,(uintptr_t)origin,
        sqrtf(1-position)*gain,sqrtf(position)*gain,reverb ? .12f : 0};
    return 1;
}
void PCSound_Stop(const void *origin, int id)
{
    int i;
    for (i=0; i<VOICES; i++)
        if (origin ? voices[i].origin==(uintptr_t)origin : voices[i].id==id) voices[i].clip=-1;
}
void PCSound_Pause(int value) { paused=value; }
int PCSound_Active(int id)
{
    int i;
    for(i=0;i<VOICES;i++) if(voices[i].clip>=0 && voices[i].id==id) return 1;
    return 0;
}
void PCSound_Mix(int16_t *stereo, int frames)
{
    int frame,i;
    float master=SDL_clamp(SfxVolume,0,100)/100.0f;
    if (paused) return;
    if (!pc_config.sound_upgrades || demoplayback || demorecording) { PCSound_StopAll(); return; }
    for (frame=0;frame<frames;frame++) {
        float left=0,right=0,send_l=0,send_r=0;
        for(i=0;i<VOICES;i++) {
            voice_t *v=&voices[i];
            if(v->clip<0) continue;
            clip_t *c=&clips[v->clip];
            if(v->pos>=c->frames) { v->clip=-1; continue; }
            float l=c->pcm[v->pos*2]*v->l, r=c->pcm[v->pos*2+1]*v->r;
            left+=l;right+=r;send_l+=l*v->send;send_r+=r*v->send;
            if(++v->pos>=c->frames) v->clip=-1;
        }
        if(tail && tail_size) {
            float l=tail[tail_pos*2],r=tail[tail_pos*2+1];
            left+=l;right+=r;
            tail[tail_pos*2]=send_l+r*.36f;
            tail[tail_pos*2+1]=send_r+l*.36f;
            tail_pos=(tail_pos+1)%tail_size;
        }
        stereo[frame*2]=(Sint16)SDL_clamp(stereo[frame*2]+left*master,-32768,32767);
        stereo[frame*2+1]=(Sint16)SDL_clamp(stereo[frame*2+1]+right*master,-32768,32767);
    }
}
