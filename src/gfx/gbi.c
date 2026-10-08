/*
 * gbi.c - interpreter for the F3DEX-style display lists the Doom 64 code
 * builds (see src/port/include/ultra64.h for the encoding).
 *
 * The interpreter emulates the parts of the RSP (matrices, vertex
 * transform, fog) and RDP (TMEM loads, tiles, combiner/blender state) that
 * Doom 64 uses, and produces a D64GfxFrame for the renderer.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <ultra64.h>
#include "gbi.h"
#include "d64gfx.h"

#define MAX_VERTS_OUT   (1 << 18)
#define MAX_CMDS_OUT    (1 << 14)
#define RSP_VTX_MAX     64
#define MTX_STACK_MAX   16
#define TEXCACHE_SIZE   4096   /* power of two */
#define TEXCACHE_MAX_LIVE 3000

/* ------------------------------------------------------------------ */
/* state                                                              */
/* ------------------------------------------------------------------ */
typedef struct {
    float clip[4];
    float eye[3];
    float s, t;          /* texture coords in texels * scale (pre shift) */
    uint8_t shade[4];
} rspvtx_t;

typedef struct {
    int fmt, siz, line, tmem, palette;
    int cms, cmt, masks, maskt, shifts, shiftt;
    int uls, ult, lrs, lrt;     /* 10.2 */
} tile_t;

static struct {
    float mv[MTX_STACK_MAX][4][4];
    int   mv_top;
    float proj[4][4];
    float mvp[4][4];
    int   mvp_dirty;
    int   mv_muls;           /* MUL operations since the last modelview LOAD */
    uint32_t geom;
    int   tex_on, tex_tile;
    float tex_scale_s, tex_scale_t;
    int16_t fog_mul, fog_off;
    float vp_scale[2], vp_trans[2];
    rspvtx_t vtx[RSP_VTX_MAX];
} rsp;

static struct {
    uint32_t om_h, om_l;
    uint32_t cc_w0, cc_w1;
    uint8_t prim[4], env[4], fog[4], blend[4];
    float prim_lod_frac;
    uint32_t fill;
    int timg_fmt, timg_siz, timg_width;
    const uint8_t *timg;
    int cimg_siz;
    tile_t tiles[8];
    uint8_t tmem[4096];
    int16_t scissor[4];
    uint32_t half1, half2;
} rdp;

/* output */
static D64GfxVertex *out_v;
static uint32_t out_vn;
static D64GfxDrawCmd *out_c;
static uint32_t out_cn;
static D64GfxDrawCmd cur_cmd;
static int cur_cmd_valid;

static int out_w = 1280, out_h = 960;

/* Widescreen: the game's 320x240 space becomes a centred 4:3 area inside a
 * virtual screen vw units wide. 2D (HUD, menus, weapon) keeps its aspect;
 * the 3D world widens its field of view; full-width 2D (sky, fades, clears)
 * and the sky stretch to the full width. */
static float vw = 320.0f;
static float ndc_xscale = 1.0f;   /* 320 / vw */

static int   have_camera;
static float cam_mv[4][4];
static float cam_inv[4][4];
static uint32_t world_first_cmd;

static const D64GfxLight *frame_lights;
static uint32_t frame_light_count;
static float world_fog[4];
static float world_fog_near;

static gbistats_t stats;
static uint32_t frame_counter;

/* texture cache */
typedef struct {
    uint64_t key;
    uint32_t id;
    uint32_t last_used;
    int used;
} texent_t;
static texent_t texcache[TEXCACHE_SIZE];
static uint32_t texcache_live;

/* ------------------------------------------------------------------ */
/* math                                                               */
/* ------------------------------------------------------------------ */
static void mtx_mul(float r[4][4], const float a[4][4], const float b[4][4])
{
    float t[4][4];
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
    memcpy(r, t, sizeof(t));
}

static void mtx_from_n64(float m[4][4], const void *src)
{
    const int32_t *w = (const int32_t *)src;
    int i, j;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            int idx = (i * 4 + j) / 2;
            uint32_t wi = (uint32_t)w[idx];
            uint32_t wf = (uint32_t)w[8 + idx];
            int16_t ip = (j & 1) ? (int16_t)(wi & 0xffff) : (int16_t)(wi >> 16);
            uint16_t fp = (j & 1) ? (uint16_t)(wf & 0xffff) : (uint16_t)(wf >> 16);
            m[i][j] = (float)ip + (float)fp / 65536.0f;
        }
    }
}

static int mtx_invert(float out[4][4], const float m[4][4])
{
    /* general 4x4 inverse (row major) */
    float a[16], inv[16], det;
    int i;
    memcpy(a, m, sizeof(a));
    inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
    inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
    inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
    det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (fabsf(det) < 1e-12f)
        return 0;
    det = 1.0f / det;
    for (i = 0; i < 16; i++)
        inv[i] *= det;
    memcpy(out, inv, sizeof(inv));
    return 1;
}

/* ------------------------------------------------------------------ */
/* TMEM                                                               */
/* ------------------------------------------------------------------ */
static int bytes_per_texel_x2(int siz) /* bytes * 2 so 4b = 1 */
{
    switch (siz)
    {
    case G_IM_SIZ_4b:  return 1;
    case G_IM_SIZ_8b:  return 2;
    case G_IM_SIZ_16b: return 4;
    default:           return 8;
    }
}

static void tmem_load_block(int tile, int uls, int ult, int lrs, int dxt)
{
    const tile_t *t = &rdp.tiles[tile];
    int texels = lrs - uls + 1;
    int bpt2 = bytes_per_texel_x2(rdp.timg_siz);
    const uint8_t *src = rdp.timg + (((ult * rdp.timg_width + uls) * bpt2) >> 1);
    int base = (t->tmem * 8) & 0xfff;
    uint32_t cnt = 0;
    int words, i, b;

    if (!rdp.timg)
        return;

    if (rdp.timg_siz == G_IM_SIZ_32b)
    {
        /* 32-bit texels are split: RG to the low half, BA to the high half */
        int k;
        for (k = 0; k < texels; k++)
        {
            int word = (k * 2) / 8;
            int odd = ((uint32_t)(word * dxt) >> 11) & 1; /* approximate per word */
            int a = (base + k * 2) & 0x7ff;
            if (odd) a ^= 4;
            rdp.tmem[a]            = src[k * 4 + 0];
            rdp.tmem[a + 1]        = src[k * 4 + 1];
            rdp.tmem[(a | 0x800)]     = src[k * 4 + 2];
            rdp.tmem[(a | 0x800) + 1] = src[k * 4 + 3];
        }
        return;
    }

    words = ((texels * bpt2) / 2 + 7) / 8;
    for (i = 0; i < words; i++)
    {
        int odd = (cnt >> 11) & 1;
        int dst = (base + i * 8) & 0xfff;
        for (b = 0; b < 8; b++)
        {
            int sb = odd ? (b ^ 4) : b;
            rdp.tmem[(dst + b) & 0xfff] = src[i * 8 + sb];
        }
        cnt += (uint32_t)dxt;
    }
}

