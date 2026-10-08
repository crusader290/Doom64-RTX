/*
 * d64pc.h - host compatibility glue pulled into doomdef.h on PC builds.
 * Doom64-RTX PC port, GPLv3.
 */
#ifndef D64PC_H
#define D64PC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* The ROM image is exposed through virtual "cartridge" addresses that
 * osPiStartDma() (src/port/os.c) resolves via src/port/rom.c. */
#define D64_ROM_WAD_BASE 0x10000000u
#define D64_ROM_WMD_BASE 0x20000000u
#define D64_ROM_WSD_BASE 0x30000000u
#define D64_ROM_WDD_BASE 0x40000000u
#define _doom64_wadSegmentRomStart ((char *)(uintptr_t)D64_ROM_WAD_BASE)
#define _doom64_wmdSegmentRomStart ((char *)(uintptr_t)D64_ROM_WMD_BASE)
#define _doom64_wsdSegmentRomStart ((char *)(uintptr_t)D64_ROM_WSD_BASE)
#define _doom64_wddSegmentRomStart ((char *)(uintptr_t)D64_ROM_WDD_BASE)

/* MIPS FPU control register: the game switches to truncating float->int
 * conversion, which is what C casts already do on the host. */
#define FPCSR_RM_RM 0x00000003
#define __osGetFpcCsr() 0u
#define __osSetFpcCsr(x) ((void)(x))

/* Big-endian helpers for data the N64 CPU read natively (asset headers). */
static inline uint16_t d64_bswap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t d64_bswap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xff00u) | ((v & 0xff00u) << 8) | (v << 24);
}
/* C11 _Generic keeps the field's signedness (MSVC has no __typeof__ in C). */
#define BE16(x) _Generic((x), \
    unsigned short: (unsigned short)d64_bswap16((uint16_t)(x)), \
    default: (short)d64_bswap16((uint16_t)(x)))
#define BE32(x) _Generic((x), \
    unsigned int: (unsigned int)d64_bswap32((uint32_t)(x)), \
    default: (int)d64_bswap32((uint32_t)(x)))

/* The original printf-alike took a hand-rolled argument pointer. */
#define D_vsprintf(buf, fmt, args) vsprintf((buf), (fmt), (args))

/* Prototypes the original code relied on implicitly */
void ST_Init(void);
void ST_InitEveryLevel(void);
void ST_UpdateFlash(void);
void ST_Message(int x, int y, char *text, int color);
void ST_DrawString(int x, int y, char *text, int color);
void P_CheckCheats(void);
void P_RefreshBrightness(void);
void P_SpawnPlayer(void);
void G_PlayerFinishLevel(int player);

/* PC platform services (src/port) */
void I_PCFatal(const char *fmt, ...);
int  I_PCMouseTurn(void);
void I_PCAudioUpdate(void);
void I_PCToggleRaytracing(void);

#endif
