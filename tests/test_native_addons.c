#include "../src/port/native_addons.c"
#include "pc_hud.h"
#include "st_main.h"
#include "d64gfx.h"
#include <assert.h>
#include <limits.h>
pcconfig_t pc_config;
boolean demoplayback,demorecording;
player_t players[MAXPLAYERS];
int gametic;
weaponinfo_t weaponinfo[NUMWEAPONS];
sbflash_t flashCards[6];
static char loaded_face[9];
int PCText_Available(void) { return 1; }
float PCText_Width(float size,const char *text) { return size*strlen(text)/2; }
void PCText_Image(unsigned id,float x,float y,float w,float h,unsigned c)
{ (void)id;(void)x;(void)y;(void)w;(void)h;(void)c; }
const uint8_t *ResPack_Image(const char *name,int *w,int *h)
{ static uint8_t pixels[36*37*4];SDL_strlcpy(loaded_face,name,sizeof(loaded_face));*w=36;*h=37;return pixels; }
uint32_t d64gfx_texture_create(uint32_t w,uint32_t h,const uint8_t *data)
{ (void)w;(void)h;(void)data;return 1; }
static int actions,sounds;
int I_PCActions(void) { return actions; }
void S_StartSound(mobj_t *p,int id) { (void)p;(void)id;sounds++; }
void PCText_Draw(float x,float y,float s,const char *t,unsigned c)
{ (void)x;(void)y;(void)s;(void)t;(void)c; }
void PCText_Box(float x,float y,float w,float h,unsigned c)
{ (void)x;(void)y;(void)w;(void)h;(void)c; }
int main(void)
{
    _Static_assert(sizeof(D64GfxLight)==64,"C/Rust/shader light stride");
    _Static_assert(offsetof(D64GfxLight,direction)==32,"spotlight direction offset");
    _Static_assert(offsetof(D64GfxLight,cos_inner)==48,"spotlight cone offset");
    pc_config.native_addons=pc_config.respacks=pc_config.flashlight=1;
    pc_config.gore_life=-1;pc_config.gore_limit=128;PCAddon_Init();
    const char fake[]="// class RTBloodPersistHandler\n /* class D64RtFlashlightHud */ \"class RTBloodPersistHandler\"";
    PCAddon_Definition("ZSCRIPT",fake,sizeof(fake)-1);assert(!persistent && !flashlight);
    const char scripts[]="class RTBloodPersistHandler : EventHandler {} class D64RtFlashlightHud : EventHandler {} class D64PoisonFx : EventHandler {} class D64LavaFx : EventHandler {} class Unknown : Actor {}";
    PCAddon_Definition("ZSCRIPT",scripts,sizeof(scripts)-1);
    assert(persistent && flashlight && PCAddon_GoreLife()==0);
    assert(PCAddon_Environment()==3);
    const char sbarfake[]="// DrawMugShot\n \"DrawMugShot\"";
    PCAddon_Definition("SBARINFO",sbarfake,sizeof(sbarfake)-1);assert(!mugshot);
    const char sbar[]="statusbar fullscreen { DrawMugShot 5, 102, -38; }";
    PCAddon_Definition("SBARINFO",sbar,sizeof(sbar)-1);pc_config.mugshot=1;assert(PCHud_Active());
    players[0].health=100;players[0].pendingweapon=wp_nochange;PCHud_Reset();
    PCHud_Draw();assert(!strcmp(loaded_face,"STFST00"));
    players[0].health=73;PCHud_Tick();PCHud_Draw();assert(!strcmp(loaded_face,"STFOUCH1"));

    for(int i=0;i<1000;i++) PCHud_Draw();assert(players[0].health==73);
    for(int i=0;i<18;i++) PCHud_Tick();players[0].attackdown=1;PCHud_Draw();assert(!strcmp(loaded_face,"STFKILL1"));
    players[0].health=0;PCHud_Draw();assert(!strcmp(loaded_face,"STFDEAD0"));
    players[0].health=100;players[0].attackdown=0;players[0].cheats=CF_GODMODE;PCHud_Draw();assert(!strcmp(loaded_face,"STFGOD0"));
    players[0].cheats=0;PCHud_Reset();
    const char decorate[]="ACTOR Test : 64NightmareImp replaces 64NightmareImp { BloodColor \"96 54 B4\" } ACTOR Black replaces 64Cacodemon { BloodColor \"00 00 00\" } ACTOR Bad replaces 64PainElemental { BloodColor \"100 ff 01\" }";
    PCAddon_Definition("DECORATE",decorate,sizeof(decorate)-1);
    assert(PCAddon_BloodColor(MT_IMP2)==0x9654b4 && PCAddon_BloodColor(MT_CACODEMON)==0);
    assert(PCAddon_BloodColor(MT_PAIN)==0x730403 && PCAddon_BloodColor(-1)==0x730403);
    pc_config.gore_limit=0;assert(PCAddon_GoreLimit()==1);pc_config.gore_limit=1500;assert(PCAddon_GoreLimit()==128);
    pc_config.gore_life=INT_MAX;assert(PCAddon_GoreLife()==108000);pc_config.gore_life=-1;
    mobj_t mo={0};players[0].mo=&mo;players[0].playerstate=PST_LIVE;
    actions=8;PCAddon_Tick();assert(burning && charge==899 && sounds==1);
    for(int i=0;i<50;i++) PCAddon_Tick();assert(burning && sounds==1);
    int before=charge;for(int i=0;i<100;i++) { PCAddon_Draw();PCAddon_Flashlight(); }
    assert(charge==before);actions=0;
    for(int i=0;i<900;i++) PCAddon_Tick();assert(!burning && charge>0 && sounds==2);
    for(int i=0;i<450;i++) PCAddon_Tick();assert(charge==900);
    PCAddon_ResetLevel();assert(!burning && charge==900);
    demoplayback=1;actions=8;PCAddon_Tick();assert(!burning && PCAddon_GoreLife()==900 && !PCHud_Active());
    assert(!PCAddon_Environment());
    assert(PCAddon_BloodColor(MT_IMP2)==0x730403);demoplayback=0;
    pc_config.native_addons=0;assert(PCAddon_Flashlight()==0 && PCAddon_GoreLife()==900);
    puts("PASS: bounded definition lexer, colors, limits, portraits/render purity, battery edges/expiry, ABI and demo gates");
    return 0;
}
