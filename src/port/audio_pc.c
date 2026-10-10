/*
 * audio_pc.c - PC replacement for the game's N64 audio manager (audio.c):
 * WESS initialisation, cartridge reads for the sound banks, and SDL3 audio
 * output fed by the software synth in n64synth.c.
 *
 * Threading: the synth (and the WESS sequencer it calls every 8.333 ms of
 * audio) runs on SDL's audio thread. The game enters WESS only through
 * s_sound.c, whose functions take I_PCAudioLock(), so the two never run
 * WESS code at the same time.
 *
 * Test hook: D64_WAVOUT=<file.wav> renders audio on the game thread instead
 * (exactly 1/30 s per presented frame, deterministic with
 * D64_FIXED_TIMESTEP) and writes it to a WAV file.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ultra64.h"
#include "libaudio.h"
#include "wessapi.h"
#include "wessarc.h"
#include "wessshell.h"

#include "config.h"
#include "n64synth.h"
#include "soundpack.h"
#include "intro_audio.h"
#include "rom.h"

extern void N64_wdd_location(char *wdd_location);
extern void N64_set_output_rate(u32 rate);
extern int wesssys_init(void);
extern void wesssys_exit(int rflag);

ALVoice *voice;
char *reverb_status;

static ALGlobals am_globals;
static u32 init_completed;

static SDL_AudioStream *stream;
static SDL_Mutex *audio_lock;
static int pump_mode;          /* render on the game thread */
static Uint64 pump_last_ns;
static FILE *wav_file;
static Uint32 wav_frames;
static s32 rate;

/* ---- locking ------------------------------------------------------------- */

void I_PCAudioLock(void)
{
    if (audio_lock)
        SDL_LockMutex(audio_lock);
}

void I_PCAudioUnlock(void)
{
    if (audio_lock)
        SDL_UnlockMutex(audio_lock);
}

/* wess_disable/wess_enable: the outer lock already serialises WESS. */
unsigned int wesssys_disable_ints(void)
{
    return 0;
}

void wesssys_restore_ints(unsigned int state)
{
    (void)state;
}

/* ---- audio.c API ---------------------------------------------------------- */

int wess_memfill(void *dst, unsigned char fill, int count)
{
    memset(dst, fill, (size_t)count);
    return 0;
}

int wess_rom_copy(char *src, char *dest, int len)
{
    if (init_completed && len > 0)
    {
        if (!ROM_Read((uint32_t)(uintptr_t)src, dest, (uint32_t)len))
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "audio: ROM read failed at 0x%08x (+%d)",
                        (unsigned)(uintptr_t)src, len);
            memset(dest, 0, (size_t)len);
        }
        return len;
    }
    return 0;
}

s32 milli_to_param(s32 paramvalue, s32 samplerate)
{
    return (s32)((f32)paramvalue * ((f32)samplerate / 1000.0f)) & ~0x7;
}

static void wav_write_header(void)
{
    Uint8 h[44];
    Uint32 data = wav_frames * 4;
    memcpy(h, "RIFF", 4);
    SDL_memcpy(h + 4, &(Uint32){ SDL_Swap32LE(36 + data) }, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    SDL_memcpy(h + 16, &(Uint32){ SDL_Swap32LE(16) }, 4);
    SDL_memcpy(h + 20, &(Uint16){ SDL_Swap16LE(1) }, 2);
    SDL_memcpy(h + 22, &(Uint16){ SDL_Swap16LE(2) }, 2);
    SDL_memcpy(h + 24, &(Uint32){ SDL_Swap32LE((Uint32)rate) }, 4);
    SDL_memcpy(h + 28, &(Uint32){ SDL_Swap32LE((Uint32)rate * 4) }, 4);
    SDL_memcpy(h + 32, &(Uint16){ SDL_Swap16LE(4) }, 2);
    SDL_memcpy(h + 34, &(Uint16){ SDL_Swap16LE(16) }, 2);
    memcpy(h + 36, "data", 4);
    SDL_memcpy(h + 40, &(Uint32){ SDL_Swap32LE(data) }, 4);
    fseek(wav_file, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), wav_file);
    fseek(wav_file, 0, SEEK_END);
}

