/*
 * gfx_null.c - renderer that draws nothing; used for headless testing of
 * the game and the display list interpreter (-DD64_NULL_RENDERER=ON).
 * Doom64-RTX PC port, GPLv3.
 */
#include <stdio.h>
#include <string.h>
#include "d64gfx.h"

static uint32_t next_id = 1;
static uint32_t frames;

int32_t d64gfx_init(const D64GfxInitInfo *info, char *err, uint32_t errlen)
{
    (void)info; (void)err; (void)errlen;
    return 1;
}
void d64gfx_shutdown(void) {}
uint32_t d64gfx_texture_create(uint32_t w, uint32_t h, const uint8_t *rgba8)
{
    (void)w; (void)h; (void)rgba8;
    return next_id++;
}
void d64gfx_texture_destroy(uint32_t id) { (void)id; }
void d64gfx_render_frame(const D64GfxFrame *frame)
{
    if ((frames++ % 30) == 0)
        printf("[null gfx] frame %u: %u cmds, %u verts, camera %d\n", frames, frame->cmd_count,
               frame->vertex_count, frame->has_camera);
}
void d64gfx_set_raytracing(int32_t enabled) { (void)enabled; }
void d64gfx_set_vsync(int32_t enabled) { (void)enabled; }
void d64gfx_status(D64GfxStatus *out)
{
    memset(out, 0, sizeof(*out));
    strcpy(out->backend_name, "null");
    strcpy(out->device_name, "none");
}
void d64gfx_read_frame(uint8_t *dst, uint32_t w, uint32_t h) { memset(dst, 0, (size_t)w * h * 4); }
void d64gfx_set_rt_params(int32_t spp, int32_t bounces, int32_t denoise, float light_scale, int32_t sun)
{
    (void)spp; (void)bounces; (void)denoise; (void)light_scale; (void)sun;
}
