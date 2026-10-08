/*
 * d64gfx.h - C ABI of the renderer (implemented in Rust: renderer/).
 *
 * The C side (src/gfx/gbi.c) interprets the game's N64 display lists and
 * hands the renderer one fully resolved frame: a vertex array, a list of
 * draw commands with the RDP state they need, and (for the ray traced path)
 * the camera and light sources.
 *
 * Keep this file in sync with renderer/src/ffi.rs.
 * Doom64-RTX PC port, GPLv3.
 */
#ifndef D64GFX_H
#define D64GFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D64GFX_API_VERSION 2

typedef enum {
    D64GFX_BACKEND_VULKAN = 0,
    D64GFX_BACKEND_OPENGL = 1
} D64GfxBackend;

/* Window system glue provided by the C side (SDL3). */
typedef struct {
    void *user;
    /* Vulkan */
    void *(*vk_get_instance_proc_addr)(void *user);           /* returns PFN_vkGetInstanceProcAddr */
    const char *const *(*vk_instance_extensions)(void *user, uint32_t *count);
    int (*vk_create_surface)(void *user, uint64_t instance, uint64_t *surface_out); /* VkInstance is a pointer handle; passed as integer */
    /* OpenGL */
    void *(*gl_get_proc_address)(void *user, const char *name);
    void (*gl_swap_window)(void *user);
    void (*gl_set_swap_interval)(void *user, int interval);
    /* both */
    void (*drawable_size)(void *user, int *w, int *h);
    void (*log)(void *user, int level, const char *msg);     /* level: 0 info, 1 warn, 2 error */
} D64GfxWindow;

typedef struct {
    uint32_t     api_version;     /* D64GFX_API_VERSION */
    D64GfxBackend backend;
    D64GfxWindow window;
    int32_t      vsync;
    int32_t      raytracing;      /* request the RT path (Vulkan only) */
    int32_t      gpu_index;       /* -1 = auto */
    int32_t      validation;
    uint32_t     game_width;      /* 320 */
    uint32_t     game_height;     /* 240 */
} D64GfxInitInfo;

/* Vertex, already transformed to clip space by the interpreter. */
typedef struct {
    float   pos[4];     /* clip space, OpenGL conventions (y up, z -w..w) */
    float   uv[2];      /* texture coordinates, normalised to the bound texture */
    uint8_t shade[4];   /* RGBA; alpha holds the fog factor when the draw fogs */
    float   world[3];   /* world space position (D64GFX_CMD_WORLD draws only) */
    float   uv1[2];     /* coordinates for the second texture (2-cycle TEXEL1) */
} D64GfxVertex;

/* Draw command flags */
#define D64GFX_CMD_BLEND        (1u << 0)   /* src alpha / one minus src alpha */
#define D64GFX_CMD_FOG          (1u << 1)   /* blender cycle 1: mix(fog, combined, shade.a) */
#define D64GFX_CMD_ALPHA_CVG    (1u << 2)   /* coverage from alpha (TEX_EDGE): discard a < 0.5 */
#define D64GFX_CMD_ALPHA_THRESH (1u << 3)   /* discard a < blend.a (min 1/255) */
#define D64GFX_CMD_TWO_CYCLE    (1u << 4)
#define D64GFX_CMD_FILTER       (1u << 5)   /* bilinear (else point) */
#define D64GFX_CMD_LINES        (1u << 6)   /* reserved; lines are expanded to quads */
#define D64GFX_CMD_WORLD        (1u << 7)   /* 3D world geometry (eligible for RT) */
#define D64GFX_CMD_DEPTH_TEST   (1u << 8)
#define D64GFX_CMD_DEPTH_WRITE  (1u << 9)
#define D64GFX_CMD_SKY          (1u << 10)  /* sky backdrop */
#define D64GFX_CMD_TEX1_FILTER  (1u << 11)

/* wrap modes per axis: bit0 = s, bit1 = t */
#define D64GFX_WRAP_REPEAT 0
#define D64GFX_WRAP_MIRROR 1
#define D64GFX_WRAP_CLAMP  2

