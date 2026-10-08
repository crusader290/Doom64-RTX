/*
 * savegame.c - PC save/load of the complete level state (Doom 64 only had
 * passwords and Controller Pak notes between levels).
 *
 * A save holds every thing (mobj) and every thinker (doors, lifts, lights,
 * cameras, ...) as their raw structures with each pointer field replaced by
 * a tagged reference (thing #n, thinker #n, sector #n, line #n, state #n,
 * function #n, ...), followed by the changeable parts of sectors, lines and
 * sides, the light colour table, the macro (scripting) state, the player and
 * the level globals. Loading reloads the map normally, then replaces its
 * contents with the saved ones.
 *
 * The raw structures tie a save to this build's struct layout; the header
 * records the sizes and refuses saves from an incompatible build.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <time.h>

#include "doomdef.h"
#include "p_local.h"
#include "r_local.h"
#include "config.h"
#include "savegame.h"

/* ---- game globals saved alongside the level ------------------------------ */

extern mobj_t mobjhead;
extern thinker_t thinkercap;
extern int rndindex, prndindex;
extern int start_time, ticon;
extern int pc_macro_total;
extern mobj_t *macroactivator;
extern line_t macrotempline;
extern line_t *macroline;
extern thinker_t *macrothinker;
extern int macrointeger;
extern macro_t *restartmacro;
extern int macrocounter;
extern macroactivator_t macroqueue[4];
extern int macroidx1, macroidx2;
extern int tempMacroIndex;
extern int nummacros;
extern int deathmocktics;
extern int infraredFactor, FlashEnvColor;
extern fixed_t quakeviewx, quakeviewy;
extern mobj_t *cameratarget;
extern angle_t camviewpitch;
extern fixed_t FogNear;
extern int FogColor, Skyfadeback;

void G_CompleteLevel(void);
void L_MissileHit(mobj_t *mo);
void L_SkullBash(mobj_t *mo);
void T_Combine(combine_t *combine);
void T_LaserThinker(laser_t *laser);

#define SAVE_MAGIC   "D64RTXSV"
#define SAVE_VERSION 1

/* ---- byte buffer ------------------------------------------------------------ */

typedef struct {
    Uint8 *data;
    size_t len, cap, pos;
    int bad;
} buf_t;

