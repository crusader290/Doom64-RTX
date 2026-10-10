/*
 * rtlights.c - light sources for the ray traced renderer.
 *
 * Following the doom64-rt approach of turning painted light into real
 * emitters, every frame we look at the map objects and emit point lights for:
 *   - things whose current frame is FF_FULLBRIGHT (fireballs, plasma, BFG,
 *     explosions, lit pickups...), coloured per sprite;
 *   - always-lit decorations (candles, lamps, torches/flames);
 *   - the player's muzzle flash;
 *   - with doom64-rt data in a resource pack: things whose sprite has a
 *     lightColorHEX (items, projectiles, fire), and static lights in front of
 *     walls / under ceilings / over floors whose texture has an emissive map
 *     (coloured by that map's average).
 * Sector lighting is not converted: the baked sector colours stay as the
 * base lighting term in the RT shader. Lights are sent nearest first (the
 * shader shadows a limited number per pixel).
 *
 * World space matches the display list vertices: (x, height, -y) in map units.
 * Doom64-RTX PC port, GPLv3.
 */
#include "doomdef.h"
#include "p_local.h"
#include "gbi.h"
#include "rtlights.h"
#include "respack.h"
#include "native_addons.h"
#include "env_fx.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MAX_RT_LIGHTS 128

typedef struct {
    int   sprite;
    float r, g, b;
    float intensity;
    float radius;
    int   always;      /* lit even without FF_FULLBRIGHT */
} lightdef_t;

static const lightdef_t lightdefs[] = {
    /* projectiles */
    { SPR_BAL1, 1.00f, 0.55f, 0.20f, 1.4f, 260.0f, 0 }, /* imp fireball */
    { SPR_BAL2, 1.00f, 0.30f, 0.60f, 1.4f, 260.0f, 0 }, /* cacodemon */
    { SPR_BAL3, 1.00f, 0.55f, 0.20f, 1.4f, 260.0f, 0 },
    { SPR_BAL7, 0.40f, 1.00f, 0.30f, 1.4f, 280.0f, 0 }, /* baron/knight */
    { SPR_BAL8, 1.00f, 0.50f, 0.20f, 1.4f, 260.0f, 0 },
    { SPR_APLS, 0.40f, 1.00f, 0.40f, 1.2f, 240.0f, 0 }, /* arachnotron plasma */
    { SPR_MANF, 1.00f, 0.50f, 0.15f, 1.6f, 300.0f, 0 }, /* mancubus */
    { SPR_TRCR, 1.00f, 0.45f, 0.25f, 1.2f, 240.0f, 0 }, /* revenant */
    { SPR_DART, 1.00f, 0.40f, 0.20f, 1.0f, 200.0f, 0 },
    { SPR_RBAL, 1.00f, 0.45f, 0.20f, 1.4f, 260.0f, 0 },
    { SPR_MISL, 1.00f, 0.60f, 0.25f, 1.8f, 320.0f, 0 }, /* rocket / explosion */
    { SPR_PLSS, 0.35f, 0.55f, 1.00f, 1.3f, 240.0f, 0 }, /* plasma rifle */
    { SPR_BFS1, 0.35f, 1.00f, 0.35f, 2.0f, 400.0f, 0 }, /* BFG ball */
    { SPR_BFE2, 0.35f, 1.00f, 0.35f, 2.0f, 400.0f, 0 },
    { SPR_LASS, 1.00f, 0.20f, 0.15f, 1.5f, 260.0f, 0 }, /* unmaker */
    { SPR_FIRE, 1.00f, 0.55f, 0.20f, 1.6f, 300.0f, 0 },
    { SPR_TFOG, 0.40f, 1.00f, 0.50f, 1.2f, 260.0f, 0 }, /* teleport fog */
    { SPR_PUFF, 1.00f, 0.80f, 0.50f, 0.6f, 120.0f, 0 },
    /* decorations, always lit */
    { SPR_CAND, 1.00f, 0.70f, 0.35f, 0.9f, 180.0f, 1 },
    { SPR_LMP1, 1.00f, 0.90f, 0.70f, 1.2f, 300.0f, 1 },
    { SPR_LMP2, 1.00f, 0.90f, 0.70f, 1.2f, 300.0f, 1 },
    { SPR_BFLM, 0.35f, 0.55f, 1.00f, 1.3f, 280.0f, 1 }, /* blue flame */
    { SPR_RFLM, 1.00f, 0.35f, 0.20f, 1.3f, 280.0f, 1 }, /* red flame */
    { SPR_YFLM, 1.00f, 0.75f, 0.30f, 1.3f, 280.0f, 1 }, /* yellow flame */
    /* pickups with glowing frames */
    { SPR_SOUL, 0.40f, 0.60f, 1.00f, 0.8f, 160.0f, 0 },
    { SPR_MEGA, 0.90f, 0.90f, 1.00f, 0.8f, 160.0f, 0 },
    { SPR_PINV, 0.60f, 1.00f, 0.60f, 0.6f, 140.0f, 0 },
    { SPR_BON1, 0.40f, 0.60f, 1.00f, 0.4f, 100.0f, 0 },
};

