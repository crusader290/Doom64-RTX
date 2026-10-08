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
    int   aspect;            /* 0 = 4:3 pillarbox, 1 = stretch */
    int   filter;            /* 0 = follow game (N64 bilinear), 1 = force nearest */
    int   brightness;        /* initial in-game brightness 0..100 (N64 default 0) */
    int   mouse;             /* mouse turning */
    float mouse_sens;
    int   gpu_index;         /* preferred Vulkan device index, -1 = auto */
    int   validation;        /* Vulkan validation layers */
} pcconfig_t;

extern pcconfig_t pc_config;

void Config_Defaults(void);
void Config_Load(void);
void Config_Save(void);
void Config_ParseArgs(int argc, char **argv);
const char *Config_Path(void);

#endif