static void put(buf_t *b, const void *p, size_t n)
{
    if (b->len + n > b->cap)
    {
        size_t cap = b->cap ? b->cap * 2 : 65536;
        while (cap < b->len + n)
            cap *= 2;
        b->data = SDL_realloc(b->data, cap);
        b->cap = cap;
    }
    SDL_memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void get(buf_t *b, void *p, size_t n)
{
    if (b->bad || b->pos + n > b->len)
    {
        b->bad = 1;
        SDL_memset(p, 0, n);
        return;
    }
    SDL_memcpy(p, b->data + b->pos, n);
    b->pos += n;
}

static void put32(buf_t *b, Sint32 v) { put(b, &v, 4); }
static Sint32 get32(buf_t *b) { Sint32 v; get(b, &v, 4); return v; }
static void put64(buf_t *b, Uint64 v) { put(b, &v, 8); }
static Uint64 get64(buf_t *b) { Uint64 v; get(b, &v, 8); return v; }

/* ---- references --------------------------------------------------------------- */

enum {
    REF_NULL, REF_MOBJ, REF_THINKER, REF_SECTOR, REF_SOUNDORG, REF_LINE, REF_SIDE,
    REF_PLAYER, REF_STATE, REF_FUNC, REF_ACTION, REF_MACRO, REF_TEMPLINE, REF_RAW
};

#define REF(kind, v) (((Uint64)(kind) << 56) | ((Uint64)(v) & 0x00ffffffffffffffull))
#define REF_KIND(r)  ((int)((r) >> 56))
#define REF_VAL(r)   ((r) & 0x00ffffffffffffffull)

/* functions a thing or thinker can point to */
static void *const functab[] = {
    (void *)T_MoveCeiling, (void *)T_VerticalDoor, (void *)T_MobjExplode, (void *)T_FadeThinker,
    (void *)T_MoveFloor, (void *)T_MoveSplitPlane, (void *)T_FireFlicker, (void *)T_Glow,
    (void *)T_LightFlash, (void *)T_StrobeFlash, (void *)T_SequenceGlow, (void *)T_LightMorph,
    (void *)T_Combine, (void *)T_AimCamera, (void *)T_CountdownTimer, (void *)T_Quake,
    (void *)T_MoveCamera, (void *)T_PlatRaise, (void *)T_FadeInBrightness, (void *)T_LaserThinker,
    (void *)G_CompleteLevel, (void *)L_MissileHit, (void *)L_SkullBash, (void *)P_ExplodeMissile,
    (void *)P_RemoveMobj,
};
#define NUMFUNCS ((int)(sizeof(functab) / sizeof(functab[0])))

/* pointer <-> index tables for things and thinkers */
typedef struct {
    void **ptrs;
    int count, cap;
    void **hkeys;
    int *hvals;
    int hcap;
} ptrmap_t;

static Uint32 phash(const void *p)
{
    Uint64 v = (Uint64)(uintptr_t)p;
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdull;
    v ^= v >> 33;
    return (Uint32)v;
}

static void pm_free(ptrmap_t *m)
{
    SDL_free(m->ptrs);
    SDL_free(m->hkeys);
    SDL_free(m->hvals);
    SDL_memset(m, 0, sizeof(*m));
}

static void pm_add(ptrmap_t *m, void *p)
{
    if (m->count == m->cap)
    {
        m->cap = m->cap ? m->cap * 2 : 256;
        m->ptrs = SDL_realloc(m->ptrs, sizeof(void *) * (size_t)m->cap);
    }
    m->ptrs[m->count++] = p;
}

static void pm_index(ptrmap_t *m)
{
    int i;
    m->hcap = 64;
    while (m->hcap < m->count * 2)
        m->hcap *= 2;
    m->hkeys = SDL_calloc((size_t)m->hcap, sizeof(void *));
    m->hvals = SDL_calloc((size_t)m->hcap, sizeof(int));
    for (i = 0; i < m->count; i++)
    {
        Uint32 h = phash(m->ptrs[i]) & (Uint32)(m->hcap - 1);
        while (m->hkeys[h])
            h = (h + 1) & (Uint32)(m->hcap - 1);
        m->hkeys[h] = m->ptrs[i];
        m->hvals[h] = i;
    }
}

static int pm_find(const ptrmap_t *m, const void *p)
{
    Uint32 h;
    if (!m->hcap || !p)
        return -1;
    h = phash(p) & (Uint32)(m->hcap - 1);
    while (m->hkeys[h])
    {
        if (m->hkeys[h] == p)
            return m->hvals[h];
        h = (h + 1) & (Uint32)(m->hcap - 1);
    }
    return -1;
}

static ptrmap_t mobjmap, thinkmap;

static const char *enc_ctx = "";

static Uint64 encode(const void *p)
{
    uintptr_t v = (uintptr_t)p;
    uintptr_t base;
    int i;

    if (!p)
        return REF(REF_NULL, 0);
    if ((i = pm_find(&mobjmap, p)) >= 0)
        return REF(REF_MOBJ, i);
    if ((i = pm_find(&thinkmap, p)) >= 0)
        return REF(REF_THINKER, i);

    base = (uintptr_t)sectors;
    if (v >= base && v < base + sizeof(sector_t) * (size_t)numsectors)
    {
        size_t idx = (v - base) / sizeof(sector_t);
        if (v == (uintptr_t)&sectors[idx])
            return REF(REF_SECTOR, idx);
        if (v == (uintptr_t)&sectors[idx].soundorg)
            return REF(REF_SOUNDORG, idx);
    }
    base = (uintptr_t)lines;
    if ((v & ~(uintptr_t)1) >= base && (v & ~(uintptr_t)1) < base + sizeof(line_t) * (size_t)numlines &&
        ((v & ~(uintptr_t)1) - base) % sizeof(line_t) == 0)
        return REF(REF_LINE, (((v & ~(uintptr_t)1) - base) / sizeof(line_t)) * 2 + (v & 1));
    base = (uintptr_t)sides;
    if (v >= base && v < base + sizeof(side_t) * (size_t)numsides && (v - base) % sizeof(side_t) == 0)
        return REF(REF_SIDE, (v - base) / sizeof(side_t));
    if (p == &players[0])
        return REF(REF_PLAYER, 0);
    base = (uintptr_t)states;
    if (v >= base && v < base + sizeof(states) && (v - base) % sizeof(state_t) == 0)
        return REF(REF_STATE, (v - base) / sizeof(state_t));
    for (i = 0; i < NUMFUNCS; i++)
        if (functab[i] == p)
            return REF(REF_FUNC, i);
    for (i = 0; i < NUMSTATES; i++)
        if ((void *)states[i].action == p)
            return REF(REF_ACTION, i);
    if (macros && pc_macro_total > 0)
    {
        base = (uintptr_t)macros[0];
        if (v >= base && v < base + sizeof(macro_t) * (size_t)pc_macro_total && (v - base) % sizeof(macro_t) == 0)
            return REF(REF_MACRO, (v - base) / sizeof(macro_t));
    }
    if (p == &macrotempline)
        return REF(REF_TEMPLINE, 0);
    if (v < 0x10000) /* small integers kept in pointer fields (mobj->extradata counters) */
        return REF(REF_RAW, v);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "savegame: unknown pointer %p in %s saved as NULL", p, enc_ctx);
    return REF(REF_NULL, 0);
}

