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
    float x=right-strlen(text)*10;
    for(char *p=text;*p;p++,x+=10) {
        char lump[9];SDL_snprintf(lump,sizeof(lump),"BDNUM%c",*p);
        unsigned digit=face(lump);
        if(digit) PCText_Image(digit,x,211,10,16,color);
        else { char fallback[2]={*p,0};PCText_Draw(x,211,15,fallback,color); }
    }
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
    unsigned bar=face("BDBAR");
    if(bar) PCText_Image(bar,0,208,320,32,0xffffffffu);
    else PCText_Box(0,208,320,32,0x302d28ffu);
    if(portrait) PCText_Image(portrait,145,209,30,30,0xffffffffu);
    PCText_Draw(65,230,7,"HEALTH",0xb9a98fffu);value(107,p->health,0xffffffffu);
    PCText_Draw(188,230,7,"ARMOR",0xb9a98fffu);value(225,p->armorpoints,0xffffffffu);
    PCText_Draw(7,230,7,"AMMO",0xb9a98fffu);
    int weapon=p->pendingweapon==wp_nochange ? p->readyweapon : p->pendingweapon;
    if(weapon>=0 && weapon<NUMWEAPONS && weaponinfo[weapon].ammo!=am_noammo)
        value(44,p->ammo[weaponinfo[weapon].ammo],0xffffffffu);
    static const unsigned colors[3]={0x559bdfffu,0xe0bd52ffu,0xdb6655ffu};
    for(int i=0;i<NUMCARDS;i++) if(p->cards[i] || (flashCards[i].active && flashCards[i].doDraw)) {
        float x=240+(i%3)*14,y=211+(i/3)*11;
        PCText_Box(x,y,9,8,colors[i%3]);
        PCText_Draw(x+2,y,7,i<3 ? "I" : "S",0x211c18ffu);
    }
}
