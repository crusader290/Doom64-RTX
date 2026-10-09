/*
 * respack.c - GZDoom-style resource packs: PNG replacements for Doom 64
 * textures and sprites, looked up by WAD lump name.
 *
 * Packs are .pk3 (zip) files or plain folders found in packs/ next to the
 * executable, in the working directory and in the preference folder, plus
 * any listed in the ini (packs = a.pk3;b) or with -pack <file>. Later packs
 * override earlier ones. Inside a pack any folder works; the file name
 * (without extension) must equal the lump name, e.g. textures/C1.png or
 * sprites/SKULA1.png.
 *
 * How a replacement finds its way to the screen: W_CacheLumpNum registers
 * each texture/sprite lump's decompressed memory here; the display-list
 * interpreter asks which lump (and which rows of it) a TMEM load came from
 * and uploads the matching part of the PNG instead of the N64 pixels. UVs
 * are normalised, so higher resolutions need no other change.
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>

#include "respack.h"
#include "config.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "third_party/stb_image.h"

/* ---- packs and their files --------------------------------------------- */

typedef struct {
    char path[1024];
    int is_dir;
    Uint8 *zip;        /* whole zip file in memory */
    size_t zip_size;
} pack_t;

typedef struct {
    char name[9];      /* lump name, upper case */
    int pack;
    char file[512];    /* folder packs: full path */
    Uint32 method, csize, usize, local_ofs; /* zip entry */
    /* decoded image cache */
    Uint8 *rgba;
    int w, h;
    int failed;
} entry_t;

static pack_t *packs;
static int npacks;
static entry_t *entries;
static int nentries, centries;
static int enabled = 1;

static Uint32 rd16(const Uint8 *p) { return (Uint32)p[0] | ((Uint32)p[1] << 8); }
static Uint32 rd32(const Uint8 *p) { return rd16(p) | (rd16(p + 2) << 16); }

static int find_entry(const char *name)
{
    int i;
    for (i = nentries - 1; i >= 0; i--) /* last pack wins */
        if (!SDL_strcmp(entries[i].name, name))
            return i;
    return -1;
}

static entry_t *add_entry(const char *filename)
{
    const char *base = SDL_strrchr(filename, '/');
    const char *dot;
    char name[9];
    size_t n, i;
    entry_t *e;

    base = base ? base + 1 : filename;
    dot = SDL_strrchr(base, '.');
    if (!dot || SDL_strcasecmp(dot, ".png"))
        return NULL;
    n = (size_t)(dot - base);
    if (n == 0 || n > 8)
        return NULL;
    for (i = 0; i < n; i++)
        name[i] = (char)SDL_toupper((unsigned char)base[i]);
    name[n] = 0;
    if (nentries == centries)
    {
        centries = centries ? centries * 2 : 256;
        entries = SDL_realloc(entries, sizeof(entry_t) * (size_t)centries);
    }
    e = &entries[nentries++];
    SDL_memset(e, 0, sizeof(*e));
    SDL_strlcpy(e->name, name, sizeof(e->name));
    e->pack = npacks - 1;
    return e;
}

static void scan_zip(void)
{
    pack_t *p = &packs[npacks - 1];
    const Uint8 *z = p->zip;
    size_t n = p->zip_size, i, cd, pos;
    int count = 0, k, total;

    if (n < 22)
        return;
    for (i = n - 22;; i--) /* end of central directory */
    {
        if (rd32(z + i) == 0x06054b50)
            break;
        if (i == 0 || n - i > 65536 + 22)
            return;
    }
    total = (int)rd16(z + i + 10);
    cd = rd32(z + i + 16);
    pos = cd;
    for (k = 0; k < total && pos + 46 <= n; k++)
    {
        Uint32 nlen = rd16(z + pos + 28), xlen = rd16(z + pos + 30), clen = rd16(z + pos + 32);
        char fname[512];
        entry_t *e;
        if (rd32(z + pos) != 0x02014b50 || pos + 46 + nlen > n)
            break;
        SDL_memcpy(fname, z + pos + 46, nlen < 511 ? nlen : 511);
        fname[nlen < 511 ? nlen : 511] = 0;
        e = add_entry(fname);
        if (e)
        {
            e->method = rd16(z + pos + 10);
            e->csize = rd32(z + pos + 20);
            e->usize = rd32(z + pos + 24);
            e->local_ofs = rd32(z + pos + 42);
            count++;
        }
        pos += 46 + nlen + xlen + clen;
    }
    SDL_Log("resource pack: %s (%d images)", p->path, count);
}

static int count_dir;