static void **dec_mobjs, **dec_thinkers;
static int dec_nmobjs, dec_nthinkers;

static void *decode(Uint64 r)
{
    Uint64 v = REF_VAL(r);
    switch (REF_KIND(r))
    {
    case REF_NULL: return NULL;
    case REF_MOBJ: return v < (Uint64)dec_nmobjs ? dec_mobjs[v] : NULL;
    case REF_THINKER: return v < (Uint64)dec_nthinkers ? dec_thinkers[v] : NULL;
    case REF_SECTOR: return v < (Uint64)numsectors ? &sectors[v] : NULL;
    case REF_SOUNDORG: return v < (Uint64)numsectors ? (void *)&sectors[v].soundorg : NULL;
    case REF_LINE:
        if ((v >> 1) >= (Uint64)numlines)
            return NULL;
        return (void *)((uintptr_t)&lines[v >> 1] | (uintptr_t)(v & 1));
    case REF_SIDE: return v < (Uint64)numsides ? &sides[v] : NULL;
    case REF_PLAYER: return &players[0];
    case REF_STATE: return v < NUMSTATES ? &states[v] : NULL;
    case REF_FUNC: return v < (Uint64)NUMFUNCS ? functab[v] : NULL;
    case REF_ACTION: return v < NUMSTATES ? (void *)states[v].action : NULL;
    case REF_MACRO: return (macros && v < (Uint64)pc_macro_total) ? &macros[0][v] : NULL;
    case REF_TEMPLINE: return &macrotempline;
    case REF_RAW: return (void *)(uintptr_t)v;
    }
    return NULL;
}

/* encode/decode a pointer field in place (both are 8 bytes) */
static void enc_field(void *field)
{
    void *p;
    Uint64 r;
    SDL_memcpy(&p, field, sizeof(p));
    r = encode(p);
    SDL_memcpy(field, &r, sizeof(r));
}

static void dec_field(void *field)
{
    Uint64 r;
    void *p;
    SDL_memcpy(&r, field, sizeof(r));
    p = decode(r);
    SDL_memcpy(field, &p, sizeof(p));
}

/* ---- thinker types ------------------------------------------------------------- */

typedef struct {
    void *func;
    size_t size;
    size_t ptrs[4];
    int nptrs;
} thinkdef_t;

#define TD(fn, type, n, ...) { (void *)fn, sizeof(type), { __VA_ARGS__ }, n }
static const thinkdef_t thinkdefs[] = {
    TD(T_MoveCeiling, ceiling_t, 1, offsetof(ceiling_t, sector)),
    TD(T_VerticalDoor, vldoor_t, 1, offsetof(vldoor_t, sector)),
    TD(T_MobjExplode, mobjexp_t, 1, offsetof(mobjexp_t, mobj)),
    TD(T_FadeThinker, fade_t, 1, offsetof(fade_t, mobj)),
    TD(T_MoveFloor, floormove_t, 1, offsetof(floormove_t, sector)),
    TD(T_MoveSplitPlane, splitmove_t, 1, offsetof(splitmove_t, sector)),
    TD(T_FireFlicker, fireflicker_t, 1, offsetof(fireflicker_t, sector)),
    TD(T_Glow, glow_t, 1, offsetof(glow_t, sector)),
    TD(T_LightFlash, lightflash_t, 1, offsetof(lightflash_t, sector)),
    TD(T_StrobeFlash, strobe_t, 1, offsetof(strobe_t, sector)),
    TD(T_SequenceGlow, sequenceglow_t, 2, offsetof(sequenceglow_t, sector), offsetof(sequenceglow_t, headsector)),
    TD(T_LightMorph, lightmorph_t, 1, offsetof(lightmorph_t, sector)),
    TD(T_Combine, combine_t, 2, offsetof(combine_t, sector), offsetof(combine_t, combiner)),
    TD(T_AimCamera, aimcamera_t, 1, offsetof(aimcamera_t, viewmobj)),
    TD(T_CountdownTimer, delay_t, 1, offsetof(delay_t, finishfunc)),
    TD(T_Quake, quake_t, 0, 0),
    TD(T_MoveCamera, movecamera_t, 0, 0),
    TD(T_PlatRaise, plat_t, 1, offsetof(plat_t, sector)),
    TD(T_FadeInBrightness, fadebright_t, 0, 0),
};
#define NUMTHINKDEFS ((int)(sizeof(thinkdefs) / sizeof(thinkdefs[0])))
#define TD_CEILING 0
#define TD_PLAT    17

