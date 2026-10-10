/*
 * pc_options.c - the PC option pages shown by the game's menu code
 * (Options > Graphics, Options > Gameplay, and the F7 Debug page).
 *
 * The menu side (src/doom64/m_main.c, [PC]) only knows "page, item, label,
 * value, change(dir)"; everything about settings lives here.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>

#include "doomdef.h"
#include "p_local.h"
#include "config.h"
#include "input.h"
#include "d64gfx.h"
#include "pc_options.h"
#include "savegame.h"
#include "respack.h"
#include "gbi.h"

extern mobj_t mobjhead;
extern mapthing_t *spawnlist;
extern int spawncount;

void I_PCSetFullscreen(int on);
void PCOpt_RequestQuit(void)
{
    SDL_Event event={0};
    event.type=SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

static const char *onoff(int v)
{
    return v ? "On" : "Off";
}

static int rt_supported(void)
{
    D64GfxStatus st;
    d64gfx_status(&st);
    return st.rt_supported;
}

static void apply_rt_params(void)
{
    d64gfx_set_rt_params(pc_config.rt_spp, pc_config.rt_bounces, pc_config.rt_denoise,
                         pc_config.rt_light_scale, pc_config.rt_sun);
}

static int step_choice(int cur, const int *vals, int n, int dir)
{
    int i, idx = 0;
    for (i = 0; i < n; i++)
        if (vals[i] == cur)
            idx = i;
    if (dir == 0)
        dir = 1;
    idx = (idx + dir + n) % n;
    return vals[idx];
}

/* ---- Graphics ------------------------------------------------------------ */

enum { G_RT, G_RT_SPP, G_RT_BOUNCES, G_RT_DENOISE, G_FPS, G_ASPECT, G_VSYNC, G_FULLSCREEN, G_RENDERER, G_PACKS,
       G_BRIGHTNESS, G_FILTER, G_SKY, G_INTRO,
       G_COUNT };

static const char *gfx_label(int i)
{
    static const char *l[G_COUNT] = { "Ray Tracing", "RT Samples", "RT Bounces", "RT Denoiser",
                                      "Frame Rate", "Aspect Ratio", "VSync", "Fullscreen", "Renderer",
                                      "Texture Packs", "Brightness", "Texture Filter", "Sky Upgrade", "Title Intro" };
    return l[i];
}

static const char *gfx_value(int i, char *buf, int len)
{
    switch (i)
    {
    case G_RT:
        if (!rt_supported())
            return "None";
        return onoff(pc_config.raytracing);
    case G_RT_SPP:
        SDL_snprintf(buf, len, "%d", pc_config.rt_spp);
        return buf;
    case G_RT_BOUNCES:
        SDL_snprintf(buf, len, "%d", pc_config.rt_bounces);
        return buf;
    case G_RT_DENOISE:
        return onoff(pc_config.rt_denoise);
    case G_FPS:
        SDL_snprintf(buf, len, "%d", pc_config.fps);
        return buf;
    case G_ASPECT:
        return I_PCGetWidescreen() ? "16:9" : "4:3";
    case G_VSYNC:
        return onoff(pc_config.vsync);
    case G_FULLSCREEN:
        return onoff(pc_config.fullscreen);
    case G_RENDERER:
        return pc_config_file.renderer == RENDERER_OPENGL ? "OpenGL" : "Vulkan";
    case G_PACKS:
        return onoff(pc_config.respacks);
    case G_BRIGHTNESS: SDL_snprintf(buf, len, "%d%%", pc_config.brightness); return buf;
    case G_SKY: return onoff(pc_config.sky_upgrades);
    case G_INTRO: return onoff(pc_config.title_intro);
    case G_FILTER: return pc_config.filter ? "Nearest" : "Smooth";
    }
    return NULL;
}

