/*
 * rom.c - Doom 64 ROM detection, byte-order normalisation, and the virtual
 * cartridge address space used by osPiStartDma().
 *
 * Segment offsets per region come from Erick194's Doom 64 extractor
 * (tools/dm64ex.c in this repository).
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "rom.h"
#include "d64pc.h"

typedef struct {
    const char *name;
    uint32_t wad_off, wad_size;
    uint32_t wmd_off, wmd_size;
    uint32_t wsd_off, wsd_size;
    uint32_t wdd_off, wdd_size;
} romlayout_t;

static const romlayout_t rom_layouts[] = {
    { "Doom 64 (USA)",        0x63D10, 0x5D18B0, 0x6355C0, 0xB9E0, 0x640FA0, 0x142F8, 0x6552A0, 0x1716C4 },
    { "Doom 64 (Europe)",     0x63F60, 0x5D6CDC, 0x63AC40, 0xB9E0, 0x646620, 0x142F8, 0x65A920, 0x1716C4 },
    { "Doom 64 (Japan)",      0x64580, 0x5D8478, 0x63CA00, 0xB9E0, 0x6483E0, 0x142F8, 0x65C6E0, 0x1716C4 },
    { "Doom 64 (USA) (Rev 1)",0x63DC0, 0x5D301C, 0x636DE0, 0xB9E0, 0x6427C0, 0x142F8, 0x656AC0, 0x1716C4 },
};

static romsegdata_t segs[ROM_SEG_COUNT];
static char rom_desc[512];

static const uint32_t seg_base[ROM_SEG_COUNT] = {
    D64_ROM_WAD_BASE, D64_ROM_WMD_BASE, D64_ROM_WSD_BASE, D64_ROM_WDD_BASE
};

int ROM_NormalizeByteOrder(uint8_t *rom, size_t size)
{
    size_t i;
    if (size < 0x40)
        return 0;
    if (rom[0] == 0x80 && rom[1] == 0x37 && rom[2] == 0x12 && rom[3] == 0x40)
        return 1; /* .z64, already big-endian */
    if (rom[0] == 0x37 && rom[1] == 0x80 && rom[2] == 0x40 && rom[3] == 0x12)
    {   /* .v64: 16-bit byte swapped */
        for (i = 0; i + 1 < size; i += 2)
        {
            uint8_t t = rom[i]; rom[i] = rom[i + 1]; rom[i + 1] = t;
        }
        return 2;
    }
    if (rom[0] == 0x40 && rom[1] == 0x12 && rom[2] == 0x37 && rom[3] == 0x80)
    {   /* .n64: 32-bit little-endian words */
        for (i = 0; i + 3 < size; i += 4)
        {
            uint8_t a = rom[i], b = rom[i + 1];
            rom[i] = rom[i + 3]; rom[i + 1] = rom[i + 2];
            rom[i + 2] = b; rom[i + 3] = a;
        }
        return 3;
    }
    return 0;
}

int ROM_IdentifyImage(const uint8_t *rom, size_t size, const char **name)
{
    int region;
    const char *title = (const char *)rom + 0x20;

    if (size < 0x1000)
        return -1;
    if (SDL_strncasecmp(title, "Doom64", 6) != 0)
        return -1;

    switch (rom[0x3E])
    {
    case 'E': region = (rom[0x10] == 0x42) ? 3 : 0; break;
    case 'P': region = 1; break;
    case 'J': region = 2; break;
    default: return -1;
    }
    if (name)
        *name = rom_layouts[region].name;
    return region;
}

static int wad_header_ok(const uint8_t *p, size_t avail)
{
    int32_t numlumps, infotableofs;
    if (avail < 12 || memcmp(p, "IWAD", 4) != 0)
        return 0;
    memcpy(&numlumps, p + 4, 4);       /* little-endian like PC wads */
    memcpy(&infotableofs, p + 8, 4);
    return numlumps > 0 && numlumps < 100000 && infotableofs > 0 &&
           (size_t)infotableofs + (size_t)numlumps * 16 <= avail;
}