static D64GfxLight lights_buf[MAX_RT_LIGHTS];
static float light_dist[MAX_RT_LIGHTS];

static const lightdef_t *find_def(int sprite)
{
    size_t i;
    for (i = 0; i < sizeof(lightdefs) / sizeof(lightdefs[0]); i++)
        if (lightdefs[i].sprite == sprite)
            return &lightdefs[i];
    return NULL;
}

static int add_light(float x, float y, float z, float r, float g, float b, float intensity,
                      float radius, float px, float py, float pz, int *count)
{
    float dx = x - px, dy = y - py, dz = z - pz;
    float d = dx * dx + dy * dy + dz * dz;
    int slot = *count;

    if (slot >= MAX_RT_LIGHTS)
    {
        /* keep the closest lights: replace the farthest one if this is nearer */
        int i, far = 0;
        for (i = 1; i < MAX_RT_LIGHTS; i++)
            if (light_dist[i] > light_dist[far])
                far = i;
        if (d >= light_dist[far])
            return -1;
        slot = far;
    }
    else
        (*count)++;

    memset(&lights_buf[slot],0,sizeof(lights_buf[slot]));
    lights_buf[slot].pos[0] = x;
    lights_buf[slot].pos[1] = y;
    lights_buf[slot].pos[2] = z;
    lights_buf[slot].radius = radius;
    lights_buf[slot].color[0] = r;
    lights_buf[slot].color[1] = g;
    lights_buf[slot].color[2] = b;
    lights_buf[slot].intensity = intensity;
    light_dist[slot] = d;
    return slot;
}

/* ---- static lights from emissive world textures (rebuilt per level) ---- */

typedef struct { float x, y, z, r, g, b, intensity, radius; } worldlight_t;
static worldlight_t *wlights;
static int nwlights, cwlights;
static const void *wl_sectors;
static int wl_map = -1, wl_numsectors = -1;

static void add_wlight(float x, float y, float z, const float rgb[3], float strength, float radius)
{
    worldlight_t *l;
    if (nwlights == cwlights)
    {
        cwlights = cwlights ? cwlights * 2 : 256;
        wlights = realloc(wlights, sizeof(worldlight_t) * (size_t)cwlights);
    }
    l = &wlights[nwlights++];
    l->x = x; l->y = y; l->z = z;
    l->r = rgb[0]; l->g = rgb[1]; l->b = rgb[2];
    l->intensity = 0.9f * strength;
    l->radius = radius;
}

static int tex_glow(int texindex, float rgb[3], float *strength)
{
    char name[9];
    if (texindex < 0 || texindex >= numtextures)
        return 0;
    W_PCLumpName(firsttex + texindex, name);
    return ResPack_EmissiveGlow(name, rgb, strength);
}

/* one light every 128 units along a wall part, 8 units in front of it */
static void wall_lights(line_t *li, side_t *sd, int texindex, fixed_t zlo, fixed_t zhi, int back)
{
    float rgb[3], strength, x1, y1, x2, y2, nx, ny, len, zmid;
    int n, k;
    if (zhi <= zlo || !tex_glow(texindex, rgb, &strength))
        return;
    (void)sd;
    x1 = (float)(li->v1->x >> FRACBITS); y1 = (float)(li->v1->y >> FRACBITS);
    x2 = (float)(li->v2->x >> FRACBITS); y2 = (float)(li->v2->y >> FRACBITS);
    len = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    if (len < 1.0f)
        return;
    /* front side normal (right of v1->v2) */
    nx = (y2 - y1) / len;
    ny = -(x2 - x1) / len;
    if (back)
    {
        nx = -nx;
        ny = -ny;
    }
    zmid = (float)((zlo + zhi) >> 1) / 65536.0f;
    n = (int)(len / 128.0f) + 1;
    for (k = 0; k < n; k++)
    {
        float t = ((float)k + 0.5f) / (float)n;
        float x = x1 + (x2 - x1) * t + nx * 8.0f;
        float y = y1 + (y2 - y1) * t + ny * 8.0f;
        float h = (float)((zhi - zlo) >> FRACBITS);
        add_wlight(x, zmid, -y, rgb, strength, 96.0f + (h > 128.0f ? 128.0f : h));
    }
}