static int gfx_change(int i, int dir)
{
    static const int spp[] = { 1, 2, 4, 8 };
    static const int bounces[] = { 0, 1, 2 };
    static const int fps[] = { 30, 60, 120 };

    switch (i)
    {
    case G_BRIGHTNESS:
        CONFIG_SET(brightness, SDL_clamp(pc_config.brightness + (dir < 0 ? -5 : 5), 0, 100));
        brightness = pc_config.brightness;
        P_RefreshBrightness();
        break;
    case G_SKY: CONFIG_SET(sky_upgrades,!pc_config.sky_upgrades);break;
    case G_INTRO: CONFIG_SET(title_intro,!pc_config.title_intro);break;
    case G_FILTER: CONFIG_SET(filter, !pc_config.filter); break;
    case G_RT:
        I_PCToggleRaytracing();
        break;
    case G_RT_SPP:
        CONFIG_SET(rt_spp, step_choice(pc_config.rt_spp, spp, 4, dir));
        apply_rt_params();
        break;
    case G_RT_BOUNCES:
        CONFIG_SET(rt_bounces, step_choice(pc_config.rt_bounces, bounces, 3, dir));
        apply_rt_params();
        break;
    case G_RT_DENOISE:
        CONFIG_SET(rt_denoise, !pc_config.rt_denoise);
        apply_rt_params();
        break;
    case G_FPS:
        CONFIG_SET(fps, step_choice(pc_config.fps, fps, 3, dir));
        break;
    case G_ASPECT:
        I_PCSetWidescreen(!I_PCGetWidescreen());
        break;
    case G_VSYNC:
        CONFIG_SET(vsync, !pc_config.vsync);
        d64gfx_set_vsync(pc_config.vsync);
        break;
    case G_FULLSCREEN:
        I_PCSetFullscreen(!pc_config.fullscreen);
        break;
    case G_RENDERER:
        /* takes effect on the next start */
        pc_config_file.renderer = pc_config_file.renderer == RENDERER_OPENGL ? RENDERER_VULKAN : RENDERER_OPENGL;
        break;
    case G_PACKS:
        CONFIG_SET(respacks, !pc_config.respacks);
        ResPack_SetEnabled(pc_config.respacks);
        GBI_FlushTextureCache(); /* re-decode with or without replacements */
        break;
    }
    Config_Save();
    return 0;
}

/* ---- Gameplay ------------------------------------------------------------ */

enum { P_MOUSELOOK, P_INVERT, P_SENS, P_RUN, P_AUTOAIM, P_CROSSHAIR, P_JUMP, P_BOB, P_ADS, P_FASTWEAP,
       P_GORE, P_AUTOSAVE, P_ANIMS, P_NATIVE, P_LIGHT, P_LIFE, P_LIMIT, P_FACE, P_ENV, P_WEAPONS, P_COUNT };

static const char *play_label(int i)
{
    static const char *l[P_COUNT] = { "Mouse Look", "Invert Mouse", "Mouse Speed", "Always Run",
                                      "Autoaim", "Crosshair", "Jumping", "Bobbing", "Aim Sights",
                                      "Fast Weapons", "Immersive Gore", "Autosave", "Combat Animation",
                                      "Native Add-ons", "Flashlight", "Blood Lifetime", "Blood Limit", "Face HUD", "Liquid Effects", "Imported Weapons" };
    return l[i];
}

static const char *play_value(int i, char *buf, int len)
{
    switch (i)
    {
    case P_MOUSELOOK: return onoff(pc_config.mouselook);
    case P_INVERT: return onoff(pc_config.invert_mouse);
    case P_SENS:
        SDL_snprintf(buf, len, "%.2f", pc_config.mouse_sens);
        return buf;
    case P_RUN: return onoff(pc_config.always_run);
    case P_AUTOAIM: return onoff(pc_config.autoaim);
    case P_CROSSHAIR: return onoff(pc_config.crosshair);
    case P_JUMP: return onoff(pc_config.jump);
    case P_BOB:
        SDL_snprintf(buf, len, "%d", pc_config.weapon_bob);
        return buf;
    case P_ADS: return onoff(pc_config.ads);
    case P_FASTWEAP: return onoff(pc_config.fast_weapons);
    case P_GORE: return onoff(pc_config.gore);
    case P_AUTOSAVE: return onoff(pc_config.autosave);
    case P_ANIMS: return onoff(pc_config.combat_anims);
    case P_NATIVE: return onoff(pc_config.native_addons);
    case P_LIGHT: return onoff(pc_config.flashlight);
    case P_WEAPONS: return onoff(pc_config.imported_weapons);
    case P_ENV: return onoff(pc_config.environment_fx);
    case P_FACE: return onoff(pc_config.mugshot);
    case P_LIFE:
        if(pc_config.gore_life<0) return "Pack Default";
        if(!pc_config.gore_life) return "Permanent";
        SDL_snprintf(buf,len,"%d s",pc_config.gore_life/30);return buf;
    case P_LIMIT: SDL_snprintf(buf,len,"%d",pc_config.gore_limit);return buf;
    }
    return NULL;
}