static void tmem_load_tile(int tile, int uls, int ult, int lrs, int lrt)
{
    const tile_t *t = &rdp.tiles[tile];
    int s0 = uls >> 2, t0 = ult >> 2, s1 = lrs >> 2, t1 = lrt >> 2;
    int bpt2 = bytes_per_texel_x2(rdp.timg_siz);
    int base = t->tmem * 8;
    int line = t->line * 8;
    int row, col;

    if (!rdp.timg)
        return;
    for (row = t0; row <= t1; row++)
    {
        int r = row - t0;
        for (col = s0; col <= s1; col++)
        {
            int c = col - s0;
            if (rdp.timg_siz == G_IM_SIZ_32b)
            {
                const uint8_t *sp = rdp.timg + ((size_t)row * rdp.timg_width + col) * 4;
                int a = (base + r * line + c * 2) & 0x7ff;
                if (r & 1) a ^= 4;
                rdp.tmem[a] = sp[0];
                rdp.tmem[a + 1] = sp[1];
                rdp.tmem[(a | 0x800)] = sp[2];
                rdp.tmem[(a | 0x800) + 1] = sp[3];
            }
            else
            {
                int nb = bpt2 >> 1; /* bytes per texel (4b not allowed for LoadTile) */
                const uint8_t *sp = rdp.timg + (((size_t)row * rdp.timg_width + col) * bpt2 >> 1);
                int a = base + r * line + c * nb;
                int k;
                for (k = 0; k < nb; k++)
                {
                    int ad = a + k;
                    if (r & 1) ad ^= 4;
                    rdp.tmem[ad & 0xfff] = sp[k];
                }
            }
        }
    }
}

static void tmem_load_tlut(int tile, int count)
{
    const tile_t *t = &rdp.tiles[tile];
    int i, lane;
    int base = t->tmem * 8;
    const uint8_t *src;
    if (!rdp.timg)
        return;
    /* gDPLoadTLUTCmd always loads from the start of the texture image */
    src = rdp.timg;
    for (i = 0; i <= count; i++)
        for (lane = 0; lane < 4; lane++)
        {
            int a = (base + i * 8 + lane * 2) & 0xfff;
            rdp.tmem[a] = src[i * 2];
            rdp.tmem[a + 1] = src[i * 2 + 1];
        }
}

static void rgba5551(uint16_t c, uint8_t *o)
{
    o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    o[1] = (uint8_t)(((c >> 6) & 31) * 255 / 31);
    o[2] = (uint8_t)(((c >> 1) & 31) * 255 / 31);
    o[3] = (c & 1) ? 255 : 0;
}

static void tlut_color(int idx, uint8_t *o)
{
    int a = 0x800 + (idx & 0xff) * 8;
    uint16_t c = (uint16_t)((rdp.tmem[a] << 8) | rdp.tmem[a + 1]);
    uint32_t tlut = rdp.om_h & (3u << G_MDSFT_TEXTLUT);
    if (tlut == G_TT_IA16)
    {
        o[0] = o[1] = o[2] = (uint8_t)(c >> 8);
        o[3] = (uint8_t)c;
    }
    else
        rgba5551(c, o);
}

