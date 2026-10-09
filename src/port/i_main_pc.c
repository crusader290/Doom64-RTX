/*
 * i_main_pc.c - PC replacement for the N64 i_main.c: program entry, SDL3
 * window, renderer bring-up (Vulkan with OpenGL fallback), frame pacing,
 * display list submission, and the screen wipes.
 *
 * The original ran the game on its own thread and kicked the RSP from a
 * vblank driven scheduler thread. Here the game runs on the main thread;
 * I_DrawFrame interprets the finished display list, presents it, then
 * waits for the next 30 Hz slot like the N64 did (two 60 Hz vblanks).
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "doomdef.h"
#include "st_main.h"
#include "config.h"
#include "rom.h"
#include "input.h"
#include "gbi.h"
#include "d64gfx.h"
#include "rtlights.h"
#include "r_local.h"
#include "log.h"
#include "interp.h"
#include "respack.h"

/* ------------------------------------------------------------------ */
/* globals the game expects from i_main.c                             */
/* ------------------------------------------------------------------ */
extern u32 cfb[2][SCREEN_WD * SCREEN_HT];
extern int globallump;
extern int globalcm;

OSTask vid_rsptask[2];
OSTask *vid_task;
u32 vid_side;
u32 GfxIndex;
u32 VtxIndex;
u8 gamepad_bit_pattern = 1;
boolean disabledrawing = false;
s32 vsync = 0;
s32 drawsync2 = 0;
s32 drawsync1 = 0;
u32 NextFrameIdx = 0;
s32 gamepad_system_busy = 0;
u32 SystemTickerStatus = 0;

Gfx Gfx_base[2][MAX_GFX];
Mtx Mtx_base[2][MAX_MTX];
Vtx Vtx_base[2][MAX_VTX];
Gfx *GFX1, *GFX2;
Vtx *VTX1, *VTX2;
Mtx *MTX1, *MTX2;
Gfx *GfxBlocks[8];
Vtx *VtxBlocks[8];

Vp vid_viewport = { { { SCREEN_WD * 2, SCREEN_HT * 2, G_MAXZ, 0 },
                      { SCREEN_WD * 2, SCREEN_HT * 2, 0, 0 } } };

OSMesgQueue romcopy_msgque;
static OSMesg romcopy_msgbuf;

static SDL_Window *window;
static SDL_GLContext gl_context;
static int using_backend = -1;
static Uint64 time_base_ns;
static int last_pad;
static int window_focused = 1;

/* ------------------------------------------------------------------ */
/* errors / logging                                                   */
/* ------------------------------------------------------------------ */
void I_PCFatal(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FATAL: %s", buf);
    if (!SDL_getenv("D64_HEADLESS"))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Doom64-RTX", buf, window);
    Log_Shutdown();
    exit(1);
}

void printstr(u32 color, int x, int y, char *text) { (void)color; (void)x; (void)y; SDL_Log("%s", text); }
void PRINTF_D(u32 color_, char *c_, ...)
{
    char buf[512];
    va_list ap;
    (void)color_;
    va_start(ap, c_);
    SDL_vsnprintf(buf, sizeof(buf), c_, ap);
    va_end(ap);
    SDL_Log("%s", buf);
}
void PRINTF_D2(u32 color_, int x, int y, char *c_, ...)
{
    char buf[512];
    va_list ap;
    (void)color_; (void)x; (void)y;
    va_start(ap, c_);
    SDL_vsnprintf(buf, sizeof(buf), c_, ap);
    va_end(ap);
    SDL_Log("%s", buf);
}
void WAIT(void) {}

void I_Error(char *error, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, error);
    vsnprintf(buffer, sizeof(buffer), error, args);
    va_end(args);
    I_PCFatal("I_Error: %s", buffer);
}

/* ------------------------------------------------------------------ */
/* renderer glue                                                      */
/* ------------------------------------------------------------------ */
static void *cb_vk_gipa(void *user)
{
    (void)user;
    return (void *)SDL_Vulkan_GetVkGetInstanceProcAddr();
}

static const char *const *cb_vk_exts(void *user, uint32_t *count)
{
    (void)user;
    return SDL_Vulkan_GetInstanceExtensions(count);
}

static int cb_vk_surface(void *user, uint64_t instance, uint64_t *surface_out)
{
    VkSurfaceKHR surf = 0;
    (void)user;
    if (!SDL_Vulkan_CreateSurface(window, (VkInstance)(uintptr_t)instance, NULL, &surf))
    {
        SDL_Log("SDL_Vulkan_CreateSurface: %s", SDL_GetError());
        return 0;
    }
    *surface_out = (uint64_t)(uintptr_t)surf;
    return 1;
}

