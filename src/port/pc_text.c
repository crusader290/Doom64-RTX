/* High-resolution TrueType atlas, drawn in display-list order on every backend.
 * Share Tech Mono by Carrois (SIL OFL); stb_truetype by Sean Barrett et al (MIT/PD). */
#include <SDL3/SDL.h>
#include "doomdef.h"
#include "r_local.h"
#include "gbi.h"
#include "pc_text.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

typedef struct { float x, y, size, w, h,z,rx,ry; unsigned color, texture; int world;char text[192]; } textcmd_t;
static textcmd_t commands[256];
static unsigned command_count, atlas, solid;
static int attempted;
static stbtt_bakedchar glyphs[96];

void PCText_BeginFrame(void) { command_count = 0; }
int PCText_Available(void)
{
    unsigned char *font, *mask, *rgba;
    size_t len;
    char path[1024];
    int i;
    if (attempted) return atlas != 0;
    attempted = 1;
    SDL_snprintf(path, sizeof(path), "%sassets/fonts/ShareTechMono-Regular.ttf", SDL_GetBasePath());
    font = SDL_LoadFile(path, &len);
    if (!font) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "retro font unavailable: %s", path); return 0; }
    mask = SDL_calloc(512 * 512, 1); rgba = SDL_malloc(512 * 512 * 4);
    if (!mask || !rgba || stbtt_BakeFontBitmap(font, 0, 64, mask, 512, 512, 32, 96, glyphs) <= 0) {
        SDL_free(font); SDL_free(mask); SDL_free(rgba); return 0;
    }
    for (i = 0; i < 512 * 512; i++) {
        rgba[4*i] = rgba[4*i+1] = rgba[4*i+2] = 255; rgba[4*i+3] = mask[i];
    }
    atlas = d64gfx_texture_create(512, 512, rgba);
    { const unsigned char pixel[4] = {255,255,255,255}; solid = d64gfx_texture_create(1, 1, pixel); }
    SDL_free(font); SDL_free(mask); SDL_free(rgba);
    SDL_Log("retro font: Share Tech Mono, 64px glyph atlas, texture %u", atlas);
    return atlas != 0;
}
float PCText_Width(float size, const char *text)
{
    float width = 0, line = 0;
    if (!PCText_Available()) return (float)strlen(text) * size * .5f;
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        if (c == '\n') { if (line > width) width = line; line = 0; }
        else if (c >= 32 && c < 128) line += glyphs[c-32].xadvance * size / 64;
    }
    return line > width ? line : width;
}
void PCText_Draw(float x, float y, float size, const char *text, unsigned color)
{
    textcmd_t *cmd;
    if (!PCText_Available()) { ST_DrawString((int)x, (int)y, (char *)text, (int)color); return; }
    if (command_count >= 256) return;
    cmd = &commands[command_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->x = x < 0 ? (320 - PCText_Width(size, text)) * .5f : x;
    cmd->y = y; cmd->size = size; cmd->color = color;
    SDL_strlcpy(cmd->text, text, sizeof(cmd->text));
    I_CheckGFX();
    _gW(GFX1++, _SHIFTL(G_PC_TEXT, 24, 8), (uintptr_t)cmd);
}
void PCText_Box(float x, float y, float w, float h, unsigned color)
{
    textcmd_t *cmd;
    if (!PCText_Available() || command_count >= 256) return;
    cmd = &commands[command_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->x=x; cmd->y=y; cmd->w=w; cmd->h=h; cmd->color=color;
    I_CheckGFX();
    _gW(GFX1++, _SHIFTL(G_PC_TEXT, 24, 8), (uintptr_t)cmd);
}
void PCText_Emit(const void *command)
{
    const textcmd_t *cmd = command;
    int pass;
    if(cmd->world==2) { GBI_SkyQuad(cmd->texture,cmd->z,cmd->rx,cmd->ry);return; }
    if(cmd->world) {
        float sx=cmd->rx*cmd->w*.5f,sy=cmd->ry*cmd->w*.5f;
        float vertices[4][3]={{cmd->x-sx,cmd->z+cmd->h*.5f,-(cmd->y-sy)},
            {cmd->x+sx,cmd->z+cmd->h*.5f,-(cmd->y+sy)},
            {cmd->x+sx,cmd->z-cmd->h*.5f,-(cmd->y+sy)},
            {cmd->x-sx,cmd->z-cmd->h*.5f,-(cmd->y-sy)}};
        GBI_WorldQuad(cmd->texture,vertices,cmd->color);return;
    }
    if (cmd->w > 0) {
        GBI_OverlayQuad(cmd->texture ? cmd->texture : solid, cmd->x, cmd->y, cmd->w, cmd->h, 0, 0, 1, 1, cmd->color);
        return;
    }
    for (pass = 0; pass < 2; pass++) {
        float scale = cmd->size / 64, x = cmd->x, y = cmd->y;
        const unsigned char *s = (const unsigned char *)cmd->text;
        for (; *s; s++) {
            stbtt_aligned_quad q;
            float gx = 0, gy = 0;
            if (*s == '\n') { y += cmd->size; x = cmd->x; continue; }
            if (*s < 32 || *s >= 128) { x += cmd->size * .5f; continue; }
            stbtt_GetBakedQuad(glyphs, 512, 512, *s - 32, &gx, &gy, &q, 1);
            GBI_OverlayQuad(atlas, x + q.x0 * scale + (pass ? 0 : .5f),
                y + cmd->size * .8f + q.y0 * scale + (pass ? 0 : .5f),
                (q.x1-q.x0)*scale, (q.y1-q.y0)*scale, q.s0, q.t0, q.s1, q.t1,
                pass ? cmd->color : cmd->color & 255u);
            x += gx * scale;
        }
    }
}

void PCText_Image(unsigned texture, float x, float y, float w, float h, unsigned color)
{
    textcmd_t *cmd;
    if (!texture || command_count>=256) return;
    cmd=&commands[command_count++];
    memset(cmd,0,sizeof(*cmd));
    cmd->texture=texture;cmd->x=x;cmd->y=y;cmd->w=w;cmd->h=h;cmd->color=color;
    I_CheckGFX();
    _gW(GFX1++,_SHIFTL(G_PC_TEXT,24,8),(uintptr_t)cmd);
}
void PCText_WorldImage(unsigned texture,float x,float y,float z,float w,float h,unsigned color)
{
    textcmd_t *cmd;
    if(!texture || command_count>=256) return;
    cmd=&commands[command_count++];memset(cmd,0,sizeof(*cmd));
    cmd->texture=texture;cmd->x=x;cmd->y=y;cmd->z=z;cmd->w=w;cmd->h=h;cmd->color=color;
    cmd->world=1;cmd->rx=viewsin/(float)FRACUNIT;cmd->ry=-viewcos/(float)FRACUNIT;
    I_CheckGFX();_gW(GFX1++,_SHIFTL(G_PC_TEXT,24,8),(uintptr_t)cmd);
}

void PCText_SkyImage(unsigned texture,float u0,float span,float pitch)
{
    if(!texture || command_count>=256) return;
    textcmd_t *cmd=&commands[command_count++];memset(cmd,0,sizeof(*cmd));
    cmd->world=2;cmd->texture=texture;cmd->z=u0;cmd->rx=span;cmd->ry=pitch;
    I_CheckGFX();_gW(GFX1++,_SHIFTL(G_PC_TEXT,24,8),(uintptr_t)cmd);
}
