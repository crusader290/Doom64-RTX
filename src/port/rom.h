/*
 * rom.h - Doom 64 ROM detection and the virtual cartridge address space.
 * Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_ROM_H
#define D64_ROM_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    ROM_SEG_WAD,
    ROM_SEG_WMD,
    ROM_SEG_WSD,
    ROM_SEG_WDD,
    ROM_SEG_COUNT
} romseg_t;

typedef struct {
    uint8_t *data;
    size_t   size;
} romsegdata_t;

/* Locate game data and load it. Search order:
 *   1. explicit path (config "rom=" or "-rom <file>" on the command line)
 *   2. ROM images (.z64/.n64/.v64, any byte order) in the working directory
 *   3. ROM images next to the executable
 *   4. pre-extracted DOOM64.WAD/.WMD/.WSD/.WDD in those directories
 * Returns 1 on success; on failure fills errbuf with a user readable reason. */
int ROM_Init(const char *explicit_path, char *errbuf, size_t errlen);
void ROM_Shutdown(void);

/* Read from the virtual cartridge address space (see d64pc.h bases). */
int ROM_Read(uint32_t addr, void *dst, uint32_t size);

const romsegdata_t *ROM_Segment(romseg_t seg);
const char *ROM_Description(void);

/* Exposed for tests/tools: identifies a ROM image already in memory
 * (big-endian .z64 order) and returns the region index or -1. */
int ROM_IdentifyImage(const uint8_t *rom, size_t size, const char **name);
/* Converts .n64/.v64 byte orders to .z64 in place. Returns 0 if unknown. */
int ROM_NormalizeByteOrder(uint8_t *rom, size_t size);

#endif