/* Fetch one texel of tile 'tl' at tile-local coordinates (s,t). */
static void tmem_fetch(const tile_t *tl, int s, int t, uint8_t *o)
{
    int base = tl->tmem * 8 + t * tl->line * 8;
    int x = (t & 1) ? 4 : 0;
    uint32_t tlut = rdp.om_h & (3u << G_MDSFT_TEXTLUT);
    int a;
    switch (tl->siz)
    {
    case G_IM_SIZ_4b:
    {
        int v;
        a = ((base + (s >> 1)) ^ x) & 0xfff;
        v = (s & 1) ? (rdp.tmem[a] & 15) : (rdp.tmem[a] >> 4);
        if (tl->fmt == G_IM_FMT_CI || tlut != G_TT_NONE)
            tlut_color((tl->palette << 4) | v, o);
        else if (tl->fmt == G_IM_FMT_IA)
        {
            uint8_t i = (uint8_t)(((v >> 1) & 7) * 255 / 7);
            o[0] = o[1] = o[2] = i;
            o[3] = (v & 1) ? 255 : 0;
        }
        else
        {
            o[0] = o[1] = o[2] = o[3] = (uint8_t)(v * 17);
        }
        break;
    }
    case G_IM_SIZ_8b:
    {
        uint8_t v;
        a = ((base + s) ^ x) & 0xfff;
        v = rdp.tmem[a];
        if (tl->fmt == G_IM_FMT_CI || tlut != G_TT_NONE)
            tlut_color(v, o);
        else if (tl->fmt == G_IM_FMT_IA)
        {
            o[0] = o[1] = o[2] = (uint8_t)((v >> 4) * 17);
            o[3] = (uint8_t)((v & 15) * 17);
        }
        else
            o[0] = o[1] = o[2] = o[3] = v;
        break;
    }
    case G_IM_SIZ_16b:
    {
        uint16_t c;
        a = ((base + s * 2) ^ x) & 0xfff;
        c = (uint16_t)((rdp.tmem[a] << 8) | rdp.tmem[(a + 1) & 0xfff]);
        if (tl->fmt == G_IM_FMT_IA)
        {
            o[0] = o[1] = o[2] = (uint8_t)(c >> 8);
            o[3] = (uint8_t)c;
        }
        else if (tl->fmt == G_IM_FMT_I)
        {
            o[0] = o[1] = o[2] = o[3] = (uint8_t)(c >> 8);
        }
        else
            rgba5551(c, o);
        break;
    }
    default:
    {
        a = ((tl->tmem * 8 + t * tl->line * 8 + s * 2) ^ x) & 0x7ff;
        o[0] = rdp.tmem[a];
        o[1] = rdp.tmem[a + 1];
        o[2] = rdp.tmem[a | 0x800];
        o[3] = rdp.tmem[(a | 0x800) + 1];
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* texture cache                                                      */
/* ------------------------------------------------------------------ */
static uint64_t fnv1a(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static void tile_dims(const tile_t *tl, int *w, int *h)
{
    int tw = ((tl->lrs - tl->uls) >> 2) + 1;
    int th = ((tl->lrt - tl->ult) >> 2) + 1;
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tl->masks && (1 << tl->masks) < tw) tw = 1 << tl->masks;
    if (tl->maskt && (1 << tl->maskt) < th) th = 1 << tl->maskt;
    if (tw > 1024) tw = 1024;
    if (th > 1024) th = 1024;
    *w = tw;
    *h = th;
}

static uint32_t texture_for_tile(int tidx)
{
    const tile_t *tl = &rdp.tiles[tidx];
    int w, h, x, y;
    uint64_t key = 0xcbf29ce484222325ULL;
    uint32_t tlut = rdp.om_h & (3u << G_MDSFT_TEXTLUT);
    int bytes_line = (tl->line * 8);
    int used, slot, probe, oldest = -1;
    uint32_t oldest_frame = 0xffffffffu;
    uint8_t *rgba;
    uint32_t id;

    tile_dims(tl, &w, &h);
    {
        int params[8] = { tl->fmt, tl->siz, tl->line, tl->tmem, tl->palette, w, h, (int)tlut };
        key = fnv1a(key, params, sizeof(params));
    }
    /* hash the TMEM bytes this tile can reach */
    used = bytes_line ? bytes_line * h : ((w * h * bytes_per_texel_x2(tl->siz)) >> 1);
    if (tl->siz == G_IM_SIZ_32b)
    {
        int lo = (tl->tmem * 8) & 0x7ff;
        int n = used > 2048 - lo ? 2048 - lo : used;
        key = fnv1a(key, rdp.tmem + lo, (size_t)n);
        key = fnv1a(key, rdp.tmem + 0x800 + lo, (size_t)n);
    }
    else
    {
        int lo = (tl->tmem * 8) & 0xfff;
        int n = used > 4096 - lo ? 4096 - lo : used;
        key = fnv1a(key, rdp.tmem + lo, (size_t)n);
        if (tl->fmt == G_IM_FMT_CI || tlut != G_TT_NONE)
        {
            if (tl->siz == G_IM_SIZ_4b)
                key = fnv1a(key, rdp.tmem + 0x800 + tl->palette * 16 * 8, 16 * 8);
            else
                key = fnv1a(key, rdp.tmem + 0x800, 256 * 8);
        }
    }
    if (key == 0)
        key = 1;

    slot = (int)(key & (TEXCACHE_SIZE - 1));
    for (probe = 0; probe < TEXCACHE_SIZE; probe++)
    {
        texent_t *e = &texcache[(slot + probe) & (TEXCACHE_SIZE - 1)];
        if (!e->used)
            break;
        if (e->key == key)
        {
            e->last_used = frame_counter;
            stats.tex_cached++;
            return e->id;
        }
    }

    /* evict when the cache gets crowded */
    if (texcache_live >= TEXCACHE_MAX_LIVE)
    {
        int i;
        for (i = 0; i < TEXCACHE_SIZE; i++)
            if (texcache[i].used && texcache[i].last_used < oldest_frame)
            {
                oldest_frame = texcache[i].last_used;
                oldest = i;
            }
        if (oldest >= 0)
        {
            /* removing from open addressing: rebuild the table */
            texent_t *old = (texent_t *)malloc(sizeof(texcache));
            int i2;
            d64gfx_texture_destroy(texcache[oldest].id);
            texcache[oldest].used = 0;
            memcpy(old, texcache, sizeof(texcache));
            memset(texcache, 0, sizeof(texcache));
            texcache_live = 0;
            for (i2 = 0; i2 < TEXCACHE_SIZE; i2++)
            {
                if (old[i2].used)
                {
                    int s2 = (int)(old[i2].key & (TEXCACHE_SIZE - 1));
                    while (texcache[s2].used)
                        s2 = (s2 + 1) & (TEXCACHE_SIZE - 1);
                    texcache[s2] = old[i2];
                    texcache_live++;
                }
            }
            free(old);
        }
    }

    rgba = (uint8_t *)malloc((size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            tmem_fetch(tl, x, y, rgba + ((size_t)y * w + x) * 4);
    id = d64gfx_texture_create((uint32_t)w, (uint32_t)h, rgba);
    if (getenv("D64_DUMPTEX"))
    {   /* debug: write every uploaded texture as a PAM file */
        static int dumped;
        char name[96];
        FILE *f;
        snprintf(name, sizeof(name), "tex_%04d_f%d_s%d_%dx%d.pam", dumped++, tl->fmt, tl->siz, w, h);
        f = fopen(name, "wb");
        if (f)
        {
            fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
            fwrite(rgba, 4, (size_t)w * h, f);
            fclose(f);
        }
    }
    free(rgba);
    stats.tex_uploads++;

    slot = (int)(key & (TEXCACHE_SIZE - 1));
    while (texcache[slot].used)
        slot = (slot + 1) & (TEXCACHE_SIZE - 1);
    texcache[slot].used = 1;
    texcache[slot].key = key;
    texcache[slot].id = id;
    texcache[slot].last_used = frame_counter;
    texcache_live++;
    return id;
}

void GBI_FlushTextureCache(void)
{
    int i;
    for (i = 0; i < TEXCACHE_SIZE; i++)
        if (texcache[i].used)
            d64gfx_texture_destroy(texcache[i].id);
    memset(texcache, 0, sizeof(texcache));
    texcache_live = 0;
}

/* ------------------------------------------------------------------ */
/* combiner / blender translation                                     */
/* ------------------------------------------------------------------ */
static uint8_t cc_rgb_a(int v)
{
    static const uint8_t t[16] = { D64GFX_CC_COMBINED, D64GFX_CC_TEXEL0, D64GFX_CC_TEXEL1, D64GFX_CC_PRIM,
        D64GFX_CC_SHADE, D64GFX_CC_ENV, D64GFX_CC_ONE, D64GFX_CC_NOISE, D64GFX_CC_ZERO, D64GFX_CC_ZERO,
        D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO };
    return t[v & 15];
}
static uint8_t cc_rgb_b(int v)
{
    static const uint8_t t[16] = { D64GFX_CC_COMBINED, D64GFX_CC_TEXEL0, D64GFX_CC_TEXEL1, D64GFX_CC_PRIM,
        D64GFX_CC_SHADE, D64GFX_CC_ENV, D64GFX_CC_ZERO /*center*/, D64GFX_CC_ZERO /*k4*/, D64GFX_CC_ZERO,
        D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO };
    return t[v & 15];
}
static uint8_t cc_rgb_c(int v)
{
    static const uint8_t t[32] = { D64GFX_CC_COMBINED, D64GFX_CC_TEXEL0, D64GFX_CC_TEXEL1, D64GFX_CC_PRIM,
        D64GFX_CC_SHADE, D64GFX_CC_ENV, D64GFX_CC_ONE /*scale*/, D64GFX_CC_COMBINED_A, D64GFX_CC_TEXEL0_A,
        D64GFX_CC_TEXEL1_A, D64GFX_CC_PRIM_A, D64GFX_CC_SHADE_A, D64GFX_CC_ENV_A, D64GFX_CC_LOD_FRAC,
        D64GFX_CC_PRIM_LOD_FRAC, D64GFX_CC_ZERO /*k5*/,
        D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO,
        D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO,
        D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO, D64GFX_CC_ZERO };
    return t[v & 31];
}
static uint8_t cc_rgb_d(int v)
{
    static const uint8_t t[8] = { D64GFX_CC_COMBINED, D64GFX_CC_TEXEL0, D64GFX_CC_TEXEL1, D64GFX_CC_PRIM,
        D64GFX_CC_SHADE, D64GFX_CC_ENV, D64GFX_CC_ONE, D64GFX_CC_ZERO };
    return t[v & 7];
}
/* alpha a, b, d share an encoding; c differs at 0 and 6 */
static uint8_t cc_alpha_abd(int v)
{
    static const uint8_t t[8] = { D64GFX_CC_COMBINED_A, D64GFX_CC_TEXEL0_A, D64GFX_CC_TEXEL1_A, D64GFX_CC_PRIM_A,
        D64GFX_CC_SHADE_A, D64GFX_CC_ENV_A, D64GFX_CC_ONE, D64GFX_CC_ZERO };
    return t[v & 7];
}
static uint8_t cc_alpha_c(int v)
{
    static const uint8_t t[8] = { D64GFX_CC_LOD_FRAC, D64GFX_CC_TEXEL0_A, D64GFX_CC_TEXEL1_A, D64GFX_CC_PRIM_A,
        D64GFX_CC_SHADE_A, D64GFX_CC_ENV_A, D64GFX_CC_PRIM_LOD_FRAC, D64GFX_CC_ZERO };
    return t[v & 7];
}

static void decode_combiner(uint8_t cc[16])
{
    uint32_t w0 = rdp.cc_w0, w1 = (uint32_t)rdp.cc_w1;
    cc[0] = cc_rgb_a((w0 >> 20) & 15);
    cc[1] = cc_rgb_b((w1 >> 28) & 15);
    cc[2] = cc_rgb_c((w0 >> 15) & 31);
    cc[3] = cc_rgb_d((w1 >> 15) & 7);
    cc[4] = cc_alpha_abd((w0 >> 12) & 7);
    cc[5] = cc_alpha_abd((w1 >> 12) & 7);
    cc[6] = cc_alpha_c((w0 >> 9) & 7);
    cc[7] = cc_alpha_abd((w1 >> 9) & 7);
    cc[8] = cc_rgb_a((w0 >> 5) & 15);
    cc[9] = cc_rgb_b((w1 >> 24) & 15);
    cc[10] = cc_rgb_c((w0 >> 0) & 31);
    cc[11] = cc_rgb_d((w1 >> 6) & 7);
    cc[12] = cc_alpha_abd((w1 >> 21) & 7);
    cc[13] = cc_alpha_abd((w1 >> 3) & 7);
    cc[14] = cc_alpha_c((w1 >> 18) & 7);
    cc[15] = cc_alpha_abd((w1 >> 0) & 7);
}

static int cc_uses_texel(const uint8_t cc[16], int two_cycle, int which)
{
    int i, n = two_cycle ? 16 : 8;
    uint8_t c = which ? D64GFX_CC_TEXEL1 : D64GFX_CC_TEXEL0;
    uint8_t ca = which ? D64GFX_CC_TEXEL1_A : D64GFX_CC_TEXEL0_A;
    for (i = 0; i < n; i++)
        if (cc[i] == c || cc[i] == ca)
            return 1;
    return 0;
}

static uint32_t cycle_type(void)
{
    return rdp.om_h & (3u << G_MDSFT_CYCLETYPE);
}

static uint32_t blend_flags(void)
{
    uint32_t l = rdp.om_l;
    uint32_t f = 0;
    int two = cycle_type() == G_CYC_2CYCLE;
    /* final blender cycle */
    int m1a, m1b, m2a, m2b;
    if (two)
    {
        m1a = (l >> 28) & 3; m1b = (l >> 24) & 3; m2a = (l >> 20) & 3; m2b = (l >> 16) & 3;
        if (((l >> 30) & 3) == G_BL_CLR_FOG)
            f |= D64GFX_CMD_FOG;
    }
    else
    {
        m1a = (l >> 30) & 3; m1b = (l >> 26) & 3; m2a = (l >> 22) & 3; m2b = (l >> 18) & 3;
    }
    (void)m1a;
    if (m2a == G_BL_CLR_MEM && m1b == G_BL_A_IN && m2b == G_BL_1MA && (l & FORCE_BL))
        f |= D64GFX_CMD_BLEND;
    if ((l & CVG_X_ALPHA) && (l & ALPHA_CVG_SEL))
        f |= D64GFX_CMD_ALPHA_CVG;
    if ((l & 3) == G_AC_THRESHOLD)
        f |= D64GFX_CMD_ALPHA_THRESH;
    if ((l & Z_CMP) && (rsp.geom & G_ZBUFFER))
        f |= D64GFX_CMD_DEPTH_TEST;
    if ((l & Z_UPD) && (rsp.geom & G_ZBUFFER))
        f |= D64GFX_CMD_DEPTH_WRITE;
    if (two)
        f |= D64GFX_CMD_TWO_CYCLE;
    if ((rdp.om_h & (3u << G_MDSFT_TEXTFILT)) != G_TF_POINT)
        f |= D64GFX_CMD_FILTER;
    return f;
}

static uint8_t wrap_bits(const tile_t *tl)
{
    int s = (tl->cms & G_TX_CLAMP) ? D64GFX_WRAP_CLAMP : (tl->cms & G_TX_MIRROR) ? D64GFX_WRAP_MIRROR : D64GFX_WRAP_REPEAT;
    int t = (tl->cmt & G_TX_CLAMP) ? D64GFX_WRAP_CLAMP : (tl->cmt & G_TX_MIRROR) ? D64GFX_WRAP_MIRROR : D64GFX_WRAP_REPEAT;
    /* a clamped tile larger than its mask still wraps at the mask */
    if (!tl->masks && s == D64GFX_WRAP_REPEAT) s = D64GFX_WRAP_CLAMP;
    if (!tl->maskt && t == D64GFX_WRAP_REPEAT) t = D64GFX_WRAP_CLAMP;
    return (uint8_t)(s | (t << 2));
}

/* ------------------------------------------------------------------ */
/* command building                                                   */
/* ------------------------------------------------------------------ */
static void flush_cmd(void)
{
    if (cur_cmd_valid && cur_cmd.vertex_count > 0 && out_cn < MAX_CMDS_OUT)
        out_c[out_cn++] = cur_cmd;
    cur_cmd_valid = 0;
}

static int same_state(const D64GfxDrawCmd *a, const D64GfxDrawCmd *b)
{
    return a->tex[0] == b->tex[0] && a->tex[1] == b->tex[1] && !memcmp(a->cc, b->cc, 16) &&
           a->flags == b->flags && a->wrap[0] == b->wrap[0] && a->wrap[1] == b->wrap[1] &&
           !memcmp(a->prim, b->prim, 4) && !memcmp(a->env, b->env, 4) && !memcmp(a->fog, b->fog, 4) &&
           !memcmp(a->blend, b->blend, 4) && a->prim_lod_frac == b->prim_lod_frac &&
           !memcmp(a->scissor, b->scissor, sizeof(a->scissor));
}

/* Prepare state for a draw; returns texture scale info through the tiles. */
static void begin_draw(uint32_t extra_flags, int textured, int tile_index)
{
    D64GfxDrawCmd c;
    int two = cycle_type() == G_CYC_2CYCLE;

    memset(&c, 0, sizeof(c));
    decode_combiner(c.cc);
    if (!two)
        memcpy(c.cc + 8, c.cc, 8);
    c.flags = blend_flags() | extra_flags;
    if (cycle_type() == G_CYC_COPY)
        c.flags &= ~(D64GFX_CMD_FILTER | D64GFX_CMD_BLEND | D64GFX_CMD_FOG);
    memcpy(c.prim, rdp.prim, 4);
    memcpy(c.env, rdp.env, 4);
    memcpy(c.fog, rdp.fog, 4);
    memcpy(c.blend, rdp.blend, 4);
    c.prim_lod_frac = rdp.prim_lod_frac;
    memcpy(c.scissor, rdp.scissor, sizeof(c.scissor));
    if (vw != 320.0f)
    {
        float off = (vw - 320.0f) * 0.5f;
        if (c.scissor[0] <= 0 && c.scissor[2] >= 320)
        {
            c.scissor[0] = 0;
            c.scissor[2] = (int16_t)(vw + 0.999f);
        }
        else
        {
            c.scissor[0] = (int16_t)(c.scissor[0] + off);
            c.scissor[2] = (int16_t)(c.scissor[2] + off + 0.999f);
        }
    }

    if (textured)
    {
        if (cc_uses_texel(c.cc, 1, 0))
        {
            c.tex[0] = texture_for_tile(tile_index);
            c.wrap[0] = wrap_bits(&rdp.tiles[tile_index]);
        }
        if (two && cc_uses_texel(c.cc, 1, 1))
        {
            c.tex[1] = texture_for_tile((tile_index + 1) & 7);
            c.wrap[1] = wrap_bits(&rdp.tiles[(tile_index + 1) & 7]);
        }
    }

    if (cur_cmd_valid && same_state(&cur_cmd, &c) &&
        cur_cmd.first_vertex + cur_cmd.vertex_count == out_vn)
        return;
    flush_cmd();
    c.first_vertex = out_vn;
    c.vertex_count = 0;
    cur_cmd = c;
    cur_cmd_valid = 1;
    if ((c.flags & D64GFX_CMD_WORLD) && world_first_cmd == 0xffffffffu)
        world_first_cmd = out_cn;
}

static void tile_uv(const tile_t *tl, float s, float t, float *u, float *v)
{
    int w, h;
    /* shift */
    if (tl->shifts)
        s = (tl->shifts <= 10) ? s / (float)(1 << tl->shifts) : s * (float)(1 << (16 - tl->shifts));
    if (tl->shiftt)
        t = (tl->shiftt <= 10) ? t / (float)(1 << tl->shiftt) : t * (float)(1 << (16 - tl->shiftt));
    s -= (float)tl->uls / 4.0f;
    t -= (float)tl->ult / 4.0f;
    tile_dims(tl, &w, &h);
    *u = s / (float)w;
    *v = t / (float)h;
}

static void emit_vertex(const float clip[4], const float world[3], float s, float t,
                        const uint8_t shade[4], int tile_index)
{
    D64GfxVertex *o;
    if (out_vn >= MAX_VERTS_OUT)
        return;
    o = &out_v[out_vn++];
    memcpy(o->pos, clip, sizeof(o->pos));
    if (world)
        memcpy(o->world, world, sizeof(o->world));
    else
        o->world[0] = o->world[1] = o->world[2] = 0.0f;
    tile_uv(&rdp.tiles[tile_index], s, t, &o->uv[0], &o->uv[1]);
    tile_uv(&rdp.tiles[(tile_index + 1) & 7], s, t, &o->uv1[0], &o->uv1[1]);
    memcpy(o->shade, shade, 4);
    cur_cmd.vertex_count++;
}

/* N64 viewport to our full-screen clip space */
static void viewport_adjust(float c[4])
{
    float w = c[3];
    float nx, ny, sx, sy;
    if (rsp.vp_scale[0] == 160.0f && rsp.vp_trans[0] == 160.0f &&
        rsp.vp_scale[1] == 120.0f && rsp.vp_trans[1] == 120.0f)
        return;
    if (fabsf(w) < 1e-6f)
        return;
    nx = c[0] / w;
    ny = c[1] / w;
    sx = rsp.vp_trans[0] + nx * rsp.vp_scale[0];
    sy = rsp.vp_trans[1] - ny * rsp.vp_scale[1];
    c[0] = (sx / 160.0f - 1.0f) * w;
    c[1] = (1.0f - sy / 120.0f) * w;
}

static int is_world_draw(void)
{
    /* The 3D view is a perspective projection with a modelview built from
     * a LOAD and at least one MUL (r_main.c R_RenderPlayerView). The sky
     * clouds and automap use the bare model matrix. */
    return rsp.proj[2][3] != 0.0f && rsp.mv_muls >= 1;
}

static void draw_triangle(int i0, int i1, int i2)
{
    const rspvtx_t *v[3];
    int k, world;
    uint32_t extra = 0;

    if (i0 >= RSP_VTX_MAX || i1 >= RSP_VTX_MAX || i2 >= RSP_VTX_MAX)
        return;
    v[0] = &rsp.vtx[i0];
    v[1] = &rsp.vtx[i1];
    v[2] = &rsp.vtx[i2];

    /* culling (in screen space, N64 y down after viewport) */
    if (rsp.geom & G_CULL_BOTH)
    {
        float ax = v[0]->clip[0] / v[0]->clip[3], ay = v[0]->clip[1] / v[0]->clip[3];
        float bx = v[1]->clip[0] / v[1]->clip[3], by = v[1]->clip[1] / v[1]->clip[3];
        float cx = v[2]->clip[0] / v[2]->clip[3], cy = v[2]->clip[1] / v[2]->clip[3];
        float cross = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        int ws = (v[0]->clip[3] < 0) + (v[1]->clip[3] < 0) + (v[2]->clip[3] < 0);
        if (ws == 0)
        {
            if ((rsp.geom & G_CULL_BACK) && cross < 0) return;
            if ((rsp.geom & G_CULL_FRONT) && cross > 0) return;
        }
    }

    world = is_world_draw();
    if (world)
    {
        extra |= D64GFX_CMD_WORLD;
        if (!have_camera)
        {
            memcpy(cam_mv, rsp.mv[rsp.mv_top], sizeof(cam_mv));
            if (mtx_invert(cam_inv, cam_mv))
                have_camera = 1;
        }
    }
    else if (rsp.proj[2][3] != 0.0f)
        extra |= D64GFX_CMD_SKY;

    begin_draw(extra, rsp.tex_on, rsp.tex_tile);
    for (k = 0; k < 3; k++)
    {
        float wp[3];
        float c[4];
        memcpy(c, v[k]->clip, sizeof(c));
        viewport_adjust(c);
        if (!(extra & D64GFX_CMD_SKY))
            c[0] *= ndc_xscale;
        if (world && have_camera)
        {
            const float *e = v[k]->eye;
            int j;
            for (j = 0; j < 3; j++)
                wp[j] = e[0] * cam_inv[0][j] + e[1] * cam_inv[1][j] + e[2] * cam_inv[2][j] + cam_inv[3][j];
        }
        emit_vertex(c, world ? wp : NULL, v[k]->s, v[k]->t, v[k]->shade, rsp.tex_tile);
    }
}

static void screen_to_clip(float x, float y, float c[4], int full_width)
{
    c[0] = (x / 160.0f - 1.0f) * (full_width ? 1.0f : ndc_xscale);
    c[1] = 1.0f - y / 120.0f;
    c[2] = 0.0f;
    c[3] = 1.0f;
}

static void emit_quad_screen(float x0, float y0, float x1, float y1,
                             float s0, float t0, float s1, float t1,
                             int flip, const uint8_t shade[4], int tile_index)
{
    float c[4][4];
    float st[4][2];
    int order[6] = { 0, 1, 2, 0, 2, 3 };
    int i;
    int full = (x0 <= 0.5f && x1 >= 319.5f); /* full width: stretch in widescreen */
    screen_to_clip(x0, y0, c[0], full);
    screen_to_clip(x1, y0, c[1], full);
    screen_to_clip(x1, y1, c[2], full);
    screen_to_clip(x0, y1, c[3], full);
    if (!flip)
    {
        st[0][0] = s0; st[0][1] = t0;
        st[1][0] = s1; st[1][1] = t0;
        st[2][0] = s1; st[2][1] = t1;
        st[3][0] = s0; st[3][1] = t1;
    }
    else
    {
        st[0][0] = s0; st[0][1] = t0;
        st[1][0] = s0; st[1][1] = t1;
        st[2][0] = s1; st[2][1] = t1;
        st[3][0] = s1; st[3][1] = t0;
    }
    for (i = 0; i < 6; i++)
        emit_vertex(c[order[i]], NULL, st[order[i]][0], st[order[i]][1], shade, tile_index);
}

static void do_texrect(uint32_t w0, uint32_t w1, uint32_t h1, uint32_t h2, int flip)
{
    float xh = (float)((w0 >> 12) & 0xfff) / 4.0f;
    float yh = (float)(w0 & 0xfff) / 4.0f;
    int tile = (w1 >> 24) & 7;
    float xl = (float)((w1 >> 12) & 0xfff) / 4.0f;
    float yl = (float)(w1 & 0xfff) / 4.0f;
    float s = (float)(int16_t)(h1 >> 16) / 32.0f;
    float t = (float)(int16_t)(h1 & 0xffff) / 32.0f;
    float dsdx = (float)(int16_t)(h2 >> 16) / 1024.0f;
    float dtdy = (float)(int16_t)(h2 & 0xffff) / 1024.0f;
    /* Rectangles have no shade on the RDP. Keep rgb white for combiners that
     * multiply by SHADE, but alpha 0 so the fog blender leaves them alone
     * (the weapon sprite is drawn with fog enabled). */
    static const uint8_t white[4] = { 255, 255, 255, 0 };
    float s1, t1;

    if (cycle_type() == G_CYC_COPY)
    {
        /* copy mode: inclusive coordinates, 4 texels per x step */
        xh += 1.0f;
        yh += 1.0f;
        dsdx /= 4.0f;
    }
    if (!flip)
    {
        s1 = s + (xh - xl) * dsdx;
        t1 = t + (yh - yl) * dtdy;
    }
    else
    {
        s1 = s + (yh - yl) * dsdx;
        t1 = t + (xh - xl) * dtdy;
    }
    begin_draw(0, 1, tile);
    emit_quad_screen(xl, yl, xh, yh, s, t, s1, t1, flip, white, tile);
}

static void do_fillrect(uint32_t w0, uint32_t w1)
{
    float x1 = (float)((w0 >> 14) & 0x3ff);
    float y1 = (float)((w0 >> 2) & 0x3ff);
    float x0 = (float)((w1 >> 14) & 0x3ff);
    float y0 = (float)((w1 >> 2) & 0x3ff);
    uint8_t col[4];
    D64GfxDrawCmd save;

    if (cycle_type() == G_CYC_FILL)
    {
        uint8_t saved_prim[4];
        uint32_t saved_cc0 = rdp.cc_w0, saved_cc1 = rdp.cc_w1;
        uint32_t saved_l = rdp.om_l;
        x1 += 1.0f;
        y1 += 1.0f;
        if (rdp.cimg_siz == G_IM_SIZ_32b)
        {
            col[0] = (uint8_t)(rdp.fill >> 24); col[1] = (uint8_t)(rdp.fill >> 16);
            col[2] = (uint8_t)(rdp.fill >> 8);  col[3] = 255;
        }
        else
        {
            rgba5551((uint16_t)(rdp.fill & 0xffff), col);
            col[3] = 255;
        }
        /* draw as an opaque primitive-colour rectangle */
        memcpy(saved_prim, rdp.prim, 4);
        memcpy(rdp.prim, col, 4);
        rdp.cc_w0 = (G_CCMUX_0 & 15) << 20 | (G_CCMUX_0 & 31) << 15 | (G_ACMUX_0 << 12) | (G_ACMUX_0 << 9) |
                    (G_CCMUX_0 & 15) << 5 | (G_CCMUX_0 & 31);
        rdp.cc_w1 = (uint32_t)(G_CCMUX_0 & 15) << 28 | (uint32_t)(G_CCMUX_0 & 15) << 24 | (G_ACMUX_0 << 21) |
                    (G_ACMUX_0 << 18) | (G_CCMUX_PRIMITIVE << 15) | (G_ACMUX_0 << 12) | (G_ACMUX_1 << 9) |
                    (G_CCMUX_PRIMITIVE << 6) | (G_ACMUX_0 << 3) | G_ACMUX_1;
        rdp.om_l = 0;
        begin_draw(0, 0, 0);
        emit_quad_screen(x0, y0, x1, y1, 0, 0, 0, 0, 0, col, 0);
        flush_cmd();
        memcpy(rdp.prim, saved_prim, 4);
        rdp.cc_w0 = saved_cc0;
        rdp.cc_w1 = saved_cc1;
        rdp.om_l = saved_l;
        (void)save;
        return;
    }
    {
        static const uint8_t white[4] = { 255, 255, 255, 255 };
        begin_draw(0, 0, 0);
        emit_quad_screen(x0, y0, x1, y1, 0, 0, 0, 0, 0, white, 0);
    }
}

static void do_line3d(int i0, int i1)
{
    /* expand the line to a thin screen space quad */
    const rspvtx_t *a = &rsp.vtx[i0], *b = &rsp.vtx[i1];
    float ca[4], cb[4];
    float ax, ay, bx, by, dx, dy, len, px, py, hw;
    float q[4][4];
    int order[6] = { 0, 1, 2, 0, 2, 3 };
    int i;

    memcpy(ca, a->clip, sizeof(ca));
    memcpy(cb, b->clip, sizeof(cb));
    viewport_adjust(ca);
    viewport_adjust(cb);
    ca[0] *= ndc_xscale;
    cb[0] *= ndc_xscale;
    if (ca[3] <= 0.0f || cb[3] <= 0.0f)
        return; /* the automap never puts line ends behind the eye */
    ax = ca[0] / ca[3]; ay = ca[1] / ca[3];
    bx = cb[0] / cb[3]; by = cb[1] / cb[3];
    dx = (bx - ax) * (float)out_w;
    dy = (by - ay) * (float)out_h;
    len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f)
        return;
    hw = (float)out_h / 240.0f * 0.5f;      /* one game pixel wide */
    if (hw < 0.5f) hw = 0.5f;
    px = -dy / len * hw * 2.0f / (float)out_w;
    py = dx / len * hw * 2.0f / (float)out_h;
    q[0][0] = ax + px; q[0][1] = ay + py;
    q[1][0] = bx + px; q[1][1] = by + py;
    q[2][0] = bx - px; q[2][1] = by - py;
    q[3][0] = ax - px; q[3][1] = ay - py;
    for (i = 0; i < 4; i++)
    {
        q[i][2] = 0.0f;
        q[i][3] = 1.0f;
    }
    begin_draw(0, 0, 0);
    for (i = 0; i < 6; i++)
    {
        int k = order[i];
        const rspvtx_t *src = (k == 1 || k == 2) ? b : a;
        emit_vertex(q[k], NULL, 0, 0, src->shade, 0);
    }
}

static void load_vertices(const Vtx *src, int n, int v0)
{
    int i;
    if (rsp.mvp_dirty)
    {
        mtx_mul(rsp.mvp, rsp.mv[rsp.mv_top], rsp.proj);
        rsp.mvp_dirty = 0;
    }
    for (i = 0; i < n && v0 + i < RSP_VTX_MAX; i++)
    {
        const Vtx_t *v = &src[i].v;
        rspvtx_t *d = &rsp.vtx[v0 + i];
        float x = v->ob[0], y = v->ob[1], z = v->ob[2];
        const float (*m)[4] = rsp.mvp;
        const float (*mv)[4] = rsp.mv[rsp.mv_top];
        uint32_t col;
        int j;

        for (j = 0; j < 4; j++)
            d->clip[j] = x * m[0][j] + y * m[1][j] + z * m[2][j] + m[3][j];
        for (j = 0; j < 3; j++)
            d->eye[j] = x * mv[0][j] + y * mv[1][j] + z * mv[2][j] + mv[3][j];

        d->s = (float)v->tc[0] * rsp.tex_scale_s / 32.0f;
        d->t = (float)v->tc[1] * rsp.tex_scale_t / 32.0f;

        /* the game writes colours as native 32-bit 0xRRGGBBAA words */
        memcpy(&col, v->cn, 4);
        d->shade[0] = (uint8_t)(col >> 24);
        d->shade[1] = (uint8_t)(col >> 16);
        d->shade[2] = (uint8_t)(col >> 8);
        d->shade[3] = (uint8_t)col;
        if (!(rsp.geom & G_SHADE))
            d->shade[0] = d->shade[1] = d->shade[2] = 255;

        if (rsp.geom & G_FOG)
        {
            float w = d->clip[3];
            float f;
            if (fabsf(w) < 1e-6f) w = 1e-6f;
            f = d->clip[2] / w * (float)rsp.fog_mul + (float)rsp.fog_off;
            if (w < 0.0f) f = 255.0f;
            if (f < 0.0f) f = 0.0f;
            if (f > 255.0f) f = 255.0f;
            d->shade[3] = (uint8_t)f;
        }
    }
}

/* ------------------------------------------------------------------ */
/* main loop                                                          */
/* ------------------------------------------------------------------ */
static void reset_state(void)
{
    int i;
    memset(&rsp, 0, sizeof(rsp));
    for (i = 0; i < 4; i++)
    {
        rsp.mv[0][i][i] = 1.0f;
        rsp.proj[i][i] = 1.0f;
    }
    rsp.mvp_dirty = 1;
    rsp.tex_scale_s = rsp.tex_scale_t = 1.0f;
    rsp.vp_scale[0] = rsp.vp_trans[0] = 160.0f;
    rsp.vp_scale[1] = rsp.vp_trans[1] = 120.0f;
    rdp.scissor[0] = 0; rdp.scissor[1] = 0; rdp.scissor[2] = 320; rdp.scissor[3] = 240;
    rdp.cimg_siz = G_IM_SIZ_32b;
}

static void run_dl(Gfx *dl, int depth)
{
    int guard = 0;
    while (dl && guard++ < (1 << 22))
    {
        uint32_t w0 = dl->words.w0;
        uintptr_t w1 = dl->words.w1;
        int op = (int)(w0 >> 24);
        dl++;
        stats.dl_words++;

        switch (op)
        {
        case G_SPNOOP:
        case G_NOOP:
        case G_RDPLOADSYNC:
        case G_RDPPIPESYNC:
        case G_RDPTILESYNC:
        case G_RDPFULLSYNC:
        case G_CULLDL:
        case G_SETZIMG:
        case G_SETKEYGB:
        case G_SETKEYR:
        case G_SETCONVERT:
        case G_SETPRIMDEPTH:
            break;

        case G_MTX:
        {
            float m[4][4];
            int p = (int)(w0 & 0xff);
            mtx_from_n64(m, (const void *)w1);
            if (p & G_MTX_PROJECTION)
            {
                if (p & G_MTX_LOAD)
                    memcpy(rsp.proj, m, sizeof(m));
                else
                    mtx_mul(rsp.proj, m, rsp.proj);
            }
            else
            {
                if ((p & G_MTX_PUSH) && rsp.mv_top < MTX_STACK_MAX - 1)
                {
                    memcpy(rsp.mv[rsp.mv_top + 1], rsp.mv[rsp.mv_top], sizeof(m));
                    rsp.mv_top++;
                }
                if (p & G_MTX_LOAD)
                {
                    memcpy(rsp.mv[rsp.mv_top], m, sizeof(m));
                    rsp.mv_muls = 0;
                }
                else
                {
                    mtx_mul(rsp.mv[rsp.mv_top], m, rsp.mv[rsp.mv_top]);
                    rsp.mv_muls++;
                }
            }
            rsp.mvp_dirty = 1;
            break;
        }
        case G_POPMTX:
            if (rsp.mv_top > 0)
                rsp.mv_top--;
            rsp.mvp_dirty = 1;
            break;

        case G_MOVEMEM:
            if (((w0 >> 16) & 0xff) == G_MV_VIEWPORT)
            {
                const Vp_t *vp = (const Vp_t *)w1;
                rsp.vp_scale[0] = vp->vscale[0] / 4.0f;
                rsp.vp_scale[1] = vp->vscale[1] / 4.0f;
                rsp.vp_trans[0] = vp->vtrans[0] / 4.0f;
                rsp.vp_trans[1] = vp->vtrans[1] / 4.0f;
            }
            break;

        case G_VTX:
            load_vertices((const Vtx *)w1, (int)((w0 >> 8) & 0xff), (int)((w0 >> 16) & 0xff));
            break;

        case G_TRI1:
            draw_triangle((int)((w1 >> 16) & 0xff), (int)((w1 >> 8) & 0xff), (int)(w1 & 0xff));
            break;
        case G_TRI2:
            draw_triangle((int)((w0 >> 16) & 0xff), (int)((w0 >> 8) & 0xff), (int)(w0 & 0xff));
            draw_triangle((int)((w1 >> 16) & 0xff), (int)((w1 >> 8) & 0xff), (int)(w1 & 0xff));
            break;
        case G_LINE3D:
            do_line3d((int)((w1 >> 16) & 0xff), (int)((w1 >> 8) & 0xff));
            break;

        case G_DL:
            if (((w0 >> 16) & 0xff) == G_DL_NOPUSH)
                dl = (Gfx *)w1;
            else if (depth < 16)
                run_dl((Gfx *)w1, depth + 1);
            break;
        case G_ENDDL:
            return;

        case G_TEXTURE:
            rsp.tex_on = (int)(w0 & 0xff);
            rsp.tex_tile = (int)((w0 >> 8) & 7);
            rsp.tex_scale_s = (float)((w1 >> 16) & 0xffff) / 65536.0f;
            rsp.tex_scale_t = (float)(w1 & 0xffff) / 65536.0f;
            if ((w1 >> 16 & 0xffff) == 0xffff) rsp.tex_scale_s = 1.0f;
            if ((w1 & 0xffff) == 0xffff) rsp.tex_scale_t = 1.0f;
            break;
        case G_SETGEOMETRYMODE:
            rsp.geom |= (uint32_t)w1;
            break;
        case G_CLEARGEOMETRYMODE:
            rsp.geom &= ~(uint32_t)w1;
            break;
        case G_MOVEWORD:
        {
            int index = (int)(w0 & 0xff);
            if (index == G_MW_FOG)
            {
                rsp.fog_mul = (int16_t)(w1 >> 16);
                rsp.fog_off = (int16_t)(w1 & 0xffff);
            }
            break;
        }
        case G_SETOTHERMODE_H:
        case G_SETOTHERMODE_L:
        {
            int sft = (int)((w0 >> 8) & 0xff);
            int len = (int)(w0 & 0xff);
            uint32_t mask = (len >= 32) ? 0xffffffffu : (((1u << len) - 1u) << sft);
            uint32_t *om = (op == G_SETOTHERMODE_H) ? &rdp.om_h : &rdp.om_l;
            *om = (*om & ~mask) | ((uint32_t)w1 & mask);
            break;
        }
        case G_RDPSETOTHERMODE:
            rdp.om_h = w0 & 0xffffff;
            rdp.om_l = (uint32_t)w1;
            break;

        case G_SETCOMBINE:
            rdp.cc_w0 = w0 & 0xffffff;
            rdp.cc_w1 = (uint32_t)w1;
            break;
        case G_SETENVCOLOR:
        case G_SETPRIMCOLOR:
        case G_SETBLENDCOLOR:
        case G_SETFOGCOLOR:
        {
            uint8_t *dst = op == G_SETENVCOLOR ? rdp.env : op == G_SETPRIMCOLOR ? rdp.prim :
                           op == G_SETBLENDCOLOR ? rdp.blend : rdp.fog;
            uint32_t c = (uint32_t)w1;
            dst[0] = (uint8_t)(c >> 24); dst[1] = (uint8_t)(c >> 16);
            dst[2] = (uint8_t)(c >> 8);  dst[3] = (uint8_t)c;
            if (op == G_SETPRIMCOLOR)
                rdp.prim_lod_frac = (float)(w0 & 0xff) / 255.0f;
            break;
        }
        case G_SETFILLCOLOR:
            rdp.fill = (uint32_t)w1;
            break;

        case G_SETTIMG:
            rdp.timg_fmt = (int)((w0 >> 21) & 7);
            rdp.timg_siz = (int)((w0 >> 19) & 3);
            rdp.timg_width = (int)(w0 & 0xfff) + 1;
            rdp.timg = (const uint8_t *)w1;
            break;
        case G_SETCIMG:
            rdp.cimg_siz = (int)((w0 >> 19) & 3);
            break;
        case G_SETTILE:
        {
            tile_t *t = &rdp.tiles[(w1 >> 24) & 7];
            t->fmt = (int)((w0 >> 21) & 7);
            t->siz = (int)((w0 >> 19) & 3);
            t->line = (int)((w0 >> 9) & 0x1ff);
            t->tmem = (int)(w0 & 0x1ff);
            t->palette = (int)((w1 >> 20) & 15);
            t->cmt = (int)((w1 >> 18) & 3);
            t->maskt = (int)((w1 >> 14) & 15);
            t->shiftt = (int)((w1 >> 10) & 15);
            t->cms = (int)((w1 >> 8) & 3);
            t->masks = (int)((w1 >> 4) & 15);
            t->shifts = (int)(w1 & 15);
            break;
        }
        case G_SETTILESIZE:
        {
            tile_t *t = &rdp.tiles[(w1 >> 24) & 7];
            t->uls = (int)((w0 >> 12) & 0xfff);
            t->ult = (int)(w0 & 0xfff);
            t->lrs = (int)((w1 >> 12) & 0xfff);
            t->lrt = (int)(w1 & 0xfff);
            break;
        }
        case G_LOADBLOCK:
        {
            int tile = (int)((w1 >> 24) & 7);
            tmem_load_block(tile, (int)((w0 >> 12) & 0xfff), (int)(w0 & 0xfff),
                            (int)((w1 >> 12) & 0xfff), (int)(w1 & 0xfff));
            break;
        }
        case G_LOADTILE:
        {
            int tile = (int)((w1 >> 24) & 7);
            tile_t *t = &rdp.tiles[tile];
            t->uls = (int)((w0 >> 12) & 0xfff);
            t->ult = (int)(w0 & 0xfff);
            t->lrs = (int)((w1 >> 12) & 0xfff);
            t->lrt = (int)(w1 & 0xfff);
            tmem_load_tile(tile, t->uls, t->ult, t->lrs, t->lrt);
            break;
        }
        case G_LOADTLUT:
            tmem_load_tlut((int)((w1 >> 24) & 7), (int)((w1 >> 14) & 0x3ff));
            break;

        case G_SETSCISSOR:
            rdp.scissor[0] = (int16_t)(((w0 >> 12) & 0xfff) / 4);
            rdp.scissor[1] = (int16_t)((w0 & 0xfff) / 4);
            rdp.scissor[2] = (int16_t)(((w1 >> 12) & 0xfff) / 4);
            rdp.scissor[3] = (int16_t)((w1 & 0xfff) / 4);
            break;

        case G_FILLRECT:
            do_fillrect(w0, (uint32_t)w1);
            break;
        case G_TEXRECT:
        case G_TEXRECTFLIP:
        {
            uint32_t h1 = (uint32_t)dl[0].words.w1;
            uint32_t h2 = (uint32_t)dl[1].words.w1;
            dl += 2;
            do_texrect(w0, (uint32_t)w1, h1, h2, op == G_TEXRECTFLIP);
            break;
        }
        case G_RDPHALF_1:
        case G_RDPHALF_2:
            break;

        default:
            break;
        }
    }
}

void GBI_SetOutputSize(int w, int h)
{
    if (w > 0 && h > 0)
    {
        out_w = w;
        out_h = h;
    }
}

void GBI_SetAspect(float aspect)
{
    if (aspect < 1.0f)
        aspect = 4.0f / 3.0f;
    vw = 240.0f * aspect;
    if (vw < 320.0f)
        vw = 320.0f;
    ndc_xscale = 320.0f / vw;
}

float GBI_VirtualWidth(void)
{
    return vw;
}

void GBI_SetLights(const D64GfxLight *lights, uint32_t count)
{
    frame_lights = lights;
    frame_light_count = count;
}

void GBI_SetWorldFog(const float color[4], float fog_near)
{
    memcpy(world_fog, color, sizeof(world_fog));
    world_fog_near = fog_near;
}

const gbistats_t *GBI_Stats(void)
{
    return &stats;
}

void GBI_RunFrame(Gfx *dl)
{
    D64GfxFrame frame;
    int i, j;

    if (!out_v)
    {
        out_v = (D64GfxVertex *)malloc(sizeof(D64GfxVertex) * MAX_VERTS_OUT);
        out_c = (D64GfxDrawCmd *)malloc(sizeof(D64GfxDrawCmd) * MAX_CMDS_OUT);
    }
    memset(&stats, 0, sizeof(stats));
    out_vn = 0;
    out_cn = 0;
    cur_cmd_valid = 0;
    have_camera = 0;
    world_first_cmd = 0xffffffffu;
    frame_counter++;

    reset_state();
    run_dl(dl, 0);
    flush_cmd();

    memset(&frame, 0, sizeof(frame));
    frame.vertices = out_v;
    frame.vertex_count = out_vn;
    frame.cmds = out_c;
    frame.cmd_count = out_cn;
    frame.has_camera = have_camera;
    if (have_camera)
    {
        /* column-major world->eye (our row-vector matrices transpose to
         * column-major storage without change) */
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
            {
                frame.view[i * 4 + j] = cam_mv[i][j];
                frame.proj[i * 4 + j] = rsp.proj[i][j] * (j == 0 ? ndc_xscale : 1.0f);
            }
        frame.cam_pos[0] = cam_inv[3][0];
        frame.cam_pos[1] = cam_inv[3][1];
        frame.cam_pos[2] = cam_inv[3][2];
    }
    frame.world_first_cmd = world_first_cmd == 0xffffffffu ? 0 : world_first_cmd;
    frame.lights = frame_lights;
    frame.light_count = frame_light_count;
    memcpy(frame.fog_color, world_fog, sizeof(world_fog));
    frame.fog_near = world_fog_near;
    frame.ambient_scale = 1.0f;
    frame.frame_index = frame_counter;
    frame.virtual_width = vw;

    stats.cmds = out_cn;
    stats.vertices = out_vn;
    {
        /* debug: D64_DUMPCMDS=<frame> prints that frame's draw commands */
        const char *dc = getenv("D64_DUMPCMDS");
        if (dc && (uint32_t)atoi(dc) == frame_counter)
        {
            uint32_t k;
            for (k = 0; k < out_cn; k++)
            {
                const D64GfxDrawCmd *c = &out_c[k];
                const D64GfxVertex *v = &out_v[c->first_vertex];
                fprintf(stderr, "cmd %u: n=%u tex=%u,%u flags=%x cc=%02x%02x%02x%02x/%02x%02x%02x%02x %02x%02x%02x%02x/%02x%02x%02x%02x "
                        "prim=%02x%02x%02x%02x env=%02x%02x%02x%02x shade0=%02x%02x%02x%02x pos0=%.2f,%.2f,%.2f,%.2f uv0=%.3f,%.3f\n",
                        k, c->vertex_count, c->tex[0], c->tex[1], c->flags,
                        c->cc[0], c->cc[1], c->cc[2], c->cc[3], c->cc[4], c->cc[5], c->cc[6], c->cc[7],
                        c->cc[8], c->cc[9], c->cc[10], c->cc[11], c->cc[12], c->cc[13], c->cc[14], c->cc[15],
                        c->prim[0], c->prim[1], c->prim[2], c->prim[3], c->env[0], c->env[1], c->env[2], c->env[3],
                        v->shade[0], v->shade[1], v->shade[2], v->shade[3], v->pos[0], v->pos[1], v->pos[2], v->pos[3],
                        v->uv[0], v->uv[1]);
            }
        }
    }
    d64gfx_render_frame(&frame);
}
