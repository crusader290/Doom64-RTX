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
#include "native_addons.h"

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

enum { MAP_ALBEDO, MAP_ORM, MAP_NORMAL, MAP_EMISSIVE, MAP_COUNT };

typedef struct {
    char name[9];      /* lump name, upper case */
    int map;           /* MAP_* (file suffix _orm, _n, _e; none = albedo) */
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

static int find_entry_map(const char *name, int map)
{
    int i;
    for (i = nentries - 1; i >= 0; i--) /* last pack wins */
        if (entries[i].map == map && !SDL_strcmp(entries[i].name, name))
            return i;
    return -1;
}

static int find_entry(const char *name)
{
    return find_entry_map(name, MAP_ALBEDO);
}

/* doom64-rt material defaults (rt/data/ json files) */
typedef struct {
    char name[9];
    float rough, metal, emis;
    int has_rough, has_metal;
    int has_light;
    float light[3], light_intensity;
} matdef_t;
static matdef_t *matdefs;
static int nmatdefs;
static int nmaps;

static entry_t *add_entry(const char *filename)
{
    const char *base = SDL_strrchr(filename, '/');
    const char *dot;
    char name[9];
    size_t n, i;
    entry_t *e;

    int map = MAP_ALBEDO;
    const char *us;

    /* doom64-rt keeps development/quarantined copies next to the real maps */
    if (SDL_strstr(filename, "quarantine") || SDL_strstr(filename, "_dev/"))
        return NULL;
    base = base ? base + 1 : filename;
    dot = SDL_strrchr(base, '.');
    if (!dot || SDL_strcasecmp(dot, ".png"))
        return NULL;
    n = (size_t)(dot - base);
    us = SDL_strrchr(base, '_');
    if (us && us < dot)
    {
        size_t sl = (size_t)(dot - us - 1);
        if (sl == 3 && !SDL_strncasecmp(us + 1, "orm", 3)) map = MAP_ORM;
        else if (sl == 1 && (us[1] == 'n' || us[1] == 'N')) map = MAP_NORMAL;
        else if (sl == 1 && (us[1] == 'e' || us[1] == 'E')) map = MAP_EMISSIVE;
        else if (sl == 1 && (us[1] == 'h' || us[1] == 'H')) return NULL; /* height: unused */
        if (map != MAP_ALBEDO)
            n = (size_t)(us - base);
    }
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
    e->map = map;
    e->pack = npacks - 1;
    if (map != MAP_ALBEDO)
        nmaps++;
    return e;
}

static Uint8 *read_entry(entry_t *e, size_t *len);

static const char *json_num(const char *obj, const char *end, const char *key, float *out)
{
    const char *k = obj;
    size_t kl = SDL_strlen(key);
    while ((k = SDL_strstr(k, key)) && k < end)
    {
        const char *p = k + kl;
        if (k > obj && k[-1] == '"' && *p == '"')
        {
            p++;
            while (p < end && (*p == ' ' || *p == ':' || *p == '\t'))
                p++;
            *out = (float)SDL_strtod(p, NULL);
            return p;
        }
        k = p;
    }
    return NULL;
}

/* Reads doom64-rt material defaults: [{ "textureName": "X", "roughnessDefault": r,
 * "metallicDefault": m, "emissiveMult": e }, ...]. Tolerant, not a full JSON parser. */
static void parse_matjson(const char *text, size_t len)
{
    char *buf = SDL_malloc(len + 1);
    const char *p, *end;
    int count = 0;
    SDL_memcpy(buf, text, len);
    buf[len] = 0;
    end = buf + len;
    p = buf;
    while ((p = SDL_strstr(p, "\"textureName\"")))
    {
        const char *ob = p, *oe = SDL_strchr(p, '}'), *q;
        matdef_t md;
        size_t i = 0;
        while (ob > buf && *ob != '{')
            ob--;
        if (!oe)
            oe = end;
        q = SDL_strchr(p + 13, '"');
        if (!q || q >= oe)
            break;
        q++;
        SDL_memset(&md, 0, sizeof(md));
        while (q < oe && *q != '"' && i < 8)
            md.name[i++] = (char)SDL_toupper((unsigned char)*q++);
        md.name[i] = 0;
        md.emis = 1.0f;
        md.has_rough = json_num(ob, oe, "roughnessDefault", &md.rough) != NULL;
        md.has_metal = json_num(ob, oe, "metallicDefault", &md.metal) != NULL;
        json_num(ob, oe, "emissiveMult", &md.emis);
        {
            const char *lc = SDL_strstr(ob, "\"lightColorHEX\"");
            if (lc && lc < oe)
            {
                const char *hq = SDL_strchr(lc + 15, '"');
                if (hq && hq < oe)
                {
                    unsigned long v = SDL_strtoul(hq + 1, NULL, 16);
                    md.has_light = 1;
                    md.light[0] = (float)((v >> 16) & 255) / 255.0f;
                    md.light[1] = (float)((v >> 8) & 255) / 255.0f;
                    md.light[2] = (float)(v & 255) / 255.0f;
                }
            }
            json_num(ob, oe, "lightIntensity", &md.light_intensity);
        }
        matdefs = SDL_realloc(matdefs, sizeof(matdef_t) * (size_t)(nmatdefs + 1));
        matdefs[nmatdefs++] = md;
        count++;
        p = oe;
    }
    SDL_free(buf);
    (void)count;
}

static int is_matjson(const char *path)
{
    const char *dot = SDL_strrchr(path, '.');
    return dot && !SDL_strcasecmp(dot, ".json") && SDL_strstr(path, "rt/data/") && !SDL_strstr(path, "/scenes/");
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
        if (is_matjson(fname) || PCAddon_IsDefinition(fname))
        {
            entry_t tmp;
            size_t jl = 0;
            Uint8 *js;
            SDL_memset(&tmp, 0, sizeof(tmp));
            tmp.pack = npacks - 1;
            tmp.method = rd16(z + pos + 10);
            tmp.csize = rd32(z + pos + 20);
            tmp.usize = rd32(z + pos + 24);
            tmp.local_ofs = rd32(z + pos + 42);
            js = read_entry(&tmp, &jl);
            if (js) {
                if(PCAddon_IsDefinition(fname)) PCAddon_Definition(fname,js,jl);
                else parse_matjson((const char *)js, jl);
            }
            SDL_free(js);
        }
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
    else if (is_matjson(full) || PCAddon_IsDefinition(fname))
    {
        size_t jl = 0;
        void *js = SDL_LoadFile(full, &jl);
        if (js) {
            if(PCAddon_IsDefinition(fname)) PCAddon_Definition(fname,js,jl);
            else parse_matjson(js, jl);
        }
        SDL_free(js);
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
    if(info.type!=SDL_PATHTYPE_DIRECTORY && SDL_strlen(path)>=4 &&
       !SDL_strcasecmp(path+SDL_strlen(path)-4,".wad")) {
        SDL_Log("native add-on: %s skipped; GZDoom WAD maps/scripts require a native map conversion",path);
        return;
    }
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

typedef struct { char path[1024]; } packpath_t;
typedef struct { packpath_t *items; int count; } packlist_t;
static int compare_packpaths(const void *a,const void *b)
{
    int order=SDL_strcasecmp(((const packpath_t *)a)->path,((const packpath_t *)b)->path);
    return order ? order : SDL_strcmp(((const packpath_t *)a)->path,((const packpath_t *)b)->path);
}
static SDL_EnumerationResult SDLCALL packs_dir_cb(void *userdata, const char *dirname, const char *fname)
{
    char full[1024];
    const char *dot = SDL_strrchr(fname, '.');
    SDL_PathInfo info;
    packlist_t *list=userdata;
    SDL_snprintf(full, sizeof(full), "%s%s", dirname, fname);
    if (!SDL_GetPathInfo(full, &info))
        return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY ||
        (dot && (!SDL_strcasecmp(dot, ".pk3") || !SDL_strcasecmp(dot, ".zip") || !SDL_strcasecmp(dot,".wad")))) {
        if(list->count<256) SDL_strlcpy(list->items[list->count++].path,full,sizeof(full));
        else SDL_Log("resource packs: directory limit of 256 reached; skipped %s",full);
    }
    return SDL_ENUM_CONTINUE;
}

static void scan_packs_dir(const char *base)
{
    char dir[1024];
    if (!base)
        return;
    SDL_snprintf(dir, sizeof(dir), "%spacks/", base);
    packlist_t list={SDL_calloc(256,sizeof(packpath_t)),0};
    if(!list.items) return;
    SDL_EnumerateDirectory(dir, packs_dir_cb, &list);
    qsort(list.items,(size_t)list.count,sizeof(packpath_t),compare_packpaths);
    for(int i=0;i<list.count;i++) add_pack(list.items[i].path);
    SDL_free(list.items);
}

void ResPack_Init(void)
{
    char *cwd = SDL_GetCurrentDirectory();
    char *pref = SDL_GetPrefPath("Doom64RTX", "Doom64RTX");
    const char *exe = SDL_GetBasePath();
    char list[1024], *tok, *save;

    enabled = pc_config.respacks;
    PCAddon_Init();
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
        SDL_Log("resource packs: %d loaded, %d images (%d RT material maps, %d material defaults)%s", npacks,
                nentries, nmaps, nmatdefs, enabled ? "" : " (disabled in settings)");
}

int ResPack_Count(void)
{
    return enabled ? nentries + nmatdefs : 0;
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
            if(e->csize != e->usize) return NULL;
            Uint8 *out = SDL_malloc(e->csize);
            if(!out) return NULL;
            SDL_memcpy(out, z + data, e->csize);
            *len = e->csize;
            return out;
        }
        if (e->method == 8)
        {
            /* Trust neither declared size nor compressed content. */
            if(!e->usize || e->usize > 64u*1024u*1024u || e->csize > INT_MAX) return NULL;
            Uint8 *out=SDL_malloc(e->usize);
            if(!out) return NULL;
            int outlen=stbi_zlib_decode_noheader_buffer((char *)out,(int)e->usize,
                (const char *)z+data,(int)e->csize);
            if(outlen==(int)e->usize) { *len=(size_t)outlen;return out; }
            SDL_free(out);
        }
    }
    return NULL;
}