static void *cb_gl_proc(void *user, const char *name)
{
    (void)user;
    return (void *)SDL_GL_GetProcAddress(name);
}

static void cb_gl_swap(void *user)
{
    (void)user;
    SDL_GL_SwapWindow(window);
}

static void cb_gl_interval(void *user, int interval)
{
    (void)user;
    SDL_GL_SetSwapInterval(interval);
}

static void cb_drawable_size(void *user, int *w, int *h)
{
    (void)user;
    SDL_GetWindowSizeInPixels(window, w, h);
}

static void cb_log(void *user, int level, const char *msg)
{
    (void)user;
    if (level >= 2)
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", msg);
    else if (level == 1)
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", msg);
    else
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%s", msg);
}

static void destroy_window(void)
{
    if (gl_context)
    {
        SDL_GL_DestroyContext(gl_context);
        gl_context = NULL;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }
}

static int create_window(int backend)
{
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (pc_config.fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    if (backend == D64GFX_BACKEND_VULKAN)
        flags |= SDL_WINDOW_VULKAN;
    else
    {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    }
    window = SDL_CreateWindow("Doom 64 RTX", pc_config.width, pc_config.height, flags);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return 0;
    }
    if (backend == D64GFX_BACKEND_OPENGL)
    {
        gl_context = SDL_GL_CreateContext(window);
        if (!gl_context)
        {
            SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError());
            destroy_window();
            return 0;
        }
        SDL_GL_MakeCurrent(window, gl_context);
        SDL_GL_SetSwapInterval(pc_config.vsync ? 1 : 0);
    }
    return 1;
}

static int start_renderer(int backend, char *err, size_t errlen)
{
    D64GfxInitInfo info;
    if (!create_window(backend))
    {
        SDL_snprintf(err, errlen, "could not create a %s window: %s",
                     backend == D64GFX_BACKEND_VULKAN ? "Vulkan" : "OpenGL", SDL_GetError());
        return 0;
    }
    memset(&info, 0, sizeof(info));
    info.api_version = D64GFX_API_VERSION;
    info.backend = (D64GfxBackend)backend;
    info.window.vk_get_instance_proc_addr = cb_vk_gipa;
    info.window.vk_instance_extensions = cb_vk_exts;
    info.window.vk_create_surface = cb_vk_surface;
    info.window.gl_get_proc_address = cb_gl_proc;
    info.window.gl_swap_window = cb_gl_swap;
    info.window.gl_set_swap_interval = cb_gl_interval;
    info.window.drawable_size = cb_drawable_size;
    info.window.log = cb_log;
    info.vsync = pc_config.vsync;
    info.raytracing = pc_config.raytracing;
    info.gpu_index = pc_config.gpu_index;
    info.validation = pc_config.validation;
    info.game_width = SCREEN_WD;
    info.game_height = SCREEN_HT;
    if (!d64gfx_init(&info, err, (uint32_t)errlen))
    {
        destroy_window();
        return 0;
    }
    d64gfx_set_rt_params(pc_config.rt_spp, pc_config.rt_bounces, pc_config.rt_denoise,
                         pc_config.rt_light_scale, pc_config.rt_sun);
    using_backend = backend;
    return 1;
}

static void init_video(void)
{
    char err[1024];
    D64GfxStatus st;

    if (pc_config.renderer == RENDERER_VULKAN)
    {
        if (!start_renderer(D64GFX_BACKEND_VULKAN, err, sizeof(err)))
        {
            SDL_Log("Vulkan renderer unavailable (%s); falling back to OpenGL", err);
            if (!start_renderer(D64GFX_BACKEND_OPENGL, err, sizeof(err)))
                I_PCFatal("No usable renderer.\n\n%s", err);
        }
    }
    else if (!start_renderer(D64GFX_BACKEND_OPENGL, err, sizeof(err)))
    {
        SDL_Log("OpenGL renderer unavailable (%s); trying Vulkan", err);
        if (!start_renderer(D64GFX_BACKEND_VULKAN, err, sizeof(err)))
            I_PCFatal("No usable renderer.\n\n%s", err);
    }
    d64gfx_status(&st);
    SDL_Log("Renderer: %s on %s (ray tracing %s)", st.backend_name, st.device_name,
            st.rt_supported ? (st.rt_enabled ? "on" : "available, off") : "not supported");
}