static void render_frames(int frames)
{
    static s16 buf[1024 * 2];
    while (frames > 0)
    {
        int n = frames > 1024 ? 1024 : frames;
        Synth_Render(buf, n);
        PCSound_Mix(buf,n);
        PCIntroAudio_Mix(buf,n);
        if (stream && !pump_mode)
            SDL_PutAudioStreamData(stream, buf, n * 4);
        if (wav_file)
        {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            int i;
            for (i = 0; i < n * 2; i++)
                buf[i] = (s16)SDL_Swap16LE((Uint16)buf[i]);
#endif
            fwrite(buf, 4, (size_t)n, wav_file);
            wav_frames += (Uint32)n;
        }
        frames -= n;
    }
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *s, int additional_amount, int total_amount)
{
    (void)userdata;
    (void)s;
    (void)total_amount;
    if (additional_amount <= 0)
        return;
    SDL_LockMutex(audio_lock);
    render_frames(additional_amount / 4);
    SDL_UnlockMutex(audio_lock);
}

/* Called once per presented frame by the platform layer. */
void I_PCAudioUpdate(void)
{
    if (!init_completed || !pump_mode)
        return;
    I_PCAudioLock();
    if (wav_file || SDL_getenv("D64_FIXED_TIMESTEP"))
    {
        render_frames(rate / 30);
    }
    else
    {
        /* no device: keep the sequencer running in real time */
        Uint64 now = SDL_GetTicksNS();
        Uint64 frames = pump_last_ns ? (now - pump_last_ns) * (Uint64)rate / 1000000000ull : 0;
        if (frames > (Uint64)rate / 4)
            frames = (Uint64)rate / 4;
        if (frames)
            render_frames((int)frames);
        pump_last_ns = pump_last_ns && frames ? pump_last_ns + frames * 1000000000ull / (Uint64)rate : now;
    }
    I_PCAudioUnlock();
}

static void wav_close(void)
{
    if (wav_file)
    {
        wav_write_header();
        fclose(wav_file);
        wav_file = NULL;
    }
}

static void open_output(void)
{
    const char *wav = SDL_getenv("D64_WAVOUT");
    SDL_AudioSpec spec;

    if (wav && *wav)
    {
        wav_file = fopen(wav, "wb");
        if (wav_file)
        {
            wav_frames = 0;
            wav_write_header();
            atexit(wav_close);
            pump_mode = 1;
            SDL_Log("audio: writing %s (%d Hz)", wav, rate);
            return;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "audio: cannot write %s", wav);
    }

    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = rate;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (!stream)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "audio: no output device (%s); running silent", SDL_GetError());
        pump_mode = 1;
        return;
    }
    {
        SDL_AudioSpec dev;
        int frames = 0;
        if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &dev, &frames))
            SDL_Log("audio: %s, device %d Hz %d ch, %d frame buffer; synth %d Hz",
                    SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?", dev.freq, dev.channels,
                    frames, rate);
    }
    SDL_ResumeAudioStreamDevice(stream);
}

void wess_init(WessConfig *wessconfig)
{
    ALSynConfig config;

    if (!audio_lock)
        audio_lock = SDL_CreateMutex();

    rate = pc_config.audio_rate;
    if (rate < 11025)
        rate = 11025;
    if (rate > 96000)
        rate = 96000;

    N64_wdd_location(wessconfig->wdd_location);
    N64_set_output_rate((u32)rate);

    memset(&config, 0, sizeof(config));
    config.maxPVoices = config.maxVVoices = wess_driver_voices;
    config.maxUpdates = wess_driver_updates;
    config.fxType[0] = wessconfig->reverb_id;
    config.outputRate = rate;
    config.heap = wessconfig->heap_ptr;

    voice = (ALVoice *)SDL_calloc((size_t)wess_driver_voices, sizeof(ALVoice));
    reverb_status = (char *)SDL_calloc((size_t)wess_driver_voices, 1);

    alInit(&am_globals, &config);
    SSP_SeqpNew();
    wesssys_init();
    init_completed = 1;
    PCSound_Init(rate);
    PCIntroAudio_Init(rate);

    if (pc_config.sound)
        open_output();
    else
        pump_mode = 1;
}