static SDL_EnumerationResult SDLCALL scan_dir_cb(void *userdata, const char *dirname, const char *fname)
{
    char full[1024];
    SDL_PathInfo info;
    (void)userdata;
    SDL_snprintf(full, sizeof(full), "%s%s", dirname, fname);
    if (!SDL_GetPathInfo(full, &info))
        return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY)
    {
        SDL_strlcat(full, "/", sizeof(full));
        SDL_EnumerateDirectory(full, scan_dir_cb, NULL);
    }
    else
    {
        entry_t *e = add_entry(full);
        if (e)
        {
            SDL_strlcpy(e->file, full, sizeof(e->file));
            count_dir++;
        }
    }
    return SDL_ENUM_CONTINUE;
}

static void add_pack(const char *path)
{
    SDL_PathInfo info;
    pack_t *p;
    int i;

    if (!SDL_GetPathInfo(path, &info))
        return;
    for (i = 0; i < npacks; i++)
        if (!SDL_strcmp(packs[i].path, path))
            return;
    packs = SDL_realloc(packs, sizeof(pack_t) * (size_t)(npacks + 1));
    p = &packs[npacks++];
    SDL_memset(p, 0, sizeof(*p));
    SDL_strlcpy(p->path, path, sizeof(p->path));
    if (info.type == SDL_PATHTYPE_DIRECTORY)
    {
        char dir[1024];
        p->is_dir = 1;
        SDL_snprintf(dir, sizeof(dir), "%s%s", path, path[SDL_strlen(path) - 1] == '/' ? "" : "/");
        count_dir = 0;
        SDL_EnumerateDirectory(dir, scan_dir_cb, NULL);
        SDL_Log("resource pack: %s (folder, %d images)", path, count_dir);
    }
    else
    {
        p->zip = SDL_LoadFile(path, &p->zip_size);
        if (p->zip)
            scan_zip();
    }
}

static SDL_EnumerationResult SDLCALL packs_dir_cb(void *userdata, const char *dirname, const char *fname)
{
    char full[1024];
    const char *dot = SDL_strrchr(fname, '.');
    SDL_PathInfo info;
    (void)userdata;
    SDL_snprintf(full, sizeof(full), "%s%s", dirname, fname);
    if (!SDL_GetPathInfo(full, &info))
        return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY ||
        (dot && (!SDL_strcasecmp(dot, ".pk3") || !SDL_strcasecmp(dot, ".zip"))))
        add_pack(full);
    return SDL_ENUM_CONTINUE;
}

static void scan_packs_dir(const char *base)
{
    char dir[1024];
    if (!base)
        return;
    SDL_snprintf(dir, sizeof(dir), "%spacks/", base);
    SDL_EnumerateDirectory(dir, packs_dir_cb, NULL); /* alphabetical order not guaranteed: fine, names rarely clash */
}

void ResPack_Init(void)
{
    char *cwd = SDL_GetCurrentDirectory();
    char *pref = SDL_GetPrefPath("Doom64RTX", "Doom64RTX");
    const char *exe = SDL_GetBasePath();
    char list[1024], *tok, *save;

    enabled = pc_config.respacks;
    scan_packs_dir(exe);
    if (cwd && (!exe || SDL_strcmp(cwd, exe)))
        scan_packs_dir(cwd);
    scan_packs_dir(pref);
    SDL_strlcpy(list, pc_config.packs, sizeof(list));
    for (tok = SDL_strtok_r(list, ";", &save); tok; tok = SDL_strtok_r(NULL, ";", &save))
        if (*tok)
            add_pack(tok);
    SDL_free(cwd);
    SDL_free(pref);
    if (npacks)
        SDL_Log("resource packs: %d loaded, %d replacement images%s", npacks, nentries,
                enabled ? "" : " (disabled in settings)");
}

int ResPack_Count(void)
{
    return enabled ? nentries : 0;
}

void ResPack_SetEnabled(int on)
{
    enabled = on;
}

static Uint8 *read_entry(entry_t *e, size_t *len)
{
    pack_t *p = &packs[e->pack];
    if (p->is_dir)
        return SDL_LoadFile(e->file, len);
    {
        const Uint8 *z = p->zip;
        size_t lo = e->local_ofs, data;
        if (lo + 30 > p->zip_size || rd32(z + lo) != 0x04034b50)
            return NULL;
        data = lo + 30 + rd16(z + lo + 26) + rd16(z + lo + 28);
        if (data + e->csize > p->zip_size)
            return NULL;
        if (e->method == 0)
        {
            Uint8 *out = SDL_malloc(e->csize);
            SDL_memcpy(out, z + data, e->csize);
            *len = e->csize;
            return out;
        }
        if (e->method == 8)
        {
            int outlen = 0;
            char *out = stbi_zlib_decode_noheader_malloc((const char *)z + data, (int)e->csize, &outlen);
            *len = (size_t)outlen;
            if (out)
            {
                Uint8 *copy = SDL_malloc((size_t)outlen);
                SDL_memcpy(copy, out, (size_t)outlen);
                STBI_FREE(out);
                return copy;
            }
        }
    }
    return NULL;
}

