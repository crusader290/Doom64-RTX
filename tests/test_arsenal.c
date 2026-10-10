/* Exercise actual native weapon logic: ammo, damage, gates and draw purity. */
#include "../src/port/arsenal.c"
#include <assert.h>
pcconfig_t pc_config;
boolean demoplayback,demorecording;
int gametic;
fixed_t bulletslope;
static int attacks,damage,sound,rng,images;
static angle_t attack_angle;
const uint8_t *ResPack_Image(const char *n,int *w,int *h)
{ static uint8_t rgba[4];(void)n;*w=*h=1;return rgba; }
uint32_t d64gfx_texture_create(uint32_t w,uint32_t h,const uint8_t *p)
{ (void)w;(void)h;(void)p;return 1; }
void PCCombat_Offsets(float *x,float *y) { *x=*y=0; }
void PCText_Image(unsigned t,float x,float y,float w,float h,unsigned c)
{ (void)x;(void)y;(void)w;(void)h;(void)c;assert(t);images++; }
int P_Random(void) { rng++;return rng%2 ? 255 : 0; }
void P_BulletSlope(mobj_t *mo) { (void)mo;bulletslope=123; }
void P_LineAttack(mobj_t *mo,angle_t a,fixed_t z,fixed_t range,fixed_t slope,int d)
{ (void)mo;assert(z==0 && range==MISSILERANGE && slope==123);attacks++;damage=d;attack_angle=a; }
void S_StartSound(mobj_t *mo,int id) { (void)mo;sound=id; }
int main(void)
{
    player_t p={0};mobj_t mo={0};subsector_t sub={0};sector_t sector={0};state_t state={0};
    sub.sector=&sector;mo.subsector=&sub;p.mo=&mo;p.readyweapon=wp_pistol;
    p.psprites[ps_weapon].state=&state;p.psprites[ps_weapon].alpha=255;
    pc_config.imported_weapons=pc_config.respacks=pc_config.sound_upgrades=1;
    p.ammo[am_clip]=3;assert(PCArsenal_Fire(&p));
    assert(p.ammo[am_clip]==2 && damage==12 && sound==PC_SOUND_SMG && rng==2);
    angle_t wide=attack_angle;p.pc_ads=FRACUNIT;assert(PCArsenal_Fire(&p));
    assert(attack_angle<wide && p.ammo[am_clip]==1);
    p.readyweapon=wp_chaingun;pc_config.sound_upgrades=0;assert(PCArsenal_Fire(&p));
    assert(damage==20 && p.ammo[am_clip]==0 && sound==sfx_pistol);
    int before=attacks;assert(PCArsenal_Fire(&p));assert(attacks==before && p.ammo[am_clip]==0);
    player_t saved=p;int saved_rng=rng,saved_fire=last_fire;
    for(int i=0;i<1000;i++) assert(PCArsenal_Draw(&p));
    assert(images==1000 && rng==saved_rng && last_fire==saved_fire && !memcmp(&saved,&p,sizeof(p)));
    demoplayback=1;assert(!PCArsenal_Fire(&p) && !PCArsenal_Draw(&p));demoplayback=0;
    demorecording=1;assert(!PCArsenal_Active(wp_pistol));demorecording=0;
    pc_config.imported_weapons=0;assert(!PCArsenal_Fire(&p));pc_config.imported_weapons=1;
    assert(!PCArsenal_Active(wp_shotgun));PCArsenal_Reset();assert(last_fire==-100 && fire_weapon==-1);
    puts("PASS: native SMG/rifle damage, ammo exhaustion, ADS, sound fallback, render purity and demo gates");
}
