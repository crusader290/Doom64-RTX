/*
 * n64synth.c - clean-room software replacement for the parts of the N64
 * audio library (libultra "alSyn*") that the WESS sound system drives.
 *
 * Voices play VADPCM or raw 16-bit big-endian samples from the WDD bank with
 * pitch-ratio resampling (linear interpolation), equal-power pan and
 * dry/wet split, linear volume ramps, and loops (with decoder state restore
 * for VADPCM). The wet bus feeds a delay-line reverb built like the N64
 * "big room" effect (allpass/comb sections with a low-pass in the feedback).
 * Players (WESS registers one) are called back on their own microsecond
 * schedule, sample accurately, from inside Synth_Render.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "ultra64.h"
#include "libaudio.h"
#include "n64synth.h"

/* WESS's wave table layout (wessarc.h ALWaveTable2 with native pointers). */
typedef struct {
    u32 start, end, count;
    s16 state[16];
    u32 pad;
} SynAdpcmLoop;

typedef struct {
    u32 start, end, count, pad;
} SynRawLoop;

typedef struct {
    s32 order;
    s32 npredictors;
    s16 book[128];
} SynBook;

typedef struct {
    u8  *base;
    s32  len;
    u8   type;
    u8   flags;
    void *loop;
    void *book;
} SynWave;

#define MAX_PVOICES 32
#define MAX_PLAYERS 4

typedef struct {
    int      in_use;
    ALVoice *owner;
    s16      priority;
    const SynWave *wave;
    int      playing;
    /* playback cursor: interpolating between cur (ipos) and nxt (npos) */
    s32      ipos, npos;
    double   frac;
    s16      cur, nxt;
    double   ratio;          /* source samples per output sample */
    u32      nsamples;
    /* loop */
    u32      loop_start, loop_end;
    s32      loop_count;     /* -1 forever, 0 none */
    const s16 *loop_state;
    /* decoded window: samples [win_start, win_start + 16) */
    s32      win_start;
    s16      win[16];
    s16      hist[16];       /* decoder history (last decoded frame) */
    /* mix */
    float    vol, vol_target, vol_step;
    s32      vol_samples;
    u8       pan;
    u8       fxmix;
} PVoice;

static ALGlobals *g_glob;
ALGlobals *alGlobals;

static PVoice   pvoices[MAX_PVOICES];
static ALPlayer *players;
static ALPlayer *pl_keys[MAX_PLAYERS];
static double   pl_frac[MAX_PLAYERS];

static int pl_slot(ALPlayer *pl)
{
    int i;
    for (i = 0; i < MAX_PLAYERS; i++)
        if (pl_keys[i] == pl)
            return i;
    for (i = 0; i < MAX_PLAYERS; i++)
        if (!pl_keys[i])
        {
            pl_keys[i] = pl;
            pl_frac[i] = 0;
            return i;
        }
    return MAX_PLAYERS - 1;
}
static s32      out_rate = 22050;
static double   us_per_sample;
static double   max_ratio;

/* equal-power curve, 0..127 -> 1..0 */
static float eqpower[128];

/* ---- reverb (N64 "big room" style) -------------------------------------- */

typedef struct {
    s32   input, output;     /* tap offsets in samples behind the write head */
    float fbcoef, ffcoef, gain;
    float lpcoef;            /* 0 = no filter */
    float lpstate;
} RevSection;

static float      *rev_line;
static s32         rev_len;
static s32         rev_pos;
static RevSection  rev_sec[4];
static int         rev_nsec;

static void voice_begin(PVoice *p);

static void reverb_init(void)
{
    /* Sections from the N64 big-room preset; times in ms of the original
     * table, converted for our output rate. */
    static const float spec[4][6] = {
        /* in ms, out ms, fbcoef, ffcoef, gain, lowpass */
        {  0.0f, 66.0f,  9830.0f, -9830.0f, 0.0f,     0.0f    },
        { 22.0f, 54.0f,  3276.0f, -3276.0f, 16383.0f, 0.0f    },
        { 66.0f, 91.0f,  3276.0f, -3276.0f, 16383.0f, 0.0f    },
        {  0.0f, 94.0f,  8000.0f,  0.0f,    0.0f,     20480.0f},
    };
    const float samples_per_ms = (float)out_rate / 1000.0f;
    int i;

    rev_len = (s32)(100.0f * samples_per_ms) + 16;
    free(rev_line);
    rev_line = (float *)calloc((size_t)rev_len, sizeof(float));
    rev_pos = 0;
    rev_nsec = 4;
    for (i = 0; i < 4; i++)
    {
        rev_sec[i].input = (s32)(spec[i][0] * samples_per_ms);
        rev_sec[i].output = (s32)(spec[i][1] * samples_per_ms);
        rev_sec[i].fbcoef = spec[i][2] / 32768.0f;
        rev_sec[i].ffcoef = spec[i][3] / 32768.0f;
        rev_sec[i].gain = spec[i][4] / 32768.0f;
        rev_sec[i].lpcoef = spec[i][5] / 32768.0f;
        rev_sec[i].lpstate = 0;
    }
}

