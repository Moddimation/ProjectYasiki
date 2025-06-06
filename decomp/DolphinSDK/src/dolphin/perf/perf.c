#include <dolphin/gx.h>
#include <dolphin/perf.h>

#include "../gx/GXPrivate.h"
#include "PERFPrivate.h"

#define TOKEN_MAX 0xFFFF

// .bss
static struct OSAlarm PERFAlarm; // size: 0x28, address: 0x0

// .sdata
static volatile s32 CurrAutoSample = 0xFFFFFFFF; // size: 0x4, address: 0x0
static volatile u32 CurrToken      = 0x0000FFFF; // size: 0x4, address: 0x4

// .sbss
static volatile u16 magic;               // size: 0x1, address: 0x0
static void*        (*PerfAlloc) (u32);  // size: 0x4, address: 0x4
static void         (*PerfFree) (void*); // size: 0x4, address: 0x8
static void         (*DSCB) (u16);       // size: 0x4, address: 0xC
u32                 PERFNumFrames;       // size: 0x4, address: 0x28
u32                 PERFNumEvents;       // size: 0x4, address: 0x24
u32                 PERFNumSamples;      // size: 0x4, address: 0x20
struct Frame*       PERFFrames;          // size: 0x4, address: 0x1C
struct PerfEvent*   PERFEvents;          // size: 0x4, address: 0x18
u32                 PERFCurrFrame;       // size: 0x4, address: 0x14
volatile s32        PERFCurrSample;      // size: 0x4, address: 0x10

// functions
static void           PERFResetAllMemMetrics ();
static void           PERFGetAllMemMetrics (struct PerfSample* s, u32 i);
void                  PERFSetDrawSyncCallback (void (*cb) (u16));
static void           PERFTokenCallback (u16 token);
u32                   PERFInit (u32           numSamples,
                                u32           numFramesHistory,
                                unsigned long numTypes,
                                void*         (*allocator) (u32),
                                void          (*deallocator) (void*),
                                void          (*initDraw)());
void                  PERFSetEvent (u16 id, char* name, PerfType type);
void                  PERFSetEventColor (u16 id, GXColor color);
void                  PERFStartFrame ();
void                  PERFEndFrame ();
void                  PERFEventStart (u16 id);
__declspec (weak) s32 PERFGetNewSample (void);
void                  PERFEventEnd (u16 id);
static void           PERFStartAutoSample ();
static void           PERFEndAutoSample ();
static void           PERFTimerCallback (OSAlarm* alarm, OSContext* context);
void                  PERFStartAutoSampling (float msInterval);
void                  PERFStopAutoSampling ();

#ifndef DEBUG
inline s32
PERFGetNewSample ()
{
    if (PERFCurrSample >= (PERFNumSamples - 1))
    {
        PERFCurrSample = PERFNumSamples - 1;
        return PERFCurrSample;
    }
    return PERFCurrSample++;
}
#endif
static void
PERFResetAllMemMetrics ()
{
    ((u16*)__memReg)[25] = 0;
    ((u16*)__memReg)[26] = 0;
    ((u16*)__memReg)[27] = 0;
    ((u16*)__memReg)[28] = 0;
    ((u16*)__memReg)[30] = 0;
    ((u16*)__memReg)[29] = 0;
    ((u16*)__memReg)[32] = 0;
    ((u16*)__memReg)[31] = 0;
    ((u16*)__memReg)[34] = 0;
    ((u16*)__memReg)[33] = 0;
    ((u16*)__memReg)[36] = 0;
    ((u16*)__memReg)[35] = 0;
    ((u16*)__memReg)[38] = 0;
    ((u16*)__memReg)[37] = 0;
    ((u16*)__memReg)[40] = 0;
    ((u16*)__memReg)[39] = 0;
    ((u16*)__memReg)[42] = 0;
    ((u16*)__memReg)[41] = 0;
    ((u16*)__memReg)[44] = 0;
    ((u16*)__memReg)[43] = 0;
}

static void
PERFGetAllMemMetrics (struct PerfSample* s, u32 i)
{
    u32 ctrl;
    u32 ctrh;

    GXReadXfRasMetric (&s->xfWaitIn[i], &s->xfWaitOut[i], &s->rasBusy[i], &s->rasClocks[i]);

    ctrl           = ((u16*)__memReg)[26];
    ctrh           = ((u16*)__memReg)[25];
    s->cpReq[i]    = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[28];
    ctrh           = ((u16*)__memReg)[27];
    s->tcReq[i]    = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[30];
    ctrh           = ((u16*)__memReg)[29];
    s->cpuRdReq[i] = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[32];
    ctrh           = ((u16*)__memReg)[31];
    s->cpuWrReq[i] = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[34];
    ctrh           = ((u16*)__memReg)[33];
    s->dspReq[i]   = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[36];
    ctrh           = ((u16*)__memReg)[35];
    s->ioReq[i]    = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[38];
    ctrh           = ((u16*)__memReg)[37];
    s->viReq[i]    = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[40];
    ctrh           = ((u16*)__memReg)[39];
    s->peReq[i]    = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[42];
    ctrh           = ((u16*)__memReg)[41];
    s->rfReq[i]    = ((ctrh << 0x10) | ctrl);

    ctrl           = ((u16*)__memReg)[44];
    ctrh           = ((u16*)__memReg)[43];
    s->fiReq[i]    = ((ctrh << 0x10) | ctrl);
}