static int play_change(int i, int dir)
{
    static const int bob[] = { 0, 25, 50, 75, 100 };
    switch (i)
    {
    case P_MOUSELOOK:
        CONFIG_SET(mouselook, !pc_config.mouselook);
        if (!pc_config.mouselook)
            players[0].pc_pitch = 0;
        break;
    case P_INVERT: CONFIG_SET(invert_mouse, !pc_config.invert_mouse); break;
    case P_SENS:
    {
        float s = pc_config.mouse_sens + (dir < 0 ? -0.25f : 0.25f);
        if (s < 0.25f)
            s = dir == 0 ? 0.25f : 0.25f;
        if (s > 4.0f)
            s = dir == 0 ? 0.25f : 4.0f;
        CONFIG_SET(mouse_sens, s);
        break;
    }
    case P_RUN: CONFIG_SET(always_run, !pc_config.always_run); break;
    case P_AUTOAIM: CONFIG_SET(autoaim, !pc_config.autoaim); break;
    case P_CROSSHAIR: CONFIG_SET(crosshair, !pc_config.crosshair); break;
    case P_JUMP: CONFIG_SET(jump, !pc_config.jump); break;
    case P_BOB: CONFIG_SET(weapon_bob, step_choice(pc_config.weapon_bob, bob, 5, dir)); break;
    case P_ADS: CONFIG_SET(ads, !pc_config.ads); break;
    case P_FASTWEAP: CONFIG_SET(fast_weapons, !pc_config.fast_weapons); break;
    case P_GORE: CONFIG_SET(gore, !pc_config.gore); break;
    case P_AUTOSAVE: CONFIG_SET(autosave, !pc_config.autosave); break;
    case P_ANIMS: CONFIG_SET(combat_anims, !pc_config.combat_anims); break;
    case P_NATIVE: CONFIG_SET(native_addons,!pc_config.native_addons);break;
    case P_LIGHT: CONFIG_SET(flashlight,!pc_config.flashlight);break;
    case P_WEAPONS: CONFIG_SET(imported_weapons,!pc_config.imported_weapons);break;
    case P_ENV: CONFIG_SET(environment_fx,!pc_config.environment_fx);break;
    case P_FACE: CONFIG_SET(mugshot,!pc_config.mugshot);break;
    case P_LIFE: {
        static const int lives[]={-1,0,300,900,1800};
        CONFIG_SET(gore_life,step_choice(pc_config.gore_life,lives,5,dir));break;
    }
    case P_LIMIT: {
        static const int limits[]={32,64,96,128};
        CONFIG_SET(gore_limit,step_choice(pc_config.gore_limit,limits,4,dir));break;
    }
    }
    Config_Save();
    return 0;
}

/* ---- Debug (F7) ---------------------------------------------------------- */

enum { D_STATS, D_GOD, D_NOCLIP, D_GIVE, D_KILL, D_FREEZE, D_MAP, D_BRIGHT, D_RT, D_NEXTMAP, D_COUNT };

static const char *dbg_label(int i)
{
    static const char *l[D_COUNT] = { "Show Stats", "God Mode", "No Clipping", "Give Everything",
                                      "Kill Monsters", "Freeze Monsters", "Reveal Map", "Full Bright",
                                      "Ray Tracing", "Next Level" };
    return l[i];
}

static const char *dbg_value(int i, char *buf, int len)
{
    player_t *p = &players[0];
    (void)buf;
    (void)len;
    switch (i)
    {
    case D_STATS: return onoff(pc_config.show_stats);
    case D_GOD: return onoff(p->cheats & CF_GODMODE);
    case D_NOCLIP: return onoff(p->mo && (p->mo->flags & MF_NOCLIP));
    case D_FREEZE: return onoff(p->cheats & CF_LOCKMOSTERS);
    case D_MAP: return onoff(p->cheats & CF_ALLMAP);
    case D_BRIGHT: return onoff(p->cheats & CF_FULLBRIGHT);
    case D_RT: return rt_supported() ? onoff(pc_config.raytracing) : "None";
    }
    return NULL;
}

static void give_everything(player_t *p)
{
    int i;
    mobj_t *m;

    for (i = 0; i < NUMWEAPONS; i++)
        p->weaponowned[i] = true;
    if (!p->backpack)
    {
        for (i = 0; i < NUMAMMO; i++)
            p->maxammo[i] *= 2;
        p->backpack = true;
    }
    for (i = 0; i < NUMAMMO; i++)
        p->ammo[i] = p->maxammo[i];
    p->health = 200;
    if (p->mo)
        p->mo->health = 200;
    p->armorpoints = 200;
    p->armortype = 2;
    for (i = 0; i < NUMCARDS; i++)
        p->cards[i] = true;
    for (i = 0; i < 3; i++)
        p->artifacts |= 1 << i;
    p->cheats |= CF_WEAPONS | CF_HEALTH | CF_ALLKEYS;
    (void)m;
}