static int thinker_type(thinker_t *th)
{
    int i;
    if (th->function == NULL)
    {
        /* in stasis: a ceiling or a lift */
        for (i = 0; i < MAXCEILINGS; i++)
            if ((thinker_t *)activeceilings[i] == th)
                return TD_CEILING;
        for (i = 0; i < MAXPLATS; i++)
            if ((thinker_t *)activeplats[i] == th)
                return TD_PLAT;
        return -1;
    }
    for (i = 0; i < NUMTHINKDEFS; i++)
        if (thinkdefs[i].func == (void *)th->function)
            return i;
    return -1;
}

/* ---- files ---------------------------------------------------------------------- */

static void slot_path(int slot, char *out, size_t len)
{
    char *pref = SDL_GetPrefPath("Doom64RTX", "Doom64RTX");
    SDL_snprintf(out, len, "%ssave%d.d64s", pref ? pref : "", slot);
    SDL_free(pref);
}

typedef struct {
    char   magic[8];
    Sint32 version;
    Sint32 size_mobj, size_player, size_sector, size_line;
    Sint32 gamemap, gameskill;
    Sint32 numsectors, numlines, numsides, numlights;
    Sint64 timestamp;
    char   desc[64];
} saveheader_t;

static int read_header(int slot, saveheader_t *h)
{
    char path[1024];
    SDL_IOStream *io;
    size_t n;
    slot_path(slot, path, sizeof(path));
    io = SDL_IOFromFile(path, "rb");
    if (!io)
        return 0;
    n = SDL_ReadIO(io, h, sizeof(*h));
    SDL_CloseIO(io);
    return n == sizeof(*h) && !SDL_memcmp(h->magic, SAVE_MAGIC, 8) && h->version == SAVE_VERSION;
}

int G_PCSaveSlotInfo(int slot, char *desc, int len)
{
    saveheader_t h;
    if (!read_header(slot, &h))
        return 0;
    h.desc[sizeof(h.desc) - 1] = 0;
    SDL_strlcpy(desc, h.desc, (size_t)len);
    return 1;
}

/* ---- save ----------------------------------------------------------------------- */

static void collect(int skip_lasers)
{
    mobj_t *mo;
    thinker_t *th;

    pm_free(&mobjmap);
    pm_free(&thinkmap);
    for (mo = mobjhead.next; mo != &mobjhead; mo = mo->next)
    {
        if (skip_lasers && mo->extradata && thinker_type((thinker_t *)mo->extradata) < 0)
        {
            /* laser end markers belong to a laser thinker, which is not saved */
            thinker_t *t;
            int is_laser = 0;
            for (t = thinkercap.next; t != &thinkercap; t = t->next)
                if (t == (thinker_t *)mo->extradata && t->function == (think_t)T_LaserThinker)
                    is_laser = 1;
            if (is_laser)
                continue;
        }
        pm_add(&mobjmap, mo);
    }
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        if (th->function == (think_t)-1 || thinker_type(th) < 0)
            continue; /* removed, or a short lived laser beam */
        pm_add(&thinkmap, th);
    }
    pm_index(&mobjmap);
    pm_index(&thinkmap);
}

