/* Native behavior ports, not a ZScript/DECORATE virtual machine. Recognize
 * selected upstream handlers and literal BloodColor properties; report other
 * script classes instead of silently claiming they ran. GPLv3. */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "config.h"
#include "pc_text.h"
#include "native_addons.h"

static int persistent, flashlight, previous_action, charge, burning;
static uint32_t colors[NUMMOBJTYPES];
typedef struct { const char *p,*end; char token[128]; int quoted; } lexer_t;
static int token(lexer_t *l)
{
    size_t n=0;l->quoted=0;
    for(;;) {
        while(l->p<l->end && (unsigned char)*l->p<=32) l->p++;
        if(l->end-l->p>=2 && l->p[0]=='/' && l->p[1]=='/') {
            while(l->p<l->end && *l->p!='\n') l->p++;continue;
        }
        if(l->end-l->p>=2 && l->p[0]=='/' && l->p[1]=='*') {
            l->p+=2;while(l->end-l->p>=2 && !(l->p[0]=='*' && l->p[1]=='/')) l->p++;
            if(l->end-l->p>=2) l->p+=2;else l->p=l->end;continue;
        }
        break;
    }
    if(l->p==l->end) return 0;
    if(*l->p=='"') {
        l->quoted=1;l->p++;
        while(l->p<l->end && *l->p!='"') {
            if(n+1<sizeof(l->token)) l->token[n++]=*l->p;l->p++;
        }
        if(l->p<l->end) l->p++;
    } else if(SDL_isalnum((unsigned char)*l->p) || *l->p=='_') {
        while(l->p<l->end && (SDL_isalnum((unsigned char)*l->p) || *l->p=='_')) {
            if(n+1<sizeof(l->token)) l->token[n++]=*l->p;l->p++;
        }
    } else l->token[n++]=*l->p++;
    l->token[n]=0;return 1;
}
static int actor_type(const char *s)
{
    static const struct { const char *name; int type; } map[]={
        {"64NightmareImp",MT_IMP2},{"64Cacodemon",MT_CACODEMON},
        {"64PainElemental",MT_PAIN},{"64Arachnotron",MT_BABY},
        {"64HellKnight",MT_BRUISER2},{"64BaronOfHell",MT_BRUISER1}};
    for(size_t i=0;i<sizeof(map)/sizeof(map[0]);i++)
        if(!SDL_strcasecmp(s,map[i].name)) return map[i].type;
    return -1;
}
void PCAddon_Init(void)
{
    persistent=flashlight=0;memset(colors,0,sizeof(colors));
    PCAddon_ResetLevel();
}
int PCAddon_IsDefinition(const char *name)
{
    const char *base=SDL_strrchr(name,'/');base=base ? base+1 : name;
    return !SDL_strcasecmp(base,"DECORATE") || !SDL_strcasecmp(base,"ZSCRIPT") ||
           !SDL_strcasecmp(base,"MAPINFO") || !SDL_strcasecmp(base,"SBARINFO") ||
           !SDL_strcasecmp(base,"GLDEFS") || !SDL_strcasecmp(base,"TEXTURES") ||
           !SDL_strcasecmp(base,"CVARINFO") || !SDL_strcasecmp(base,"LOADACS");
}
void PCAddon_Definition(const char *name,const void *data,size_t size)
{
    lexer_t l={(const char *)data,(const char *)data+size,{0},0};
    int type=-1,depth=0,recognized=0,unknown=0,classes=0;
    if(size>1024*1024) return;
    const char *base=SDL_strrchr(name,'/');base=base ? base+1 : name;
    if(SDL_strcasecmp(base,"ZSCRIPT") && SDL_strcasecmp(base,"DECORATE")) {
        SDL_Log("native add-on: %s requires a native behavior/map conversion; declaration execution skipped",name);
        return;
    }
    while(token(&l)) {
        if(l.quoted) continue;
        if(!SDL_strcasecmp(l.token,"class")) {
            if(!token(&l)) break;
            if(!SDL_isalpha((unsigned char)l.token[0]) && l.token[0]!='_') continue;
            classes++;
            if(!SDL_strcasecmp(l.token,"RTBloodPersistHandler")) {
                persistent=1;recognized++;
                SDL_Log("native add-on: persistent blood handler adapted (128 stain cap, native physics)");
            } else if(!SDL_strcasecmp(l.token,"D64RtFlashlightHud")) {
                flashlight=1;recognized++;
                SDL_Log("native add-on: flashlight battery/HUD adapted (native RT spotlight)");
            } else unknown++;
        } else if(!SDL_strcasecmp(l.token,"ACTOR")) { type=-1;depth=0; }
        else if(!SDL_strcasecmp(l.token,"replaces")) {
            if(token(&l)) type=actor_type(l.token);
        } else if(!strcmp(l.token,"{")) depth++;
        else if(!strcmp(l.token,"}")) { if(depth>0) depth--;if(!depth) type=-1; }
        else if(type>=0 && !SDL_strcasecmp(l.token,"BloodColor") && token(&l)) {
            unsigned r,g,b;char tail;
            if(sscanf(l.token,"%x %x %x %c",&r,&g,&b,&tail)==3 && r<=255 && g<=255 && b<=255) {
                colors[type]=0x1000000u|(r<<16)|(g<<8)|b;recognized++;
            }
        }
    }
    SDL_Log("native add-on definitions %s: %d adapted declarations; %d unsupported classes%s",
        name,recognized,unknown,classes ? "; general script execution unavailable" : "; native properties only");
}
static int active(void) { return pc_config.native_addons && pc_config.respacks && !demoplayback && !demorecording; }
int PCAddon_GoreLife(void)
{
    if(pc_config.gore_life>=0) return SDL_clamp(pc_config.gore_life,0,108000);
    return active() && persistent ? 0 : 900;
}
int PCAddon_GoreLimit(void) { return SDL_clamp(pc_config.gore_limit,1,128); }
uint32_t PCAddon_BloodColor(int type)
{
    if(active() && type>=0 && type<NUMMOBJTYPES) {
        if(colors[type]) return colors[type]&0xffffffu;
        if(persistent && type==MT_BRUISER2) return 0x188c31;
    }
    return 0x730403;
}
void PCAddon_ResetLevel(void) { charge=900;burning=previous_action=0; }
void PCAddon_Tick(void)
{
    int action=I_PCActions()&8;
    if(!active() || !flashlight || !pc_config.flashlight || !players[0].mo || players[0].playerstate!=PST_LIVE) {
        previous_action=action;burning=0;return;
    }
    if(action && !previous_action) { burning=!burning;S_StartSound(NULL,sfx_switch1); }
    previous_action=action;
    if(burning) {
        if(charge>0) charge--;
        if(!charge) { burning=0;S_StartSound(NULL,sfx_switch2); }
    } else if(charge<900) charge=SDL_min(charge+2,900);
}
float PCAddon_Flashlight(void)
{
    if(!active() || !flashlight || !pc_config.flashlight || !burning) return 0;
    if(charge<120 && (gametic%11)<3) return .12f;
    return 1;
}
void PCAddon_Draw(void)
{
    if(!active() || !flashlight || !pc_config.flashlight) return;
    unsigned color=burning ? (charge<120 ? 0xdc7648ffu : 0xdfd1adffu) : 0x867964ffu;
    char label[32];
    SDL_snprintf(label,sizeof(label),"%s  LIGHT",SDL_GetScancodeName((SDL_Scancode)pc_config.bind_keys[B_LIGHT]));
    PCText_Draw(132,194,7,label,color);
    for(int i=0;i<5;i++) PCText_Box(139+i*9,204,7,3,charge>i*180 ? color : 0x39332cffu);
}
