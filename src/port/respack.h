/*
 * respack.h - PNG resource packs (pk3 / folders) replacing Doom 64 textures
 * and sprites by lump name. Doom64-RTX PC port, GPLv3.
 */
#ifndef D64_RESPACK_H
#define D64_RESPACK_H

#include <stdint.h>
#include <stddef.h>

enum { RESPACK_OTHER, RESPACK_TEXTURE, RESPACK_SPRITE };

typedef struct {
    char name[9];
    int kind;      /* RESPACK_TEXTURE / RESPACK_SPRITE */
    int offset;    /* byte offset of the queried address inside the lump */
    int header;    /* lump header size (pixels start here) */
    int pixbytes;  /* sprites: size of the pixel block (rows padded to 8) */
    int width, height; /* sprites: picture size */
} respack_src_t;

void ResPack_Init(void);
int  ResPack_Count(void);
void ResPack_SetEnabled(int on);
/* Decoded RGBA replacement for a lump name, or NULL. Owned by the pack cache. */
const uint8_t *ResPack_Image(const char *lumpname, int *w, int *h);
/* Named pack resources (sounds/scripts): SDL-allocated, caller SDL_free(). */
void *ResPack_File(const char *path, size_t *size);
/* Decode an external PNG for PC overlays; free with ResPack_FreePNG(). */
uint8_t *ResPack_LoadPNG(const char *path, int *w, int *h);
void ResPack_FreePNG(void *pixels);

/* RT material for a lump: maps (orm, normal, emissive; NULL if absent) and
 * doom64-rt defaults (rough/metal < 0 = not set). Returns 0 if nothing. */
typedef struct {
    const uint8_t *map[3];
    int w[3], h[3];
    float rough, metal, emissive;
} respack_mat_t;
int ResPack_Material(const char *lumpname, respack_mat_t *m);

/* doom64-rt light colour for a sprite/texture (lightColorHEX); radius 0 = default */
int ResPack_LightFor(const char *lumpname, float rgb[3], float *strength, float *radius);
/* average colour of the emissive map (for placing world lights) */
int ResPack_EmissiveGlow(const char *lumpname, float rgb[3], float *strength);

/* called by W_CacheLumpNum when it (re)loads a lump */
void ResPack_RegisterLump(int lump, const char *name8, int kind, const void *data, int size);
/* lets the registry check that a lump is still cached at the same address */
void ResPack_SetLumpCache(void *const *cache_array);
/* which lump does this address belong to (for the GBI interpreter) */
int  ResPack_SourceOf(const void *addr, respack_src_t *out);

#endif