static inline float *rev_tap(s32 back)
{
    s32 i = rev_pos - back;
    while (i < 0)
        i += rev_len;
    return &rev_line[i];
}

/* One sample through the reverb: in = mono wet send, returns wet output. */
static inline float reverb_sample(float in)
{
    float out = 0.0f;
    int i;

    if (!rev_line)
        return 0.0f;
    rev_line[rev_pos] = in;
    for (i = 0; i < rev_nsec; i++)
    {
        RevSection *d = &rev_sec[i];
        float *ip = rev_tap(d->input);
        float *op = rev_tap(d->output);
        float b1 = *ip;
        float b2 = *op;

        if (d->ffcoef != 0.0f)
            b2 += d->ffcoef * b1;
        if (d->fbcoef != 0.0f)
        {
            b1 += d->fbcoef * b2;
            *ip = b1;
        }
        if (d->lpcoef != 0.0f)
        {
            d->lpstate = b2 * (1.0f - d->lpcoef) + d->lpstate * d->lpcoef;
            b2 = d->lpstate;
        }
        *op = b2;
        if (d->gain != 0.0f)
            out += d->gain * b2;
    }
    if (++rev_pos >= rev_len)
        rev_pos = 0;
    return out;
}

/* ---- heap / links -------------------------------------------------------- */

void alHeapInit(ALHeap *hp, u8 *base, s32 len)
{
    uintptr_t a = ((uintptr_t)base + 15) & ~(uintptr_t)15;
    hp->base = (u8 *)a;
    hp->cur = hp->base;
    hp->len = len - (s32)(a - (uintptr_t)base);
    hp->count = 0;
}

void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *hp, s32 num, s32 size)
{
    s32 bytes = (num * size + 15) & ~15;
    u8 *p;
    (void)file;
    (void)line;
    if (hp->cur + bytes > hp->base + hp->len)
        return NULL;
    p = hp->cur;
    hp->cur += bytes;
    hp->count++;
    return p;
}

void alUnlink(ALLink *ln)
{
    if (ln->next)
        ln->next->prev = ln->prev;
    if (ln->prev)
        ln->prev->next = ln->next;
}

void alLink(ALLink *ln, ALLink *to)
{
    ln->next = to->next;
    ln->prev = to;
    if (to->next)
        to->next->prev = ln;
    to->next = ln;
}

/* ---- init ---------------------------------------------------------------- */

void alInit(ALGlobals *glob, ALSynConfig *c)
{
    int i;
    g_glob = glob;
    alGlobals = glob;
    memset(pvoices, 0, sizeof(pvoices));
    players = NULL;
    glob->drvr.head = NULL;
    glob->drvr.outputRate = c ? c->outputRate : out_rate;
    for (i = 0; i < 128; i++)
        eqpower[i] = (float)cos((double)i / 127.0 * (M_PI / 2.0));
    Synth_SetOutputRate(glob->drvr.outputRate);
}

void alClose(ALGlobals *glob)
{
    (void)glob;
    players = NULL;
    free(rev_line);
    rev_line = NULL;
}

void Synth_SetOutputRate(s32 rate)
{
    out_rate = rate > 0 ? rate : 22050;
    us_per_sample = 1000000.0 / (double)out_rate;
    /* the N64 resampler tops out just under 2x at its 22050 Hz output */
    max_ratio = 1.99996 * 22050.0 / (double)out_rate;
    reverb_init();
}

s32 Synth_OutputRate(void)
{
    return out_rate;
}

void alSynAddPlayer(ALSynth *s, ALPlayer *client)
{
    (void)s;
    client->samplesLeft = (s32)((double)client->callTime / us_per_sample);
    pl_frac[pl_slot(client)] = 0;
    client->next = players;
    players = client;
}

void alSynRemovePlayer(ALSynth *s, ALPlayer *client)
{
    ALPlayer **pp;
    (void)s;
    for (pp = &players; *pp; pp = &(*pp)->next)
    {
        if (*pp == client)
        {
            *pp = client->next;
            break;
        }
    }
}

/* ---- voices -------------------------------------------------------------- */

