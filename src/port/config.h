/*
 * config.h - PC settings (doom64rtx.ini next to the executable / in the
 * user's pref dir). Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_CONFIG_H
#define D64_CONFIG_H

typedef enum { RENDERER_VULKAN = 0, RENDERER_OPENGL = 1 } renderer_t;

typedef struct {
    char  rom[512];          /* explicit ROM path, empty = auto detect */
    int   renderer;          /* renderer_t */
    int   raytracing;        /* 1 = use the Vulkan ray traced path when supported */
    int   rt_spp;            /* samples per pixel for the RT path */
    int   rt_bounces;        /* indirect bounces (0..2) */
    int   rt_denoise;        /* temporal accumulation / denoise */
    int   rt_sun;            /* moonlight on sky-lit areas */
    float rt_light_scale;    /* emitter intensity multiplier */
    int   fullscreen;
    int   width, height;     /* window size */
    int   vsync;
    int   aspect;            /* 0 = 4:3, 1 = 16:9 widescreen (Options > Display) */
    int   filter;            /* 0 = follow game (N64 bilinear), 1 = force nearest */
    int   brightness;        /* initial in-game brightness 0..100 (N64 default 0) */
    int   mouse;             /* mouse turning */
    float mouse_sens;
    int   gpu_index;         /* preferred Vulkan device index, -1 = auto */
    int   validation;        /* Vulkan validation layers */
    int   log;               /* write doom64rtx.log (default on) */
    int   sound;             /* 1 = sound and music (WESS synth) */
    int   music_volume, sfx_volume; /* 0..100, applied by audio settings */
    int   fps;               /* frame rate cap: 30 (N64), 60 or 120 (interpolated) */
    int   mouselook;         /* free look with the mouse (pitch) */
    int   invert_mouse;      /* invert vertical mouse look */
    int   always_run;
    int   autoaim;           /* vertical autoaim (classic Doom aim assist) */
    int   crosshair;
    int   jump;              /* allow jumping */
    int   weapon_bob;        /* 0..100 % of the N64 view/weapon bob */
    int   ads;               /* right mouse aims down the sights (zoom) */
    int   fast_weapons;      /* faster switching and shorter attack states */
    int   gore;              /* extra blood */
    int   autosave;          /* save to the Auto slot at each level start */
    int   respacks;          /* use PNG resource packs (packs/ folders, packs =) */
    char  packs[1024];       /* extra packs, ';' separated (also -pack <file>) */
    int   show_stats;        /* F7 debug overlay: fps / position */
    int   audio_rate;        /* synth output rate in Hz (N64: 22050) */
} pcconfig_t;

extern pcconfig_t pc_config;   /* effective settings (file + command line) */
extern pcconfig_t pc_config_file;  /* what doom64rtx.ini holds; Config_Save writes this */

/* Change a setting at runtime and make it persistent. Command-line
 * overrides are never written back to the ini. */
#define CONFIG_SET(field, value) \
    do { pc_config.field = (value); pc_config_file.field = pc_config.field; } while (0)

void Config_Defaults(void);
void Config_Load(void);
void Config_Save(void);
void Config_Snapshot(void);
void Config_ParseArgs(int argc, char **argv);
const char *Config_Path(void);

#endif