static uint8_t *dup_range(const uint8_t *src, size_t off, size_t len)
{
    uint8_t *p = (uint8_t *)malloc(len ? len : 1);
    if (p)
        memcpy(p, src + off, len);
    return p;
}

static void free_segs(void)
{
    int i;
    for (i = 0; i < ROM_SEG_COUNT; i++)
    {
        free(segs[i].data);
        segs[i].data = NULL;
        segs[i].size = 0;
    }
}

static int load_from_rom_image(const char *path, char *err, size_t errlen)
{
    size_t size = 0;
    uint8_t *rom = (uint8_t *)SDL_LoadFile(path, &size);
    const char *name = NULL;
    const romlayout_t *L;
    int order, region;

    if (!rom)
    {
        SDL_snprintf(err, errlen, "%s: %s", path, SDL_GetError());
        return 0;
    }
    order = ROM_NormalizeByteOrder(rom, size);
    if (!order)
    {
        SDL_snprintf(err, errlen, "%s: not an N64 ROM image", path);
        SDL_free(rom);
        return 0;
    }
    region = ROM_IdentifyImage(rom, size, &name);
    if (region < 0)
    {
        SDL_snprintf(err, errlen, "%s: N64 ROM, but not Doom 64", path);
        SDL_free(rom);
        return 0;
    }
    L = &rom_layouts[region];
    if ((size_t)L->wdd_off + L->wdd_size > size ||
        !wad_header_ok(rom + L->wad_off, L->wad_size))
    {
        SDL_snprintf(err, errlen, "%s: %s, but the data tables do not match (bad dump?)", path, name);
        SDL_free(rom);
        return 0;
    }

    free_segs();
    segs[ROM_SEG_WAD].data = dup_range(rom, L->wad_off, L->wad_size); segs[ROM_SEG_WAD].size = L->wad_size;
    segs[ROM_SEG_WMD].data = dup_range(rom, L->wmd_off, L->wmd_size); segs[ROM_SEG_WMD].size = L->wmd_size;
    segs[ROM_SEG_WSD].data = dup_range(rom, L->wsd_off, L->wsd_size); segs[ROM_SEG_WSD].size = L->wsd_size;
    segs[ROM_SEG_WDD].data = dup_range(rom, L->wdd_off, L->wdd_size); segs[ROM_SEG_WDD].size = L->wdd_size;
    SDL_free(rom);

    SDL_snprintf(rom_desc, sizeof(rom_desc), "%s [%s] from %s", name,
                 order == 1 ? "z64" : order == 2 ? "v64" : "n64", path);
    return 1;
}

static int has_rom_extension(const char *file)
{
    const char *dot = SDL_strrchr(file, '.');
    if (!dot)
        return 0;
    return !SDL_strcasecmp(dot, ".z64") || !SDL_strcasecmp(dot, ".n64") ||
           !SDL_strcasecmp(dot, ".v64") || !SDL_strcasecmp(dot, ".rom");
}

typedef struct {
    const char *dir;
    char *err;
    size_t errlen;
    int found;
} scanctx_t;

static SDL_EnumerationResult SDLCALL scan_cb(void *userdata, const char *dirname, const char *fname)
{
    scanctx_t *ctx = (scanctx_t *)userdata;
    char path[1024];
    char err[256];

    if (!has_rom_extension(fname))
        return SDL_ENUM_CONTINUE;
    SDL_snprintf(path, sizeof(path), "%s%s", dirname, fname);
    if (load_from_rom_image(path, err, sizeof(err)))
    {
        ctx->found = 1;
        return SDL_ENUM_SUCCESS;
    }
    SDL_Log("ROM scan: %s", err);
    SDL_snprintf(ctx->err, ctx->errlen, "%s", err);
    return SDL_ENUM_CONTINUE;
}

