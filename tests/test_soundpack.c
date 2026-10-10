/* Real PCM asset loading, conversion and mixer lifecycle under sanitizers. */
#include "../src/port/soundpack.c"
#include <assert.h>
pcconfig_t pc_config;
boolean demoplayback,demorecording;
int SfxVolume=100;
void *ResPack_File(const char *name,size_t *size)
{
    char path[512];const char *base=strrchr(name,'/');
    SDL_snprintf(path,sizeof(path),"assets/sounds/%s",base ? base+1 : name);
    return SDL_LoadFile(path,size);
}
static Sint16 buffer[2048];
static int nonzero(void) { for(int i=0;i<2048;i++) if(buffer[i]) return 1;return 0; }
int main(void)
{
    pc_config.sound_upgrades=1;PCSound_Init(48000);
    for(int i=0;i<CLIPS;i++) assert(clips[i].pcm && clips[i].frames>0);
    assert(!PCSound_Play(NULL,-1,127,64,0));
    assert(PCSound_Play((void *)1,sfx_pistol,127,0,0));
    PCSound_Mix(buffer,1024);assert(nonzero());
    for(int i=0;i<1024;i++) assert(buffer[i*2+1]==0);
    int position=voices[0].pos;PCSound_Pause(1);PCSound_Mix(buffer,1024);
    assert(voices[0].pos==position);PCSound_Pause(0);
    PCSound_Stop((void *)1,0);assert(!PCSound_Active(sfx_pistol));
    for(int i=0;i<1000;i++) assert(PCSound_Play((void *)2,sfx_punch,127,64,16));
    int active=0;for(int i=0;i<VOICES;i++) active+=voices[i].clip>=0;
    assert(active==VOICES);
    PCSound_StopAll();memset(buffer,0,sizeof(buffer));PCSound_Mix(buffer,1024);assert(!nonzero());
    assert(PCSound_Play(NULL,PC_SOUND_KICK,127,64,0));
    SfxVolume=0;memset(buffer,0,sizeof(buffer));PCSound_Mix(buffer,1024);assert(!nonzero());
    SfxVolume=100;PCSound_StopAll();
    demoplayback=1;assert(!PCSound_Play(NULL,sfx_pistol,127,64,0));
    demoplayback=0;demorecording=1;assert(!PCSound_Play(NULL,sfx_pistol,127,64,0));
    demorecording=0;assert(PCSound_Play(NULL,sfx_slop,127,64,0));
    pc_config.sound_upgrades=0;memset(buffer,0,sizeof(buffer));PCSound_Mix(buffer,1024);
    assert(!nonzero() && !PCSound_Active(sfx_slop));
    pc_config.sound_upgrades=1;assert(PCSound_Play(NULL,sfx_slop,127,64,0));
    for(int i=0;i<100;i++) { memset(buffer,0,sizeof(buffer));PCSound_Mix(buffer,1024); }
    assert(!PCSound_Active(sfx_slop));
    PCSound_Shutdown();puts("PASS: PCM loading/resampling, voice cap, pan, volume, pause/stop, expiry and demo fallback");
    return 0;
}
