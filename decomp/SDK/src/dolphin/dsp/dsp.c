#include <dolphin/os.h>

#include <stddef.h>

#include "dsp_private.h"

u32
DSPCheckMailToDSP (void)
{
    return (u32)(__DSPRegs[0] >> 0xF & 1);
}

u32
DSPCheckMailFromDSP (void)
{
    return (u32)(__DSPRegs[2] >> 0xF & 1);
}

u32
DSPReadCPUToDSPMbox (void)
{
    return (u32)(__DSPRegs[0] << 0x10) | (u32)__DSPRegs[1];
}

u32
DSPReadMailFromDSP (void)
{
    return (u32)(__DSPRegs[2] << 0x10) | (u32)__DSPRegs[3];
}

void
DSPSendMailToDSP (u32 mail)
{
    __DSPRegs[0] = (u16)(mail >> 16);
    __DSPRegs[1] = (u16)(mail & 0xFFFF);
}

void
DSPAssertInt (void)
{
    BOOL old;
    u16  tmp;

    old          = OSDisableInterrupts();

    tmp          = __DSPRegs[5];
    tmp          = (u16)((tmp & ~0xA8) | 2);
    __DSPRegs[5] = tmp;

    OSRestoreInterrupts (old);
}

static BOOL  __DSP_init_flag;
DSPTaskInfo* __DSP_first_task;
DSPTaskInfo* __DSP_last_task;
DSPTaskInfo* __DSP_curr_task;
DSPTaskInfo* __DSP_tmp_task;

#ifndef JAUDIO_OLD
void
DSPInit (void)
{
    BOOL old;
    u16  tmp;

    __DSP_debug_printf ("DSPInit(): Build Date: %s %s\n", BUILD_DATE, BUILD_TIME);

    if (__DSP_init_flag)
    {
        return;
    }

    old = OSDisableInterrupts();
    __OSSetInterruptHandler (7, __DSPHandler);
    __OSUnmaskInterrupts (OS_INTERRUPTMASK_DSP_DSP);

    tmp          = __DSPRegs[5];
    tmp          = (u16)((tmp & ~0xA8) | 0x800);
    __DSPRegs[5] = tmp;

    tmp          = __DSPRegs[5];
    __DSPRegs[5] = tmp = (u16)(tmp & ~0xAC);

    __DSP_first_task = __DSP_last_task = __DSP_curr_task = __DSP_tmp_task = NULL;
    __DSP_init_flag                                                       = TRUE;

    OSRestoreInterrupts (old);
}
#endif

BOOL
DSPCheckInit (void)
{
    return __DSP_init_flag;
}

void
DSPReset (void)
{
    BOOL old;
    u16  tmp;

    old             = OSDisableInterrupts();

    tmp             = __DSPRegs[5];
    tmp             = (u16)((tmp & ~0xA8) | 0x800 | 1);
    __DSPRegs[5]    = tmp;
    __DSP_init_flag = FALSE;

    OSRestoreInterrupts (old);
}

void
DSPHalt (void)
{
    BOOL old;
    u16  tmp;

    old          = OSDisableInterrupts();

    tmp          = __DSPRegs[5];
    tmp          = (u16)((tmp & ~0xA8) | 4);
    __DSPRegs[5] = tmp;

    OSRestoreInterrupts (old);
}

void
DSPUnhalt (void)
{
    BOOL old;
    u16  tmp;

    old          = OSDisableInterrupts();

    tmp          = __DSPRegs[5];
    tmp          = (u16)((tmp & ~0xAC));
    __DSPRegs[5] = tmp;

    OSRestoreInterrupts (old);
}

u32
DSPGetDMAStatus (void)
{
    return (u32)(__DSPRegs[5] & (1 << 9));
}

DSPTaskInfo*
DSPAddTask (DSPTaskInfo* task)
{
    BOOL old;

    ASSERTMSGLINE (0x21E,
                   __DSP_init_flag == TRUE,
                   "DSPAddTask(): DSP driver not initialized!\n");

    old = OSDisableInterrupts();

    __DSP_insert_task (task);
    task->state = 0;
    task->flags = 1;

    OSRestoreInterrupts (old);
    if (task == __DSP_first_task)
    {
        __DSP_boot_task (task);
    }

    return task;
}

DSPTaskInfo*
DSPCancelTask (DSPTaskInfo* task)
{
    BOOL old;

    ASSERTMSGLINE (0x242,
                   __DSP_init_flag == 1,
                   "DSPCancelTask(): DSP driver not initialized!\n");

    old          = OSDisableInterrupts();

    task->flags |= 2;

    OSRestoreInterrupts (old);

    return task;
}

extern DSPTaskInfo* __DSP_rude_task;
extern int          __DSP_rude_task_pending;

DSPTaskInfo*
DSPAssertTask (DSPTaskInfo* task)
{
    s32 old;

    ASSERTMSGLINE (0x261,
                   __DSP_init_flag == TRUE,
                   "DSPAssertTask(): DSP driver not initialized!\n");
    ASSERTMSGLINE (0x262,
                   task->flags & 1,
                   "DSPAssertTask(): Specified task not in active task list!\n");

    old = OSDisableInterrupts();

    if (__DSP_curr_task == task)
    {
        __DSP_rude_task         = task;
        __DSP_rude_task_pending = 1;
        OSRestoreInterrupts (old);
        return task;
    }
    if (task->priority < __DSP_curr_task->priority)
    {
        __DSP_rude_task         = task;
        __DSP_rude_task_pending = 1;
        if (__DSP_curr_task->state == 1)
        {
            DSPAssertInt();
        }
        OSRestoreInterrupts (old);
        return task;
    }

    OSRestoreInterrupts (old);

    return NULL;
}
