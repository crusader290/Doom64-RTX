/*
 * os.c - single threaded emulation of the libultra OS calls the game uses.
 * Doom64-RTX PC port, GPLv3.
 */
#include <ultra64.h>
#include "rom.h"
#include "d64pc.h"

/* Microcode symbols: never executed on PC, only used as identities. */
u64 rspbootTextStart[1], rspbootTextEnd[1];
u64 gspF3DEX_NoN_fifoTextStart[1], gspF3DEX_NoN_fifoDataStart[1];
u64 gspL3DEX_fifoTextStart[1], gspL3DEX_fifoDataStart[1];
u64 aspMainTextStart[1], aspMainDataStart[1];

OSViMode osViModeTable[64];
s32 osTvType = OS_TV_NTSC;

extern u32 cfb[2][320 * 240];
extern u32 vid_side;

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count)
{
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msg;
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    (void)flag;
    if (mq->validCount >= mq->msgCount)
        return -1;
    mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
    mq->validCount++;
    return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag)
{
    (void)flag;
    if (mq->validCount >= mq->msgCount)
        return -1;
    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;
    return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag)
{
    /* Everything that used to complete asynchronously (DMA) completes
     * immediately on PC, so a blocking receive on an empty queue simply
     * returns: there is no other thread that could ever fill it. */
    (void)flag;
    if (mq->validCount == 0)
    {
        if (msg)
            *msg = NULL;
        return -1;
    }
    if (msg)
        *msg = mq->msg[mq->first];
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    return 0;
}

void osYieldThread(void) {}
void osInvalDCache(void *p, s32 len) { (void)p; (void)len; }
void osWritebackDCacheAll(void) {}
uintptr_t osVirtualToPhysical(void *p) { return (uintptr_t)p; }

s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, u32 devAddr,
                 void *dramAddr, u32 size, OSMesgQueue *mq)
{
    (void)pri;
    if (direction != OS_READ)
        I_PCFatal("osPiStartDma: cartridge writes are not supported");
    if (!ROM_Read(devAddr, dramAddr, size))
        I_PCFatal("osPiStartDma: bad ROM read %08x (+%u)", devAddr, size);
    if (mb)
    {
        mb->devAddr = devAddr;
        mb->dramAddr = dramAddr;
        mb->size = size;
    }
    if (mq)
        osSendMesg(mq, (OSMesg)mb, OS_MESG_NOBLOCK);
    return 0;
}

void *osViGetNextFramebuffer(void) { return cfb[vid_side]; }
void *osViGetCurrentFramebuffer(void) { return cfb[vid_side ^ 1]; }
