#include <dolphin/os.h>

#include "OSPrivate.h"

#define ENQUEUE_MUTEX(mutex, queue, link)                                                      \
    do {                                                                                       \
        OSMutex* __prev = (queue)->tail;                                                       \
        if (__prev == NULL)                                                                    \
        {                                                                                      \
            (queue)->head = (mutex);                                                           \
        }                                                                                      \
        else                                                                                   \
        {                                                                                      \
            __prev->link.next = (mutex);                                                       \
        }                                                                                      \
        (mutex)->link.prev = __prev;                                                           \
        (mutex)->link.next = 0;                                                                \
        (queue)->tail = (mutex);                                                               \
    }                                                                                          \
    while (0);

#define DEQUEUE_MUTEX(mutex, queue, link)                                                      \
    do {                                                                                       \
        OSMutex* __next = (mutex)->link.next;                                                  \
        OSMutex* __prev = (mutex)->link.prev;                                                  \
        if (__next == NULL)                                                                    \
        {                                                                                      \
            (queue)->tail = __prev;                                                            \
        }                                                                                      \
        else                                                                                   \
        {                                                                                      \
            __next->link.prev = __prev;                                                        \
        }                                                                                      \
        if (__prev == NULL)                                                                    \
        {                                                                                      \
            (queue)->head = __next;                                                            \
        }                                                                                      \
        else                                                                                   \
        {                                                                                      \
            __prev->link.next = __next;                                                        \
        }                                                                                      \
    }                                                                                          \
    while (0);

#define DEQUEUE_HEAD(mutex, queue, link)                                                       \
    do {                                                                                       \
        OSMutex* __next = mutex->link.next;                                                    \
        if (__next == NULL)                                                                    \
        {                                                                                      \
            (queue)->tail = 0;                                                                 \
        }                                                                                      \
        else                                                                                   \
        {                                                                                      \
            __next->link.prev = 0;                                                             \
        }                                                                                      \
        (queue)->head = __next;                                                                \
    }                                                                                          \
    while (0);

static BOOL IsMember (OSMutexQueue* queue, OSMutex* mutex);
int         __OSCheckMutex (OSMutex* mutex);
int         __OSCheckDeadLock (OSThread* thread);
int         __OSCheckMutexes (OSThread* thread);

void
OSInitMutex (OSMutex* mutex)
{
    OSInitThreadQueue (&mutex->queue);
    mutex->thread = 0;
    mutex->count = 0;
}

void
OSLockMutex (OSMutex* mutex)
{
    int       enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();

    ASSERTMSGLINE (0x8C, currentThread, "OSLockMutex(): current thread does not exist.");
    ASSERTMSGLINE (0x8E,
                   currentThread->state == 2,
                   "OSLockMutex(): current thread is not running.");

    while (1)
    {
        OSThread* ownerThread = mutex->thread;
        if (ownerThread == 0)
        {
            mutex->thread = currentThread;
            mutex->count++;
            ENQUEUE_MUTEX (mutex, &currentThread->queueMutex, link);
            break;
        }
        else if (ownerThread == currentThread)
        {
            mutex->count++;
            break;
        }
        else
        {
            currentThread->mutex = mutex;
            __OSPromoteThread (mutex->thread, currentThread->priority);
            ASSERTMSG2LINE (0xA4,
                            __OSCheckDeadLock (currentThread) == 0,
                            "OSLockMutex(): detected deadlock: current thread %p, mutex %p.",
                            currentThread,
                            mutex);
            OSSleepThread (&mutex->queue);
            currentThread->mutex = NULL;
        }
    }
    OSRestoreInterrupts (enabled);
}

void
OSUnlockMutex (OSMutex* mutex)
{
    int       enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();

    ASSERTMSGLINE (0xBD, currentThread, "OSUnlockMutex(): current thread does not exist.");
    ASSERTMSGLINE (0xBF,
                   currentThread->state == 2,
                   "OSUnlockMutex(): current thread is not running.");
    ASSERTMSG2LINE (0xC2,
                    mutex->thread == currentThread,
                    "OSUnlockMutex(): current thread %p is not the owner of mutex %p.",
                    currentThread,
                    mutex);

    if (mutex->thread == currentThread)
    {
        if (!--mutex->count)
        {
            DEQUEUE_MUTEX (mutex, &currentThread->queueMutex, link);
            mutex->thread = 0;

            if (currentThread->priority < currentThread->base)
            {
                currentThread->priority = __OSGetEffectivePriority (currentThread);
            }
            OSWakeupThread (&mutex->queue);
        }
    }
    OSRestoreInterrupts (enabled);
}