static void kill_monsters(void)
{
    mobj_t *m;
    int n = 0;
    for (m = mobjhead.next; m != &mobjhead; m = m->next)
    {
        if ((m->flags & MF_COUNTKILL) && m->health > 0)
        {
            P_DamageMobj(m, NULL, NULL, 10000);
            n++;
        }
    }
    I_PCLog("debug: killed %d monsters", n);
}

static int dbg_change(int i, int dir)
{
    player_t *p = &players[0];
    (void)dir;
    switch (i)
    {
    case D_STATS:
        CONFIG_SET(show_stats, !pc_config.show_stats);
        Config_Save();
        break;
    case D_GOD: p->cheats ^= CF_GODMODE; break;
    case D_NOCLIP:
        if (p->mo)
        {
            p->mo->flags ^= MF_NOCLIP;
            if (p->mo->flags & MF_NOCLIP)
                p->cheats |= CF_NOCLIP;
            else
                p->cheats &= ~CF_NOCLIP;
        }
        break;
    case D_GIVE: give_everything(p); break;
    case D_KILL: kill_monsters(); break;
    case D_FREEZE: p->cheats ^= CF_LOCKMOSTERS; break;
    case D_MAP: p->cheats ^= CF_ALLMAP; break;
    case D_BRIGHT: p->cheats ^= CF_FULLBRIGHT; break;
    case D_RT: I_PCToggleRaytracing(); break;
    case D_NEXTMAP:
        if (gamemap < 33)
        {
            gamemap += 1;
            startmap = gamemap;
            return ga_warped;
        }
        break;
    }
    return 0;
}

/* ---- Save / Load ----------------------------------------------------------- */

static const char *slot_label(int i)
{
    static char l[PC_SAVE_SLOTS][16];
    if (i == PC_QUICK_SLOT)
        return "Quick";
    if (i == PC_AUTO_SLOT)
        return "Auto";
    SDL_snprintf(l[i], sizeof(l[i]), "Slot %d", i + 1);
    return l[i];
}

static const char *slot_value(int i, char *buf, int len)
{
    if (!G_PCSaveSlotInfo(i, buf, len))
        return "Empty";
    return buf;
}

static int save_change(int i, int dir)
{
    if (dir != 0 || i == PC_AUTO_SLOT)
        return 0;
    if (G_PCSaveGame(i))
    {
        players[0].message = "Game saved.";
        players[0].messagetic = MSGTICS;
        return ga_exit;
    }
    return 0;
}

static int load_change(int i, int dir)
{
    if (dir != 0)
        return 0;
    if (G_PCLoadGame(i))
        return ga_warped;
    return 0;
}

/* ---- page table ---------------------------------------------------------- */

int PCOpt_Count(int page)
{
    switch (page)
    {
    case PCPAGE_GRAPHICS: return G_COUNT;
    case PCPAGE_GAMEPLAY: return P_COUNT;
    case PCPAGE_DEBUG: return D_COUNT;
    case PCPAGE_AUDIO: return 4;
    case PCPAGE_CONTROLS: return 11;
    case PCPAGE_BINDINGS: return B_COUNT+1;
    case PCPAGE_SAVE:
    case PCPAGE_LOAD: return PC_SAVE_SLOTS;
    }
    return 0;
}

const char *PCOpt_Title(int page)
{
    switch (page)
    {
    case PCPAGE_GRAPHICS: return "Graphics";
    case PCPAGE_GAMEPLAY: return "Gameplay";
    case PCPAGE_DEBUG: return "Debug";
    case PCPAGE_AUDIO: return "Audio";
    case PCPAGE_CONTROLS: return "Controls";
    case PCPAGE_BINDINGS: return "Key Bindings";
    case PCPAGE_SAVE: return "Save Game";
    case PCPAGE_LOAD: return "Load Game";
    }
    return "";
}

