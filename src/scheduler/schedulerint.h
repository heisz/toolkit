/**
 * Internal definitions for the Summit M:N scheduler, exposed for test.
 * 
 * Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */

#ifndef GMP_SCHEDULERINT_H
#define GMP_SCHEDULERINT_H 1

#include <stdint.h>
#include <stdatomic.h>
#include "thread.h"

/*
 * Lightweight context switching implementation, similar to ucontext but
 * without the unneeded overhead of signal mask retention (needs a syscall).
 * Unlike the Go gogo/mcall functions, however, the calling Summit methods
 * are all written in C and will need to support callee-saved register
 * conditions (Go uses stack exclusively).
 */ 

#if defined(__x86_64__) || defined(_M_X64)
    
typedef struct GMPS_Ctx {
    /* Stack pointer */
    uintptr_t rsp;
    /* Instruction pointer (return address) */
    uintptr_t rip;
    /* Frame pointer */
    uintptr_t rbp;

    /* Set of callee-saved registers */
    uintptr_t rbx;
    uintptr_t r12;
    uintptr_t r13;
    uintptr_t r14;
    uintptr_t r15;

    /* Stack info */
    uint8_t *stack;
    size_t stackSize;
} GMPS_Ctx;

#elif defined(__aarch64__) || defined(_M_ARM64)

typedef struct GMPS_Ctx {
    /* Stack pointer */
    uintptr_t sp;
    /* Program counter (saved from x30/lr) */
    uintptr_t pc;
    /* Frame pointer */
    uintptr_t x29;

    /* Set of callee-saved registers */
    uintptr_t x19;
    uintptr_t x20;
    uintptr_t x21;
    uintptr_t x22;
    uintptr_t x23;
    uintptr_t x24;
    uintptr_t x25;
    uintptr_t x26;
    uintptr_t x27;
    uintptr_t x28;

    /* Stack info */
    uint8_t *stack;
    size_t stackSize;
} GMPS_Ctx;

#else

/* Yah, I don't ever see porting this to Windows...*/
#error "Unsupported architecture - need x86-64 or ARM64"

#endif

/* Fiber and processor status enumerations */
typedef enum {
    /* Just allocated, not initialized */
    SFBR_IDLE = 0,
    /* On a run queue, not running */
    SFBR_RUNNABLE,
    /* Currently executing on a machine thread */
    SFBR_RUNNING,
    /* Blocked, not on a run queue */
    SFBR_WAITING,
    /* In a blocking syscall, thread detached from processor */
    SFBR_SYSCALL,
    /* Finished execution, can be reused */
    SFBR_DEAD,
    /* Preempted, waiting to be rescheduled */
    SFBR_PREEMPTED
} GMPS_FiberStatus;

typedef enum {
    /* Not associated with a machine thread */
    SPRC_IDLE = 0,
    /* Associated with a machine thread, running code */
    SPRC_RUNNING,
    /* Not used but being consistent */
    SPRC_DEAD
} GMPS_ProcessorStatus;

/* Love me some forward declarations */
typedef struct GMPS_Fiber GMPS_Fiber;
typedef struct GMPS_Processor GMPS_Processor;
typedef struct GMPS_Thread GMPS_Thread;

/* Structure to contain a fiber of execution (a no-go goroutine) */
struct GMPS_Fiber {
    /* Context used for fiber switching, at root for trivial casting */
    GMPS_Ctx ctx;

    /* Tracking/management details */
    uint64_t id;
    _Atomic(GMPS_FiberStatus) status;

    /* Storage of execution target for start handoff */
    GMPS_StartFn startFn;
    void *fnArg;

    /* Thread running this fiber, NULL when idle/parked */
    GMPS_Thread *thread;

    /* Tracking elements for network I/O wait states */
    uint32_t waitSocket;
    uint32_t waitEvents;
    uint32_t readyEvents;

    /* Valgrind stack registration identifier for custom stack tracking */
#ifdef HAVE_VALGRIND_VALGRIND_H
    unsigned valgrindStackId;
#endif

    /* Chaining pointer for linked fiber list support (global queue) */
    _Atomic(GMPS_Fiber *) nextFiber;
};

/* Local run queue size for processor, must be power of two */
#define LOCAL_RUNQ_SIZE 4

/* Structure for a logical processor that runs fibers on machine threads */
struct GMPS_Processor {
    /* Tracking/management details (processors aren't dynamic but still) */
    uint32_t id;
    _Atomic(GMPS_ProcessorStatus) status;

    /* Thread associated to this processor */
    _Atomic(GMPS_Thread *) thread;

    /* Local run queue (lock-free ring buffer) and priority routing */
    _Atomic(uint32_t) runQHead;
    _Atomic(uint32_t) runQTail;
    GMPS_Fiber *runQ[LOCAL_RUNQ_SIZE];
    _Atomic(GMPS_Fiber *) runNext;

    /* Local free fiber list for reuse */
    GMPS_Fiber *freeFiberList;
    int32_t freeFiberCount;

    /* Tick on every schedule() that reads queue, allows global fairness */
    uint32_t schedTick;

    /* Chaining pointer for linked process list support */
    GMPS_Processor *nextProc;
};

/*
 * Prototype for post-yield callback, run on g0 after context switch.  Ensures
 * actions are not taken while fiber is still executing.  Returns TRUE if
 * parked, FALSE if it needs to be requeued.
 */
typedef int (*GMPS_ParkFn)(GMPS_Fiber *fbr, void *arg);

/* Structure for machine thread instance */
struct GMPS_Thread {
    /* Tracking id and underlying (actual) OS thread */
    uint64_t id; 
    WXThread osThread;

    /* Thread state management */
    WXThread_Mutex idleLock;
    WXThread_Cond idleCond;
    _Atomic(int) spinning;
    _Atomic(int) idle;

    /* The fiber running the scheduler for this thread (named to match Go) */
    GMPS_Fiber *g0;

    /* Tracking elements for execution state */
    GMPS_Processor *currProcessor;
    GMPS_Fiber *currFiber;

    /* Callback details for post-yield processing */
    GMPS_ParkFn parkFn;
    void *parkArg;
    GMPS_Fiber *parkFiber;

    /* Completed fiber awaiting release on g0 (deffered from own stack) */
    GMPS_Fiber *exitFiber;

    /* Targetted processor to run on this thread */
    GMPS_Processor *targProcessor;

    /* Processor released during syscall for fast-path reacquisition */
    GMPS_Processor *syscallProc;

    /* Linked list chains for all and idle thread list */
    GMPS_Thread *allNextThread;
    GMPS_Thread *idleNextThread;
};

#endif
