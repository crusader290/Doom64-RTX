#include "../src/port/config.c"
#include "../src/port/input.c"
#include <assert.h>
#include <limits.h>
int I_PCTestLook(void) { return 0; }
int I_PCTestActions(void) { return 0; }
static void press(SDL_Scancode code,int repeat)
{
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=code;e.key.repeat=repeat;
    IN_HandleEvent(&e);
}
int main(void)
{
    SDL_strlcpy(config_path,"build-tests/bindings.ini",sizeof(config_path));
    Config_Defaults();Config_Snapshot();assert(pc_config.bind_keys[B_FORWARD]==SDL_SCANCODE_W);
    IN_BeginBinding(B_FORWARD);press(SDL_SCANCODE_S,0);
    assert(capture==-1 && pc_config.bind_keys[B_FORWARD]==SDL_SCANCODE_S);
    assert(pc_config.bind_keys[B_BACK]==SDL_SCANCODE_W);
    assert(pc_config_file.bind_keys[B_BACK]==SDL_SCANCODE_W);
    Config_Defaults();Config_Load();assert(pc_config.bind_keys[B_FORWARD]==SDL_SCANCODE_S);
    assert(pc_config.bind_keys[B_BACK]==SDL_SCANCODE_W);
    IN_BeginBinding(B_KICK);press(SDL_SCANCODE_RETURN,0);assert(capture==B_KICK);
    press(SDL_SCANCODE_F7,0);assert(capture==B_KICK);
    press(SDL_SCANCODE_1,0);assert(capture==B_KICK && weapon_key==0);
    press(SDL_SCANCODE_UP,0);assert(capture==B_KICK);
    press(SDL_SCANCODE_G,1);assert(capture==B_KICK);
    press(SDL_SCANCODE_ESCAPE,0);assert(capture==-1 && pc_config.bind_keys[B_KICK]==SDL_SCANCODE_V);
    IN_BeginBinding(-1);assert(capture==-1);IN_BeginBinding(B_COUNT);assert(capture==-1);
    IN_BeginBinding(B_KICK);press(SDL_SCANCODE_G,0);assert(pc_config.bind_keys[B_KICK]==SDL_SCANCODE_G);
    bool keys[SDL_SCANCODE_COUNT]={0};keys[SDL_SCANCODE_G]=1;
    assert(bound(keys,B_KICK));pc_config.bind_keys[B_KICK]=-1;assert(!bound(keys,B_KICK));
    pc_config.bind_keys[B_KICK]=INT_MAX;assert(!bound(keys,B_KICK));
    Config_ResetBindings();assert(pc_config.bind_keys[B_KICK]==SDL_SCANCODE_V);
    assert(pc_config_file.bind_keys[B_KICK]==SDL_SCANCODE_V);
    char *args[]={"doom64rtx","-warp","7","-skill","1"};Config_ParseArgs(5,args);
    assert(pc_config.startup_map==7 && pc_config.startup_skill==1);
    assert(pc_config_file.startup_map==0);
    char *bad[]={"doom64rtx","-warp","33","-skill","9"};Config_ParseArgs(5,bad);
    assert(pc_config.startup_map==7 && pc_config.startup_skill==1);
    Config_Save();Config_Defaults();Config_Load();assert(pc_config.startup_map==0 && pc_config.startup_skill==3);
    puts("PASS: binding conflicts, save/reload, cancellation, repeats, reserved/invalid keys and transient warp/skill");return 0;
}