int G_PCSaveGame(int slot)
{
    buf_t b = { 0 };
    saveheader_t h;
    char path[1024];
    SDL_IOStream *io;
    player_t pl;
    int i, j;

    if (gamemap < 1 || !players[0].mo || players[0].playerstate != PST_LIVE)
        return 0;

    collect(1);

    SDL_memset(&h, 0, sizeof(h));
    SDL_memcpy(h.magic, SAVE_MAGIC, 8);
    h.version = SAVE_VERSION;
    h.size_mobj = (Sint32)sizeof(mobj_t);
    h.size_player = (Sint32)sizeof(player_t);
    h.size_sector = (Sint32)sizeof(sector_t);
    h.size_line = (Sint32)sizeof(line_t);
    h.gamemap = gamemap;
    h.gameskill = gameskill;
    h.numsectors = numsectors;
    h.numlines = numlines;
    h.numsides = numsides;
    h.numlights = numlights;
    h.timestamp = (Sint64)time(NULL);
    {
        char when[32];
        time_t t = (time_t)h.timestamp;
        struct tm *tm = localtime(&t);
        if (tm)
            strftime(when, sizeof(when), "%H:%M", tm);
        else
            SDL_strlcpy(when, "", sizeof(when));
        SDL_snprintf(h.desc, sizeof(h.desc), "MAP%02d %s", gamemap, when);
    }
    put(&b, &h, sizeof(h));

    /* things */
    put32(&b, mobjmap.count);
    for (i = 0; i < mobjmap.count; i++)
    {
        mobj_t m = *(mobj_t *)mobjmap.ptrs[i];
        m.subsector = NULL;
        m.prev = m.next = m.snext = m.sprev = m.bnext = m.bprev = NULL;
        m.info = NULL;
        enc_ctx = "thing";
        enc_field(&m.player);
        enc_field(&m.target);
        enc_field(&m.tracer);
        enc_field(&m.state);
        enc_field(&m.extradata);
        enc_field(&m.latecall);
        put(&b, &m, sizeof(m));
    }

    /* thinkers */
    put32(&b, thinkmap.count);
    for (i = 0; i < thinkmap.count; i++)
    {
        thinker_t *th = thinkmap.ptrs[i];
        int type = thinker_type(th);
        const thinkdef_t *td = &thinkdefs[type];
        Uint8 tmp[512];

        enc_ctx = "thinker";
        put32(&b, type);
        put32(&b, th->function == NULL); /* in stasis */
        SDL_memcpy(tmp, th, td->size);
        SDL_memset(tmp, 0, sizeof(thinker_t));
        for (j = 0; j < td->nptrs; j++)
            enc_field(tmp + td->ptrs[j]);
        put(&b, tmp, td->size);
    }

    /* sectors, lines, sides */
    enc_ctx = "sector";
    for (i = 0; i < numsectors; i++)
    {
        sector_t *s = &sectors[i];
        put32(&b, s->floorheight);
        put32(&b, s->ceilingheight);
        put32(&b, s->floorpic);
        put32(&b, s->ceilingpic);
        for (j = 0; j < 5; j++)
            put32(&b, s->colors[j]);
        put32(&b, s->lightlevel);
        put32(&b, s->special);
        put32(&b, s->tag);
        put32(&b, s->xoffset);
        put32(&b, s->yoffset);
        put32(&b, s->soundtraversed);
        put32(&b, s->flags);
        put64(&b, encode(s->soundtarget));
        put64(&b, encode(s->specialdata));
    }
    for (i = 0; i < numlines; i++)
    {
        line_t *l = &lines[i];
        put32(&b, l->flags);
        put32(&b, l->special);
        put32(&b, l->tag);
        put64(&b, encode(l->specialdata));
    }
    for (i = 0; i < numsides; i++)
    {
        side_t *sd = &sides[i];
        put32(&b, sd->textureoffset);
        put32(&b, sd->rowoffset);
        put32(&b, sd->toptexture);
        put32(&b, sd->bottomtexture);
        put32(&b, sd->midtexture);
    }
    put(&b, lights, sizeof(light_t) * (size_t)numlights);

    /* macros (scripting) */
    enc_ctx = "macros";
    put32(&b, pc_macro_total);
    if (pc_macro_total > 0)
        put(&b, macros[0], sizeof(macro_t) * (size_t)pc_macro_total);
    put64(&b, encode(activemacro));
    put64(&b, encode(macroactivator));
    {
        line_t tl = macrotempline;
        tl.v1 = tl.v2 = NULL;
        enc_field(&tl.frontsector);
        enc_field(&tl.backsector);
        enc_field(&tl.specialdata);
        put(&b, &tl, sizeof(tl));
    }
    put64(&b, encode(macroline));
    put64(&b, encode(macrothinker));
    put32(&b, macrointeger);
    put64(&b, encode(restartmacro));
    put32(&b, macrocounter);
    for (i = 0; i < 4; i++)
    {
        put32(&b, macroqueue[i].tag);
        put64(&b, encode(macroqueue[i].activator));
    }
    put32(&b, macroidx1);
    put32(&b, macroidx2);
    put32(&b, tempMacroIndex);

    /* player */
    enc_ctx = "player";
    pl = players[0];
    enc_field(&pl.mo);
    enc_field(&pl.attacker);
    enc_field(&pl.lastsoundsector);
    pl.message = NULL;
    pl.messagetic = 0;
    for (i = 0; i < NUMPSPRITES; i++)
        enc_field(&pl.psprites[i].state);
    put(&b, &pl, sizeof(pl));

    /* level globals */
    enc_ctx = "globals";
    for (i = 0; i < MAXCEILINGS; i++)
        put64(&b, encode(activeceilings[i]));
    for (i = 0; i < MAXPLATS; i++)
        put64(&b, encode(activeplats[i]));
    for (i = 0; i < MAXBUTTONS; i++)
    {
        button_t bt = buttonlist[i];
        enc_field(&bt.side);
        enc_field(&bt.soundorg);
        put(&b, &bt, sizeof(bt));
    }
    put(&b, anims, sizeof(anims));
    put32(&b, lastanim ? (Sint32)(lastanim - anims) : -1);
    put64(&b, encode(cameratarget));
    put32(&b, (Sint32)camviewpitch);
    put32(&b, quakeviewx);
    put32(&b, quakeviewy);
    put32(&b, FlashEnvColor);
    put32(&b, infraredFactor);
    put32(&b, FogNear);
    put32(&b, FogColor);
    put32(&b, Skyfadeback);
    put32(&b, totalkills);
    put32(&b, totalitems);
    put32(&b, totalsecret);
    put32(&b, rndindex);
    put32(&b, prndindex);
    put32(&b, ticon - start_time);
    put32(&b, MapBlueKeyType);
    put32(&b, MapRedKeyType);
    put32(&b, MapYellowKeyType);
    put32(&b, deathmocktics);
    put(&b, "END!", 4);

    slot_path(slot, path, sizeof(path));
    io = SDL_IOFromFile(path, "wb");
    if (!io || SDL_WriteIO(io, b.data, b.len) != b.len)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "savegame: cannot write %s", path);
        if (io)
            SDL_CloseIO(io);
        SDL_free(b.data);
        return 0;
    }
    SDL_CloseIO(io);
    SDL_Log("savegame: slot %d saved (%s, %d things, %d thinkers, %u bytes)", slot, h.desc, mobjmap.count,
            thinkmap.count, (unsigned)b.len);
    SDL_free(b.data);
    pm_free(&mobjmap);
    pm_free(&thinkmap);
    return 1;
}

