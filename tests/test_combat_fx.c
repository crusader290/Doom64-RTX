/* Exercise the actual animation implementation without a renderer or ROM. */
#include "../src/port/combat_fx.c"
#include <assert.h>
pcconfig_t pc_config;
boolean demoplayback, demorecording;
player_t players[MAXPLAYERS];
int gametic;
static double fraction;
static unsigned images;
double I_PCInterpDrawFraction(void) { return fraction; }
const uint8_t *ResPack_Image(const char *n,int *w,int *h)
{ static uint8_t rgba[4]; (void)n;*w=*h=1;return rgba; }
uint8_t *ResPack_LoadPNG(const char *p,int *w,int *h)
{ (void)p;(void)w;(void)h;return NULL; }
void ResPack_FreePNG(void *p) { (void)p; }
uint32_t d64gfx_texture_create(uint32_t w,uint32_t h,const uint8_t *p)
{ (void)w;(void)h;(void)p;return 1; }
void PCText_Image(unsigned t,float x,float y,float w,float h,unsigned c)
{ (void)x;(void)y;(void)w;(void)h;(void)c;assert(t);images++; }
void PCText_Draw(float x,float y,float s,const char *t,unsigned c)
{ (void)x;(void)y;(void)s;(void)t;(void)c; }
int main(void)
{
    mobj_t mo={0}; sector_t sector={0}; subsector_t sub={0};
    sub.sector=&sector;mo.subsector=&sub;players[0].mo=&mo;
    players[0].playerstate=PST_LIVE;players[0].readyweapon=wp_shotgun;
    pc_config.combat_anims=pc_config.crosshair=1;pc_config.weapon_bob=100;
    PCCombat_Reset();PCCombat_Tick();PCCombat_Fire();assert(recoil==6);
    PCCombat_Kick();PCCombat_Hit(1);assert(pose.kick==0 && pose.hit==8);
    pose_t saved=pose;float saved_recoil=recoil,saved_velocity=velocity;
    for(int i=0;i<500;i++) {
        float x,y;fraction=(i%5)/4.0;PCCombat_Offsets(&x,&y);PCCombat_Draw();
        assert(isfinite(x) && isfinite(y));
    }
    assert(images==500 && !memcmp(&saved,&pose,sizeof(pose)));
    assert(recoil==saved_recoil && velocity==saved_velocity);
    for(int i=0;i<14;i++) { gametic++;PCCombat_Tick(); }
    assert(pose.kick==-1 && pose.hit==0);
    players[0].pc_kicktics=11;PCCombat_Reset();PCCombat_Tick();
    assert(previous.kick==3 && pose.kick==4);players[0].pc_kicktics=0;
    demoplayback=1;PCCombat_Tick();PCCombat_Fire();PCCombat_Kick();
    assert(recoil==0 && pose.kick==-1);demoplayback=0;
    demorecording=1;PCCombat_Hit(1);assert(pose.hit==0);demorecording=0;
    pc_config.combat_anims=0;PCCombat_Fire();assert(recoil==0);
    puts("PASS: recoil, kick timing/load recovery, hit expiry, render purity and demo gates");
    return 0;
}
