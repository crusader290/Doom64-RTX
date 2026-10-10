/* Native adaptation of Retribution's DrawMugShot layout. No game RNG or save fields. */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "st_main.h"
#include "config.h"
#include "native_addons.h"
#include "pc_hud.h"
#include "pc_text.h"
#include "respack.h"
#include "d64gfx.h"

static int health, hurt, ouch, pickup, oldweapon;
static struct { char name[9]; unsigned texture; } faces[64];
static int count;
void PCHud_Reset(void) { health=players[0].health;hurt=ouch=pickup=0;oldweapon=players[0].readyweapon; }
int PCHud_Active(void) { return PCAddon_Mugshot() && pc_config.mugshot && PCText_Available(); }
void PCHud_Tick(void)
{
    player_t *p=&players[0];
    if(hurt>0) hurt--;
    if(pickup>0) pickup--;
    if(p->health<health) { hurt=18;ouch=health-p->health>=20; }
    if(p->readyweapon!=oldweapon && p->bonuscount>0) pickup=24;
    health=p->health;oldweapon=p->readyweapon;
}
static unsigned face(const char *name)
{
    int w,h;
    for(int i=0;i<count;i++) if(!strcmp(faces[i].name,name)) return faces[i].texture;
    if(count>=64) return 0;
    const uint8_t *image=ResPack_Image(name,&w,&h);
    SDL_strlcpy(faces[count].name,name,sizeof(faces[count].name));
    faces[count].texture=image ? d64gfx_texture_create(w,h,image) : 0;
    return faces[count++].texture;
}
static void value(float right,int number,unsigned color)
{
    char text[16];SDL_snprintf(text,sizeof(text),"%d",SDL_max(number,0));
    PCText_Draw(right-PCText_Width(15,text),216,15,text,color);
}
void PCHud_Draw(void)
{
    player_t *p=&players[0];char name[9];
    int band=SDL_clamp((100-p->health)/20,0,4);
    if(p->health<=0) SDL_strlcpy(name,"STFDEAD0",sizeof(name));
    else if((p->cheats&CF_GODMODE) || p->powers[pw_invulnerability]) SDL_strlcpy(name,"STFGOD0",sizeof(name));
    else if(hurt) SDL_snprintf(name,sizeof(name),ouch ? "STFOUCH%d" : "STFTL%d0",band);
    else if(pickup) SDL_snprintf(name,sizeof(name),"STFEVL%d",band);
    else if(p->attackdown) SDL_snprintf(name,sizeof(name),"STFKILL%d",band);
    else SDL_snprintf(name,sizeof(name),"STFST%d%d",band,(gametic/35)%3);
    unsigned portrait=face(name);
    if(!portrait) portrait=face("STFST00");
    PCText_Box(76,200,64,40,0x151311c0u);
    PCText_Box(101,201,38,39,0x645744c0u);
    if(portrait) PCText_Image(portrait,102,202,36,37,0xffffffffu);
    PCText_Draw(76,205,7,"HP",0xcbbb9cffu);value(99,p->health,p->health<25 ? 0xe97050ffu : 0xe5d4afffu);
    PCText_Draw(227,205,7,"ARMOR",0xcbbb9cffu);value(255,p->armorpoints,0xe5d4afffu);
    PCText_Draw(279,205,7,"AMMO",0xcbbb9cffu);
    int weapon=p->pendingweapon==wp_nochange ? p->readyweapon : p->pendingweapon;
    if(weapon>=0 && weapon<NUMWEAPONS && weaponinfo[weapon].ammo!=am_noammo)
        value(306,p->ammo[weaponinfo[weapon].ammo],0xe5d4afffu);
    static const unsigned colors[3]={0x559bdfffu,0xe0bd52ffu,0xdb6655ffu};
    for(int i=0;i<NUMCARDS;i++) if(p->cards[i] || (flashCards[i].active && flashCards[i].doDraw)) {
        float x=174+(i%3)*14,y=210+(i/3)*11;
        PCText_Box(x,y,9,8,colors[i%3]);
        PCText_Draw(x+2,y,7,i<3 ? "I" : "S",0x211c18ffu);
    }
}
