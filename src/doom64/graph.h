/*
 * graph.h - [PC] replacement for the N64 SDK debug text header.
 * The original printed directly into the framebuffer; on PC the debug
 * print helpers log to stdout (src/port/i_main_pc.c).
 */
#ifndef _graph_h_
#define _graph_h_

#define WHITE       0xffffffff
#define BLACK       0x0000
#define GRAY        GPACK_RGBA5551(127,127,127,1)
#define BLUE        GPACK_RGBA5551(0,0,255,1)
#define GREEN       GPACK_RGBA5551(0,255,0,1)
#define RED         GPACK_RGBA5551(255,0,0,1)
#define YELLOW      GPACK_RGBA5551(255,255,0,1)

void printstr(u32 color, int x, int y, char *text);
void PRINTF_D(u32 color_, char *c_, ...);
void PRINTF_D2(u32 color_, int x, int y, char *c_, ...);
void WAIT(void);

#endif
