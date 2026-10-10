/*
 * config.c - tiny ini reader/writer for doom64rtx.ini.
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"

pcconfig_t pc_config;
pcconfig_t pc_config_file;
static char config_path[1024];

void Config_Defaults(void)
{
    memset(&pc_config, 0, sizeof(pc_config));
    pc_config.renderer = RENDERER_VULKAN;
    pc_config.raytracing = 1; /* falls back to raster when the GPU cannot ray trace */
    pc_config.rt_spp = 1;
    pc_config.rt_bounces = 1;
    pc_config.rt_denoise = 1;
    pc_config.rt_sun = 1;
    pc_config.rt_light_scale = 1.0f;
    pc_config.width = 1280;
    pc_config.height = 960;
    pc_config.vsync = 1;
    pc_config.mouse = 1;
    pc_config.brightness = 50;
    pc_config.sound = 1;
    pc_config.music_volume = 70;
    pc_config.sfx_volume = 90;
    pc_config.sound_upgrades = pc_config.combat_anims = 1;
    pc_config.fps = 120;
    pc_config.fast_weapons = 1;
    pc_config.always_run = 1;
    pc_config.mouselook = 1;
    pc_config.autoaim = 1;
    pc_config.crosshair = 1;
    pc_config.jump = 1;
    pc_config.weapon_bob = 100;
    pc_config.ads = 1;
    pc_config.gore = 1;
    pc_config.autosave = 1;
    pc_config.respacks = 1;
    pc_config.audio_rate = 44100;
    pc_config.mouse_sens = 1.0f;
    pc_config.gpu_index = -1;
    pc_config.log = 1;
}

typedef enum { CV_INT, CV_FLOAT, CV_STR, CV_RENDERER } cvtype_t;
typedef struct { const char *name; cvtype_t type; void *ptr; size_t len; } cvar_t;

static const cvar_t cvars[] = {
    { "rom",            CV_STR,      pc_config.rom, sizeof(pc_config.rom) },
    { "renderer",       CV_RENDERER, &pc_config.renderer, 0 },
    { "raytracing",     CV_INT,      &pc_config.raytracing, 0 },
    { "rt_spp",         CV_INT,      &pc_config.rt_spp, 0 },
    { "rt_bounces",     CV_INT,      &pc_config.rt_bounces, 0 },
    { "rt_denoise",     CV_INT,      &pc_config.rt_denoise, 0 },
    { "rt_sun",         CV_INT,      &pc_config.rt_sun, 0 },
    { "rt_light_scale", CV_FLOAT,    &pc_config.rt_light_scale, 0 },
    { "fullscreen",     CV_INT,      &pc_config.fullscreen, 0 },
    { "width",          CV_INT,      &pc_config.width, 0 },
    { "height",         CV_INT,      &pc_config.height, 0 },
    { "vsync",          CV_INT,      &pc_config.vsync, 0 },
    { "widescreen",     CV_INT,      &pc_config.aspect, 0 },
    { "aspect",         CV_INT,      &pc_config.aspect, 0 },   /* old name */
    { "filter",         CV_INT,      &pc_config.filter, 0 },
    { "brightness",     CV_INT,      &pc_config.brightness, 0 },
    { "mouse",          CV_INT,      &pc_config.mouse, 0 },
    { "mouse_sens",     CV_FLOAT,    &pc_config.mouse_sens, 0 },
    { "gpu_index",      CV_INT,      &pc_config.gpu_index, 0 },
    { "validation",     CV_INT,      &pc_config.validation, 0 },
    { "log",            CV_INT,      &pc_config.log, 0 },
    { "sound",          CV_INT,      &pc_config.sound, 0 },
    { "music_volume",   CV_INT,      &pc_config.music_volume, 0 },
    { "sound_upgrades", CV_INT,      &pc_config.sound_upgrades, 0 },
    { "combat_anims",   CV_INT,      &pc_config.combat_anims, 0 },
    { "sfx_volume",     CV_INT,      &pc_config.sfx_volume, 0 },
    { "audio_rate",     CV_INT,      &pc_config.audio_rate, 0 },
    { "fps",            CV_INT,      &pc_config.fps, 0 },
    { "mouselook",      CV_INT,      &pc_config.mouselook, 0 },
    { "invert_mouse",   CV_INT,      &pc_config.invert_mouse, 0 },
    { "always_run",     CV_INT,      &pc_config.always_run, 0 },
    { "autoaim",        CV_INT,      &pc_config.autoaim, 0 },
    { "crosshair",      CV_INT,      &pc_config.crosshair, 0 },
    { "jump",           CV_INT,      &pc_config.jump, 0 },
    { "weapon_bob",     CV_INT,      &pc_config.weapon_bob, 0 },
    { "ads",            CV_INT,      &pc_config.ads, 0 },
    { "fast_weapons",   CV_INT,      &pc_config.fast_weapons, 0 },
    { "gore",           CV_INT,      &pc_config.gore, 0 },
    { "autosave",       CV_INT,      &pc_config.autosave, 0 },
    { "respacks",       CV_INT,      &pc_config.respacks, 0 },
    { "packs",          CV_STR,      pc_config.packs, sizeof(pc_config.packs) },
    { "show_stats",     CV_INT,      &pc_config.show_stats, 0 },
};

static void set_cvar_in(pcconfig_t *cfg, const char *key, const char *val);
static void set_cvar(const char *key, const char *val)
{
    set_cvar_in(&pc_config, key, val);
}