void
__OSUnlockAllMutex (OSThread* thread)
{
    OSMutex* mutex;

    while (thread->queueMutex.head)
    {
        mutex = thread->queueMutex.head;
        DEQUEUE_HEAD (mutex, &thread->queueMutex, link);
        ASSERTLINE (0xE5, mutex->thread == thread);
        mutex->count = 0;
        mutex->thread = 0;
        OSWakeupThread (&mutex->queue);
    }
}

BOOL
OSTryLockMutex (OSMutex* mutex)
{
    int       enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();
    int       locked;

    ASSERTMSGLINE (0xFF, currentThread, "OSTryLockMutex(): current thread does not exist.");
    ASSERTMSGLINE (0x101,
                   currentThread->state == 2,
                   "OSTryLockMutex(): current thread is not running.");

    if (!mutex->thread)
    {
        mutex->thread = currentThread;
        mutex->count++;
        ENQUEUE_MUTEX (mutex, &currentThread->queueMutex, link);
        locked = TRUE;
    }
    else if (mutex->thread == currentThread)
    {
        mutex->count++;
        locked = TRUE;
    }
    else
    {
        locked = FALSE;
    }
    OSRestoreInterrupts (enabled);
    return locked;
}

void
OSInitCond (OSCond* cond)
{
    OSInitThreadQueue (&cond->queue);
}

void
OSWaitCond (OSCond* cond, OSMutex* mutex)
{
    int       enabled = OSDisableInterrupts();
    OSThread* currentThread = OSGetCurrentThread();

    ASSERTMSGLINE (0x139, currentThread, "OSWaitCond(): current thread does not exist.");
    ASSERTMSGLINE (0x13B,
                   currentThread->state == 2,
                   "OSWaitCond(): current thread is not running.");
    ASSERTMSG2LINE (0x13E,
                    mutex->thread == currentThread,
                    "OSWaitCond(): current thread %p is not the owner of mutex %p.",
                    currentThread,
                    mutex);

    if (mutex->thread == currentThread)
    {
        s32 count = mutex->count;
        mutex->count = 0;
        DEQUEUE_MUTEX (mutex, &currentThread->queueMutex, link);
        mutex->thread = 0;
        if (currentThread->priority < currentThread->base)
        {
            currentThread->priority = __OSGetEffectivePriority (currentThread);
        }
        OSDisableScheduler();
        OSWakeupThread (&mutex->queue);
        OSEnableScheduler();
        OSSleepThread (&cond->queue);
        OSLockMutex (mutex);
        mutex->count = count;
    }
    OSRestoreInterrupts (enabled);
}

void
OSSignalCond (OSCond* cond)
{
    OSWakeupThread (&cond->queue);
}

static BOOL
IsMember (OSMutexQueue* queue, OSMutex* mutex)
{
    OSMutex* member = queue->head;

    while (member)
    {
        if (mutex == member)
        {
            return TRUE;
        }
        member = member->link.next;
    }

    return FALSE;
}

BOOL
__OSCheckMutex (OSMutex* mutex)
{
    OSThread*      thread;
    OSThreadQueue* queue;
    s32            priority;

    priority = 0;
    queue = &mutex->queue;

    if (queue->head != NULL && queue->head->link.prev != NULL)
    {
        return FALSE;
    }
    if (queue->tail != NULL && queue->tail->link.next != NULL)
    {
        return FALSE;
    }
    thread = queue->head;
    while (thread)
    {
        if (thread->link.next != NULL && (thread != thread->link.next->link.prev))
        {
            return FALSE;
        }
        if (thread->link.prev != NULL && (thread != thread->link.prev->link.next))
        {
            return FALSE;
        }
        if (thread->state != 4)
        {
            return FALSE;
        }
        if (thread->priority < priority)
        {
            return FALSE;
        }
        priority = thread->priority;
        thread = thread->link.next;
    }
    if (mutex->thread)
    {
        if (mutex->count <= 0)
        {
            return FALSE;
        }
    }
    else if (mutex->count != 0)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL
__OSCheckDeadLock (OSThread* thread)
{
    OSMutex* mutex = thread->mutex;

    while (mutex && mutex->thread)
    {
        if (mutex->thread == thread)
        {
            return TRUE;
        }
        mutex = mutex->thread->mutex;
    }

    return FALSE;
}

BOOL
__OSCheckMutexes (OSThread* thread)
{
    OSMutex* mutex = thread->queueMutex.head;

    while (mutex)
    {
        if (mutex->thread != thread)
        {
            return FALSE;
        }
        if (__OSCheckMutex (mutex) == 0)
        {
            return FALSE;
        }
        mutex = mutex->link.next;
    }

    return TRUE;
}
