/* Native SMG/rifle variants of existing ammo/pickup slots. GPLv3.
 * Imported frames remain unmodified. No actor VM or new save fields. */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "p_local.h"
#include "config.h"
#include "respack.h"
#include "d64gfx.h"
#include "pc_text.h"
#include "combat_fx.h"
#include "soundpack.h"
#include "arsenal.h"

static const char *names[8]={"SMGRA0","SMGFA0","SMGFC0","SMGFB0","RIFGA0","RIFFA0","RIFFB0","RIFFC0"};
static const float x[8]={83,28,92,92,77,21,85,85};
static const float y[8]={112,0,112,112,112,119,112,112};
static unsigned weapon_textures[8];
static int width[8],height[8],attempted,last_fire=-100,fire_weapon=-1;
extern fixed_t bulletslope;
void P_BulletSlope(mobj_t *mo);
static void load(void)
{
    if(attempted) return;
    attempted=1;
    for(int i=0;i<8;i++) {
        const uint8_t *data=ResPack_Image(names[i],&width[i],&height[i]);
        if(data) weapon_textures[i]=d64gfx_texture_create(width[i],height[i],data);
    }
}
int PCArsenal_Active(int weapon)
{
    if(!pc_config.imported_weapons || !pc_config.respacks || demoplayback || demorecording) return 0;
    int base=weapon==wp_pistol ? 0 : weapon==wp_chaingun ? 4 : -1;
    if(base<0) return 0;
    load();
    for(int i=base;i<base+4;i++) if(!weapon_textures[i]) return 0;
    return 1;
}
void PCArsenal_Reset(void) { last_fire=-100;fire_weapon=-1; }
int PCArsenal_Fire(player_t *p)
{
    if(!PCArsenal_Active(p->readyweapon)) return 0;
    if(p->ammo[am_clip]<=0) return 1;
    p->ammo[am_clip]--;
    P_BulletSlope(p->mo);
    int r1=P_Random(),r2=P_Random();
    /* Fixed upstream base damage; ADS narrows native horizontal spread. */
    fixed_t spread=(r1-r2)*262144;
    spread=FixedMul(spread,FRACUNIT-FixedMul(p->pc_ads,0xc000));
    P_LineAttack(p->mo,p->mo->angle+(angle_t)spread,0,MISSILERANGE,
                 bulletslope,p->readyweapon==wp_pistol ? 12 : 20);
    S_StartSound(p->mo,pc_config.sound_upgrades ?
        (p->readyweapon==wp_pistol ? PC_SOUND_SMG : PC_SOUND_RIFLE) : sfx_pistol);
    last_fire=gametic;fire_weapon=p->readyweapon;
    return 1;
}
int PCArsenal_Draw(player_t *p)
{
    if(!PCArsenal_Active(p->readyweapon) || !p->psprites[ps_weapon].state) return 0;
    int index=p->readyweapon==wp_pistol ? 0 : 4;
    int age=gametic-last_fire;
    if(fire_weapon==p->readyweapon && age>=0 && age<3) index+=age+1;
    float px,py;PCCombat_Offsets(&px,&py);
    unsigned light=(unsigned)SDL_clamp(p->mo->subsector->sector->lightlevel,80,255);
    if(age==0 && fire_weapon==p->readyweapon) light=255;
    unsigned alpha=(p->mo->flags&MF_SHADOW) ? 100 : p->psprites[ps_weapon].alpha;
    PCText_Image(weapon_textures[index],x[index]+px+p->psprites[ps_weapon].sx/(float)FRACUNIT,
        y[index]+py+p->psprites[ps_weapon].sy/(float)FRACUNIT,width[index],height[index],
        (light<<24)|(light<<16)|(light<<8)|alpha);
    return 1;
}