void
PERFSetDrawSyncCallback (void (*cb) (u16))
{
    DSCB = cb;
}

static void
PERFTokenCallback (u16 token)
{
    s32 sample;

    if ((token < 0xE000) || (((int)((u32)token >> 8) & 0xF) != magic) || (PERFCurrSample == 0))
    {
        if (DSCB)
        {
            DSCB (token);
        }
    }
    else
    {
        if (token >= 0xF000)
        {
            if (CurrToken == TOKEN_MAX)
            {
                ASSERTLINE (0x14B, CurrAutoSample >= 0);
                PERFEndAutoSample();
                PERFStartAutoSample();
                return;
            }
            sample = (u8)(u32)token;
            if ((u8)(u16)CurrToken != sample)
            {
                sample = (u8)(u16)CurrToken;
            }
            ASSERTLINE (0x15B, sample < PERFCurrSample);
            PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampEnd  = PPCMfpmc4();
            PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[1]  = PPCMfpmc3();
            PERFFrames[PERFCurrFrame].samples[sample].instructions[1] = PPCMfpmc1();
            PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[sample], 1);
            if (CurrAutoSample >= 0)
            {
                ASSERTLINE (0x167, CurrToken == TOKEN_MAX);
                PERFEndAutoSample();
            }
            CurrToken = 0xFFFF;
            PERFStartAutoSample();
            return;
        }
        if (CurrToken < 0xFFFF)
        {
            ASSERTLINE (0x176, CurrAutoSample < 0);
            sample                                                    = (u8)(u16)CurrToken;
            PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampEnd  = PPCMfpmc4();
            PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[1]  = PPCMfpmc3();
            PERFFrames[PERFCurrFrame].samples[sample].instructions[1] = PPCMfpmc1();
            PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[sample], 1);
        }
        else
        {
            ASSERTLINE (0x180, CurrAutoSample >= 0);
            PERFEndAutoSample();
        }
        sample                                                    = (u8)(u32)token;
        PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[0]  = PPCMfpmc3();
        PERFFrames[PERFCurrFrame].samples[sample].instructions[0] = PPCMfpmc1();
        PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampStart =
            PERFFrames[PERFCurrFrame].samples[sample].origgpStart = PPCMfpmc4();
        PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[sample], 0);
        CurrToken = (u32)(u16)token;
    }
}

u32
PERFInit (u32   numSamples,
          u32   numFramesHistory,
          u32   numTypes,
          void* (*allocator) (u32),
          void  (*deallocator) (void*),
          void  (*initDraw)())
{
    u32 i;
    u32 size;

    PerfAlloc       = allocator;
    PerfFree        = deallocator;
    PERFNumFrames   = numFramesHistory;
    PERFNumEvents   = numTypes;
    PERFNumSamples  = numSamples;

    size            = (numFramesHistory * 0x10);
    size           += (numFramesHistory * (numSamples * 0xB0));
    size           += (numTypes * 0x10);

    PERFFrames      = (struct Frame*)PerfAlloc (numFramesHistory * 0x10);

    for (i = 0; i < PERFNumFrames; i++)
    {
        PERFFrames[i].samples    = (PerfSample*)PerfAlloc (numSamples * 0xB0);
        PERFFrames[i].lastSample = 0;
    }
    PERFEvents = (struct PerfEvent*)PerfAlloc (numTypes * 0x10);
    for (i = 0; i < numTypes; i++)
    {
        PERFEvents[i].name       = 0;
        PERFEvents[i].currSample = -1;
    }
    __PERFDrawInit (initDraw);
    GXSetDrawSyncCallback (PERFTokenCallback);
    GXInitXfRasMetric();
    OSInitAlarm();
    return size;
}

void
PERFSetEvent (u16 id, char* name, PerfType type)
{
    GXColor def               = { 0xFF, 0x19, 0x00, 0xC8 };

    PERFEvents[id].name       = name;
    PERFEvents[id].type       = type;
    PERFEvents[id].currSample = -1;
    PERFEvents[id].color      = def;
}