static void build_world_lights(void)
{
    int i;
    nwlights = 0;
    for (i = 0; i < numlines; i++)
    {
        line_t *li = &lines[i];
        int s;
        for (s = 0; s < 2; s++)
        {
            side_t *sd;
            sector_t *fs, *bs;
            if (li->sidenum[s] < 0)
                continue;
            sd = &sides[li->sidenum[s]];
            fs = s == 0 ? li->frontsector : li->backsector;
            bs = s == 0 ? li->backsector : li->frontsector;
            if (!fs)
                continue;
            if (!bs)
                wall_lights(li, sd, sd->midtexture, fs->floorheight, fs->ceilingheight, s);
            else
            {
                if (bs->ceilingheight < fs->ceilingheight)
                    wall_lights(li, sd, sd->toptexture, bs->ceilingheight, fs->ceilingheight, s);
                if (bs->floorheight > fs->floorheight)
                    wall_lights(li, sd, sd->bottomtexture, fs->floorheight, bs->floorheight, s);
            }
        }
    }
    for (i = 0; i < numsectors; i++)
    {
        sector_t *sec = &sectors[i];
        float rgb[3], strength, minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, ext;
        int k, has_c, has_f;
        float crgb[3], cs;
        has_c = sec->ceilingpic >= 0 && tex_glow(sec->ceilingpic, crgb, &cs);
        has_f = tex_glow(sec->floorpic, rgb, &strength);
        if (!has_c && !has_f)
            continue;
        for (k = 0; k < sec->linecount; k++)
        {
            line_t *li = sec->lines[k];
            float xs[2] = { (float)(li->v1->x >> FRACBITS), (float)(li->v2->x >> FRACBITS) };
            float ys[2] = { (float)(li->v1->y >> FRACBITS), (float)(li->v2->y >> FRACBITS) };
            int j;
            for (j = 0; j < 2; j++)
            {
                if (xs[j] < minx) minx = xs[j];
                if (xs[j] > maxx) maxx = xs[j];
                if (ys[j] < miny) miny = ys[j];
                if (ys[j] > maxy) maxy = ys[j];
            }
        }
        if (minx > maxx)
            continue;
        ext = (maxx - minx) > (maxy - miny) ? (maxx - minx) : (maxy - miny);
        ext = ext * 0.75f + 64.0f;
        if (ext > 512.0f)
            ext = 512.0f;
        if (has_c)
            add_wlight((minx + maxx) * 0.5f, (float)(sec->ceilingheight >> FRACBITS) - 8.0f,
                       -(miny + maxy) * 0.5f, crgb, cs, ext);
        if (has_f)
            add_wlight((minx + maxx) * 0.5f, (float)(sec->floorheight >> FRACBITS) + 8.0f,
                       -(miny + maxy) * 0.5f, rgb, strength, ext);
    }
    if (nwlights)
        I_PCLog("RT: %d static lights from emissive textures", nwlights);
}

