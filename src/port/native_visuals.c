/* Native panorama/title adaptation; original sky logic still advances normally. */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "r_local.h"
#include "config.h"
#include "respack.h"
#include "pc_text.h"
#include "native_addons.h"
#include "intro_audio.h"
#include "interp.h"
#include "d64gfx.h"
#include "native_visuals.h"
static int title_tics;
static unsigned logo,sky_images[7];
static int logo_attempted,sky_attempted;
static const char *sky_names[7]={"MOONSKY","SKYMTNA","SKYMTNB","SKYMTNC","SKYCLDPK","SKYCLDBR","SKYSTORM"};
static unsigned image(const char *name)
{
    int w,h;const uint8_t *rgba=ResPack_Image(name,&w,&h);
    return rgba ? d64gfx_texture_create(w,h,rgba) : 0;
}
void PCVisual_Reset(void)
{
    title_tics=0;I_PCIntroMusic(0);
    if(SDL_getenv("D64_SKY_TRACE")) for(int i=0;i<numsectors;i++) {
        sector_t *s=&sectors[i];if(s->ceilingpic!=-1 || !s->linecount) continue;
        double x=0,y=0;
        for(int j=0;j<s->linecount;j++) { x+=s->lines[j]->v1->x/(double)FRACUNIT;y+=s->lines[j]->v1->y/(double)FRACUNIT; }
        SDL_Log("sky probe: type %d sector %d center %.0f,%.0f",skytexture,i,x/s->linecount,y/s->linecount);
    }
}
void PCVisual_Stop(void) { I_PCIntroMusic(0); }
void PCVisual_Tick(void)
{
    if(gamemap!=33 || !PCAddon_Title() || !pc_config.title_intro) return;
    if(++title_tics==9 && I_PCIntroMusic(1)) S_StopMusic();
}
static float title_alpha(float t)
{
    t-=9;
    float alpha=t<0 ? 0 : t<30 ? t/30 : t<210 ? 1 : t<249 ? 1-(t-210)/39 : 0;
    return alpha*alpha;
}
void PCVisual_Title(void)
{
    if(gamemap!=33 || !PCAddon_Title() || !pc_config.title_intro) return;
    if(!logo_attempted) { logo_attempted=1;logo=image("D64RTLGO"); }
    float alpha=title_alpha(title_tics+(float)I_PCInterpDrawFraction());
    if(!logo || alpha<=0) return;
    unsigned opacity=(unsigned)(255*alpha);
    if(!opacity) return;
    PCText_Image(logo,60.8f,38.4f,198.4f,125.55f,0xffffff00u|opacity);
    PCText_Draw(-1,174,10,"RTX EDITION",0xd9c7a500u|opacity);
}
int PCVisual_MenuLogo(void)
{
    if(!pc_config.respacks) return 0;
    if(!logo_attempted) { logo_attempted=1;logo=image("D64RTLGO"); }
    if(!logo) return 0;
    PCText_Image(logo,89,24,142,89.86f,0xffffffd0u);return 1;
}
void PCVisual_Sky(int native_sky)
{
    if(!PCAddon_Sky() || !pc_config.sky_upgrades) return;
    int index;
    switch(native_sky) {
    case 6:index=0;break;case 11:index=1;break;case 3:index=2;break;case 10:index=3;break;
    case 1:index=gamemap==11 ? 6 : 4;break;case 2:case 5:index=5;break;
    default:return; /* Preserve procedural fire, void and evil skies. */
    }
    if(!sky_attempted) { sky_attempted=1;for(int i=0;i<7;i++) sky_images[i]=image(sky_names[i]); }
    if(!sky_images[index]) return;
    float yaw=viewangle/4294967296.f;
    float drift=index>=4 ? (gametic+(float)I_PCInterpDrawFraction())/12000.f : 0;
    /* 90-degree panorama window; horizontal FOV expands with widescreen. */
    float span=pc_config.aspect ? 1.f/3 : .25f;
    float pitch=(int32_t)(players[0].pc_pitch+players[0].recoilpitch)/4294967296.f;
    PCText_SkyImage(sky_images[index],yaw+drift-span*.5f,span,pitch);
}
