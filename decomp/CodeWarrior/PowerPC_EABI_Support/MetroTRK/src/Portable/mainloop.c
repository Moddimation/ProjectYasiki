#include "Os/dolphin/targcont.h"
#include "Portable/dispatch.h"
#include "Portable/msgbuf.h"
#include "Portable/serpoll.h"
#include "Processor/ppc/Generic/targimpl.h"

void
TRKHandleRequestEvent (TRKEvent* event)
{
    TRKBuffer* buffer = TRKGetBuffer (event->msgBufID);
    TRKDispatchMessage (buffer);
}

void
TRKHandleSupportEvent (TRKEvent* event)
{
    TRKTargetSupportRequest();
}

void
TRKIdle ()
{
    if (TRKTargetStopped() == FALSE)
    {
        TRKTargetContinue();
    }
}

void
TRKNubMainLoop (void)
{
    void*    msg;
    TRKEvent event;
    BOOL     isShutdownRequested;
    BOOL     isNewInput;

    isShutdownRequested = FALSE;
    isNewInput = FALSE;
    while (isShutdownRequested == FALSE)
    {
        if (TRKGetNextEvent (&event) != FALSE)
        {
            isNewInput = FALSE;

            switch (event.eventType)
            {
                case NUBEVENT_Null:
                    break;

                case NUBEVENT_Request:
                    TRKHandleRequestEvent (&event);
                    break;

                case NUBEVENT_Shutdown:
                    isShutdownRequested = TRUE;
                    break;

                case NUBEVENT_Breakpoint:
                case NUBEVENT_Exception:
                    TRKTargetInterrupt (&event);
                    break;

                case NUBEVENT_Support:
                    TRKHandleSupportEvent (&event);
                    break;
            }

            TRKDestructEvent (&event);
            continue;
        }

        if ((isNewInput == FALSE) || (*(u8*)gTRKInputPendingPtr != 'ﾂ･0'))
        {
            isNewInput = TRUE;
            TRKGetInput();
            continue;
        }

        TRKIdle();
        isNewInput = FALSE;
    }
}