const char *PCOpt_Label(int page, int i)
{
    static const char *audio[] = {"Music Volume", "Effects Volume", "Output Rate", "New Sounds"};
    static const char *bindings[B_COUNT]={"Forward","Backward","Strafe Left","Strafe Right","Use / Open",
        "Jump","Kick","Flashlight","Fire","Automap","Run / Walk","Previous Weapon"};
    static const char *controls[] = {"Move / Strafe", "Fire", "Aim Sights", "Use / Open", "Jump", "Kick", "Weapons", "Quick Save / Load", "Debug / Ray Tracing", "Fullscreen / Capture", "Flashlight"};
    if (i < 0 || i >= PCOpt_Count(page))
        return "";
    switch (page)
    {
    case PCPAGE_GRAPHICS: return gfx_label(i);
    case PCPAGE_GAMEPLAY: return play_label(i);
    case PCPAGE_DEBUG: return dbg_label(i);
    case PCPAGE_AUDIO: return audio[i];
    case PCPAGE_CONTROLS: return controls[i];
    case PCPAGE_BINDINGS: return i==B_COUNT ? "Reset Bindings" : bindings[i];
    case PCPAGE_SAVE:
    case PCPAGE_LOAD: return slot_label(i);
    }
    return "";
}

const char *PCOpt_Value(int page, int i, char *buf, int len)
{
    static const char *keys[] = {"W A S D", "LMB / Ctrl", "RMB", "E / MMB", "Space", "V / Mouse4", "1-8 / Wheel", "F5 / F9", "F7 / F10", "F11 / F12", "F (Ray Tracing)"};
    if (i < 0 || i >= PCOpt_Count(page))
        return NULL;
    switch (page)
    {
    case PCPAGE_GRAPHICS: return gfx_value(i, buf, len);
    case PCPAGE_GAMEPLAY: return play_value(i, buf, len);
    case PCPAGE_DEBUG: return dbg_value(i, buf, len);
    case PCPAGE_CONTROLS:
        switch(i) {
        case 0: SDL_snprintf(buf,len,"%s %s %s %s",SDL_GetScancodeName(pc_config.bind_keys[B_FORWARD]),
            SDL_GetScancodeName(pc_config.bind_keys[B_LEFT]),SDL_GetScancodeName(pc_config.bind_keys[B_BACK]),
            SDL_GetScancodeName(pc_config.bind_keys[B_RIGHT]));return buf;
        case 1: SDL_snprintf(buf,len,"LMB / %s",SDL_GetScancodeName(pc_config.bind_keys[B_FIRE]));return buf;
        case 3: SDL_snprintf(buf,len,"%s / MMB",SDL_GetScancodeName(pc_config.bind_keys[B_USE]));return buf;
        case 4: return SDL_GetScancodeName(pc_config.bind_keys[B_JUMP]);
        case 5: SDL_snprintf(buf,len,"%s / Mouse4",SDL_GetScancodeName(pc_config.bind_keys[B_KICK]));return buf;
        case 10: SDL_snprintf(buf,len,"%s (RT)",SDL_GetScancodeName(pc_config.bind_keys[B_LIGHT]));return buf;
        default:return keys[i];
        }
    case PCPAGE_BINDINGS: if(i==B_COUNT) return NULL;return IN_BindingIndex()==i ? "Press a key..." :
        SDL_GetScancodeName((SDL_Scancode)pc_config.bind_keys[i]);
    case PCPAGE_AUDIO:
        SDL_snprintf(buf, len, i == 2 ? "%d Hz" : "%d%%",
                     i == 0 ? pc_config.music_volume : i == 1 ? pc_config.sfx_volume : pc_config.audio_rate);
        if (i==3) return onoff(pc_config.sound_upgrades);
        return buf;
    case PCPAGE_SAVE:
    case PCPAGE_LOAD: return slot_value(i, buf, len);
    }
    return NULL;
}

