/*
 * interp.c - 60/120 fps rendering for a 30 Hz game.
 *
 * The game logic keeps running at the N64's 30 tics per second. Before each
 * tic the positions of things, sector floors/ceilings, the view height and
 * the weapon sprite offsets are remembered; frames drawn between tics show
 * a blend of the previous and the current tic (one tic behind, as every
 * interpolating Doom port does). The player's own view angle and pitch use
 * the current tic plus the mouse movement not yet consumed by the game, so
 * looking around stays immediate.
 *
 * Interpolated values are written into the live structures only while a
 * frame is being drawn and restored right after (I_PCInterpBegin/End).
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>

#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "config.h"
#include "interp.h"

extern mobj_t mobjhead;

#define D_abs(x) ((x) < 0 ? -(x) : (x))

int I_PCPeekMouseTurn(void);
int I_PCPeekMousePitch(void);

static unsigned gen;          /* snapshot generation */
static Uint64 tic_start_ns;
static int applied;

/* saved live values while a blended frame is drawn */
typedef struct { mobj_t *mo; fixed_t x, y, z; angle_t angle; } savedmobj_t;
static savedmobj_t *saved_mobjs;
static int saved_mobj_count, saved_mobj_cap;
static fixed_t *saved_sectors;
static int saved_sector_cap;
static fixed_t saved_viewz;
static angle_t saved_pitch;
static fixed_t saved_sx[NUMPSPRITES], saved_sy[NUMPSPRITES];

void (*I_PCCurrentDrawer)(void);

static int level_active(void)
{
    return I_PCCurrentDrawer == P_Drawer && mobjhead.next != NULL;
}

int I_PCInterpEnabled(void)
{
    return pc_config.fps > 30;
}

double I_PCInterpTicFraction(void)
{
    if (SDL_getenv("D64_FIXED_TIMESTEP"))
        return 0.0; /* deterministic tests: main frames show the previous tic */
    double f = (double)(SDL_GetTicksNS() - tic_start_ns) / (1e9 / 30.0);
    return f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
}

void I_PCInterpSnapshot(void)
{
    mobj_t *mo;
    player_t *p = &players[0];
    int i;

    tic_start_ns = SDL_GetTicksNS();
    if (!level_active())
        return;
    gen++;
    if (gen == 0)
        gen = 1;

    for (mo = mobjhead.next; mo && mo != &mobjhead; mo = mo->next)
    {
        mo->pc_ox = mo->x;
        mo->pc_oy = mo->y;
        mo->pc_oz = mo->z;
        mo->pc_oangle = mo->angle;
        mo->pc_ogen = gen;
    }
    for (i = 0; i < numsectors; i++)
    {
        sectors[i].pc_ofloor = sectors[i].floorheight;
        sectors[i].pc_oceil = sectors[i].ceilingheight;
        sectors[i].pc_ogen = gen;
    }
    p->pc_oviewz = p->viewz;
    for (i = 0; i < NUMPSPRITES; i++)
    {
        p->pc_osx[i] = p->psprites[i].sx;
        p->pc_osy[i] = p->psprites[i].sy;
    }
    p->pc_ogen = gen;
}

static fixed_t lerp(fixed_t a, fixed_t b, fixed_t f)
{
    return a + FixedMul(b - a, f);
}

static angle_t lerp_angle(angle_t a, angle_t b, fixed_t f)
{
    int d = (int)(b - a);
    return a + (angle_t)FixedMul(d, f);
}

