/* Bounded, cosmetic gore. No game RNG, mobjs, save-format changes or damage.
 * World axes at the display-list boundary are (x, height, -y).
 * Doom64-RTX PC port, GPLv3. */
#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "config.h"
#include "gore.h"
#include "native_addons.h"

#define PARTICLES 128
#define STAINS 128
typedef struct {
    float x, y, z, ox, oy, oz, vx, vy, vz, size;
    int life, total, gib;
    uint32_t color;
} gore_particle_t;
typedef struct {
    float x, y, z, nx, ny, size;
    sector_t *sector;
    int life, total;
    uint32_t color;
} gore_stain_t;
static gore_particle_t particles[PARTICLES];
static gore_stain_t stains[STAINS];
static unsigned particle_cursor, stain_cursor;
static uint32_t visual_rng;
static gore_particle_t *moving;
static float move_x, move_y;

static int enabled(void)
{
    return pc_config.gore && !demoplayback && !demorecording;
}
static float random01(void)
{
    visual_rng ^= visual_rng << 13;
    visual_rng ^= visual_rng >> 17;
    visual_rng ^= visual_rng << 5;
    return (visual_rng & 65535u) / 65535.0f;
}
static fixed_t fixed(float v) { return (fixed_t)(v * FRACUNIT); }

void I_PCGoreReset(void)
{
    memset(particles, 0, sizeof(particles));
    memset(stains, 0, sizeof(stains));
    particle_cursor = stain_cursor = 0;
    visual_rng = 0x64b100du;
}

static void stain(float x, float y, float z, float nx, float ny,
                  float size, sector_t *sector)
{
    int life=PCAddon_GoreLife();
    gore_stain_t *s = &stains[stain_cursor++ % PCAddon_GoreLimit()];
    *s = (gore_stain_t){x, y, z, nx, ny, size, sector, life ? life : -1, life,
                       moving ? moving->color : 0x730403};
}

void I_PCGoreDamage(mobj_t *target, mobj_t *inflictor, int damage)
{
    int i, count, lethal, gib;
    float dx = 0, dy = 0, length, force;
    if (!enabled() || damage <= 0 || (target->flags & MF_NOBLOOD)) return;
    lethal = damage >= target->health;
    gib = lethal && ((int64_t)target->health - damage < -target->info->spawnhealth);
    count = 4 + (damage > 40 ? 8 : damage / 5) + (lethal ? 12 : 0);
    if (inflictor) {
        dx = (target->x - inflictor->x) / (float)FRACUNIT;
        dy = (target->y - inflictor->y) / (float)FRACUNIT;
        length = sqrtf(dx * dx + dy * dy);
        if (length > 0.01f) { dx /= length; dy /= length; }
    }
    force = 1.5f + (damage > 60 ? 60 : damage) * 0.07f;
    for (i = 0; i < count; i++) {
        gore_particle_t *p = &particles[particle_cursor++ % PARTICLES];
        memset(p, 0, sizeof(*p));
        p->x = target->x / (float)FRACUNIT + (random01() - .5f) * 6;
        p->y = target->y / (float)FRACUNIT + (random01() - .5f) * 6;
        p->z = (target->z + target->height / 2) / (float)FRACUNIT;
        p->ox = p->x; p->oy = p->y; p->oz = p->z;
        p->vx = dx * force + (random01() - .5f) * force * 2;
        p->vy = dy * force + (random01() - .5f) * force * 2;
        p->vz = 1.5f + random01() * (lethal ? 7 : 4);
        p->gib = gib && i < 6;
        p->color=PCAddon_BloodColor(target->type);
        p->size = p->gib ? 2 + random01() * 2 : .6f + random01();
        p->life = p->total = p->gib ? 90 : 60;
    }
}

static boolean hit_wall(intercept_t *in)
{
    line_t *line = in->d.line;
    float nx, ny, length, frac;
    if (line->backsector) {
        fixed_t floor = line->frontsector->floorheight > line->backsector->floorheight ?
                        line->frontsector->floorheight : line->backsector->floorheight;
        fixed_t ceil = line->frontsector->ceilingheight < line->backsector->ceilingheight ?
                       line->frontsector->ceilingheight : line->backsector->ceilingheight;
        if (fixed(moving->z) > floor && fixed(moving->z) < ceil) return true;
    }
    nx = -line->dy / (float)FRACUNIT; ny = line->dx / (float)FRACUNIT;
    length = sqrtf(nx * nx + ny * ny);
    if (length < .001f) return true;
    nx /= length; ny /= length;
    if (nx * moving->vx + ny * moving->vy > 0) { nx = -nx; ny = -ny; }
    frac = in->frac / (float)FRACUNIT;
    stain(moving->x + (move_x - moving->x) * frac + nx * .3f,
          moving->y + (move_y - moving->y) * frac + ny * .3f,
          moving->z, nx, ny, moving->size * 2.5f, NULL);
    moving->life = 0;
    return false;
}