/* Combiner mux indices in cc[]:
 *   [0..3]  cycle 0 RGB   a b c d   ((a-b)*c+d)
 *   [4..7]  cycle 0 alpha a b c d
 *   [8..11] cycle 1 RGB
 *   [12..15] cycle 1 alpha
 * Values are normalised (see D64GFX_CC_*) so the shader does not need to
 * know the per-slot hardware encodings. */
enum {
    D64GFX_CC_COMBINED = 0,
    D64GFX_CC_TEXEL0,
    D64GFX_CC_TEXEL1,
    D64GFX_CC_PRIM,
    D64GFX_CC_SHADE,
    D64GFX_CC_ENV,
    D64GFX_CC_ONE,
    D64GFX_CC_ZERO,
    D64GFX_CC_COMBINED_A,
    D64GFX_CC_TEXEL0_A,
    D64GFX_CC_TEXEL1_A,
    D64GFX_CC_PRIM_A,
    D64GFX_CC_SHADE_A,
    D64GFX_CC_ENV_A,
    D64GFX_CC_LOD_FRAC,
    D64GFX_CC_PRIM_LOD_FRAC,
    D64GFX_CC_NOISE,
    D64GFX_CC_COUNT
};

typedef struct {
    uint32_t first_vertex;
    uint32_t vertex_count;     /* multiple of 3 (triangle list) */
    uint32_t tex[2];           /* texture ids, 0 = none */
    uint8_t  cc[16];
    uint32_t flags;
    uint8_t  wrap[2];          /* per texture: bit0..1 s mode, bit2..3 t mode */
    uint8_t  pad[2];
    uint8_t  prim[4];
    uint8_t  env[4];
    uint8_t  fog[4];
    uint8_t  blend[4];
    float    prim_lod_frac;
    int16_t  scissor[4];       /* x0,y0,x1,y1 in game pixels (320x240 space) */
} D64GfxDrawCmd;

/* Point light extracted from game state for the RT path. */
typedef struct {
    float pos[3];
    float radius;     /* influence radius, world units */
    float color[3];   /* linear RGB, 0..1 */
    float intensity;
} D64GfxLight;

typedef struct {
    const D64GfxVertex  *vertices;
    uint32_t             vertex_count;
    const D64GfxDrawCmd *cmds;
    uint32_t             cmd_count;

    /* Camera of the 3D world pass (valid if has_camera). Column-major. */
    int32_t  has_camera;
    float    view[16];          /* world -> eye */
    float    proj[16];          /* eye -> clip (OpenGL conventions) */
    float    cam_pos[3];
    uint32_t world_first_cmd;   /* index of the first WORLD command */

    const D64GfxLight *lights;
    uint32_t           light_count;
    float    fog_color[4];      /* world fog (sector fog colour) */
    float    fog_near;          /* 0..1000 per mille like FogNear */
    float    ambient_scale;
    uint32_t frame_index;
    float    clear_color[4];
    float    virtual_width;     /* game area width in game units (320 = 4:3, 426.7 = 16:9);
                                   scissors are in these units, height is always 240 */
} D64GfxFrame;

typedef struct {
    int32_t rt_supported;
    int32_t rt_enabled;
    char    device_name[256];
    char    backend_name[64];
} D64GfxStatus;

int32_t  d64gfx_init(const D64GfxInitInfo *info, char *err, uint32_t errlen);
void     d64gfx_shutdown(void);
uint32_t d64gfx_texture_create(uint32_t w, uint32_t h, const uint8_t *rgba8);
void     d64gfx_texture_destroy(uint32_t id);
void     d64gfx_render_frame(const D64GfxFrame *frame);
void     d64gfx_set_raytracing(int32_t enabled);
void     d64gfx_set_vsync(int32_t enabled);
void     d64gfx_status(D64GfxStatus *out);
/* Copy the last presented frame, scaled to w*h, as RGBA8 bytes (R first). */
void     d64gfx_read_frame(uint8_t *dst, uint32_t w, uint32_t h);
/* RT tuning knobs, see config.h */
void     d64gfx_set_rt_params(int32_t spp, int32_t bounces, int32_t denoise, float light_scale, int32_t sun);

#ifdef __cplusplus
}
#endif

#endif
