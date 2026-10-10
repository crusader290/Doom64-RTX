#include "../src/port/env_fx.c"
#include <assert.h>
pcconfig_t pc_config;
player_t players[MAXPLAYERS];
int firsttex;
int numsectors;
sector_t *sectors;
static sector_t testsector;
static subsector_t sub;
static const char *floorname="SLIMEA";
static int addon_mask=3,draws;
static float fraction=.5f;
static int sight_allowed=1;
boolean P_PathTraverse(fixed_t x1,fixed_t y1,fixed_t x2,fixed_t y2,int flags,boolean (*callback)(intercept_t *))
{ (void)x1;(void)y1;(void)x2;(void)y2;(void)flags;(void)callback;return sight_allowed; }
int PCAddon_Environment(void) { return addon_mask; }
void W_PCLumpName(int lump,char out[9]) { (void)lump;SDL_strlcpy(out,floorname,9); }
subsector_t *R_PointInSubsector(fixed_t x,fixed_t y) { (void)x;(void)y;return &sub; }
double I_PCInterpDrawFraction(void) { return fraction; }
const uint8_t *ResPack_Image(const char *name,int *w,int *h)
{ static uint8_t data[16];(void)name;*w=*h=2;return data; }
uint32_t d64gfx_texture_create(uint32_t w,uint32_t h,const uint8_t *data)
{ (void)w;(void)h;(void)data;return 1; }
void PCText_WorldImage(unsigned id,float x,float y,float z,float w,float h,unsigned color)
{ assert(id && isfinite(x) && isfinite(y) && isfinite(z) && w>0 && h>0 && (color&255));draws++; }
int main(void)
{
    mobj_t mo={0};players[0].mo=&mo;sub.sector=&testsector;
    testsector.ceilingheight=128*FRACUNIT;pc_config.environment_fx=1;PCEnv_Reset();
    for(int i=0;i<1000;i++) PCEnv_Tick();
    int living=0;for(int i=0;i<MAX_FX;i++) if(effects[i].life) { living++;assert(effects[i].type==1); }
    assert(living>0 && living<=MAX_FX);
    sector_t front={0},back={0};line_t line={0};intercept_t hit={0};
    line.frontsector=&front;hit.d.line=&line;assert(!visible(&hit));
    line.backsector=&back;front.ceilingheight=back.ceilingheight=64*FRACUNIT;
    sight_z=32;sight_delta=0;assert(visible(&hit));back.floorheight=40*FRACUNIT;assert(!visible(&hit));
    effect_t before[MAX_FX];memcpy(before,effects,sizeof(before));uint32_t rng_before=seed;
    for(int i=0;i<1000;i++) PCEnv_Draw();assert(draws>0 && !memcmp(before,effects,sizeof(before)) && seed==rng_before);
    float position[3],color[3];assert(PCEnv_Light(0,position,color) && color[1]>color[0]);
    floorname="BLOODA";for(int i=0;i<40;i++) PCEnv_Tick();
    for(int i=0;i<MAX_FX;i++) assert(!effects[i].life);
    floorname="HLAVA1";for(int i=0;i<40;i++) PCEnv_Tick();
    assert(PCEnv_Light(0,position,color) && color[0]>color[1]);
    testsector.ceilingheight=8*FRACUNIT;for(int i=0;i<40;i++) PCEnv_Tick();
    for(int i=0;i<MAX_FX;i++) assert(!effects[i].life);
    testsector.ceilingheight=128*FRACUNIT;floorname="SLIMEB";
    for(int i=0;i<40;i++) PCEnv_Tick();addon_mask=0;PCEnv_Tick();assert(!PCEnv_Light(0,position,color));
    PCEnv_Reset();assert(cursor==0 && ticks==0);
    addon_mask=3;sight_allowed=0;for(int i=0;i<100;i++) PCEnv_Tick();
    for(int i=0;i<MAX_FX;i++) assert(!effects[i].life);
    puts("PASS: native liquid mapping, bounded particles, expiry, ceiling collision, draw/RNG purity and disable reset");
}