void I_PCGoreTick(void)
{
    int i;
    if (!enabled()) { I_PCGoreReset(); return; }
    int limit=PCAddon_GoreLimit();
    for (i = 0; i < STAINS; i++) {
        if(i>=limit) stains[i].life=0;
        else if (stains[i].life>0) stains[i].life--;
    }
    for (i = 0; i < PARTICLES; i++) {
        gore_particle_t *p = &particles[i];
        sector_t *sector;
        float floor, ceiling;
        if (!p->life) continue;
        p->life--;
        p->ox = p->x; p->oy = p->y; p->oz = p->z;
        move_x = p->x + p->vx; move_y = p->y + p->vy; moving = p;
        if (!P_PathTraverse(fixed(p->x), fixed(p->y), fixed(move_x), fixed(move_y),
                            PT_ADDLINES, hit_wall)) continue;
        p->x = move_x; p->y = move_y; p->z += p->vz; p->vz -= .65f;
        sector = R_PointInSubsector(fixed(p->x), fixed(p->y))->sector;
        floor = sector->floorheight / (float)FRACUNIT;
        ceiling = sector->ceilingheight / (float)FRACUNIT;
        if (p->z <= floor + .5f) {
            stain(p->x, p->y, floor + .3f, 0, 0, p->size * 3, sector);
            p->life = 0;
        } else if (p->z >= ceiling - .5f) {
            p->z = ceiling - .5f; p->vz = -fabsf(p->vz) * .25f;
        }
    }
    moving = NULL;
}

static void vertex(int i, float x, float y, float z, int color)
{
    memset(&VTX1[i], 0, sizeof(VTX1[i]));
    VTX1[i].v.ob[0] = (short)x;
    VTX1[i].v.ob[1] = (short)z;
    VTX1[i].v.ob[2] = (short)-y;
    memcpy(VTX1[i].v.cn, &color, sizeof(color));
}
static int blood_color(sector_t *sector, int life, int gib, uint32_t rgb)
{
    int light = sector->lightlevel;
    int alpha = life>=0 && life < 60 ? life * 255 / 60 : 255;
    if(gib && rgb==0x730403) rgb=0xaa1814;
    return PACKRGBA(((rgb>>16)&255)*(light+80)/335,
                    ((rgb>>8)&255)*(light+80)/335,(rgb&255)*(light+80)/335,alpha);
}

void I_PCGoreDraw(float fraction)
{
    int i, j;
    if (!enabled()) return;
    gDPPipeSync(GFX1++);
    gSPTexture(GFX1++, 0, 0, 0, 0, 0);
    gDPSetCombineMode(GFX1++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(GFX1++, G_RM_XLU_SURF | Z_CMP, G_RM_XLU_SURF2 | Z_CMP);
    gDPSetAlphaCompare(GFX1++, G_AC_NONE);
    gSPClearGeometryMode(GFX1++, G_CULL_BOTH);
    for (i = 0; i < STAINS; i++) {
        gore_stain_t *s = &stains[i];
        sector_t *sector;
        float z;
        int color;
        if (!s->life) continue;
        sector = s->sector ? s->sector : R_PointInSubsector(fixed(s->x), fixed(s->y))->sector;
        z = s->sector ? sector->floorheight / (float)FRACUNIT + 1 : s->z;
        color = blood_color(sector, s->life, 0, s->color);
        I_CheckGFX();
        vertex(0, s->x, s->y, z, color);
        for (j = 0; j < 8; j++) {
            float a = j * 6.2831853f / 8;
            float u = cosf(a) * s->size * (j & 1 ? .7f : 1);
            float v = sinf(a) * s->size;
            if (s->sector) vertex(j + 1, s->x + u, s->y + v, z, color);
            else vertex(j + 1, s->x + s->ny * u, s->y - s->nx * u, z + v, color);
        }
        gSPVertex(GFX1++, VTX1, 9, 0);
        for (j = 0; j < 8; j++) gSP1Triangle(GFX1++, 0, j + 1, (j + 1) % 8 + 1, 0);
        VTX1 += 9;
    }
    for (i = 0; i < PARTICLES; i++) {
        gore_particle_t *p = &particles[i];
        float x, y, z, sx, sy;
        int color;
        if (!p->life) continue;
        x = p->ox + (p->x - p->ox) * fraction;
        y = p->oy + (p->y - p->oy) * fraction;
        z = p->oz + (p->z - p->oz) * fraction;
        color = blood_color(R_PointInSubsector(fixed(x), fixed(y))->sector, p->life, p->gib,p->color);
        sx = viewsin / (float)FRACUNIT * p->size;
        sy = -viewcos / (float)FRACUNIT * p->size;
        I_CheckGFX();
        vertex(0, x - sx, y - sy, z, color);
        vertex(1, x + sx, y + sy, z, color);
        vertex(2, x, y, z + p->size * 2, color);
        if (p->gib) {
            vertex(3, x + sy, y - sx, z - p->size, color);
            gSPVertex(GFX1++, VTX1, 4, 0);
            gSP2Triangles(GFX1++, 0, 1, 2, 0, 0, 3, 1, 0);
            gSP2Triangles(GFX1++, 0, 2, 3, 0, 1, 3, 2, 0);
            VTX1 += 4;
        } else {
            gSPVertex(GFX1++, VTX1, 3, 0);
            gSP1Triangle(GFX1++, 0, 1, 2, 0);
            VTX1 += 3;
        }
    }
    gDPPipeSync(GFX1++);
    gSPTexture(GFX1++, (512 << 6), (512 << 6), 0, 0, 1);
    gDPSetCombineMode(GFX1++, G_CC_D64COMB07, G_CC_D64COMB08);
    gDPSetRenderMode(GFX1++, G_RM_FOG_SHADE_A, G_RM_TEX_EDGE2);
    gDPSetAlphaCompare(GFX1++, G_AC_THRESHOLD);
}