void
PERFSetEventColor (u16 id, GXColor color)
{
    PERFEvents[id].color = color;
}

void
PERFStartFrame ()
{
    int enabled    = OSDisableInterrupts();

    PERFCurrSample = 0;
    CurrToken      = 0xFFFF;
    GXSetDrawSyncCallback (PERFTokenCallback);
    PERFFrames[PERFCurrFrame].lastSample = 0;
    PERFResetAllMemMetrics();
    GXClearGPMetric();
    PPCMtpmc1 (0);
    PPCMtpmc2 (0);
    PPCMtpmc3 (0);
    PPCMtpmc4 (0);
    PPCMtmmcr0 (0x8B);
    PPCMtmmcr1 (0x78400000);
    PERFStartAutoSample();
    OSRestoreInterrupts (enabled);
}

void
PERFEndFrame ()
{
    u32 i;
    int enabled;

    enabled = OSDisableInterrupts();
    PERFEndAutoSample();
    GXSetDrawSyncCallback (DSCB);
    PERFFrames[PERFCurrFrame].end             = PPCMfpmc4();
    PERFFrames[PERFCurrFrame].lastSample      = PERFCurrSample;
    PERFFrames[PERFCurrFrame].cachemisscycles = PPCMfpmc3();
    PERFCurrFrame                             = (PERFCurrFrame + 1) % PERFNumFrames;
    PERFCurrSample                            = 0;

    for (i = 0; i < PERFNumEvents; i++) { PERFEvents[i].currSample = -1; }
    magic += 1;
    if ((u8)magic >= 0x10U)
    {
        magic = 0;
    }
    OSRestoreInterrupts (enabled);
}

void
PERFEventStart (u16 id)
{
    int enabled;
    s32 sample;

    enabled = OSDisableInterrupts();

    sample  = PERFEvents[id].currSample;
    if (sample < 0)
    {
        sample                                                = PERFGetNewSample();
        PERFEvents[id].currSample                             = sample;
        PERFFrames[PERFCurrFrame].samples[sample].id          = id;
        PERFFrames[PERFCurrFrame].samples[sample].interrupted = 0;

        switch (PERFEvents[id].type)
        {
            case PERF_GP_EVENT:
                PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[0]   = 0;
                PERFFrames[PERFCurrFrame].samples[sample].instructions[0]  = 0;
                PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampStart = 0;
                PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampEnd   = 0;
                GXSetDrawSync (((u16)sample + 0x10000) + (((u16)magic << 8) & 0xFF00) - 0x2000);
                break;
            case PERF_CPU_GP_EVENT:
                PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampStart = 0;
                PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampEnd   = 0;
                PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[0]   = PPCMfpmc3();
                PERFFrames[PERFCurrFrame].samples[sample].instructions[0]  = PPCMfpmc1();
                GXSetDrawSync (((u16)sample + 0x10000) + (((u16)magic << 8) & 0xFF00) - 0x2000);
                // fallthrough
            case PERF_CPU_EVENT:
                PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[2]  = PPCMfpmc3();
                PERFFrames[PERFCurrFrame].samples[sample].instructions[2] = PPCMfpmc1();
                PERFFrames[PERFCurrFrame].samples[sample].origcpuStart =
                    PERFFrames[PERFCurrFrame].samples[sample].cpuTimeStampStart = PPCMfpmc4();
                PERFFrames[PERFCurrFrame].samples[sample].cpuTimeStampEnd       = 0;
                break;
            default:
                OSReport ("PERF : Unknown event type for ID %d - possibly out of memory\n", id);
                break;
        }
    }
    else
    {
        OSReport ("PERF : event is still open for CPU!\n");
    }
    OSRestoreInterrupts (enabled);
}
#if DEBUG
__declspec (weak) s32
PERFGetNewSample ()
{
    if (PERFCurrSample >= (PERFNumSamples - 1))
    {
        PERFCurrSample = PERFNumSamples - 1;
        return PERFCurrSample;
    }
    return PERFCurrSample++;
}
#endif
void
PERFEventEnd (u16 id)
{
    int enabled;
    s32 sample;

    enabled = OSDisableInterrupts();
    sample  = PERFEvents[id].currSample;
    if (sample < 0)
    {
        OSReport ("PERF : ending an event that never started!\n");
        OSRestoreInterrupts (enabled);
        return;
    }
    switch (PERFEvents[id].type)
    {
        case PERF_GP_EVENT:
            GXSetDrawSync (((u16)sample + 0x10000) + (((u16)magic << 8) & 0xFF00) - 0x1000);
            break;
        case PERF_CPU_GP_EVENT:
            GXSetDrawSync (((u16)sample + 0x10000) + (((u16)magic << 8) & 0xFF00) - 0x1000);
        case PERF_CPU_EVENT:
            PERFFrames[PERFCurrFrame].samples[sample].cpuTimeStampEnd = PPCMfpmc4();
            PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[3]  = PPCMfpmc3();
            PERFFrames[PERFCurrFrame].samples[sample].instructions[3] = PPCMfpmc1();
            break;
    }
    PERFEvents[id].currSample = -1;
    OSRestoreInterrupts (enabled);
}

