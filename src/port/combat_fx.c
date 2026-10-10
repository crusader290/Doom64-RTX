/* Render-only recoil, inertia, breathing, kick frames and impact feedback.
 * No game RNG/state/save fields. Updates at 30 Hz and interpolates for drawing. */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "config.h"
#include "interp.h"
#include "pc_text.h"
#include "respack.h"
#include "d64gfx.h"
#include "combat_fx.h"

typedef struct { float x,y,kick,hit; } pose_t;
static pose_t pose, previous;
static float velocity, recoil;
static angle_t last_angle;
static int angle_valid, lethal_hit, attempted;
static unsigned boots[13];
static int widths[13], heights[13];
/* Upstream grAb offsets, translated to Doom64's 320x240 overlay. */
static const float bx[13]={193,179,169,134,136,132,132,138,139,147,145,214,225};
static const float by[13]={208,176,112,112,112,112,112,112,112,176,176,222,232};
static const int sequence[16]={0,1,2,3,4,4,4,4,5,6,7,8,9,10,11,12};

static int enabled(void) { return pc_config.combat_anims && !demoplayback && !demorecording; }
static void load_boots(void)
{
    int i;
    if(attempted) return;
    attempted=1;
    for(i=0;i<13;i++) {
        char name[16],path[1024];
        uint8_t *owned=NULL;
        const uint8_t *image;
        SDL_snprintf(name,sizeof(name),"KICK%c0",'A'+i);
        image=ResPack_Image(name,&widths[i],&heights[i]);
        if(!image) {
            SDL_snprintf(path,sizeof(path),"%sassets/combat/kick/%s.png",SDL_GetBasePath(),name);
            image=owned=ResPack_LoadPNG(path,&widths[i],&heights[i]);
        }
        if(image) boots[i]=d64gfx_texture_create(widths[i],heights[i],image);
        ResPack_FreePNG(owned);
    }
    SDL_Log("combat animation: %d kick frames available", boots[12] ? 13 : 0);
}
void PCCombat_Reset(void)
{
    pose=previous=(pose_t){0,0,-1,0};
    recoil=velocity=0;angle_valid=0;lethal_hit=0;
}
void PCCombat_Tick(void)
{
    player_t *p=&players[0];
    float turn=0,ads;
    if(!enabled() || !p->mo || p->playerstate!=PST_LIVE) { PCCombat_Reset();return; }
    previous=pose;
    /* The kick timer is saved; restore its presentation after loading a slot. */
    if(pose.kick<0 && p->pc_kicktics>0 && p->pc_kicktics<=14)
        previous.kick=pose.kick=14-p->pc_kicktics;
    if(angle_valid) turn=(int32_t)(p->mo->angle-last_angle)*(360.0f/4294967296.0f);
    last_angle=p->mo->angle;angle_valid=1;
    ads=p->pc_ads/(float)FRACUNIT;
    pose.x+=(SDL_clamp(-turn*.2f,-4.0f,4.0f)*(1-ads*.8f)-pose.x)*.4f;
    velocity=(velocity-recoil*.22f)*.62f;recoil+=velocity;
    if(fabsf(recoil)<.01f && fabsf(velocity)<.01f) recoil=velocity=0;
    pose.y=recoil+sin((float)gametic*.1f)*.35f*(1-ads*.85f)*(pc_config.weapon_bob/100.0f);
    if(pose.kick>=0 && ++pose.kick>=14) pose.kick=-1;
    if(pose.hit>0) pose.hit--;
}
void PCCombat_Fire(void)
{
    float impulse=2.5f;
    if(!enabled()) return;
    switch(players[0].readyweapon) {
    case wp_shotgun: impulse=6;break;
    case wp_supershotgun: impulse=9;break;
    case wp_missile: impulse=7;break;
    case wp_bfg: impulse=8;break;
    case wp_plasma: impulse=1.5f;break;
    case wp_fist: impulse=1;break;
    default: break;
    }
    recoil=SDL_clamp(recoil+impulse,0,14);velocity=impulse*.2f;
    pose.y=recoil;
}
void PCCombat_Kick(void) { if(enabled()) { pose.kick=0;previous.kick=0; } }
void PCCombat_Hit(int lethal) { if(enabled()) { pose.hit=8;lethal_hit=lethal; } }
static float blend(float a,float b) { return a+(b-a)*(float)I_PCInterpDrawFraction(); }
void PCCombat_Offsets(float *x,float *y)
{
    *x=*y=0;
    if(!enabled()) return;
    *x=blend(previous.x,pose.x);*y=blend(previous.y,pose.y);
    if(pose.kick>=0) *y+=26*sinf(blend(previous.kick,pose.kick)*3.14159265f/14);
}
void PCCombat_Draw(void)
{
    if(!enabled() || !players[0].mo) return;
    if(pose.kick>=0) {
        int index,frame,next;
        float age=blend(previous.kick,pose.kick)*16/14,amount;
        int light=players[0].mo->subsector->sector->lightlevel;
        unsigned shade=(unsigned)SDL_clamp(100+light*155/255,100,255);
        load_boots();
        index=SDL_clamp((int)age,0,15);amount=age-index;
        frame=sequence[index];next=sequence[index<15 ? index+1 : index];
        PCText_Image(boots[frame],bx[frame]+(bx[next]-bx[frame])*amount,
            by[frame]+(by[next]-by[frame])*amount,widths[frame],heights[frame],
            (shade<<24)|(shade<<16)|(shade<<8)|255u);
    }
    if(pc_config.crosshair && pose.hit>0) {
        unsigned alpha=(unsigned)SDL_clamp(blend(previous.hit,pose.hit)*64,0,255);
        unsigned color=(lethal_hit ? 0xe5794500u : 0xe5d5ba00u)|alpha;
        PCText_Draw(155,112,17,"x",color);
    }
}