static PVoice *pv_of(ALVoice *v)
{
    PVoice *p = v ? (PVoice *)v->pvoice : NULL;
    if (p && p->owner != v)
        return NULL;
    return p;
}

s32 alSynAllocVoice(ALSynth *s, ALVoice *v, ALVoiceConfig *vc)
{
    PVoice *p = NULL;
    int i;
    (void)s;

    if (pv_of(v))
    {
        p = pv_of(v);
    }
    else
    {
        for (i = 0; i < MAX_PVOICES; i++)
        {
            if (!pvoices[i].in_use)
            {
                p = &pvoices[i];
                break;
            }
        }
        if (!p)
        {
            /* steal the lowest priority voice not above ours */
            PVoice *best = NULL;
            for (i = 0; i < MAX_PVOICES; i++)
                if (pvoices[i].priority <= vc->priority && (!best || pvoices[i].priority < best->priority))
                    best = &pvoices[i];
            if (!best)
                return 0;
            if (best->owner)
                best->owner->pvoice = NULL;
            p = best;
        }
    }
    memset(p, 0, sizeof(*p));
    p->in_use = 1;
    p->owner = v;
    p->priority = vc->priority;
    p->pan = 64;
    v->pvoice = p;
    v->priority = vc->priority;
    v->fxBus = vc->fxBus;
    v->unityPitch = vc->unityPitch;
    return 1;
}

void alSynFreeVoice(ALSynth *s, ALVoice *v)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (p)
    {
        p->in_use = 0;
        p->playing = 0;
        p->owner = NULL;
    }
    if (v)
        v->pvoice = NULL;
}

static float vol_amp(s16 vol)
{
    /* libultra squares the voice volume (perceptual fade curve) */
    float v = vol < 0 ? 0.0f : (float)vol / 32767.0f;
    return v * v;
}

static void set_ramp(PVoice *p, float target, ALMicroTime t)
{
    s32 n = (s32)((double)t / us_per_sample);
    p->vol_target = target;
    if (n <= 0)
    {
        p->vol = target;
        p->vol_step = 0;
        p->vol_samples = 0;
    }
    else
    {
        p->vol_step = (target - p->vol) / (float)n;
        p->vol_samples = n;
    }
}

void alSynStartVoiceParams(ALSynth *s, ALVoice *v, void *w, f32 pitch, s16 vol, ALPan pan, u8 fxmix,
                           ALMicroTime t)
{
    PVoice *p = pv_of(v);
    const SynWave *wave = (const SynWave *)w;
    (void)s;

    if (!p || !wave || !wave->base)
        return;
    p->wave = wave;
    p->playing = 1;
    p->ratio = pitch;
    if (p->ratio > max_ratio)
        p->ratio = max_ratio;
    if (wave->type == AL_ADPCM_WAVE)
        p->nsamples = (u32)(wave->len / 9) * 16;
    else
        p->nsamples = (u32)(wave->len / 2);

    p->loop_start = p->loop_end = 0;
    p->loop_count = 0;
    p->loop_state = NULL;
    if (wave->loop)
    {
        if (wave->type == AL_ADPCM_WAVE)
        {
            const SynAdpcmLoop *l = (const SynAdpcmLoop *)wave->loop;
            p->loop_start = l->start;
            p->loop_end = l->end;
            p->loop_count = (s32)l->count;
            p->loop_state = l->state;
        }
        else
        {
            const SynRawLoop *l = (const SynRawLoop *)wave->loop;
            p->loop_start = l->start;
            p->loop_end = l->end;
            p->loop_count = (s32)l->count;
        }
        if (p->loop_end <= p->loop_start || p->loop_end > p->nsamples)
            p->loop_count = 0;
    }
    voice_begin(p);
    p->pan = pan > 127 ? 127 : pan;
    p->fxmix = fxmix > 127 ? 127 : fxmix;
    p->vol = 0.0f;
    set_ramp(p, vol_amp(vol), t);
}

void alSynStartVoice(ALSynth *s, ALVoice *v, void *w)
{
    alSynStartVoiceParams(s, v, w, 1.0f, 32767, 64, 0, 0);
}

void alSynStopVoice(ALSynth *s, ALVoice *v)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (p)
        p->playing = 0;
}

void alSynSetVol(ALSynth *s, ALVoice *v, s16 vol, ALMicroTime t)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (p)
        set_ramp(p, vol_amp(vol), t);
}

void alSynSetPitch(ALSynth *s, ALVoice *v, f32 ratio)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (p)
    {
        p->ratio = ratio;
        if (p->ratio > max_ratio)
            p->ratio = max_ratio;
    }
}

void alSynSetPan(ALSynth *s, ALVoice *v, ALPan pan)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (p)
        p->pan = pan > 127 ? 127 : pan;
}

