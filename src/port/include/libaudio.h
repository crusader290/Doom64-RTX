/*
 * libaudio.h - clean-room PC replacement for the N64 audio library types the
 * WESS sound system uses. Implemented by src/port/n64synth.c.
 * Doom64-RTX PC port, GPLv3.
 */
#ifndef D64PC_LIBAUDIO_H
#define D64PC_LIBAUDIO_H

#include "ultra64.h"

typedef s32 ALMicroTime;
typedef u8  ALPan;
typedef s32 ALFxId;

#define AL_ADPCM_WAVE 0
#define AL_RAW16_WAVE 1

#define AL_FX_NONE      0
#define AL_FX_SMALLROOM 1
#define AL_FX_BIGROOM   2
#define AL_FX_CHORUS    3
#define AL_FX_FLANGE    4
#define AL_FX_ECHO      5
#define AL_FX_CUSTOM    6

typedef struct ALLink_s {
    struct ALLink_s *next;
    struct ALLink_s *prev;
} ALLink;

void alUnlink(ALLink *element);
void alLink(ALLink *element, ALLink *after);

typedef struct {
    u8  *base;
    u8  *cur;
    s32  len;
    s32  count;
} ALHeap;

void  alHeapInit(ALHeap *hp, u8 *base, s32 len);
void *alHeapDBAlloc(u8 *file, s32 line, ALHeap *hp, s32 num, s32 size);
#define alHeapAlloc(hp, elem, size) alHeapDBAlloc(0, 0, (hp), (elem), (size))

typedef s32 (*ALDMAproc)(s32 addr, s32 len, void *state);
typedef ALDMAproc (*ALDMANew)(void *arg);

typedef struct ALPlayer_s {
    struct ALPlayer_s *next;
    void              *clientData;
    ALMicroTime      (*handler)(void *);
    ALMicroTime        callTime;
    s32                samplesLeft;
} ALPlayer;

typedef struct {
    s16 priority;
    s16 fxBus;
    u8  unityPitch;
} ALVoiceConfig;

typedef struct ALVoice_s {
    ALLink  node;
    void   *pvoice;   /* synth private state */
    void   *clientPrivate;
    s16     priority;
    s16     fxBus;
    s16     unityPitch;
} ALVoice;

typedef struct {
    s32         maxVVoices;
    s32         maxPVoices;
    s32         maxUpdates;
    s32         maxFXbusses;
    void       *dmaproc;
    ALHeap     *heap;
    s32         outputRate;
    ALFxId      fxType[1];
    s32        *params;
} ALSynConfig;

typedef struct {
    ALPlayer *head;
    s32       outputRate;
    void     *priv;
} ALSynth;

typedef struct {
    ALSynth drvr;
} ALGlobals;

extern ALGlobals *alGlobals;

void alInit(ALGlobals *glob, ALSynConfig *c);
void alClose(ALGlobals *glob);

void alSynAddPlayer(ALSynth *s, ALPlayer *client);
void alSynRemovePlayer(ALSynth *s, ALPlayer *client);
s32  alSynAllocVoice(ALSynth *s, ALVoice *v, ALVoiceConfig *vc);
void alSynFreeVoice(ALSynth *s, ALVoice *voice);
void alSynStartVoice(ALSynth *s, ALVoice *voice, void *w);
void alSynStartVoiceParams(ALSynth *s, ALVoice *voice, void *w,
                           f32 pitch, s16 vol, ALPan pan, u8 fxmix, ALMicroTime t);
void alSynStopVoice(ALSynth *s, ALVoice *voice);
void alSynSetVol(ALSynth *s, ALVoice *voice, s16 vol, ALMicroTime t);
void alSynSetPitch(ALSynth *s, ALVoice *voice, f32 ratio);
void alSynSetPan(ALSynth *s, ALVoice *voice, ALPan pan);
void alSynSetFXMix(ALSynth *s, ALVoice *voice, u8 fxmix);
void alSynSetPriority(ALSynth *s, ALVoice *voice, s16 priority);

typedef s64 Acmd;
typedef short ADPCM_STATE[16];

#endif