void *ResPack_File(const char *path, size_t *size)
{
    int pidx;
    size_t length = strlen(path);
    *size = 0;
    if (!length || path[0] == '/' || strstr(path, "..") || strchr(path, '\\') || strchr(path, ':')) return NULL;
    for (pidx = npacks - 1; pidx >= 0; pidx--) {
        pack_t *p = &packs[pidx];
        if (p->is_dir) {
            char full[1536];
            SDL_snprintf(full, sizeof(full), "%s/%s", p->path, path);
            void *data = SDL_LoadFile(full, size);
            if (data) return data;
        } else if (p->zip_size >= 22) {
            size_t eocd = p->zip_size - 22, pos;
            int total, k;
            for (;;) {
                if (rd32(p->zip + eocd) == 0x06054b50) break;
                if (!eocd || p->zip_size - eocd > 65558) break;
                eocd--;
            }
            if (rd32(p->zip + eocd) != 0x06054b50) continue;
            pos = rd32(p->zip + eocd + 16); total = rd16(p->zip + eocd + 10);
            for (k = 0; k < total && pos + 46 <= p->zip_size; k++) {
                const Uint8 *z = p->zip + pos;
                size_t nlen, step;
                if (rd32(z) != 0x02014b50) break;
                nlen = rd16(z + 28); step = 46 + nlen + rd16(z + 30) + rd16(z + 32);
                if (step > p->zip_size - pos) break;
                if (nlen == length && !SDL_strncasecmp((const char *)z + 46, path, length)) {
                    entry_t entry = {0};
                    entry.pack = pidx; entry.method = rd16(z + 10);
                    entry.csize = rd32(z + 20); entry.usize = rd32(z + 24);
                    entry.local_ofs = rd32(z + 42);
                    if (entry.csize > 32u * 1024 * 1024 || entry.usize > 32u * 1024 * 1024) return NULL;
                    return read_entry(&entry, size);
                }
                pos += step;
            }
        }
    }
    return NULL;
}