int PCOpt_Change(int page, int i, int dir)
{
    if (i < 0 || i >= PCOpt_Count(page))
        return 0;
    switch (page)
    {
    case PCPAGE_GRAPHICS: return gfx_change(i, dir);
    case PCPAGE_GAMEPLAY: return play_change(i, dir);
    case PCPAGE_DEBUG: return dbg_change(i, dir);
    case PCPAGE_BINDINGS:
        if(!dir) { if(i==B_COUNT) { Config_ResetBindings();Config_Save(); } else IN_BeginBinding(i); }
        return 0;
    case PCPAGE_AUDIO:
        if (i == 0) {
            CONFIG_SET(music_volume, SDL_clamp(pc_config.music_volume + (dir < 0 ? -5 : 5), 0, 100));
            MusVolume = pc_config.music_volume; S_SetMusicVolume(MusVolume);
        } else if (i == 1) {
            CONFIG_SET(sfx_volume, SDL_clamp(pc_config.sfx_volume + (dir < 0 ? -5 : 5), 0, 100));
            SfxVolume = pc_config.sfx_volume; S_SetSoundVolume(SfxVolume);
        } else if (i==3) {
            CONFIG_SET(sound_upgrades,!pc_config.sound_upgrades);
        } else {
            static const int rates[] = {22050, 44100, 48000};
            CONFIG_SET(audio_rate, step_choice(pc_config.audio_rate, rates, 3, dir));
        }
        Config_Save(); return 0;
    case PCPAGE_SAVE: return save_change(i, dir);
    case PCPAGE_LOAD: return load_change(i, dir);
    }
    return 0;
}

/* Items that only act on confirm (no left/right cycling). */
int PCOpt_IsAction(int page, int i)
{
    if (page == PCPAGE_SAVE || page == PCPAGE_LOAD || page == PCPAGE_BINDINGS)
        return 1;
    return page == PCPAGE_DEBUG && (i == D_GIVE || i == D_KILL || i == D_NEXTMAP);
}

int PCOpt_ValueX(int page)
{
    return (page == PCPAGE_SAVE || page == PCPAGE_LOAD) ? 140 : 214;
}

const char *PCOpt_Help(int page, int item)
{
    if (page == PCPAGE_GRAPHICS) {
        static const char *help[] = {
            "F10 toggles tracing on supported Vulkan GPUs.", "More samples reduce noise and cost GPU time.",
            "Indirect light bounces. Higher costs GPU time.", "Smooth noisy lighting across frames.",
            "30 Hz logic; 60/120 fps interpolate motion.", "Wider view; menus and HUD retain proportions.",
            "Sync presentation to the display refresh.", "F11 or Alt+Enter toggles fullscreen.",
            "Renderer changes apply after restarting.", "Bundled sprite and RT material upgrades.",
            "Lift visibility while preserving sector light.", "Nearest is crisp; Smooth blends texels.",
            "High-resolution panoramas follow camera yaw and pitch.",
            "Fading title logo and a one-shot sting through Music Volume."};
        _Static_assert(sizeof(help)/sizeof(help[0])==G_COUNT,"graphics help matches settings");
        return item >= 0 && item < G_COUNT ? help[item] : "";
    }
    if (page == PCPAGE_GAMEPLAY) {
        if (item == P_FASTWEAP) return "Faster switching and attack recovery. Demos stay classic.";
        if (item == P_GORE) return "Blood spray, wall splashes, floor stains and overkill gibs.";
        if (item == P_NATIVE) return "Ported pack behaviors. General GZDoom scripts need conversion.";
        if (item == P_LIGHT) return "Flashlight key toggles an unlimited RT beam.";
        if (item == P_WEAPONS) return "Brutal Doom SMG and rifle use the pistol and chaingun pickups.";
        if (item == P_FACE) return "Reactive portraits, health, armor, ammo and key indicators.";
        if (item == P_ENV) return "Poison bubbles and compatible lava sparks, with small RT lights.";
        if (item == P_LIFE) return "Pack Default uses persistent blood when its adapter is loaded.";
        if (item == P_LIMIT) return "Oldest stains are replaced. Native hard cap: 128.";
        if (item == P_AUTOSAVE) return "Keep an Auto slot at each level start. F5/F9 quicksave/load.";
        if (item == P_RUN) return "Run by default; hold Shift to walk.";
        if (item == P_ADS) return "Hold RMB to focus your view. V kicks without switching.";
        return "Changes apply immediately and are saved to your configuration.";
    }
    if (page == PCPAGE_AUDIO) return item == 2 ? "Output rate changes apply after restarting." :
        item == 3 ? "Upgraded impacts and weapon sounds. Off restores original audio." :
        "Separate music and effects levels; changes apply immediately.";
    if (page == PCPAGE_CONTROLS) return "Keyboard / mouse shortcuts. Standard gamepads supported.";
    if (page == PCPAGE_BINDINGS) return "Enter to rebind; Esc cancels. Conflicts swap. Menu keys stay fixed.";
    if (page == PCPAGE_SAVE) return "Confirm to save. The Auto slot is managed by the game.";
    if (page == PCPAGE_LOAD) return "Confirm a populated slot to restore the level.";
    return "Developer tools for testing gameplay and renderer behavior.";
}