/* ------------------------------------------------------------------ */
/* timing                                                             */
/* ------------------------------------------------------------------ */
static s32 vbl_now(void)
{
    Uint64 ns = SDL_GetTicksNS() - time_base_ns;
    return (s32)(ns * 60 / 1000000000ull);
}

/* ------------------------------------------------------------------ */
/* events                                                             */
/* ------------------------------------------------------------------ */
static void save_screenshot_named(const char *name)
{
    int w = 0, h = 0;
    SDL_Surface *s;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    /* save the game area (4:3 or 16:9) at the window's height */
    w = (int)(h * GBI_VirtualWidth() / 240.0f);
    s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!s)
        return;
    d64gfx_read_frame((uint8_t *)s->pixels, (uint32_t)w, (uint32_t)h);
    SDL_SaveBMP(s, name);
    SDL_DestroySurface(s);
    SDL_Log("Saved %s", name);
}

static void save_screenshot(void)
{
    char name[256];
    SDL_snprintf(name, sizeof(name), "doom64rtx_%llu.bmp", (unsigned long long)SDL_GetTicks());
    save_screenshot_named(name);
}

/* Automated testing: D64_SHOTS="frame,frame,..." saves shot_<frame>.bmp,
 * D64_QUIT_AT=<frame> exits, D64_PRESS="frame:button,..." taps pad buttons
 * (button = hex mask of the high 16 bits). */
static unsigned frame_no;
static unsigned subframe_no;
static int in_subframe;    /* drawing an interpolated in-between frame */

static int list_has(const char *list, unsigned v)
{
    while (list && *list)
    {
        if ((unsigned)SDL_strtoul(list, NULL, 10) == v)
            return 1;
        list = SDL_strchr(list, ',');
        if (list) list++;
    }
    return 0;
}

static unsigned test_env_mask(const char *env, int shift)
{
    const char *p = SDL_getenv(env);
    unsigned pad = 0;
    while (p && *p)
    {
        char *end;
        unsigned f = (unsigned)SDL_strtoul(p, &end, 10);
        unsigned len = 4;
        if (*end == ':')
        {
            unsigned mask = (unsigned)SDL_strtoul(end + 1, &end, 16);
            if (*end == '/')
                len = (unsigned)SDL_strtoul(end + 1, &end, 10);
            if (frame_no >= f && frame_no < f + len)
                pad |= mask << shift;
        }
        p = SDL_strchr(p, ',');
        if (p) p++;
    }
    return pad;
}

static int test_pad_buttons(void)
{
    return (int)test_env_mask("D64_PRESS", 16);
}

/* D64_PCACT="frame:mask[/len],..." holds PC actions (1 jump, 2 ADS);
 * D64_LOOK="frame:degrees,..." adds mouse-look pitch once at that frame. */
int I_PCTestActions(void)
{
    return (int)test_env_mask("D64_PCACT", 0);
}

int I_PCTestLook(void)
{
    const char *p = SDL_getenv("D64_LOOK");
    int bam = 0;
    static unsigned last_frame;
    if (frame_no == last_frame)
        return 0;
    last_frame = frame_no;
    while (p && *p)
    {
        char *end;
        unsigned f = (unsigned)SDL_strtoul(p, &end, 10);
        if (*end == ':' && frame_no == f)
            bam += (int)(SDL_strtod(end + 1, &end) * (double)0xb60b61);
        p = SDL_strchr(p, ',');
        if (p) p++;
    }
    return bam;
}