uint8_t *ResPack_LoadPNG(const char *path, int *w, int *h)
{
    size_t size;
    void *data = SDL_LoadFile(path, &size);
    uint8_t *image;
    if (!data) return NULL;
    image = size <= 32u*1024*1024 ? stbi_load_from_memory(data, (int)size, w, h, NULL, 4) : NULL;
    SDL_free(data);
    return image;
}
void ResPack_FreePNG(void *pixels) { stbi_image_free(pixels); }

static void norm_name(const char *lumpname, char name[9])
{
    int i;
    for (i = 0; i < 8 && lumpname[i]; i++)
        name[i] = (char)SDL_toupper((unsigned char)(lumpname[i] & 0x7f));
    name[i] = 0;
}

static const Uint8 *decode_entry(int idx, int *w, int *h);

const Uint8 *ResPack_Image(const char *lumpname, int *w, int *h)
{
    char name[9];

    if (!enabled || !nentries || !lumpname)
        return NULL;
    norm_name(lumpname, name);
    return decode_entry(find_entry(name), w, h);
}

int ResPack_Material(const char *lumpname, respack_mat_t *m)
{
    char name[9];
    int k, i, any = 0;

    SDL_memset(m, 0, sizeof(*m));
    m->rough = -1.0f;
    m->metal = -1.0f;
    m->emissive = 1.0f;
    if (!enabled || !lumpname || (!nmaps && !nmatdefs))
        return 0;
    norm_name(lumpname, name);
    for (k = 0; k < 3; k++)
    {
        m->map[k] = decode_entry(find_entry_map(name, MAP_ORM + k), &m->w[k], &m->h[k]);
        if (m->map[k])
            any = 1;
    }
    for (i = nmatdefs - 1; i >= 0; i--)
        if (!SDL_strcmp(matdefs[i].name, name))
        {
            if (matdefs[i].has_rough)
                m->rough = matdefs[i].rough;
            if (matdefs[i].has_metal)
                m->metal = matdefs[i].metal;
            m->emissive = matdefs[i].emis;
            any = 1;
            break;
        }
    return any;
}