const Uint8 *ResPack_Image(const char *lumpname, int *w, int *h)
{
    char name[9];
    int i, idx;
    entry_t *e;

    if (!enabled || !nentries || !lumpname)
        return NULL;
    for (i = 0; i < 8 && lumpname[i]; i++)
        name[i] = (char)SDL_toupper((unsigned char)(lumpname[i] & 0x7f));
    name[i] = 0;
    idx = find_entry(name);
    if (idx < 0)
        return NULL;
    e = &entries[idx];
    if (!e->rgba && !e->failed)
    {
        size_t len = 0;
        Uint8 *file = read_entry(e, &len);
        int comp;
        if (file)
            e->rgba = stbi_load_from_memory(file, (int)len, &e->w, &e->h, &comp, 4);
        SDL_free(file);
        if (!e->rgba)
        {
            e->failed = 1;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "resource pack: cannot decode %s", e->name);
        }
    }
    if (!e->rgba)
        return NULL;
    *w = e->w;
    *h = e->h;
    return e->rgba;
}

/* ---- lump memory registry -------------------------------------------------- */

typedef struct {
    const Uint8 *base;
    int size;
    int lump;
    respack_src_t info;
} reglump_t;

static reglump_t *regs;
static int nregs, cregs;
static void *const *lumpcache_ptrs; /* &lumpcache[0].cache, stride sizeof(void*) */

void ResPack_RegisterLump(int lump, const char *name8, int kind, const void *data, int size)
{
    const Uint8 *b = data;
    int lo = 0, hi, i;
    reglump_t r;

    if (!data || size <= 0 || kind == RESPACK_OTHER)
        return;
    SDL_memset(&r, 0, sizeof(r));
    r.base = b;
    r.size = size;
    r.lump = lump;
    for (i = 0; i < 8 && name8[i]; i++)
        r.info.name[i] = (char)SDL_toupper((unsigned char)(name8[i] & 0x7f));
    r.info.name[i] = 0;
    r.info.kind = kind;
    if (kind == RESPACK_SPRITE && size >= 16)
    {
        /* spriteN64_t (big-endian): tiles, compressed, cmpsize, xoffs, yoffs, width, height, tileheight */
        r.info.header = 16;
        r.info.pixbytes = ((b[4] << 8) | b[5]);
        r.info.width = (b[10] << 8) | b[11];
        r.info.height = (b[12] << 8) | b[13];
    }
    else
    {
        r.info.header = 8; /* textureN64_t */
    }

    /* drop registrations this one overlaps (memory reused by the zone) */
    for (i = 0; i < nregs;)
    {
        if (regs[i].base < b + size && b < regs[i].base + regs[i].size)
        {
            SDL_memmove(&regs[i], &regs[i + 1], sizeof(reglump_t) * (size_t)(nregs - i - 1));
            nregs--;
        }
        else
            i++;
    }
    if (nregs == cregs)
    {
        cregs = cregs ? cregs * 2 : 512;
        regs = SDL_realloc(regs, sizeof(reglump_t) * (size_t)cregs);
    }
    hi = nregs;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (regs[mid].base < b)
            lo = mid + 1;
        else
            hi = mid;
    }
    SDL_memmove(&regs[lo + 1], &regs[lo], sizeof(reglump_t) * (size_t)(nregs - lo));
    regs[lo] = r;
    nregs++;
}

void ResPack_SetLumpCache(void *const *cache_array)
{
    lumpcache_ptrs = cache_array;
}

int ResPack_SourceOf(const void *addr, respack_src_t *out)
{
    const Uint8 *a = addr;
    int lo = 0, hi = nregs;

    if (!enabled || !nentries || !nregs)
        return 0;
    while (lo < hi) /* last registration with base <= a */
    {
        int mid = (lo + hi) / 2;
        if (regs[mid].base <= a)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return 0;
    {
        reglump_t *r = &regs[lo - 1];
        if (a >= r->base + r->size)
            return 0;
        /* still the cached copy of that lump? */
        if (lumpcache_ptrs && lumpcache_ptrs[r->lump] != (const void *)r->base)
            return 0;
        *out = r->info;
        out->offset = (int)(a - r->base);
        return 1;
    }
}