extern int rndindex, prndindex, gametic, ticon;
static void test_hooks(void)
{
    const char *q = SDL_getenv("D64_QUIT_AT");
    if (in_subframe)
    {
        /* in-between frames keep the main frame's number: shot_NNNN_k.bmp */
        subframe_no++;
        if (list_has(SDL_getenv("D64_SHOTS"), frame_no))
        {
            char name[64];
            SDL_snprintf(name, sizeof(name), "shot_%04u_%u.bmp", frame_no, subframe_no);
            save_screenshot_named(name);
        }
        return;
    }
    subframe_no = 0;
    frame_no++;
    if (SDL_getenv("D64_TRACE"))
    {
        mobj_t *mo = players[0].mo;
        printf("TRACE %u gt=%d ticon=%d r=%d p=%d mo=%d,%d,%d cam=%d,%d\n", frame_no, gametic, ticon,
               rndindex, prndindex, mo ? mo->x : 0, mo ? mo->y : 0, mo ? mo->z : 0,
               cameratarget ? cameratarget->x : 0, cameratarget ? cameratarget->y : 0);
        fflush(stdout);
    }
    if (list_has(SDL_getenv("D64_SHOTS"), frame_no))
    {
        char name[64];
        SDL_snprintf(name, sizeof(name), "shot_%04u.bmp", frame_no);
        save_screenshot_named(name);
        if (SDL_getenv("D64_SHOT_HOLD"))
        {
            SDL_Log("holding frame %u", frame_no);
            SDL_Delay((Uint32)SDL_atoi(SDL_getenv("D64_SHOT_HOLD")));
        }
    }
    if (SDL_getenv("D64_CRASH_AT") && frame_no == (unsigned)SDL_strtoul(SDL_getenv("D64_CRASH_AT"), NULL, 10))
    {
        volatile int *bad = NULL;
        SDL_Log("D64_CRASH_AT: crashing on purpose to test the crash handler");
        *bad = 1;
    }
    if (q && frame_no >= (unsigned)SDL_strtoul(q, NULL, 10))
    {
        const gbistats_t *st = GBI_Stats();
        SDL_Log("D64_QUIT_AT reached (last frame: %u cmds, %u verts, %u tex uploads)",
                st->cmds, st->vertices, st->tex_uploads);
        d64gfx_shutdown();
        destroy_window();
        Log_Shutdown();
        SDL_Quit();
        exit(0);
    }
}

void I_PCToggleRaytracing(void)
{
    D64GfxStatus st;
    d64gfx_status(&st);
    if (!st.rt_supported)
    {
        SDL_Log("Ray tracing is not supported by this renderer/GPU");
        return;
    }
    CONFIG_SET(raytracing, !st.rt_enabled);
    d64gfx_set_raytracing(pc_config.raytracing);
    Config_Save();
    SDL_Log("Ray tracing %s", pc_config.raytracing ? "enabled" : "disabled");
}

/* F7 stats overlay text (drawn by ST_Drawer). */
static double stats_fps;

int I_PCStatsLines(char lines[][64], int max)
{
    const gbistats_t *st = GBI_Stats();
    D64GfxStatus gs;
    player_t *p = &players[0];
    int n = 0;

    d64gfx_status(&gs);
    if (n < max)
        SDL_snprintf(lines[n++], 64, "%.0f FPS  %s%s", stats_fps, gs.backend_name, gs.rt_enabled ? " RT" : "");
    if (n < max)
        SDL_snprintf(lines[n++], 64, "MAP %02d  SKILL %d  TIC %d", gamemap, gameskill + 1, gametic);
    if (p->mo && n < max)
        SDL_snprintf(lines[n++], 64, "X %d Y %d Z %d", p->mo->x >> FRACBITS, p->mo->y >> FRACBITS,
                     p->mo->z >> FRACBITS);
    if (p->mo && n < max)
        SDL_snprintf(lines[n++], 64, "ANGLE %d PITCH %d", (int)((double)p->mo->angle * 360.0 / 4294967296.0),
                     (int)((double)(int)p->pc_pitch * 360.0 / 4294967296.0));
    if (p->mo && n < max)
        SDL_snprintf(lines[n++], 64, "SECTOR %d  LIGHT %d", (int)(p->mo->subsector->sector - sectors),
                     p->mo->subsector->sector->lightlevel);
    if (n < max)
        SDL_snprintf(lines[n++], 64, "DRAWS %u  VERTS %u  TEX %u", st->cmds, st->vertices, st->tex_uploads);
    if (n < max)
        SDL_snprintf(lines[n++], 64, "MOBJS %d  KILLS %d/%d", nummobjs_pc(), p->killcount, totalkills);
    return n;
}

void I_PCSetFullscreen(int on)
{
    CONFIG_SET(fullscreen, on ? 1 : 0);
    SDL_SetWindowFullscreen(window, pc_config.fullscreen ? true : false);
    Config_Save();
}

static int debug_request;
static int quick_request; /* 1 = quicksave, 2 = quickload */

int I_PCTakeQuickRequest(void)
{
    int r = quick_request;
    quick_request = 0;
    return r;
}

/* F7: the game polls this once per tic and opens the debug page. */
int I_PCTakeDebugRequest(void)
{
    int r = debug_request;
    debug_request = 0;
    return r;
}