/* cvar pointers refer to pc_config; translate to another config struct */
static void *field_in(pcconfig_t *cfg, void *ptr)
{
    return (char *)cfg + ((char *)ptr - (char *)&pc_config);
}

static void set_cvar_in(pcconfig_t *cfg, const char *key, const char *val)
{
    size_t i;
    for (i = 0; i < sizeof(cvars) / sizeof(cvars[0]); i++)
    {
        const cvar_t *c = &cvars[i];
        if (SDL_strcasecmp(c->name, key) != 0)
            continue;
        switch (c->type)
        {
        case CV_INT:   *(int *)field_in(cfg, c->ptr) = atoi(val); break;
        case CV_FLOAT: *(float *)field_in(cfg, c->ptr) = (float)atof(val); break;
        case CV_STR:   SDL_strlcpy((char *)field_in(cfg, c->ptr), val, c->len); break;
        case CV_RENDERER:
            *(int *)field_in(cfg, c->ptr) = (!SDL_strcasecmp(val, "opengl") || !SDL_strcasecmp(val, "gl"))
                                 ? RENDERER_OPENGL : RENDERER_VULKAN;
            break;
        }
        return;
    }
    SDL_Log("config: unknown setting '%s'", key);
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

const char *Config_Path(void)
{
    if (!config_path[0])
    {
        /* Portable install: an ini next to the executable wins. */
        const char *base = SDL_GetBasePath();
        char *pref;
        FILE *f;
        SDL_snprintf(config_path, sizeof(config_path), "%sdoom64rtx.ini", base ? base : "");
        f = fopen(config_path, "r");
        if (f)
        {
            fclose(f);
            return config_path;
        }
        pref = SDL_GetPrefPath("Doom64RTX", "Doom64RTX");
        if (pref)
        {
            SDL_snprintf(config_path, sizeof(config_path), "%sdoom64rtx.ini", pref);
            SDL_free(pref);
        }
    }
    return config_path;
}

void Config_Load(void)
{
    char line[1024];
    FILE *f = fopen(Config_Path(), "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f))
    {
        char *s = trim(line), *eq;
        if (!*s || *s == '#' || *s == ';' || *s == '[')
            continue;
        eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        set_cvar(trim(s), trim(eq + 1));
    }
    fclose(f);
}

/* called once after Config_Load, before command-line overrides */
void Config_Snapshot(void)
{
    pc_config_file = pc_config;
}

void Config_Save(void)
{
    size_t i;
    FILE *f = fopen(Config_Path(), "w");
    if (!f)
    {
        SDL_Log("config: cannot write %s", Config_Path());
        return;
    }
    fprintf(f, "# Doom64-RTX settings\n# renderer = vulkan | opengl ; raytracing = 0 | 1 (Vulkan only)\n");
    for (i = 0; i < sizeof(cvars) / sizeof(cvars[0]); i++)
    {
        const cvar_t *c = &cvars[i];
        if (!SDL_strcmp(c->name, "aspect"))
            continue; /* old alias of widescreen */
        switch (c->type)
        {
        case CV_INT:   fprintf(f, "%s = %d\n", c->name, *(int *)field_in(&pc_config_file, c->ptr)); break;
        case CV_FLOAT: fprintf(f, "%s = %g\n", c->name, *(float *)field_in(&pc_config_file, c->ptr)); break;
        case CV_STR:   fprintf(f, "%s = %s\n", c->name, (char *)field_in(&pc_config_file, c->ptr)); break;
        case CV_RENDERER:
            fprintf(f, "%s = %s\n", c->name,
                    *(int *)field_in(&pc_config_file, c->ptr) == RENDERER_OPENGL ? "opengl" : "vulkan");
            break;
        }
    }
    fclose(f);
}

void Config_ParseArgs(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        if (!SDL_strcmp(a, "-rom") && i + 1 < argc)        set_cvar("rom", argv[++i]);
        else if (!SDL_strcmp(a, "-pack") && i + 1 < argc)
        {
            if (pc_config.packs[0])
                SDL_strlcat(pc_config.packs, ";", sizeof(pc_config.packs));
            SDL_strlcat(pc_config.packs, argv[++i], sizeof(pc_config.packs));
        }
        else if (!SDL_strcmp(a, "-gl") || !SDL_strcmp(a, "-opengl")) pc_config.renderer = RENDERER_OPENGL;
        else if (!SDL_strcmp(a, "-vulkan") || !SDL_strcmp(a, "-vk")) pc_config.renderer = RENDERER_VULKAN;
        else if (!SDL_strcmp(a, "-rt"))      pc_config.raytracing = 1;
        else if (!SDL_strcmp(a, "-nort"))    pc_config.raytracing = 0;
        else if (!SDL_strcmp(a, "-fullscreen")) pc_config.fullscreen = 1;
        else if (!SDL_strcmp(a, "-window"))  pc_config.fullscreen = 0;
        else if (!SDL_strcmp(a, "-nolog"))   pc_config.log = 0;
        else if (!SDL_strcmp(a, "-nosound")) pc_config.sound = 0;
        else if (!SDL_strcmp(a, "-widescreen")) pc_config.aspect = 1;
        else if (!SDL_strcmp(a, "-set") && i + 2 < argc) { set_cvar(argv[i + 1], argv[i + 2]); i += 2; }
    }
}
