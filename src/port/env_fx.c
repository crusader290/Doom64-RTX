/* Bounded native Retribution liquid effects. Cosmetic RNG, no actors/save fields. */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "config.h"
#include "native_addons.h"
#include "pc_text.h"
#include "respack.h"
#include "interp.h"
#include "d64gfx.h"
#include "env_fx.h"
#define MAX_FX 48
typedef struct { float x,y,z,oldz,vz,size;int life,total,type;sector_t *sector; } effect_t;
static effect_t effects[MAX_FX];
static uint32_t seed;
static unsigned cursor, fx_textures[10];
static int frame_width[10],frame_height[10];
static int loaded,ticks,announced;
static float sight_z,sight_delta;
static boolean visible(intercept_t *in)
{
    line_t *line=in->d.line;
    if(!line->frontsector || !line->backsector) return false;
    float z=sight_z+sight_delta*(in->frac/(float)FRACUNIT);
    float floor=SDL_max(line->frontsector->floorheight,line->backsector->floorheight)/(float)FRACUNIT;
    float ceiling=SDL_min(line->frontsector->ceilingheight,line->backsector->ceilingheight)/(float)FRACUNIT;
    return z>floor && z<ceiling;
}
static float random01(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return (seed&65535u)/65535.f; }
static int enabled(void) { return pc_config.environment_fx && PCAddon_Environment(); }
static int liquid(sector_t *s);
void PCEnv_Reset(void)
{
    memset(effects,0,sizeof(effects));seed=0x84b09a13u;cursor=0;ticks=announced=0;
    int poison=0,lava=0;
    for(int i=0;i<numsectors;i++) {
        sector_t *s=&sectors[i];int type=liquid(s);poison+=type==1;lava+=type==2;
        if(type && s->linecount && SDL_getenv("D64_ENV_TRACE")) {
            double x=0,y=0;
            for(int j=0;j<s->linecount;j++) { x+=s->lines[j]->v1->x/(double)FRACUNIT;y+=s->lines[j]->v1->y/(double)FRACUNIT; }
            SDL_Log("liquid probe: sector %d center %.0f,%.0f floor %.0f",i,x/s->linecount,y/s->linecount,s->floorheight/(double)FRACUNIT);
        }
    }
    SDL_Log("native liquid effects: %d poison and %d lava sectors; visibility-limited 48 particle cap",poison,lava);
}
static int liquid(sector_t *s)
{
    if(s->floorpic<0) return 0;
    char name[9];W_PCLumpName(firsttex+s->floorpic,name);
    if(!name[0]) return 0;
    int mask=PCAddon_Environment();
    if((mask&1) && (!SDL_strncasecmp(name,"SLIME",5) || !SDL_strncasecmp(name,"D64N",4))) return 1;
    if((mask&2) && (!SDL_strncasecmp(name,"HLAVA",5) || !SDL_strncasecmp(name,"D64LAVA",7))) return 2;
    return 0;
}
void PCEnv_Tick(void)
{
    if(!enabled() || !players[0].mo) { memset(effects,0,sizeof(effects));return; }
    for(int i=0;i<MAX_FX;i++) {
        effect_t *e=&effects[i];if(!e->life) continue;
        e->oldz=e->z;e->z+=e->vz;
        if(e->type==1) { e->vz*=.94f;if(e->life<6) e->size=SDL_min(e->size*1.14f,9.f); }
        else e->vz-=.55f;
        if(--e->life<=0 || e->z<e->sector->floorheight/(float)FRACUNIT+1 ||
           e->z+e->size*.5f>e->sector->ceilingheight/(float)FRACUNIT) e->life=0;
    }
    if(++ticks%7) return;
    mobj_t *mo=players[0].mo;
    for(int attempt=0,spawned=0;attempt<12 && spawned<3;attempt++) {
        float x=mo->x/(float)FRACUNIT+(random01()-.5f)*768;
        float y=mo->y/(float)FRACUNIT+(random01()-.5f)*768;
        subsector_t *sub=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT));
        if(!sub || !sub->sector) continue;
        sector_t *s=sub->sector;int type=liquid(s);
        /* Trace at interpolated eye-to-surface height; avoid hidden-room lights. */
        float floor=s->floorheight/(float)FRACUNIT;
        if(!type || s->ceilingheight-s->floorheight<24*FRACUNIT ||
           fabsf(floor-mo->z/(float)FRACUNIT)>128) continue;
        sight_z=players[0].viewz/(float)FRACUNIT;sight_delta=floor+5-sight_z;
        if(!P_PathTraverse(mo->x,mo->y,(fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT),PT_ADDLINES,visible)) continue;
        effect_t *e=&effects[cursor++%MAX_FX];
        e->x=x;e->y=y;e->z=e->oldz=floor+3;e->sector=s;e->type=type;
        e->size=type==1 ? 3+random01()*3 : 2+random01()*2;
        e->vz=type==1 ? .4f+random01()*.9f : 3+random01()*4;
        e->life=e->total=type==1 ? 37 : 35;spawned++;
        if(!announced) { announced=1;SDL_Log("native liquid effects: first visible %s surface at %.0f, %.0f",type==1 ? "poison" : "lava",x,y); }
    }
}
static float height(effect_t *e) { return e->oldz+(e->z-e->oldz)*(float)I_PCInterpDrawFraction(); }
void PCEnv_Draw(void)
{
    if(!enabled()) return;
    if(!loaded) {
        loaded=1;
        for(int i=0;i<10;i++) {
            char name[9];int w,h;
            if(i<6) SDL_snprintf(name,sizeof(name),"PBUB%c0",'A'+i);
            else SDL_snprintf(name,sizeof(name),"LSPK%c0",'A'+i-6);
            const uint8_t *image=ResPack_Image(name,&w,&h);
            if(image) { fx_textures[i]=d64gfx_texture_create(w,h,image);frame_width[i]=w;frame_height[i]=h; }
        }
    }
    for(int i=0;i<MAX_FX;i++) {
        effect_t *e=&effects[i];if(!e->life) continue;
        int frames=e->type==1 ? 6 : 4;
        int frame=SDL_min((e->total-e->life)*frames/e->total,frames-1)+(e->type==1 ? 0 : 6);
        unsigned alpha=(unsigned)(e->life<6 ? e->life*36 : 216);
        PCText_WorldImage(fx_textures[frame],e->x,e->y,height(e),frame_width[frame]*e->size/5,
            frame_height[frame]*e->size/5,0xffffff00u|alpha);
    }
}
int PCEnv_Light(int index,float position[3],float color[3])
{
    if(!enabled()) return 0;
    int found=0;
    for(int i=0;i<MAX_FX;i++) if(effects[i].life) {
        effect_t *e=&effects[i];if(found++!=index) continue;
        position[0]=e->x;position[1]=height(e);position[2]=-e->y;
        color[0]=e->type==1 ? .16f : 1.f;color[1]=e->type==1 ? .67f : .32f;color[2]=.08f;
        return 1;
    }
    return 0;
}