static void pump_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
        case SDL_EVENT_QUIT:
            SDL_Log("quit requested");
            Config_Save();
            d64gfx_shutdown();
            destroy_window();
            Log_Shutdown();
            SDL_Quit();
            exit(0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            window_focused = 1;
            IN_SetGrab(window, 1);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            window_focused = 0;
            IN_SetGrab(window, 0);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.repeat)
                break;
            if (ev.key.scancode == SDL_SCANCODE_F10)
                I_PCToggleRaytracing();
            else if (ev.key.scancode == SDL_SCANCODE_F7)
                debug_request = 1;
            else if (ev.key.scancode == SDL_SCANCODE_F5)
                quick_request = 1;
            else if (ev.key.scancode == SDL_SCANCODE_F9)
                quick_request = 2;
            else if (ev.key.scancode == SDL_SCANCODE_F11 ||
                     (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_ALT)))
                I_PCSetFullscreen(!pc_config.fullscreen);
            else if (ev.key.scancode == SDL_SCANCODE_F12)
                save_screenshot();
            break;
        default:
            break;
        }
        IN_HandleEvent(&ev);
    }
}

/* ------------------------------------------------------------------ */
/* widescreen (Options > Display > Aspect Ratio)                      */
/* ------------------------------------------------------------------ */
static void apply_aspect(void)
{
    float vwidth;
    GBI_SetAspect(pc_config.aspect ? 16.0f / 9.0f : 4.0f / 3.0f);
    vwidth = GBI_VirtualWidth();
    R_PCFovInvScale = R_PCFovInvScaleBase = (int)(65536.0f * 320.0f / vwidth);
}

int I_PCGetWidescreen(void)
{
    return pc_config.aspect != 0;
}

void I_PCSetWidescreen(int on)
{
    CONFIG_SET(aspect, on ? 1 : 0);
    apply_aspect();
    Config_Save();
    SDL_Log("aspect ratio set to %s", on ? "16:9" : "4:3");
}

/* ------------------------------------------------------------------ */
/* game facing I_ functions                                           */
/* ------------------------------------------------------------------ */
void I_Init(void)
{
    osCreateMesgQueue(&romcopy_msgque, &romcopy_msgbuf, 1);
    vid_side = 1;
    IN_Init();
    IN_SetGrab(window, 1);
    S_Init();
    {
        extern int brightness;
        brightness = pc_config.brightness < 0 ? 0 : pc_config.brightness > 100 ? 100 : pc_config.brightness;
    }
    time_base_ns = SDL_GetTicksNS();
    vsync = vbl_now();
}

int I_GetControllerData(void)
{
    pump_events();
    if (window_focused)
        last_pad = IN_ReadPad();
    else
        last_pad = 0;
    last_pad |= test_pad_buttons();
    return last_pad;
}

void I_CheckGFX(void)
{
    memblock_t *block;
    Gfx **Gfx_Blocks;
    Vtx **Vtx_Blocks;
    int i, index, block_idx;

    index = (int)(GFX1 - GFX2); /* [PC] pointer difference */
    if (index > MAX_GFX)
        I_Error("I_CheckGFX: GFX Overflow by %d\n", index);

    if ((index < (MAX_GFX - 1024)) == 0)
    {
        Gfx_Blocks = GfxBlocks;
        block_idx = -1;
        for (i = 0; i < 8; i++)
        {
            block = (memblock_t *)((byte *)*Gfx_Blocks - sizeof(memblock_t));
            if (*Gfx_Blocks)
            {
                if (((u32)block->lockframe < NextFrameIdx - 1) == 0)
                {
                    Gfx_Blocks++;
                    continue;
                }
                block->lockframe = NextFrameIdx;
                GFX2 = (Gfx *)*Gfx_Blocks;
                goto move_gfx;
            }
            block_idx = i;
            Gfx_Blocks++;
        }
        if (block_idx < 0)
            I_Error("I_CheckGFX: GFX Cache overflow");
        GFX2 = (Gfx *)Z_Malloc(MAX_GFX * sizeof(Gfx), PU_CACHE, (void **)&GfxBlocks[block_idx]);
    move_gfx:
        gSPBranchList(GFX1, GFX2);
        GFX1 = GFX2;
        GfxIndex += index;
    }

    index = (int)(VTX1 - VTX2);
    if (index > MAX_VTX)
        I_Error("I_CheckVTX: VTX Overflow by %d\n", index);

    if ((index < (MAX_VTX - 615)) == 0)
    {
        Vtx_Blocks = VtxBlocks;
        block_idx = -1;
        for (i = 0; i < 8; i++)
        {
            block = (memblock_t *)((byte *)*Vtx_Blocks - sizeof(memblock_t));
            if (*Vtx_Blocks)
            {
                if (((u32)block->lockframe < NextFrameIdx - 1) == 0)
                {
                    Vtx_Blocks++;
                    continue;
                }
                block->lockframe = NextFrameIdx;
                VTX2 = (Vtx *)*Vtx_Blocks;
                goto move_vtx;
            }
            block_idx = i;
            Vtx_Blocks++;
        }
        if (block_idx < 0)
            I_Error("I_CheckGFX: VTX Cache overflow");
        VTX2 = (Vtx *)Z_Malloc(MAX_VTX * sizeof(Vtx), PU_CACHE, (void **)&VtxBlocks[block_idx]);
    move_vtx:
        VTX1 = VTX2;
        VtxIndex += index;
    }
}

