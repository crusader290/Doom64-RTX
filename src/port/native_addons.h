#ifndef D64_NATIVE_ADDONS_H
#define D64_NATIVE_ADDONS_H
#include <stddef.h>
#include <stdint.h>
void PCAddon_Init(void);
int PCAddon_IsDefinition(const char *name);
void PCAddon_Definition(const char *name,const void *data,size_t size);
void PCAddon_ResetLevel(void);
void PCAddon_Tick(void);
int PCAddon_GoreLife(void);
int PCAddon_GoreLimit(void);
uint32_t PCAddon_BloodColor(int type);
float PCAddon_Flashlight(void);
int PCAddon_Mugshot(void);
int PCAddon_Environment(void);
int PCAddon_Sky(void);
int PCAddon_Title(void);
#endif