/* ---- load ----------------------------------------------------------------------- */

static int pending_slot = -1;

/* Step 1 (menu): remember the slot and go to its map. */
int G_PCLoadGame(int slot)
{
    saveheader_t h;
    if (!read_header(slot, &h))
        return 0;
    if (h.size_mobj != (Sint32)sizeof(mobj_t) || h.size_player != (Sint32)sizeof(player_t) ||
        h.size_sector != (Sint32)sizeof(sector_t) || h.size_line != (Sint32)sizeof(line_t))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "savegame: slot %d is from an incompatible build", slot);
        return 0;
    }
    pending_slot = slot;
    gamemap = h.gamemap;
    startmap = h.gamemap;
    gameskill = (skill_t)h.gameskill;
    startskill = (skill_t)h.gameskill; /* used when loading from the title */
    SDL_Log("savegame: loading slot %d (%s)", slot, h.desc);
    return 1;
}

int G_PCLoadPending(void)
{
    return pending_slot >= 0;
}

static void wipe_level(void)
{
    mobj_t *mo, *next;
    thinker_t *th, *tnext;
    int i;

    for (mo = mobjhead.next; mo != &mobjhead; mo = next)
    {
        next = mo->next;
        Z_Free(mo);
    }
    mobjhead.next = mobjhead.prev = &mobjhead;
    for (th = thinkercap.next; th != &thinkercap; th = tnext)
    {
        tnext = th->next;
        Z_Free(th);
    }
    thinkercap.next = thinkercap.prev = &thinkercap;
    for (i = 0; i < numsectors; i++)
    {
        sectors[i].thinglist = NULL;
        sectors[i].specialdata = NULL;
    }
    for (i = 0; i < numlines; i++)
        lines[i].specialdata = NULL;
    SDL_memset(blocklinks, 0, sizeof(mobj_t *) * (size_t)bmapwidth * (size_t)bmapheight);
    SDL_memset(activeceilings, 0, sizeof(activeceilings));
    SDL_memset(activeplats, 0, sizeof(activeplats));
    cameratarget = NULL;
}

