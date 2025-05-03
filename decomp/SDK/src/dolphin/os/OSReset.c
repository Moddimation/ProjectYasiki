#include <dolphin/os.h>

#include <dolphin.h>

#include "OSPrivate.h"

// These macros are copied from OSThread.c. Or ARE they the same
// macros? They dont seem to be in the SDK headers.
#define ENQUEUE_INFO(info, queue)                                                   \
    do {                                                                            \
        struct OSResetFunctionInfo* __prev = (queue)->tail;                         \
        if (__prev == 0)                                                            \
        {                                                                           \
            (queue)->head = (info);                                                 \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            __prev->next = (info);                                                  \
        }                                                                           \
        (info)->prev = __prev;                                                      \
        (info)->next = 0;                                                           \
        (queue)->tail = (info);                                                     \
    }                                                                               \
    while (0);

#define DEQUEUE_INFO(info, queue)                                                   \
    do {                                                                            \
        struct OSResetFunctionInfo* __next = (info)->next;                          \
        struct OSResetFunctionInfo* __prev = (info)->prev;                          \
        if (__next == 0)                                                            \
        {                                                                           \
            (queue)->tail = __prev;                                                 \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            __next->prev = __prev;                                                  \
        }                                                                           \
        if (__prev == 0)                                                            \
        {                                                                           \
            (queue)->head = __next;                                                 \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            __prev->next = __next;                                                  \
        }                                                                           \
    }                                                                               \
    while (0);

#define ENQUEUE_INFO_PRIO(info, queue)                                              \
    do {                                                                            \
        struct OSResetFunctionInfo* __prev;                                         \
        struct OSResetFunctionInfo* __next;                                         \
        for (__next = (queue)->head;                                                \
             __next && (__next->priority <= (info)->priority);                      \
             __next = __next->next);                                                \
                                                                                    \
        if (__next == 0)                                                            \
        {                                                                           \
            ENQUEUE_INFO (info, queue);                                             \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            (info)->next = __next;                                                  \
            __prev = __next->prev;                                                  \
            __next->prev = (info);                                                  \
            (info)->prev = __prev;                                                  \
            if (__prev == 0)                                                        \
            {                                                                       \
                (queue)->head = (info);                                             \
            }                                                                       \
            else                                                                    \
            {                                                                       \
                __prev->next = (info);                                              \
            }                                                                       \
        }                                                                           \
    }                                                                               \
    while (0);

static struct OSResetFunctionQueue ResetFunctionQueue;
#ifdef __MWERKS__
OSThreadQueue __OSActiveThreadQueue : (OS_BASE_CACHED | 0x00DC);
#else
OSThreadQueue __OSActiveThreadQueue;
#endif

static int      CallResetFunctions (int final);
static ASM void Reset (u32 resetCode);
void
OSRegisterResetFunction (struct OSResetFunctionInfo* info)
{
    ASSERTLINE (0x76, info->func);

    ENQUEUE_INFO_PRIO (info, &ResetFunctionQueue);
}
void
OSUnregisterResetFunction (struct OSResetFunctionInfo* info)
{
    DEQUEUE_INFO (info, &ResetFunctionQueue);
}
static int
CallResetFunctions (int final)
{
    struct OSResetFunctionInfo* info;
    int                         err = 0;

    for (info = ResetFunctionQueue.head; info; info = info->next)
    {
        err |= !info->func (final);
    }
    err |= !__OSSyncSram();
    if (err)
    {
        return 0;
    }
    return 1;
}
static ASM void
Reset (u32 resetCode)
{
#ifdef __MWERKS__
    nofralloc;
    b L_000001BC;

L_000001A0:
    mfspr r8, HID0;
    ori   r8, r8, 0x8;
    mtspr HID0, r8;
    isync;
    sync;
    nop;
    b L_000001C0;

L_000001BC:
    b L_000001DC;

L_000001C0:
    mftb r5, 268;

L_000001C4:
    mftb   r6, 268;
    subf   r7, r5, r6;
    cmplwi r7, 0x1124;
    blt    L_000001C4;
    nop;
    b L_000001E0;

L_000001DC:
    b L_000001FC;

L_000001E0:
    lis r8, 0xcc00;
    ori r8, r8, 0x3000;
    li  r4, 0x3;
    stw r4, 0x24(r8);
    stw r3, 0x24(r8);
    nop;
    b L_00000200;

L_000001FC:
    b L_00000208;

L_00000200:
    nop;
    b L_00000200;

L_00000208:
    b L_000001A0;
#endif
}
static void
KillThreads (void)
{
    OSThread* thread;
    OSThread* next;

    for (thread = __OSActiveThreadQueue.head; thread; thread = next)
    {
        next = thread->linkActive.next;
        switch (thread->state)
        {
            case 1:
            case 4:
                OSCancelThread (thread);
                break;
        }
    }
}
void
__OSDoHotReset (s32 code)
{
    OSDisableInterrupts();
    __VIRegs[VI_DISP_CONFIG] = 0;
    ICFlashInvalidate();
    Reset (code * 8);
}
void
OSResetSystem (int reset, u32 resetCode, BOOL forceMenu)
{
    BOOL rc;
    BOOL disableRecalibration;
    BOOL enabled;

    OSDisableScheduler();
    __OSStopAudioSystem();

    while (!CallResetFunctions (FALSE))
    {
        ;
    }

    if (reset && forceMenu)
    {
        OSSram* sram;

        sram = __OSLockSram();
        sram->flags |= 0x40;
        __OSUnlockSram (TRUE);

        while (!__OSSyncSram())
        {
            ;
        }
    }

    enabled = OSDisableInterrupts();
    CallResetFunctions (TRUE);
    if (reset == OS_RESET_HOTRESET)
    {
        __OSDoHotReset (resetCode);
    }
    else
    {
        KillThreads();
        OSEnableScheduler();
        __OSReboot (resetCode, forceMenu);
    }

    OSRestoreInterrupts (enabled);
    OSEnableScheduler();
}
u32
OSGetResetCode ()
{
    return (__PIRegs[PI_RESET_CODE] & 0xFFFFFFF8) / 8;
}
