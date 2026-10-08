/*
 * rtlights.c - light sources for the ray traced renderer.
 *
 * Following the doom64-rt approach of turning painted light into real
 * emitters, every frame we look at the map objects and emit point lights for:
 *   - things whose current frame is FF_FULLBRIGHT (fireballs, plasma, BFG,
 *     explosions, lit pickups...), coloured per sprite;
 *   - always-lit decorations (candles, lamps, torches/flames);
 *   - the player's muzzle flash.
 * Sector lighting is not converted: the baked sector colours stay as the
 * base lighting term in the RT shader.
 *
 * World space matches the display list vertices: (x, height, -y) in map units.
 * Doom64-RTX PC port, GPLv3.
 */
#include "doomdef.h"
#include "p_local.h"
#include "gbi.h"
#include "rtlights.h"

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

static void add_light(float x, float y, float z, float r, float g, float b, float intensity,
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
            return;
        slot = far;
    }
    else
        (*count)++;

    lights_buf[slot].pos[0] = x;
    lights_buf[slot].pos[1] = y;
    lights_buf[slot].pos[2] = z;
    lights_buf[slot].radius = radius;
    lights_buf[slot].color[0] = r;
    lights_buf[slot].color[1] = g;
    lights_buf[slot].color[2] = b;
    lights_buf[slot].intensity = intensity;
    light_dist[slot] = d;
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

    for (mo = mobjhead.next; mo && mo != &mobjhead; mo = mo->next)
    {
        const lightdef_t *def;
        int bright;
        if (!mo->state)
            continue;
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

    /* muzzle flash */
    if (pl->psprites[ps_flash].state)
    {
        angle_t an = pl->mo->angle >> ANGLETOFINESHIFT;
        float fx = px + (float)(finecosine[an] >> 10) / 64.0f * 24.0f;
        float fz = pz - (float)(finesine[an] >> 10) / 64.0f * 24.0f;
        add_light(fx, py - 8.0f, fz, 1.0f, 0.85f, 0.55f, 1.6f, 360.0f, px, py, pz, &count);
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