void alSynSetFXMix(ALSynth *s, ALVoice *v, u8 fxmix)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (p)
        p->fxmix = fxmix > 127 ? 127 : fxmix;
}

void alSynSetPriority(ALSynth *s, ALVoice *v, s16 priority)
{
    PVoice *p = pv_of(v);
    (void)s;
    if (v)
        v->priority = priority;
    if (p)
        p->priority = priority;
}

/* ---- sample decoding ----------------------------------------------------- */

static inline s16 clamp16(s32 x)
{
    return (s16)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
}

/* Decodes one 9-byte VADPCM frame into out[16] using/updating hist. */
static void vadpcm_frame(const u8 *in, const SynBook *book, s16 hist[16], s16 out[16])
{
    s32 scale = 1 << (in[0] >> 4);
    s32 pred = in[0] & 0xf;
    s32 order = book->order;
    s32 ix[16];
    const s16 *b;
    int i, j, k, half;

    if (order < 1 || order > 4)
        order = 2;
    if (pred >= book->npredictors)
        pred = 0;
    b = &book->book[pred * order * 8];

    for (i = 0; i < 8; i++)
    {
        s32 hi = in[1 + i] >> 4, lo = in[1 + i] & 0xf;
        if (hi > 7)
            hi -= 16;
        if (lo > 7)
            lo -= 16;
        ix[i * 2] = hi * scale;
        ix[i * 2 + 1] = lo * scale;
    }

    for (half = 0; half < 2; half++)
    {
        s32 prev[4];
        const s32 *x = &ix[half * 8];
        for (j = 0; j < order; j++)
            prev[j] = half == 0 ? hist[16 - order + j] : out[8 - order + j];
        for (i = 0; i < 8; i++)
        {
            s32 acc = 0;
            for (j = 0; j < order; j++)
                acc += b[j * 8 + i] * prev[j];
            acc += x[i] << 11;
            for (k = 0; k < i; k++)
                acc += b[(order - 1) * 8 + (i - 1 - k)] * x[k];
            /* floor division by 2048 */
            acc = acc >= 0 ? acc >> 11 : -((-acc + 2047) >> 11);
            out[half * 8 + i] = clamp16(acc);
        }
    }
    memcpy(hist, out, 16 * sizeof(s16));
}

/* Makes win hold the 16-sample frame containing sample index idx. A jump
 * (loop) restarts the VADPCM decoder from the loop's saved state. */
static void load_frame(PVoice *p, s32 idx, int jumped)
{
    s32 fstart = idx & ~15;
    const SynWave *w = p->wave;

    if (!jumped && fstart == p->win_start)
        return;
    if (w->type == AL_ADPCM_WAVE)
    {
        s32 f = fstart >> 4;
        if (jumped || fstart != p->win_start + 16)
        {
            if (p->loop_state && fstart == (s32)(p->loop_start & ~15u))
                memcpy(p->hist, p->loop_state, sizeof(p->hist));
            else
                memset(p->hist, 0, sizeof(p->hist));
        }
        if ((f + 1) * 9 <= w->len)
            vadpcm_frame(w->base + f * 9, (const SynBook *)w->book, p->hist, p->win);
        else
            memset(p->win, 0, sizeof(p->win));
    }
    else
    {
        int i;
        for (i = 0; i < 16; i++)
        {
            s32 si = fstart + i;
            if ((u32)si < p->nsamples)
                p->win[i] = (s16)((w->base[si * 2] << 8) | w->base[si * 2 + 1]);
            else
                p->win[i] = 0;
        }
    }
    p->win_start = fstart;
}

/* Index of the sample after i, following the loop; -1 past the end. Sets
 * *jumped when the loop wraps. */
static s32 next_index(PVoice *p, s32 i, int *jumped)
{
    *jumped = 0;
    if (i < 0)
        return -1;
    if (p->loop_count != 0 && (u32)(i + 1) >= p->loop_end)
    {
        if (p->loop_count > 0)
            p->loop_count--;
        *jumped = 1;
        return (s32)p->loop_start;
    }
    if ((u32)(i + 1) >= p->nsamples)
        return -1;
    return i + 1;
}

static s16 fetch(PVoice *p, s32 idx, int jumped)
{
    if (idx < 0)
        return 0;
    if (jumped || idx < p->win_start || idx >= p->win_start + 16)
        load_frame(p, idx, jumped);
    return p->win[idx - p->win_start];
}

/* Moves the read cursor one source sample forward. */
static void step(PVoice *p)
{
    int jumped;
    p->ipos = p->npos;
    p->cur = p->nxt;
    p->npos = next_index(p, p->ipos, &jumped);
    p->nxt = fetch(p, p->npos, jumped);
}