static int scan_directory(const char *dir, char *err, size_t errlen)
{
    scanctx_t ctx;
    ctx.dir = dir;
    ctx.err = err;
    ctx.errlen = errlen;
    ctx.found = 0;
    SDL_EnumerateDirectory(dir, scan_cb, &ctx);
    return ctx.found;
}

static int load_extracted(const char *dir)
{
    static const char *names[ROM_SEG_COUNT] = { "DOOM64.WAD", "DOOM64.WMD", "DOOM64.WSD", "DOOM64.WDD" };
    romsegdata_t tmp[ROM_SEG_COUNT];
    char path[1024];
    int i;

    memset(tmp, 0, sizeof(tmp));
    for (i = 0; i < ROM_SEG_COUNT; i++)
    {
        SDL_snprintf(path, sizeof(path), "%s%s", dir, names[i]);
        tmp[i].data = (uint8_t *)SDL_LoadFile(path, &tmp[i].size);
        if (!tmp[i].data)
        {   /* try lower case for case sensitive file systems */
            char lower[32];
            int j;
            for (j = 0; names[i][j] && j < 31; j++)
                lower[j] = (char)SDL_tolower((unsigned char)names[i][j]);
            lower[j] = 0;
            SDL_snprintf(path, sizeof(path), "%s%s", dir, lower);
            tmp[i].data = (uint8_t *)SDL_LoadFile(path, &tmp[i].size);
        }
        if (!tmp[i].data)
            break;
    }
    if (i != ROM_SEG_COUNT || !wad_header_ok(tmp[0].data, tmp[0].size))
    {
        for (i = 0; i < ROM_SEG_COUNT; i++)
            SDL_free(tmp[i].data);
        return 0;
    }
    free_segs();
    for (i = 0; i < ROM_SEG_COUNT; i++)
    {
        segs[i].data = dup_range(tmp[i].data, 0, tmp[i].size);
        segs[i].size = tmp[i].size;
        SDL_free(tmp[i].data);
    }
    SDL_snprintf(rom_desc, sizeof(rom_desc), "extracted DOOM64.WAD/WMD/WSD/WDD in %s", dir);
    return 1;
}

int ROM_Init(const char *explicit_path, char *errbuf, size_t errlen)
{
    char cwd[1024];
    char *sdlcwd;
    const char *base;

    errbuf[0] = 0;
    if (explicit_path && explicit_path[0])
        return load_from_rom_image(explicit_path, errbuf, errlen);

    sdlcwd = SDL_GetCurrentDirectory();
    SDL_snprintf(cwd, sizeof(cwd), "%s", sdlcwd ? sdlcwd : "./");
    SDL_free(sdlcwd);
    base = SDL_GetBasePath();

    if (scan_directory(cwd, errbuf, errlen))
        return 1;
    if (base && SDL_strcmp(base, cwd) != 0 && scan_directory(base, errbuf, errlen))
        return 1;
    if (load_extracted(cwd))
        return 1;
    if (base && load_extracted(base))
        return 1;

    if (!errbuf[0])
        SDL_snprintf(errbuf, errlen,
                     "No Doom 64 ROM found.\n\nPut your Doom 64 ROM (.z64, .n64 or .v64) in:\n%s\n"
                     "or pass -rom <file>, or set rom=<file> in doom64rtx.ini.", cwd);
    return 0;
}

void ROM_Shutdown(void)
{
    free_segs();
}

int ROM_Read(uint32_t addr, void *dst, uint32_t size)
{
    int i;
    for (i = 0; i < ROM_SEG_COUNT; i++)
    {
        if ((addr & 0xF0000000u) == seg_base[i])
        {
            uint32_t off = addr & 0x0FFFFFFFu;
            if (!segs[i].data || (size_t)off + size > segs[i].size)
                return 0;
            memcpy(dst, segs[i].data + off, size);
            return 1;
        }
    }
    return 0;
}

const romsegdata_t *ROM_Segment(romseg_t seg)
{
    return &segs[seg];
}

const char *ROM_Description(void)
{
    return rom_desc;
}