void I_ClearFrame(void)
{
    NextFrameIdx += 1;

    GFX1 = Gfx_base[vid_side];
    GFX2 = GFX1;
    GfxIndex = 0;

    VTX1 = Vtx_base[vid_side];
    VTX2 = VTX1;
    VtxIndex = 0;

    MTX1 = Mtx_base[vid_side];

    vid_task = &vid_rsptask[vid_side];
    vid_task->t.ucode = (u64 *)gspF3DEX_NoN_fifoTextStart;
    vid_task->t.ucode_data = (u64 *)gspF3DEX_NoN_fifoDataStart;

    gMoveWd(GFX1++, G_MW_SEGMENT, G_MWO_SEGMENT_0, 0);
    gDPSetColorImage(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, SCREEN_WD, OS_K0_TO_PHYSICAL(cfb[vid_side]));
    gDPSetScissor(GFX1++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WD, SCREEN_HT);

    if (players[0].cheats & CF_FILTER)
        gDPSetTextureFilter(GFX1++, G_TF_POINT);
    else
        gDPSetTextureFilter(GFX1++, G_TF_BILERP);

    gSPViewport(GFX1++, &vid_viewport);
    gSPClearGeometryMode(GFX1++, -1);
    gSPSetGeometryMode(GFX1++, G_SHADE | G_SHADING_SMOOTH | G_FOG);

    globallump = -1;
    globalcm = 0;
}

static void submit_frame(void)
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    GBI_SetOutputSize(w, h);
    GBI_RunFrame(Gfx_base[vid_side]);
    GBI_SetLights(NULL, 0); /* collected again by the next R_RenderPlayerView */
}

/* ------------------------------------------------------------------ */
/* 60/120 fps: extra interpolated frames between 30 Hz game tics      */
/* ------------------------------------------------------------------ */

int I_PCInSubframe(void)
{
    return in_subframe;
}

static void draw_subframe(double frac)
{
    extern int gamevbls;
    int saved_vbls = gamevbls, saved_vblsin = vblsinframe[0];

    /* the drawer must not advance tic-based animations (fire sky, clouds,
     * lightning) a second time */
    gamevbls = gametic;
    vblsinframe[0] = 0;
    in_subframe = 1;
    I_PCInterpBegin(frac);
    I_PCCurrentDrawer();
    I_PCInterpEnd();
    in_subframe = 0;
    gamevbls = saved_vbls;
    vblsinframe[0] = saved_vblsin;
}

static void draw_subframes(void)
{
    Uint64 slot_end, period, next;
    int n, k;

    if (!I_PCInterpEnabled() || I_PCCurrentDrawer != P_Drawer || demoplayback)
        return;
    I_PCInterpEnd();

    n = pc_config.fps / 30;
    if (n < 2)
        return;
    if (SDL_getenv("D64_FIXED_TIMESTEP"))
    {
        for (k = 1; k < n; k++)
            draw_subframe((double)k / (double)n);
        return;
    }

    /* the next tic starts when two N64 vblanks have passed */
    slot_end = time_base_ns + (Uint64)(drawsync2 + 2) * 1000000000ull / 60ull;
    period = 1000000000ull / (Uint64)pc_config.fps;
    next = SDL_GetTicksNS() + period;
    while (next + period / 4 < slot_end)
    {
        Uint64 now = SDL_GetTicksNS();
        if (now < next)
            SDL_DelayPrecise(next - now);
        pump_events();
        draw_subframe(I_PCInterpTicFraction());
        now = SDL_GetTicksNS();
        next += period;
        if (next < now)
            next = now + period / 2; /* rendering is slower than the target: do not spiral */
    }
}