static const Uint8 *decode_entry(int idx, int *w, int *h)
{
    entry_t *e;
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

    if (!enabled || (!nentries && !nmatdefs) || !nregs)
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

static const matdef_t *find_matdef(const char *name)
{
    int i;
    for (i = nmatdefs - 1; i >= 0; i--)
        if (!SDL_strcmp(matdefs[i].name, name))
            return &matdefs[i];
    return NULL;
}

int ResPack_LightFor(const char *lumpname, float rgb[3], float *strength, float *radius)
{
    char name[9];
    const matdef_t *md;
    if (!enabled || !nmatdefs || !lumpname || !lumpname[0])
        return 0;
    norm_name(lumpname, name);
    md = find_matdef(name);
    if (!md || !md->has_light)
        return 0;
    SDL_memcpy(rgb, md->light, sizeof(md->light));
    *strength = md->emis;
    *radius = md->light_intensity > 0.0f ? md->light_intensity : 0.0f;
    return 1;
}

int ResPack_EmissiveGlow(const char *lumpname, float rgb[3], float *strength)
{
    char name[9];
    int idx, w, h, i, n;
    const Uint8 *img;
    const matdef_t *md;
    double sum[3] = { 0, 0, 0 };

    if (!enabled || !nmaps || !lumpname || !lumpname[0])
        return 0;
    norm_name(lumpname, name);
    idx = find_entry_map(name, MAP_EMISSIVE);
    if (idx < 0)
        return 0;
    img = decode_entry(idx, &w, &h);
    if (!img)
        return 0;
    n = w * h;
    for (i = 0; i < n; i++)
    {
        sum[0] += img[i * 4];
        sum[1] += img[i * 4 + 1];
        sum[2] += img[i * 4 + 2];
    }
    for (i = 0; i < 3; i++)
        rgb[i] = (float)(sum[i] / (255.0 * (n > 0 ? n : 1)));
    md = find_matdef(name);
    *strength = md ? md->emis : 1.0f;
    /* skip maps that are almost black (small glints) */
    return rgb[0] + rgb[1] + rgb[2] > 0.06f;
}
