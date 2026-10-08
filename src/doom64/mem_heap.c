
#include <ultra64.h>
#include "i_main.h"

#ifdef D64_PC /* [PC] 64-bit pointers make zone blocks larger; give the zone more room */
#define MEM_HEAP_SIZE (0x2000000) // 32 MB
#else
#define MEM_HEAP_SIZE (0x26B510) // 2.41 MB
#endif
u64 mem_heap[MEM_HEAP_SIZE / sizeof(u64)]; // 800BA2F0
