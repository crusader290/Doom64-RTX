#include "../src/port/native_addons.c"
#include "d64gfx.h"
#include <assert.h>
#include <limits.h>
pcconfig_t pc_config;
boolean demoplayback,demorecording;
player_t players[MAXPLAYERS];
int gametic;
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
    const char scripts[]="class RTBloodPersistHandler : EventHandler {} class D64RtFlashlightHud : EventHandler {} class Unknown : Actor {}";
    PCAddon_Definition("ZSCRIPT",scripts,sizeof(scripts)-1);
    assert(persistent && flashlight && PCAddon_GoreLife()==0);
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
    demoplayback=1;actions=8;PCAddon_Tick();assert(!burning && PCAddon_GoreLife()==900);
    assert(PCAddon_BloodColor(MT_IMP2)==0x730403);demoplayback=0;
    pc_config.native_addons=0;assert(PCAddon_Flashlight()==0 && PCAddon_GoreLife()==900);
    puts("PASS: bounded definition lexer, colors, limits, battery edges/expiry, ABI and demo gates");
    return 0;
}