void I_DrawFrame(void)
{
    int index;

    gDPFullSync(GFX1++);
    gSPEndDisplayList(GFX1++);

    index = (int)(GFX1 - GFX2);
    if (index > MAX_GFX)
        I_Error("I_DrawFrame: GFX Overflow by %d\n\n", index);
    index = (int)(VTX1 - VTX2);
    if (index > MAX_VTX)
        I_Error("I_DrawFrame: VTX Overflow by %d\n", index);

    submit_frame();
    if (!in_subframe)
        I_PCInterpEnd(); /* back to the real game state */
    {
        static Uint64 last_stats;
        static unsigned frames_since;
        const gbistats_t *st = GBI_Stats();
        Uint64 now = SDL_GetTicks();
        frames_since++;
        Log_SetCrashContext("map %d, gametic %d, frame cmds %u verts %u, demo %d", gamemap, gametic,
                            st->cmds, st->vertices, demoplayback);
        if (now - last_stats >= 60000)
        {
            if (last_stats)
                SDL_Log("stats: %.1f fps, map %d, %u draw cmds, %u verts, %u texture uploads",
                        frames_since * 1000.0 / (double)(now - last_stats), gamemap, st->cmds,
                        st->vertices, st->tex_uploads);
            last_stats = now;
            frames_since = 0;
        }
    }
    {
        static Uint64 last_ns;
        Uint64 now_ns = SDL_GetTicksNS();
        if (last_ns && now_ns > last_ns)
            stats_fps = stats_fps * 0.9 + 0.1 * (1e9 / (double)(now_ns - last_ns));
        last_ns = now_ns;
    }
    test_hooks();
    pump_events();
    I_PCAudioUpdate();

    if (in_subframe)
        return; /* nested draw for an interpolated frame: no pacing */
    draw_subframes();

    /* wait for the next 30 Hz slot (two N64 vblanks) */
    for (; !SDL_getenv("D64_FIXED_TIMESTEP");)
    {
        s32 now = vbl_now();
        if (now - drawsync2 >= 2)
            break;
        SDL_DelayPrecise(1000000);
    }
    vsync = vbl_now();
    if (demoplayback || demorecording || SDL_getenv("D64_FIXED_TIMESTEP"))
        vsync = drawsync2 + 2; /* deterministic: exactly one 30 Hz frame */
    drawsync1 = vsync - drawsync2;
    if (drawsync1 > 8)
        drawsync1 = 2; /* do not try to catch up after a stall */
    drawsync2 = vsync;

    vid_side ^= 1;
}

/* Copy the last presented frame into cfb[vid_side ^ 1] (RGBA bytes in
 * N64 order) for the wipe effects that read it back as a texture. */
void I_GetScreenGrab(void)
{
    d64gfx_read_frame((uint8_t *)cfb[vid_side ^ 1], SCREEN_WD, SCREEN_HT);
}

long LongSwap(long dat) { return dat; }          /* [PC] WAD data is little-endian like the host */
short LittleShort(short dat) { return dat; }
short BigShort(short dat) { return (short)(unsigned short)dat; }

void I_MoveDisplay(int x, int y) { (void)x; (void)y; }

