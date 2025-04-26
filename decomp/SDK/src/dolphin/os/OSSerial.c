#include <dolphin/os.h>

#include <dolphin.h>

static struct SIControl Si = {
    /* chan */ -1,
    /* poll */ 0,
    /* inputBytes*/ 0,
    /* input */ NULL,
    /* callback */ NULL
};

static struct SIPacket Packet[4];
static struct OSAlarm  Alarm[4];

static u32  CompleteTransfer();
static void SITransferNext(s32 chan);
static void SIIntrruptHandler(s16 unused, struct OSContext* context);
static int  __SITransfer(s32 chan, void* output, u32 outputBytes, void* input,
                         u32  inputBytes,
                         void (*callback)(s32, u32, struct OSContext*));
static void AlarmHandler(struct OSAlarm* alarm, struct OSContext* context);
int
SIBusy()
{
    return (Si.chan != -1) ? 1 : 0;
}
static u32
CompleteTransfer()
{
    u32  sr;
    u32  i;
    u32  rLen;
    u16* input;
    u32  temp;

    sr = __SIRegs[SI_STATUS_IDX];
    __SIRegs[SI_COMCSR_IDX] = SI_COMCSR_TCINT_MASK;

    if (Si.chan != -1)
    {
        input = Si.input;
        rLen = (Si.inputBytes / 4);
        for (i = 0; i < rLen; i++)
        {
            *((u32*)input)++ = __SIRegs[i + 0x20];
        }

        rLen = Si.inputBytes & 3;
        if (rLen != 0)
        {
            temp = __SIRegs[i + 32];
            for (i = 0; i < rLen; i++)
            {
                *(input++) = temp >> ((3 - i) * 8);
            }
        }
        sr >>= ((3 - Si.chan) * 8);
        sr &= 0xF;
        Si.chan = -1;
    }

    return sr;
}
static void
SITransferNext(s32 chan)
{
    int              i;
    struct SIPacket* packet;

    for (i = 0; i < 4; i++)
    {
        chan++;
        chan %= 4;
        packet = &Packet[chan];

        if (packet->chan != -1)
        {
            if (packet->time <= OSGetTime())
            {
                if (__SITransfer(packet->chan, packet->output, packet->outputBytes,
                                 packet->input, packet->inputBytes,
                                 packet->callback) != 0)
                {
                    OSCancelAlarm(&Alarm[chan]);
                    packet->chan = -1;
                }
                return;
            }
        }
    }
}
static void
SIIntrruptHandler(s16 unused, struct OSContext* context)
{
    s32  chan;
    u32  sr;
    void (*callback)(s32, u32, struct OSContext*);

    ASSERTLINE(0xE2, Si.chan != CHAN_NONE);

    chan = Si.chan;
    sr = CompleteTransfer();
    callback = Si.callback;
    Si.callback = NULL;
    SITransferNext(chan);
    if (callback)
    {
        callback(chan, sr, context);
    }
}
void
SIInit()
{
    Packet[0].chan = Packet[1].chan = Packet[2].chan = Packet[3].chan = -1;
    __SIRegs[0x30 / 4] = 0;
    do {
    }
    while (__SIRegs[SI_COMCSR_IDX] & SI_COMCSR_TSTART_MASK);
    __SIRegs[SI_COMCSR_IDX] = SI_COMCSR_TCINT_MASK;
    __OSSetInterruptHandler(0x14, SIIntrruptHandler);
    __OSUnmaskInterrupts(0x800);
}
static int
__SITransfer(s32 chan, void* output, u32 outputBytes, void* input, u32 inputBytes,
             void (*callback)(s32, u32, struct OSContext*))
{
    int enabled;
    u32 rLen;
    u32 i;
    u32 sr;
    union
    {
        u32 val;
        struct
        {
            u32 tcint      : 1;
            u32 tcintmsk   : 1;
            u32 comerr     : 1;
            u32 rdstint    : 1;
            u32 rdstintmsk : 1;
            u32 pad2       : 4;
            u32 outlngth   : 7;
            u32 pad1       : 1;
            u32 inlngth    : 7;
            u32 pad0       : 5;
            u32 channel    : 2;
            u32 tstart     : 1;
        } f;
    } comcsr;
    ASSERTMSGLINE(0x12A, (chan >= 0) && (chan < 4),
                  "SITransfer(): invalid channel.");
    ASSERTMSGLINE(0x12C, (outputBytes != 0) && (outputBytes <= 128),
                  "SITransfer(): output size is out of range (must be 1 to 128).");
    ASSERTMSGLINE(0x12E, (inputBytes != 0) && (inputBytes <= 128),
                  "SITransfer(): input size is out of range (must be 1 to 128).");

    enabled = OSDisableInterrupts();
    if (Si.chan != -1)
    {
        OSRestoreInterrupts(enabled);
        return 0;
    }
    ASSERTLINE(0x138, (__SIRegs[SI_COMCSR_IDX] &
                       (SI_COMCSR_TSTART_MASK | SI_COMCSR_TCINT_MASK)) == 0);
    sr = __SIRegs[SI_STATUS_IDX];
    sr &= (0x0F000000 >> (chan * 8));
    __SIRegs[SI_STATUS_IDX] = sr;

    Si.chan = chan;
    Si.callback = callback;
    Si.inputBytes = inputBytes;
    Si.input = input;

    rLen = ROUND(outputBytes, 4) / 4;
    for (i = 0; i < rLen; i++)
    {
        __SIRegs[i + 0x20] = ((u32*)output)[i];
    }

    comcsr.val = 0;
    comcsr.f.tcint = 1;
    comcsr.f.tcintmsk = callback ? 1 : 0;
    comcsr.f.outlngth = outputBytes == 0x80 ? 0 : outputBytes;
    comcsr.f.inlngth = inputBytes == 0x80 ? 0 : inputBytes;
    comcsr.f.channel = chan;
    comcsr.f.tstart = 1;

    __SIRegs[SI_COMCSR_IDX] = comcsr.val;
    OSRestoreInterrupts(enabled);
    return 1;
}
u32
SISync()
{
    int enabled;                             // r31
    u32 sr;                                  // r30

    do {
    }
    while (__SIRegs[SI_COMCSR_IDX] & SI_COMCSR_TSTART_MASK);

    enabled = OSDisableInterrupts();
    sr = CompleteTransfer();
    SITransferNext(4);
    OSRestoreInterrupts(enabled);
    return sr;
}
u32
SIGetStatus()
{
    return __SIRegs[SI_STATUS_IDX];
}
void
SISetCommand(s32 chan, u32 command)
{
    ASSERTMSGLINE(0x197, (chan >= 0) && (chan < 4),
                  "SISetCommand(): invalid channel.");
    __SIRegs[chan * 3] = command;
}
u32
SIGetCommand(s32 chan)
{
    ASSERTMSGLINE(0x1A9, (chan >= 0) && (chan < 4),
                  "SIGetCommand(): invalid channel.");
    return __SIRegs[chan * 3];
}
void
SITransferCommands()
{
    __SIRegs[SI_STATUS_IDX] = SI_COMCSR_TCINT_MASK;
}
u32
SISetXY(u32 x, u32 y)
{
    u32 poll;
    int enabled;

    ASSERTMSGLINE(0x1CA, x >= 8, "SISetXY(): x is out of range (8 <= x <= 255).");
    ASSERTMSGLINE(0x1CB, x <= 255, "SISetXY(): x is out of range (8 <= x <= 255).");
    ASSERTMSGLINE(0x1CC, y <= 255, "SISetXY(): y is out of range (0 <= y <= 255).");

    poll = x << 0x10;
    poll |= y << 8;
    enabled = OSDisableInterrupts();
    Si.poll &= 0xFC0000FF;
    Si.poll |= poll;
    poll = Si.poll;
    OSRestoreInterrupts(enabled);
    return poll;
}
u32
SIEnablePolling(u32 poll)
{
    int enabled;
    u32 en;

    ASSERTMSGLINE(0x1E8, !(poll & 0x0FFFFFFF),
                  "SIEnablePolling(): invalid chan bit(s).");
    if (poll == 0)
    {
        return Si.poll;
    }

    enabled = OSDisableInterrupts();
    __SIRegs[0x30 / 4] = 0;
    poll = poll >> 24;
    en = poll & 0xF0;
    ASSERTLINE(0x202, en);
    poll &= ((en >> 4) | 0x03FFFFF0);
    poll &= 0xFC0000FF;

    Si.poll &= ~(en >> 4);
    Si.poll |= poll;
    poll = Si.poll;
    __SIRegs[0x38 / 4] = 0x80000000;
    __SIRegs[0x30 / 4] = poll;
    OSRestoreInterrupts(enabled);
    return poll;
}
u32
SIDisablePolling(u32 poll)
{
    int enabled;

    ASSERTMSGLINE(0x22D, !(poll & 0x0FFFFFFF),
                  "SIDisablePolling(): invalid chan bit(s).");
    if (poll == 0)
    {
        return Si.poll;
    }
    enabled = OSDisableInterrupts();
    poll = poll >> 24;
    poll &= 0xF0;
    ASSERTLINE(0x23A, poll);
    poll = Si.poll & ~poll;
    __SIRegs[0x30 / 4] = poll;
    Si.poll = poll;
    OSRestoreInterrupts(enabled);
    return poll;
}
void
SIGetResponse(s32 chan, void* data)
{
    ASSERTMSGLINE(0x250, ((chan >= 0) && (chan < 4)),
                  "SIGetResponse(): invalid channel.");
    ((u32*)data)[0] = __SIRegs[chan * 3 + 1];
    ((u32*)data)[1] = __SIRegs[chan * 3 + 2];
}
static void
AlarmHandler(struct OSAlarm* alarm, struct OSContext* context)
{
    s32              chan;
    struct SIPacket* packet;

    chan = alarm - Alarm;

    ASSERTLINE(0x266, 0 <= chan && chan < SI_MAX_CHAN);
    ASSERTLINE(0x267,
               packet->time <= OSGetTime()); // WTF? Dereferencing a NULL POINTER?
    packet = &Packet[chan];

    if (packet->chan != -1 &&
        __SITransfer(packet->chan, packet->output, packet->outputBytes,
                     packet->input, packet->inputBytes, packet->callback))
    {
        packet->chan = -1;
    }
}
int
SITransfer(s32 chan, void* output, u32 outputBytes, void* input, u32 inputBytes,
           void (*callback)(s32, u32, struct OSContext*), s64 time)
{
    int              enabled;
    struct SIPacket* packet;
    s64              now;

    packet = &Packet[chan];
    enabled = OSDisableInterrupts();

    if (packet->chan != -1)
    {
        OSRestoreInterrupts(enabled);
        return 0;
    }
    now = OSGetTime();
    if (time == 0)
    {
        time = now;
    }
    if (now < time)
    {
        OSSetAbsAlarm(&Alarm[chan], time, AlarmHandler);
    }
    else if (__SITransfer(chan, output, outputBytes, input, inputBytes, callback))
    {
        OSRestoreInterrupts(enabled);
        return 1;
    }
    packet->chan = chan;
    packet->output = output;
    packet->outputBytes = outputBytes;
    packet->input = input;
    packet->inputBytes = inputBytes;
    packet->callback = callback;
    packet->time = time;
    OSRestoreInterrupts(enabled);
    return 1;
}