static void voice_begin(PVoice *p)
{
    int jumped;
    p->win_start = -64;
    memset(p->hist, 0, sizeof(p->hist));
    p->frac = 0.0;
    p->ipos = 0;
    p->cur = fetch(p, 0, 0);
    p->npos = next_index(p, 0, &jumped);
    p->nxt = fetch(p, p->npos, jumped);
}

/* ---- mixing -------------------------------------------------------------- */

static void mix_voice(PVoice *p, float *dry_l, float *dry_r, float *wet, int n)
{
    float pl = eqpower[p->pan], pr = eqpower[127 - p->pan];
    float dry = eqpower[p->fxmix], wetamt = eqpower[127 - p->fxmix];
    int i;

    for (i = 0; i < n; i++)
    {
        float s;

        if (p->ipos < 0)
        {
            p->playing = 0;
            break;
        }
        s = ((float)p->cur + ((float)p->nxt - (float)p->cur) * (float)p->frac) * p->vol;
        dry_l[i] += s * pl * dry;
        dry_r[i] += s * pr * dry;
        wet[i] += s * wetamt;

        p->frac += p->ratio;
        while (p->frac >= 1.0)
        {
            p->frac -= 1.0;
            step(p);
            if (p->ipos < 0)
                break;
        }
        if (p->vol_samples > 0)
        {
            p->vol += p->vol_step;
            if (--p->vol_samples == 0)
                p->vol = p->vol_target;
        }
    }
}

#define CHUNK 256

static void render_span(s16 *out, int n)
{
    float dl[CHUNK], dr[CHUNK], wet[CHUNK];
    int i;

    while (n > 0)
    {
        int m = n > CHUNK ? CHUNK : n;
        memset(dl, 0, sizeof(float) * m);
        memset(dr, 0, sizeof(float) * m);
        memset(wet, 0, sizeof(float) * m);
        for (i = 0; i < MAX_PVOICES; i++)
        {
            PVoice *p = &pvoices[i];
            if (p->in_use && p->playing)
                mix_voice(p, dl, dr, wet, m);
            else if (p->in_use && p->vol_samples > 0)
            {
                /* keep ramps moving for stopped voices */
                p->vol_samples = p->vol_samples > m ? p->vol_samples - m : 0;
                if (!p->vol_samples)
                    p->vol = p->vol_target;
            }
        }
        for (i = 0; i < m; i++)
        {
            float r = reverb_sample(wet[i] * 0.5f);
            float l = dl[i] + r, rr = dr[i] + r;
            out[i * 2] = clamp16((s32)lrintf(l));
            out[i * 2 + 1] = clamp16((s32)lrintf(rr));
        }
        out += m * 2;
        n -= m;
    }
}

/* Player callbacks are scheduled in whole samples with the remainder
 * carried in pl_frac, so the 8333 us WESS tick does not drift. */
void Synth_Render(s16 *out, int frames)
{
    while (frames > 0)
    {
        ALPlayer *pl;
        s32 span = frames;

        /* run players whose time has come, and find the next event */
        for (pl = players; pl; pl = pl->next)
        {
            int slot = pl_slot(pl);
            while (pl->samplesLeft <= 0)
            {
                ALMicroTime dt = pl->handler ? pl->handler(pl) : 0;
                double acc;
                if (dt <= 0)
                    dt = 8333;
                pl->callTime += dt;
                acc = (double)dt / us_per_sample + (double)pl->samplesLeft + pl_frac[slot];
                pl->samplesLeft = (s32)acc;
                pl_frac[slot] = acc - (double)pl->samplesLeft;
            }
            if (pl->samplesLeft < span)
                span = pl->samplesLeft;
        }

        render_span(out, span);
        out += span * 2;
        frames -= span;
        for (pl = players; pl; pl = pl->next)
            pl->samplesLeft -= span;
    }
}

/* Debug: decodes a whole wave (no loops) at its own rate into out[max]. */
int Synth_DecodeWave(const void *w, s16 *out, int max)
{
    PVoice p;
    int n = 0;
    memset(&p, 0, sizeof(p));
    p.wave = (const SynWave *)w;
    if (!p.wave || !p.wave->base)
        return 0;
    p.nsamples = p.wave->type == AL_ADPCM_WAVE ? (u32)(p.wave->len / 9) * 16 : (u32)(p.wave->len / 2);
    voice_begin(&p);
    while (p.ipos >= 0 && n < max)
    {
        out[n++] = p.cur;
        step(&p);
    }
    return n;
}