void I_WIPE_MeltScreen(void)
{
    u32 *fb;
    int y1, tpos, yscroll, height;

    fb = Z_Malloc((SCREEN_WD * SCREEN_HT) * sizeof(u32), PU_STATIC, NULL);

    I_GetScreenGrab();
    D_memcpy(&cfb[vid_side][0], &cfb[vid_side ^ 1][0], (SCREEN_WD * SCREEN_HT) * sizeof(u32));

    yscroll = 1;
    while (true)
    {
        y1 = 0;
        D_memcpy(fb, &cfb[vid_side ^ 1][0], (SCREEN_WD * SCREEN_HT) * sizeof(u32));

        I_ClearFrame();

        gDPSetCycleType(GFX1++, G_CYC_1CYCLE);
        gDPSetTextureLUT(GFX1++, G_TT_NONE);
        gDPSetTexturePersp(GFX1++, G_TP_NONE);
        gDPSetAlphaCompare(GFX1++, G_AC_THRESHOLD);
        gDPSetBlendColor(GFX1++, 0, 0, 0, 0);
        gDPSetCombineMode(GFX1++, G_CC_D64COMB19, G_CC_D64COMB19);
        gDPSetRenderMode(GFX1++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetPrimColor(GFX1++, 0, 0, 15, 0, 0, 22);

        height = SCREEN_HT - (yscroll >> 2);
        tpos = 0;
        if (height > 0)
        {
            do
            {
                gDPSetTextureImage(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, SCREEN_WD, fb);
                gDPSetTile(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, (SCREEN_WD >> 2), 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
                gDPLoadSync(GFX1++);
                gDPLoadTile(GFX1++, G_TX_LOADTILE, (0 << 2), (tpos << 2), ((SCREEN_WD - 1) << 2), (((tpos + 3) - 1) << 2));
                gDPPipeSync(GFX1++);
                gDPSetTile(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, (SCREEN_WD >> 2), 0, G_TX_RENDERTILE, 0, 0, 0, 0, 0, 0, 0);
                gDPSetTileSize(GFX1++, G_TX_RENDERTILE, (0 << 2), (tpos << 2), ((SCREEN_WD - 1) << 2), (((tpos + 3) - 1) << 2));
                gSPTextureRectangle(GFX1++, (0 << 2), (y1 << 2) + yscroll, (SCREEN_WD << 2), ((y1 + 3) << 2) + yscroll,
                                    G_TX_RENDERTILE, (0 << 5), (tpos << 5), (1 << 10), (1 << 10));
                y1 += 2;
                tpos += 2;
            } while (y1 < height);
        }

        yscroll += 2;
        if (yscroll >= 160)
            break;
        I_DrawFrame();
        I_GetScreenGrab();
    }

    Z_Free(fb);
    I_WIPE_FadeOutScreen();
}

void I_WIPE_FadeOutScreen(void)
{
    u32 *fb;
    int y1, tpos, outcnt;

    fb = Z_Malloc((SCREEN_WD * SCREEN_HT) * sizeof(u32), PU_STATIC, NULL);

    I_GetScreenGrab();
    D_memcpy(fb, &cfb[vid_side ^ 1][0], (SCREEN_WD * SCREEN_HT) * sizeof(u32));

    outcnt = 248;
    do
    {
        I_ClearFrame();

        gDPSetCycleType(GFX1++, G_CYC_1CYCLE);
        gDPSetTextureLUT(GFX1++, G_TT_NONE);
        gDPSetTexturePersp(GFX1++, G_TP_NONE);
        gDPSetAlphaCompare(GFX1++, G_AC_NONE);
        gDPSetCombineMode(GFX1++, G_CC_D64COMB06, G_CC_D64COMB06);
        gDPSetRenderMode(GFX1++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
        gDPSetPrimColor(GFX1++, 0, 0, outcnt, outcnt, outcnt, 0);

        tpos = 0;
        y1 = 0;
        do
        {
            gDPSetTextureImage(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, SCREEN_WD, fb);
            gDPSetTile(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, (SCREEN_WD >> 2), 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
            gDPLoadSync(GFX1++);
            gDPLoadTile(GFX1++, G_TX_LOADTILE, (0 << 2), (tpos << 2), ((SCREEN_WD - 1) << 2), (((tpos + 3) - 1) << 2));
            gDPPipeSync(GFX1++);
            gDPSetTile(GFX1++, G_IM_FMT_RGBA, G_IM_SIZ_32b, (SCREEN_WD >> 2), 0, G_TX_RENDERTILE, 0, 0, 0, 0, 0, 0, 0);
            gDPSetTileSize(GFX1++, G_TX_RENDERTILE, (0 << 2), (tpos << 2), ((SCREEN_WD - 1) << 2), (((tpos + 3) - 1) << 2));
            gSPTextureRectangle(GFX1++, (0 << 2), (y1 << 2), (SCREEN_WD << 2), ((y1 + 3) << 2),
                                G_TX_RENDERTILE, (0 << 5), (tpos << 5), (1 << 10), (1 << 10));
            tpos += 3;
            y1 += 3;
        } while (y1 != SCREEN_HT);

        I_DrawFrame();
        outcnt -= 8;
    } while (outcnt >= 0);

    I_GetScreenGrab();
    Z_Free(fb);
}

/* ------------------------------------------------------------------ */
/* entry point                                                        */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    char err[1024];

    SDL_SetAppMetadata("Doom 64 RTX", "0.1", "io.github.doom64rtx");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Config_Defaults();
    Config_Load();
    Config_Snapshot();
    Config_ParseArgs(argc, argv);
    Log_Init(argc, argv);
    apply_aspect();

    if (!ROM_Init(pc_config.rom, err, sizeof(err)))
        I_PCFatal("%s", err);
    SDL_Log("Game data: %s", ROM_Description());
    ResPack_Init();

    init_video();
    D_DoomMain();
    return 0;
}
