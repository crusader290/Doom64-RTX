#include "../src/port/intro_audio.c"
#include <assert.h>
#include <stdio.h>
pcconfig_t pc_config;
int MusVolume;
void *ResPack_File(const char *name,size_t *size)
{ (void)name;return SDL_LoadFile("assets/title/title.wav",size); }
static Sint16 buffer[2048];
static int nonzero(void) { for(int i=0;i<2048;i++) if(buffer[i]) return 1;return 0; }
int main(void)
{
    pc_config.sound=pc_config.title_intro=pc_config.native_addons=pc_config.respacks=pc_config.sound_upgrades=1;
    PCIntroAudio_Init(48000);assert(pcm && length>0);assert(PCIntroAudio_Start());
    MusVolume=0;PCIntroAudio_Mix(buffer,1024);assert(!nonzero() && position==1024);
    MusVolume=70;int heard=0;
    for(int i=0;i<48;i++) { memset(buffer,0,sizeof(buffer));PCIntroAudio_Mix(buffer,1024);heard|=nonzero(); }
    assert(heard);PCIntroAudio_Stop();assert(!playing && !position);
    assert(PCIntroAudio_Start());pc_config.title_intro=0;PCIntroAudio_Mix(buffer,1024);assert(!playing);
    pc_config.title_intro=1;assert(PCIntroAudio_Start());
    for(int i=0;i<length/1024+2;i++) { memset(buffer,0,sizeof(buffer));PCIntroAudio_Mix(buffer,1024); }
    assert(!playing && position==length);
    pc_config.sound_upgrades=0;assert(!PCIntroAudio_Start());pc_config.sound_upgrades=1;
    pc_config.sound=0;assert(!PCIntroAudio_Start());PCIntroAudio_Shutdown();assert(!pcm && !length);
    puts("PASS: real title PCM loading/resampling, music volume, one-shot expiry, stop/disable and silence fallback");
}