/* Step 2 (end of P_Start): replace the freshly loaded map with the save. */
void G_PCApplyPendingLoad(void)
{
    char path[1024];
    buf_t b = { 0 };
    saveheader_t h;
    size_t size;
    int slot = pending_slot;
    int i, j, n, macro_total;
    player_t pl;

    if (slot < 0)
        return;
    pending_slot = -1;

    slot_path(slot, path, sizeof(path));
    b.data = SDL_LoadFile(path, &size);
    if (!b.data)
        return;
    b.len = size;
    get(&b, &h, sizeof(h));
    if (b.bad || h.gamemap != gamemap || h.numsectors != numsectors || h.numlines != numlines ||
        h.numsides != numsides || h.numlights != numlights)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "savegame: slot %d does not match MAP%02d", slot, gamemap);
        SDL_free(b.data);
        return;
    }

    wipe_level();

    /* allocate things and thinkers first so references can be resolved */
    dec_nmobjs = get32(&b);
    dec_mobjs = SDL_calloc((size_t)(dec_nmobjs > 0 ? dec_nmobjs : 1), sizeof(void *));
    for (i = 0; i < dec_nmobjs; i++)
    {
        mobj_t *mo = Z_Malloc(sizeof(mobj_t), PU_LEVEL, NULL);
        get(&b, mo, sizeof(mobj_t));
        dec_mobjs[i] = mo;
    }
    dec_nthinkers = get32(&b);
    dec_thinkers = SDL_calloc((size_t)(dec_nthinkers > 0 ? dec_nthinkers : 1), sizeof(void *));
    {
        int *types = SDL_calloc((size_t)(dec_nthinkers > 0 ? dec_nthinkers : 1), sizeof(int) * 2);
        for (i = 0; i < dec_nthinkers && !b.bad; i++)
        {
            int type = get32(&b), stasis = get32(&b);
            thinker_t *th;
            if (type < 0 || type >= NUMTHINKDEFS)
            {
                b.bad = 1;
                break;
            }
            th = Z_Malloc((int)thinkdefs[type].size, PU_LEVSPEC, 0);
            get(&b, th, thinkdefs[type].size);
            dec_thinkers[i] = th;
            types[i * 2] = type;
            types[i * 2 + 1] = stasis;
        }
        /* resolve and link */
        for (i = 0; i < dec_nthinkers && !b.bad; i++)
        {
            thinker_t *th = dec_thinkers[i];
            const thinkdef_t *td = &thinkdefs[types[i * 2]];
            for (j = 0; j < td->nptrs; j++)
                dec_field((Uint8 *)th + td->ptrs[j]);
            P_AddThinker(th);
            th->function = types[i * 2 + 1] ? NULL : (think_t)td->func;
        }
        SDL_free(types);
    }
    for (i = 0; i < dec_nmobjs; i++)
    {
        mobj_t *mo = dec_mobjs[i];
        dec_field(&mo->player);
        dec_field(&mo->target);
        dec_field(&mo->tracer);
        dec_field(&mo->state);
        dec_field(&mo->extradata);
        dec_field(&mo->latecall);
        mo->info = &mobjinfo[mo->type];
        mo->subsector = NULL;
        mo->snext = mo->sprev = mo->bnext = mo->bprev = NULL;
        mo->prev = mobjhead.prev;
        mo->next = &mobjhead;
        mobjhead.prev->next = mo;
        mobjhead.prev = mo;
        P_SetThingPosition(mo);
    }

    for (i = 0; i < numsectors; i++)
    {
        sector_t *s = &sectors[i];
        s->floorheight = get32(&b);
        s->ceilingheight = get32(&b);
        s->floorpic = (VINT)get32(&b);
        s->ceilingpic = (VINT)get32(&b);
        for (j = 0; j < 5; j++)
            s->colors[j] = get32(&b);
        s->lightlevel = get32(&b);
        s->special = (VINT)get32(&b);
        s->tag = (VINT)get32(&b);
        s->xoffset = (VINT)get32(&b);
        s->yoffset = (VINT)get32(&b);
        s->soundtraversed = (VINT)get32(&b);
        s->flags = (VINT)get32(&b);
        s->soundtarget = decode(get64(&b));
        s->specialdata = decode(get64(&b));
    }
    for (i = 0; i < numlines; i++)
    {
        line_t *l = &lines[i];
        l->flags = (VINT)get32(&b);
        l->special = (VINT)get32(&b);
        l->tag = (VINT)get32(&b);
        l->specialdata = decode(get64(&b));
    }
    for (i = 0; i < numsides; i++)
    {
        side_t *sd = &sides[i];
        sd->textureoffset = get32(&b);
        sd->rowoffset = get32(&b);
        sd->toptexture = (VINT)get32(&b);
        sd->bottomtexture = (VINT)get32(&b);
        sd->midtexture = (VINT)get32(&b);
    }
    get(&b, lights, sizeof(light_t) * (size_t)numlights);

    macro_total = get32(&b);
    if (macro_total == pc_macro_total && macro_total > 0)
        get(&b, macros[0], sizeof(macro_t) * (size_t)macro_total);
    else if (macro_total > 0)
        b.pos += sizeof(macro_t) * (size_t)macro_total;
    activemacro = decode(get64(&b));
    macroactivator = decode(get64(&b));
    {
        line_t tl;
        get(&b, &tl, sizeof(tl));
        dec_field(&tl.frontsector);
        dec_field(&tl.backsector);
        dec_field(&tl.specialdata);
        tl.v1 = macrotempline.v1;
        tl.v2 = macrotempline.v2;
        macrotempline = tl;
    }
    macroline = decode(get64(&b));
    if (macroline && macroline != &macrotempline)
    {
        macrotempline.v1 = macroline->v1;
        macrotempline.v2 = macroline->v2;
    }
    macrothinker = decode(get64(&b));
    macrointeger = get32(&b);
    restartmacro = decode(get64(&b));
    macrocounter = get32(&b);
    for (i = 0; i < 4; i++)
    {
        macroqueue[i].tag = get32(&b);
        macroqueue[i].activator = decode(get64(&b));
    }
    macroidx1 = get32(&b);
    macroidx2 = get32(&b);
    tempMacroIndex = get32(&b);

    get(&b, &pl, sizeof(pl));
    pl.mo = decode((Uint64)(uintptr_t)pl.mo);
    pl.attacker = decode((Uint64)(uintptr_t)pl.attacker);
    pl.lastsoundsector = decode((Uint64)(uintptr_t)pl.lastsoundsector);
    for (i = 0; i < NUMPSPRITES; i++)
        pl.psprites[i].state = decode((Uint64)(uintptr_t)pl.psprites[i].state);
    players[0] = pl;

    for (i = 0; i < MAXCEILINGS; i++)
        activeceilings[i] = decode(get64(&b));
    for (i = 0; i < MAXPLATS; i++)
        activeplats[i] = decode(get64(&b));
    for (i = 0; i < MAXBUTTONS; i++)
    {
        button_t bt;
        get(&b, &bt, sizeof(bt));
        dec_field(&bt.side);
        dec_field(&bt.soundorg);
        buttonlist[i] = bt;
    }
    get(&b, anims, sizeof(anims));
    n = get32(&b);
    lastanim = (n >= 0 && n <= MAXANIMS) ? &anims[n] : lastanim;
    cameratarget = decode(get64(&b));
    camviewpitch = (angle_t)get32(&b);
    quakeviewx = get32(&b);
    quakeviewy = get32(&b);
    FlashEnvColor = get32(&b);
    infraredFactor = get32(&b);
    FogNear = get32(&b);
    FogColor = get32(&b);
    Skyfadeback = get32(&b);
    totalkills = get32(&b);
    totalitems = get32(&b);
    totalsecret = get32(&b);
    rndindex = get32(&b);
    prndindex = get32(&b);
    start_time = ticon - get32(&b);
    MapBlueKeyType = (card_t)get32(&b);
    MapRedKeyType = (card_t)get32(&b);
    MapYellowKeyType = (card_t)get32(&b);
    deathmocktics = get32(&b);
    {
        char end[4];
        get(&b, end, 4);
        if (b.bad || SDL_memcmp(end, "END!", 4))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "savegame: slot %d is truncated or corrupt", slot);
    }
    if (!cameratarget)
        cameratarget = players[0].mo;
    if (players[0].mo)
        players[0].mo->player = &players[0];

    SDL_Log("savegame: slot %d loaded (MAP%02d, %d things, %d thinkers)", slot, gamemap, dec_nmobjs,
            dec_nthinkers);
    SDL_free(dec_mobjs);
    SDL_free(dec_thinkers);
    dec_mobjs = dec_thinkers = NULL;
    dec_nmobjs = dec_nthinkers = 0;
    SDL_free(b.data);
}
