#include "../src/port/native_visuals.c"
#include <assert.h>
pcconfig_t pc_config;
player_t players[MAXPLAYERS];
int gamemap,gametic;
int numsectors,skytexture;
sector_t *sectors;
angle_t viewangle;
static int sky_enabled=1,title_enabled=1,starts,stops,logos,skies;
static float captured_u,captured_span,captured_pitch;
int PCAddon_Sky(void) { return sky_enabled; }
int PCAddon_Title(void) { return title_enabled; }
int I_PCIntroMusic(int start) { if(start) starts++;else stops++;return start; }
void S_StopMusic(void) {}
double I_PCInterpDrawFraction(void) { return .5; }
const uint8_t *ResPack_Image(const char *name,int *w,int *h)
{ static uint8_t image[16];(void)name;*w=*h=2;return image; }
uint32_t d64gfx_texture_create(uint32_t w,uint32_t h,const uint8_t *image)
{ (void)w;(void)h;(void)image;return 1; }
void PCText_Image(unsigned tex,float x,float y,float w,float h,unsigned color)
{ assert(tex && w>0 && h>0 && (color&255));(void)x;(void)y;logos++; }
void PCText_Draw(float x,float y,float size,const char *text,unsigned color)
{ (void)x;(void)y;(void)size;(void)text;(void)color; }
void PCText_SkyImage(unsigned tex,float u,float span,float pitch)
{ assert(tex);skies++;captured_u=u;captured_span=span;captured_pitch=pitch; }
int main(void)
{
    pc_config.title_intro=pc_config.sky_upgrades=1;gamemap=33;PCVisual_Reset();
    assert(title_alpha(0)==0 && title_alpha(9)==0 && title_alpha(39)==1 && title_alpha(258)==0);
    for(int i=0;i<9;i++) PCVisual_Tick();assert(starts==1);
    for(int i=0;i<3;i++) PCVisual_Tick();
    for(int i=0;i<1000;i++) PCVisual_Title();assert(title_tics==12 && starts==1 && logos==1000);
    for(int i=0;i<300;i++) PCVisual_Tick();int before=logos;PCVisual_Title();assert(logos==before && starts==1);
    PCVisual_Stop();assert(stops==2);gamemap=7;PCVisual_Reset();PCVisual_Tick();assert(title_tics==0);
    PCVisual_Title();assert(logos==before);gamemap=33;title_enabled=0;PCVisual_Tick();assert(title_tics==0);
    for(int i=1;i<=11;i++) PCVisual_Sky(i);assert(skies==7); /* fire/void/evil retain native skies */
    viewangle=ANG180;PCVisual_Sky(6);assert(fabsf(captured_u-.375f)<.0001f && captured_span==.25f);
    pc_config.aspect=1;players[0].pc_pitch=(int)ANG45;PCVisual_Sky(6);
    assert(fabsf(captured_span-1.f/3)<.0001f && captured_pitch==.125f);
    sky_enabled=0;before=skies;PCVisual_Sky(6);assert(skies==before);
    assert(!PCVisual_MenuLogo());pc_config.respacks=1;assert(PCVisual_MenuLogo());
    puts("PASS: title envelope/one-shot/reset/draw purity and panorama selection/yaw/pitch/widescreen/fallback");
}
