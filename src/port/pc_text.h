#ifndef D64_PC_TEXT_H
#define D64_PC_TEXT_H
#define G_PC_TEXT 0x07 /* PC-only display-list extension; renderer ABI unchanged */
void PCText_BeginFrame(void);
int PCText_Available(void);
void PCText_Draw(float x, float y, float size, const char *text, unsigned color);
void PCText_Box(float x, float y, float w, float h, unsigned color);
void PCText_Image(unsigned texture, float x, float y, float w, float h, unsigned color);
void PCText_WorldImage(unsigned texture,float x,float y,float z,float w,float h,unsigned color);
void PCText_SkyImage(unsigned texture,float u0,float span,float pitch);
float PCText_Width(float size, const char *text);
void PCText_Emit(const void *command);
#endif
