/*
 * pak.c - Controller Pak emulation. The game's save "notes" live in
 * controller.pak inside the user's preference directory.
 *
 * Replaces the osPfs* based I_*Pak* functions of the original i_main.c.
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "doomdef.h"

#define PAK_NOTES 16
#define PAK_PAGES 123
#define COMPANY_CODE 0x3544     /* 5D */
#define GAME_CODE 0x4e444d45    /* NDME */

OSPfsState FileState[16];
s32 File_Num;
s32 Pak_Size;
u8 *Pak_Data;
s32 Pak_Memory;
s32 FilesUsed = -1;
s32 ControllerPakStatus = 1;

char Pak_Table[256] =
{
    '\0',
    ' ', ' ', ' ', ' ', ' ',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z', '!', '"', '#', '\'',
    '*', '+', ',', '-', '.', '/', ':', '=', '?', '@',
};

char Game_Name[16] =
{
    0x1D, 0x28, 0x28, 0x26, 0x0F, 0x16, 0x14, 0x00, /* "doom 64" in Pak_Table indices */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct {
    OSPfsState state;
    uint8_t   *data;
} pakfile_t;

static pakfile_t notes[PAK_NOTES];
static int pak_loaded;

static const char *pak_path(void)
{
    static char path[1024];
    if (!path[0])
    {
        char *pref = SDL_GetPrefPath("Doom64RTX", "Doom64RTX");
        SDL_snprintf(path, sizeof(path), "%scontroller.pak", pref ? pref : "");
        SDL_free(pref);
    }
    return path;
}

static void pak_load(void)
{
    FILE *f;
    char magic[8];
    int i;

    if (pak_loaded)
        return;
    pak_loaded = 1;
    memset(notes, 0, sizeof(notes));
    f = fopen(pak_path(), "rb");
    if (!f)
        return;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "D64PAK1", 8) != 0)
    {
        fclose(f);
        return;
    }
    for (i = 0; i < PAK_NOTES; i++)
    {
        uint32_t used = 0;
        if (fread(&used, 4, 1, f) != 1)
            break;
        if (!used)
            continue;
        if (fread(&notes[i].state, sizeof(OSPfsState), 1, f) != 1)
            break;
        if (notes[i].state.file_size > 32768)
        {
            notes[i].state.file_size = 0;
            break;
        }
        notes[i].data = (uint8_t *)calloc(1, notes[i].state.file_size);
        if (fread(notes[i].data, 1, notes[i].state.file_size, f) != notes[i].state.file_size)
            break;
    }
    fclose(f);
}

static void pak_save(void)
{
    FILE *f = fopen(pak_path(), "wb");
    int i;
    if (!f)
    {
        SDL_Log("pak: cannot write %s", pak_path());
        return;
    }
    fwrite("D64PAK1", 1, 8, f);
    for (i = 0; i < PAK_NOTES; i++)
    {
        uint32_t used = (notes[i].state.file_size != 0 && notes[i].data) ? 1 : 0;
        fwrite(&used, 4, 1, f);
        if (!used)
            continue;
        fwrite(&notes[i].state, sizeof(OSPfsState), 1, f);
        fwrite(notes[i].data, 1, notes[i].state.file_size, f);
    }
    fclose(f);
}

int I_CheckControllerPak(void)
{
    int i;
    pak_load();
    Pak_Memory = PAK_PAGES;
    FilesUsed = 0;
    for (i = 0; i < PAK_NOTES; i++)
    {
        FileState[i] = notes[i].state;
        if (notes[i].state.file_size)
        {
            FilesUsed++;
            Pak_Memory -= (int)(notes[i].state.file_size >> 8);
        }
    }
    return 0;
}

int I_DeletePakFile(int filenumb)
{
    pak_load();
    if (filenumb < 0 || filenumb >= PAK_NOTES)
        return PFS_ERR_INVALID;
    if (notes[filenumb].state.file_size)
    {
        Pak_Memory += (int)(notes[filenumb].state.file_size >> 8);
        free(notes[filenumb].data);
        memset(&notes[filenumb], 0, sizeof(notes[filenumb]));
        FileState[filenumb].file_size = 0;
        pak_save();
    }
    return 0;
}

int I_SavePakFile(int filenumb, int flag, byte *data, int size)
{
    pak_load();
    if (filenumb < 0 || filenumb >= PAK_NOTES || !notes[filenumb].data)
        return PFS_ERR_INVALID;
    if (flag == PFS_WRITE)
    {
        if ((u32)size > notes[filenumb].state.file_size)
            size = (int)notes[filenumb].state.file_size;
        memcpy(notes[filenumb].data, data, (size_t)size);
        pak_save();
    }
    else
        memcpy(data, notes[filenumb].data, (size_t)size);
    return 0;
}

static int names_equal(const char *a, const char *b, int n)
{
    return memcmp(a, b, (size_t)n) == 0;
}

int I_ReadPakFile(void)
{
    int i;
    pak_load();
    Pak_Data = NULL;
    Pak_Size = 0;
    for (i = 0; i < PAK_NOTES; i++)
    {
        const OSPfsState *s = &notes[i].state;
        if (s->file_size && s->company_code == COMPANY_CODE && s->game_code == GAME_CODE &&
            names_equal(s->game_name, Game_Name, 16))
        {
            File_Num = i;
            Pak_Size = (s32)s->file_size;
            Pak_Data = (byte *)Z_Malloc(Pak_Size, PU_STATIC, NULL);
            memcpy(Pak_Data, notes[i].data, (size_t)Pak_Size);
            return 0;
        }
    }
    return PFS_ERR_INVALID;
}

int I_CreatePakFile(void)
{
    int i;
    pak_load();
    Pak_Size = (Pak_Memory < 2) ? 256 : 512;
    for (i = 0; i < PAK_NOTES; i++)
        if (!notes[i].state.file_size)
            break;
    if (i == PAK_NOTES)
        return PFS_DIR_FULL;

    Pak_Data = (byte *)Z_Malloc(Pak_Size, PU_STATIC, NULL);
    D_memset(Pak_Data, 0, Pak_Size);

    memset(&notes[i], 0, sizeof(notes[i]));
    notes[i].state.file_size = (u32)Pak_Size;
    notes[i].state.company_code = COMPANY_CODE;
    notes[i].state.game_code = GAME_CODE;
    memcpy(notes[i].state.game_name, Game_Name, 16);
    notes[i].data = (uint8_t *)calloc(1, (size_t)Pak_Size);
    File_Num = i;
    pak_save();
    I_CheckControllerPak();
    return 0;
}