void I_PCInterpBegin(double frac)
{
    mobj_t *mo;
    player_t *p = &players[0];
    fixed_t f;
    int i, n;

    if (applied || !I_PCInterpEnabled() || !level_active())
        return;
    if (frac < 0.0)
        frac = 0.0;
    if (frac > 1.0)
        frac = 1.0;
    f = (fixed_t)(frac * 65536.0);
    applied = 1;

    /* things */
    saved_mobj_count = 0;
    for (mo = mobjhead.next; mo && mo != &mobjhead; mo = mo->next)
    {
        if (saved_mobj_count == saved_mobj_cap)
        {
            saved_mobj_cap = saved_mobj_cap ? saved_mobj_cap * 2 : 1024;
            saved_mobjs = SDL_realloc(saved_mobjs, sizeof(*saved_mobjs) * (size_t)saved_mobj_cap);
        }
        saved_mobjs[saved_mobj_count].mo = mo;
        saved_mobjs[saved_mobj_count].x = mo->x;
        saved_mobjs[saved_mobj_count].y = mo->y;
        saved_mobjs[saved_mobj_count].z = mo->z;
        saved_mobjs[saved_mobj_count].angle = mo->angle;
        saved_mobj_count++;

        if (mo->pc_ogen != gen)
            continue; /* spawned this tic */
        /* teleports and other jumps snap */
        if (D_abs(mo->x - mo->pc_ox) > 128 * FRACUNIT || D_abs(mo->y - mo->pc_oy) > 128 * FRACUNIT ||
            D_abs(mo->z - mo->pc_oz) > 128 * FRACUNIT)
            continue;
        mo->x = lerp(mo->pc_ox, mo->x, f);
        mo->y = lerp(mo->pc_oy, mo->y, f);
        mo->z = lerp(mo->pc_oz, mo->z, f);
        if (!mo->player)
            mo->angle = lerp_angle(mo->pc_oangle, mo->angle, f);
    }

    /* floors and ceilings */
    n = numsectors;
    if (n * 2 > saved_sector_cap)
    {
        saved_sector_cap = n * 2;
        saved_sectors = SDL_realloc(saved_sectors, sizeof(fixed_t) * (size_t)saved_sector_cap);
    }
    for (i = 0; i < n; i++)
    {
        sector_t *s = &sectors[i];
        saved_sectors[i * 2] = s->floorheight;
        saved_sectors[i * 2 + 1] = s->ceilingheight;
        if (s->pc_ogen == gen)
        {
            s->floorheight = lerp(s->pc_ofloor, s->floorheight, f);
            s->ceilingheight = lerp(s->pc_oceil, s->ceilingheight, f);
        }
    }

    /* the local player's view */
    saved_viewz = p->viewz;
    saved_pitch = p->pc_pitch;
    for (i = 0; i < NUMPSPRITES; i++)
    {
        saved_sx[i] = p->psprites[i].sx;
        saved_sy[i] = p->psprites[i].sy;
    }
    if (p->pc_ogen == gen && p->mo && D_abs(p->viewz - p->pc_oviewz) < 128 * FRACUNIT)
    {
        p->viewz = lerp(p->pc_oviewz, p->viewz, f);
        for (i = 0; i < NUMPSPRITES; i++)
        {
            p->psprites[i].sx = lerp(p->pc_osx[i], p->psprites[i].sx, f);
            p->psprites[i].sy = lerp(p->pc_osy[i], p->psprites[i].sy, f);
        }
    }
    if (p->mo && !demoplayback && p->playerstate == PST_LIVE && !gamepaused)
    {
        int pitch;
        p->mo->angle += (angle_t)I_PCPeekMouseTurn();
        if (pc_config.mouselook)
        {
            pitch = (int)p->pc_pitch + I_PCPeekMousePitch();
            if (pitch > PC_MAXPITCH)
                pitch = PC_MAXPITCH;
            if (pitch < -PC_MAXPITCH)
                pitch = -PC_MAXPITCH;
            p->pc_pitch = (angle_t)pitch;
        }
    }
}

void I_PCInterpEnd(void)
{
    player_t *p = &players[0];
    int i;

    if (!applied)
        return;
    applied = 0;

    for (i = 0; i < saved_mobj_count; i++)
    {
        mobj_t *mo = saved_mobjs[i].mo;
        mo->x = saved_mobjs[i].x;
        mo->y = saved_mobjs[i].y;
        mo->z = saved_mobjs[i].z;
        mo->angle = saved_mobjs[i].angle;
    }
    saved_mobj_count = 0;
    for (i = 0; i < numsectors && i * 2 < saved_sector_cap; i++)
    {
        sectors[i].floorheight = saved_sectors[i * 2];
        sectors[i].ceilingheight = saved_sectors[i * 2 + 1];
    }
    p->viewz = saved_viewz;
    p->pc_pitch = saved_pitch;
    for (i = 0; i < NUMPSPRITES; i++)
    {
        p->psprites[i].sx = saved_sx[i];
        p->psprites[i].sy = saved_sy[i];
    }
}
