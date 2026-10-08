
#include <ultra64.h>
#include "i_main.h"

#ifdef D64_PC
#define AUDIO_HEAP_SIZE	(0x100000) /* [PC] must match s_sound.c */
#else
#define AUDIO_HEAP_SIZE	(0x44800)
#endif
u64 audio_heap[AUDIO_HEAP_SIZE / sizeof(u64)];//80325800