/* Test hook D64_DUMPSAMPLES=<dir>: writes every bank sample as a WAV
 * (22050 Hz, raw decode) after the module is loaded. */
void I_PCAudioDumpSamples(void)
{
    extern patchinfo_header *samplesbase;
    extern patch_group_data *ppgd;
    const char *dir = SDL_getenv("D64_DUMPSAMPLES");
    static s16 buf[1 << 20];
    int i, count;

    if (!dir || !*dir || !samplesbase || !ppgd)
        return;
    count = ppgd->pat_grp_hdr.patchinfo;
    for (i = 0; i < count; i++)
    {
        char path[1024];
        FILE *f;
        int n = Synth_DecodeWave(&samplesbase[i].wave, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        Uint8 h[44];
        Uint32 data = (Uint32)n * 2;
        SDL_snprintf(path, sizeof(path), "%s/s%03d_%s_p%d.wav", dir, i,
                     samplesbase[i].wave.type == AL_ADPCM_WAVE ? "adpcm" : "raw", samplesbase[i].pitch);
        f = fopen(path, "wb");
        if (!f)
            continue;
        memcpy(h, "RIFF", 4);
        SDL_memcpy(h + 4, &(Uint32){ SDL_Swap32LE(36 + data) }, 4);
        memcpy(h + 8, "WAVEfmt ", 8);
        SDL_memcpy(h + 16, &(Uint32){ SDL_Swap32LE(16) }, 4);
        SDL_memcpy(h + 20, &(Uint16){ SDL_Swap16LE(1) }, 2);
        SDL_memcpy(h + 22, &(Uint16){ SDL_Swap16LE(1) }, 2);
        SDL_memcpy(h + 24, &(Uint32){ SDL_Swap32LE(22050) }, 4);
        SDL_memcpy(h + 28, &(Uint32){ SDL_Swap32LE(44100) }, 4);
        SDL_memcpy(h + 32, &(Uint16){ SDL_Swap16LE(2) }, 2);
        SDL_memcpy(h + 34, &(Uint16){ SDL_Swap16LE(16) }, 2);
        memcpy(h + 36, "data", 4);
        SDL_memcpy(h + 40, &(Uint32){ SDL_Swap32LE(data) }, 4);
        fwrite(h, 1, 44, f);
        fwrite(buf, 2, (size_t)n, f);
        fclose(f);
    }
    SDL_Log("audio: dumped %d samples to %s", count, dir);
}

/* The N64 version builds an RSP task list here; the synth runs on its own. */
OSTask *wess_work(void)
{
    return NULL;
}

void wess_exit(void)
{
    if (stream)
    {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
    I_PCAudioLock();
    if (init_completed)
    {
        wesssys_exit(1);
        alClose(&am_globals);
        init_completed = 0;
        PCSound_Shutdown();
        PCIntroAudio_Shutdown();
    }
    wav_close();
    I_PCAudioUnlock();
}

/* WESS asks for the cartridge address of the WDD sample bank; on PC the ROM
 * image is already in memory, so voices read samples from it directly. */
unsigned char *I_PCWddData(char *wdd_location)
{
    const romsegdata_t *seg = ROM_Segment(ROM_SEG_WDD);
    (void)wdd_location;
    if (!seg || !seg->data)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "audio: no WDD sample bank");
        return NULL;
    }
    SDL_Log("audio: WDD sample bank %u bytes", (unsigned)seg->size);
    return seg->data;
}

int I_PCIntroMusic(int start)
{
    int result=0;I_PCAudioLock();
    if(init_completed) { if(start) result=PCIntroAudio_Start();else PCIntroAudio_Stop(); }
    I_PCAudioUnlock();return result;
}