static void
PERFStartAutoSample ()
{
    CurrAutoSample                                                = PERFGetNewSample();
    PERFFrames[PERFCurrFrame].samples[CurrAutoSample].id          = 0xFF;
    PERFFrames[PERFCurrFrame].samples[CurrAutoSample].interrupted = 0;
    PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[CurrAutoSample], 0);
    PERFFrames[PERFCurrFrame].samples[CurrAutoSample].gpTimeStampStart = PPCMfpmc4();
    PERFFrames[PERFCurrFrame].samples[CurrAutoSample].gpTimeStampEnd   = 0;
    PERFFrames[PERFCurrFrame].samples[CurrAutoSample].cacheMisses[0]   = PPCMfpmc3();
    PERFFrames[PERFCurrFrame].samples[CurrAutoSample].instructions[0]  = PPCMfpmc1();
}

static void
PERFEndAutoSample ()
{
    if (CurrAutoSample >= 0)
    {
        PERFFrames[PERFCurrFrame].samples[CurrAutoSample].gpTimeStampEnd = PPCMfpmc4();
        PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[CurrAutoSample], 1);
        PERFFrames[PERFCurrFrame].samples[CurrAutoSample].cacheMisses[1]  = PPCMfpmc3();
        PERFFrames[PERFCurrFrame].samples[CurrAutoSample].instructions[1] = PPCMfpmc1();
    }
    CurrAutoSample = -1;
}

static void
PERFTimerCallback (OSAlarm* alarm, OSContext* context)
{
    s32 sample;
    s32 newsample;

    if (PERFCurrSample != 0)
    {
        if (CurrToken < 0xFFFF)
        {
            sample = (u8)(u16)CurrToken;
            // stupid CurrToken loading AGAIN
            if ((u8)((CurrToken >> 8) & 0xF) != (s32)magic)
            {
                PERFEndAutoSample();
                PERFStartAutoSample();
                return;
            }
            ASSERTLINE (0x36A, CurrAutoSample < 0);
            newsample = PERFGetNewSample();
            memcpy (&PERFFrames[PERFCurrFrame].samples[newsample],
                    &PERFFrames[PERFCurrFrame].samples[sample],
                    0xB0);
            PERFFrames[PERFCurrFrame].samples[newsample].gpTimeStampEnd  = PPCMfpmc4();
            PERFFrames[PERFCurrFrame].samples[newsample].cacheMisses[1]  = PPCMfpmc3();
            PERFFrames[PERFCurrFrame].samples[newsample].instructions[1] = PPCMfpmc1();
            PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[newsample], 1);
            PERFFrames[PERFCurrFrame].samples[newsample].id            = 0xFF;
            PERFFrames[PERFCurrFrame].samples[sample].gpTimeStampStart = PPCMfpmc4();
            PERFFrames[PERFCurrFrame].samples[sample].cacheMisses[0]   = PPCMfpmc3();
            PERFFrames[PERFCurrFrame].samples[sample].instructions[0]  = PPCMfpmc1();
            PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[sample], 0);
            PERFFrames[PERFCurrFrame].samples[sample].interrupted = 1;
            CurrAutoSample                                        = -1;
            return;
        }
        if (CurrAutoSample < 0)
        {
            OSReport ("PERF : AUTOSAMPLE < 0!!!! SHOULD NEVER HAPPEN!\n");
            return;
        }
        PERFEndAutoSample();
        PERFStartAutoSample();
    }
}

void
PERFStartAutoSampling (float msInterval)
{
    OSSetPeriodicAlarm (&PERFAlarm,
                        OSGetTime(),
                        (u32)OSMillisecondsToTicks (msInterval),
                        PERFTimerCallback);
}

void
PERFStopAutoSampling ()
{
    int enabled = OSDisableInterrupts();

    if (CurrAutoSample >= 0)
    {
        PERFFrames[PERFCurrFrame].samples[CurrAutoSample].gpTimeStampEnd = PPCMfpmc4();
        PERFGetAllMemMetrics (&PERFFrames[PERFCurrFrame].samples[CurrAutoSample], 1);
    }
    OSCancelAlarm (&PERFAlarm);
    OSRestoreInterrupts (enabled);
}