void RT_CollectLights(void)
{
    mobj_t *mo;
    player_t *pl = &players[0];
    int count = 0;
    float px = 0, py = 0, pz = 0;

    if (!pl->mo || !mobjhead.next) /* only called from R_RenderPlayerView */
    {
        GBI_SetLights(NULL, 0);
        return;
    }
    px = (float)(pl->mo->x >> FRACBITS);
    py = (float)(pl->viewz >> FRACBITS);
    pz = (float)-(pl->mo->y >> FRACBITS);

    if (wl_map != gamemap || wl_sectors != (const void *)sectors || wl_numsectors != numsectors)
    {
        wl_map = gamemap;
        wl_sectors = sectors;
        wl_numsectors = numsectors;
        build_world_lights();
    }
    {
        int i;
        for (i = 0; i < nwlights; i++)
        {
            worldlight_t *l = &wlights[i];
            float dx = l->x - px, dz = l->z - pz;
            if (dx * dx + dz * dz > 1536.0f * 1536.0f)
                continue;
            add_light(l->x, l->y, l->z, l->r, l->g, l->b, l->intensity, l->radius, px, py, pz, &count);
        }
    }

    for (mo = mobjhead.next; mo && mo != &mobjhead; mo = mo->next)
    {
        const lightdef_t *def;
        int bright;
        if (!mo->state)
            continue;
        if (mo->sprite < NUMSPRITES && sprites[mo->sprite].spriteframes &&
            (mo->frame & FF_FRAMEMASK) < sprites[mo->sprite].numframes)
        {
            /* doom64-rt light colour for this exact sprite frame */
            char name[9];
            float rgb[3], strength, radius;
            W_PCLumpName(sprites[mo->sprite].spriteframes[mo->frame & FF_FRAMEMASK].lump[0], name);
            if (ResPack_LightFor(name, rgb, &strength, &radius))
            {
                float in = strength * 2.0f;
                if (in < 0.3f) in = 0.3f;
                if (in > 1.5f) in = 1.5f;
                add_light((float)(mo->x >> FRACBITS), (float)((mo->z + mo->height / 2) >> FRACBITS),
                          (float)-(mo->y >> FRACBITS), rgb[0], rgb[1], rgb[2], in,
                          radius > 0.0f ? radius : 160.0f, px, py, pz, &count);
                continue;
            }
        }
        def = find_def(mo->sprite);
        if (!def)
            continue;
        bright = (mo->frame & FF_FULLBRIGHT) != 0;
        if (!bright && !def->always)
            continue;
        add_light((float)(mo->x >> FRACBITS),
                  (float)((mo->z + mo->height / 2) >> FRACBITS),
                  (float)-(mo->y >> FRACBITS),
                  def->r, def->g, def->b, def->intensity, def->radius, px, py, pz, &count);
    }

    for(int fx=0;fx<8;fx++) {
        float position[3],color[3];if(!PCEnv_Light(fx,position,color)) break;
        add_light(position[0],position[1],position[2],color[0],color[1],color[2],.35f,32,px,py,pz,&count);
    }
    /* muzzle flash */
    if (pl->psprites[ps_flash].state)
    {
        angle_t an = pl->mo->angle >> ANGLETOFINESHIFT;
        float fx = px + (float)(finecosine[an] >> 10) / 64.0f * 24.0f;
        float fz = pz - (float)(finesine[an] >> 10) / 64.0f * 24.0f;
        add_light(fx, py - 8.0f, fz, 1.0f, 0.85f, 0.55f, 1.6f, 360.0f, px, py, pz, &count);
    }

    /* Native port of the flashlight: one real shadowed cone, aimed with the
     * interpolated camera. Kept nearest to reserve a shadow-budget slot. */
    float beam=PCAddon_Flashlight();
    if(beam>0 && cameratarget==pl->mo) {
        angle_t yaw=pl->mo->angle>>ANGLETOFINESHIFT;
        angle_t pitch=((angle_t)(pl->pc_pitch+pl->recoilpitch)>>ANGLETOFINESHIFT)&FINEMASK;
        int slot=add_light(px,py-3,pz,1,.9f,.72f,2.8f*beam,640,px,py,pz,&count);
        if(slot>=0) {
        lights_buf[slot].direction[0]=finecosine[yaw]/(float)FRACUNIT*finecosine[pitch]/(float)FRACUNIT;
        lights_buf[slot].direction[1]=finesine[pitch]/(float)FRACUNIT;
        lights_buf[slot].direction[2]=-finesine[yaw]/(float)FRACUNIT*finecosine[pitch]/(float)FRACUNIT;
        lights_buf[slot].cos_outer=.9063078f; /* 25 degree cone */
        lights_buf[slot].cos_inner=.9659258f; /* 15 degree core */
        }
    }

    /* nearest first: the shader shadows a limited number of lights per pixel */
    {
        D64GfxLight tmp[MAX_RT_LIGHTS];
        int order[MAX_RT_LIGHTS], i, j;
        for (i = 0; i < count; i++)
            order[i] = i;
        for (i = 1; i < count; i++) /* insertion sort, <= 128 entries */
        {
            int v = order[i];
            for (j = i - 1; j >= 0 && light_dist[order[j]] > light_dist[v]; j--)
                order[j + 1] = order[j];
            order[j + 1] = v;
        }
        for (i = 0; i < count; i++)
            tmp[i] = lights_buf[order[i]];
        memcpy(lights_buf, tmp, sizeof(D64GfxLight) * (size_t)count);
    }
    GBI_SetLights(lights_buf, (uint32_t)count);
}

/* number of map objects, for the level-load log line */
int nummobjs_pc(void)
{
    int n = 0;
    mobj_t *mo;
    for (mo = mobjhead.next; mo && mo != &mobjhead; mo = mo->next)
        n++;
    return n;
}
