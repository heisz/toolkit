/**
 * Multi-thread/fiber scheduler, modelled after the Go G-M-P scheduler design.
 *
 * Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "stdconfig.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <time.h>
#include <errno.h>
#include "scheduler.h"
#include "schedulerint.h"
#include "mem.h"
#ifdef HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

/* Only turn this on if you really mean it... */
// #define SCHEDULER_DEBUG 1

/**
 * Design/Documentation Note: unfortunately, there isn't a clean linguistic
 * path to the naming used here.  While a significant portion of this code is
 * based on the G-M-P scheduler used in Go, this isn't Go by any stretch of
 * the imagination.  The M:N threading model is closer (in generic terminology)
 * to fibers and that's the naming convention used herein.  But if it makes you
 * feel better, call it a goroutine (even if it isn't written in go).  At the
 * same time, it just seemed clearer to call a thread running on the machine a
 * thread...
 *
 * Also note that the WXEvent system uses or simulates epoll() with level
 * triggering.  For efficiency and C10K, this directly uses native epoll()
 * with edge-triggering for fiber yield/restart.  Fiber methods need to
 * ensure all data from the edge trigger is consumed.
 */

/* OS thread-local storage element for associated scheduler thread instance */
__thread GMPS_Thread *_tlsThread = NULL;

/*
 * Lightweight context switching implementation, similar to ucontext but
 * without the unneeded overhead of signal mask retention (needs a syscall).
 * Of course, different signal masking inside this library is not supported.
 * Unlike the Go gogo/mcall functions, however, this library is for methods
 * written in C and will need to support callee-saved register conditions
 * (Go uses stack exclusively).
 */

/*
 * Switch from one context to another (defined in assembly).  Saves the
 * current state into the provided 'from' context and then restores and
 * jumps to the provided 'to' context.
 */
extern void _gmps_ctx_switch(GMPS_Ctx *from, GMPS_Ctx *to);

/*
 * Jump to a context without saving current state (defined in assembly).
 */
extern void _gmps_ctx_jump(GMPS_Ctx *ctx);

/*
 * Assembly trampoline which is set up at root of stack to 'return' to g0.
 * Just a call wrapper for the return function to deal with ABI alignment.
 */
extern void _gmps_ctx_trampoline();

/*
 * Explicitly retrieve the g0 for the current thread for the trampoline/return
 * jump to allow fibers to safely migrate between threads.  Technically is
 * never called (by design) but just in case...
 */
void _gmps_ctx_trampoline_return() {
    GMPS_Thread *thr = _tlsThread;
    _gmps_ctx_jump(&(thr->g0->ctx));
}

/*
 * Initialize a context for a new fiber.  Sets up the initial stack so that
 * when the provided start function 'returns' after launching the context it
 * transfers execution back to g0 through the assembly trampoline (which
 * calls _gmps_trampoline_return above).  Provided context must have
 * stack/size stored.
 */
static void gmps_ctx_init(GMPS_Ctx *ctx, void (*startFn)()) {
    uintptr_t top;

    /* Save stack info, calculate top as x86 and ARM both grow downwards */
    top = (uintptr_t) (ctx->stack + ctx->stackSize);

#if defined(__x86_64__) || defined(_M_X64)
    /* x86-64 ABI requires 16 byte alignment for call, add return address gap */
    top &= ~((uintptr_t) 15);
    top -= 8;

    /* Stack will 'exit' through the trampoline */
    *((uintptr_t *) top) = (uintptr_t) _gmps_ctx_trampoline;

    /* Set up the context registers to target the start function */
    ctx->rsp = top;
    ctx->rip = (uintptr_t) startFn;

#elif defined(__aarch64__) || defined(_M_ARM64)
    /* AAPCS64 requires 16 byte alignment always, return is through x30 */
    top &= ~((uintptr_t) 15);

    /* Set up the context registers to target the start function */
    ctx->sp = top;
    ctx->pc = (uintptr_t) startFn;

    /* Cannot just set x30 because ctx switch manipulates it for pc. */
    /* Simulate a function prologue with x30/x29 (lr/fp) to trampoline. */
    top -= 16;
    *((uintptr_t *) (top + 8)) = (uintptr_t) _gmps_ctx_trampoline;
    *((uintptr_t *) top) = 0;
    ctx->sp = top;
    ctx->x29 = 0;

#endif
}

/*
 * Check if a context is initialized and valid (has a stack pointer).
 */
static inline int gmps_ctx_valid(GMPS_Ctx *ctx) {
#if defined(__x86_64__) || defined(_M_X64)
    return (ctx != NULL) && (ctx->rsp != 0);
#elif defined(__aarch64__) || defined(_M_ARM64)
    return (ctx != NULL) && (ctx->sp != 0);
#endif
}

/* Reset the context without wiping stack data */
static inline void gmps_ctx_reset(GMPS_Ctx *ctx) {
    uint8_t *origStack = ctx->stack;
    size_t origSize = ctx->stackSize;
    (void) memset(ctx, 0, sizeof(GMPS_Ctx));
    ctx->stack = origStack;
    ctx->stackSize = origSize;
}

/*****************************************/

/* The Marsgalia xor-shift random number generator, separate per thread */
static __thread uint32_t rstate = 1;

static void fastRandInit(uint64_t seed) {
    struct timespec ts;
    (void) clock_gettime(CLOCK_MONOTONIC, &ts);
    rstate = (uint32_t) (seed ^ (ts.tv_sec * 1000000000LL + ts.tv_nsec));
}

static uint32_t fastRandN(uint32_t range) {
    uint32_t x = rstate;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rstate = x;
    return x % range;
}

/* Note: the global queue is an FIFO queue and always accessed under lock */

typedef struct GMPS_FiberQueue {
    GMPS_Fiber *head;
    GMPS_Fiber *tail;
    uint32_t size;
} GMPS_FiberQueue;

static void fiberQueueInit(GMPS_FiberQueue *q) {
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

static int fiberQueueIsEmpty(GMPS_FiberQueue *q) {
    return (q->head == NULL);
}

/* Note that nextFiber is volatile and inside fiber, needs atomic access */

static void fiberQueuePush(GMPS_FiberQueue *q, GMPS_Fiber *fbr) {
    atomic_store(&(fbr->nextFiber), NULL);
    if (q->tail != NULL) {
        atomic_store(&(q->tail->nextFiber), fbr);
    } else {
        q->head = fbr;
    }
    q->tail = fbr;
    q->size++;
}

static GMPS_Fiber *fiberQueuePop(GMPS_FiberQueue *q) {
    GMPS_Fiber *fbr = q->head;

    /* FIFO queue so pop takes from head */
    if (fbr == NULL) return NULL;
    q->head = atomic_load(&(fbr->nextFiber));
    if (q->head == NULL) q->tail = NULL;
    q->size--;
    atomic_store(&(fbr->nextFiber), NULL);

    return fbr;
}

/* This is the fixed limit for processors (static allocation) */
#define MAXPROCS 256

/* Global scheduler tracking/management object */
struct GMPS_Sched {
    /* Mutex for controlling non-atomic field access */
    WXThread_Mutex lock;

    /* Id generators for fibers and threads, processors are fixed */
    _Atomic(uint64_t) fIdGen;
    _Atomic(uint64_t) tIdGen;

    /* Global run queue, only modify under scheduler.lock */
    GMPS_FiberQueue runQ;

    /* Free fiber list (global) */
    WXThread_Mutex freeFiberLock;
    GMPS_Fiber *freeFiberList;
    int32_t freeFiberCount;

    /* Full and idle processor list */
    uint32_t procCount;
    GMPS_Processor *processors[MAXPROCS];
    _Atomic(GMPS_Processor *) idleProcList;
    _Atomic(uint32_t) idleProcCount;

    /* Thread management elements (note that idle is managed under lock) */
    _Atomic(GMPS_Thread *) threadList;
    _Atomic(uint32_t) threadCount;
    GMPS_Thread *idleThreadList;
    int32_t idleThreadCount;
    _Atomic(int32_t) spinningCount;

    /* Network polling via epoll, thread-safe without mutex */
    int epollFd;

} scheduler;

/* For lack of a better place, wrap the global run queue here */
static void globRunQPut(GMPS_Fiber *fbr) {
    WXThread_MutexLock(&(scheduler.lock));
    fiberQueuePush(&(scheduler.runQ), fbr);
    WXThread_MutexUnlock(&(scheduler.lock));
}

/* Only four forward declarations, not bad */
static void runQPut(GMPS_Processor *proc, GMPS_Fiber *fbr, int next);
static void startThread(GMPS_Processor *proc, int spinning);
static void parkThread(GMPS_Thread *thr);
static int netpoll(int32_t delay, GMPS_FiberQueue *q);

/* NOTE: this method *must* be called under scheduler lock */
GMPS_Fiber *globRunQGet(GMPS_Processor *proc, int32_t max) {
    GMPS_Fiber *fbr, *tfbr;
    uint32_t cnt;

    /* Grabs one but drag in extras if local was drained */
    if (fiberQueueIsEmpty(&(scheduler.runQ))) return NULL;
    cnt = scheduler.runQ.size;
    if (cnt > scheduler.procCount) cnt = scheduler.procCount;
    if ((max > 0) && (cnt > max)) cnt = max;
    if (cnt > (LOCAL_RUNQ_SIZE / 2)) cnt = LOCAL_RUNQ_SIZE / 2;

    /* Grab the one we are going to return */
    fbr = fiberQueuePop(&(scheduler.runQ));
    cnt--;

    /* Push remaining (up to max/count) on local queue */
    for (; cnt > 0; cnt--) {
        tfbr = fiberQueuePop(&(scheduler.runQ));
        if (tfbr == NULL) break;
        runQPut(proc, tfbr, FALSE);
    }

    return fbr;
}

/********** Fiber management functions **********/

/* TODO - determine best size, we don't reallocate like Go */
#define STACK_SIZE (64 * 1024)

static GMPS_Fiber *allocFiber() {
    GMPS_Fiber *fbr = (GMPS_Fiber *) WXCalloc(sizeof(GMPS_Fiber));
    if (fbr == NULL) return NULL;

    /* Allocate the stack with a protected page at the bottom */
    size_t pageSize = sysconf(_SC_PAGESIZE);
    size_t totalSize = STACK_SIZE + pageSize;
    uint8_t *mem = mmap(NULL, totalSize, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        WXFree(fbr);
        return NULL;
    }
    if (mprotect(mem, pageSize, PROT_NONE) < 0) {
        (void) munmap(mem, totalSize);
        WXFree(fbr);
        return NULL;
    }

    fbr->ctx.stack = mem + pageSize;
    fbr->ctx.stackSize = STACK_SIZE;
    fbr->waitSocket = INVALID_SOCKET_FD;
#ifdef HAVE_VALGRIND_VALGRIND_H
    fbr->valgrindStackId =
        VALGRIND_STACK_REGISTER(fbr->ctx.stack,
                                fbr->ctx.stack + fbr->ctx.stackSize);
#endif

    return fbr;
}

/* Should really call this method poop()... */
static void freeFiber(GMPS_Fiber *fbr) {
    if (fbr->ctx.stack != NULL) {
#ifdef HAVE_VALGRIND_VALGRIND_H
        VALGRIND_STACK_DEREGISTER(fbr->valgrindStackId);
#endif
        size_t pageSize = sysconf(_SC_PAGESIZE);
        (void) munmap(fbr->ctx.stack - pageSize, fbr->ctx.stackSize + pageSize);
    }
    WXFree(fbr);
}

/* Get a fiber from the free cache or create a new one */
static GMPS_Fiber *getFiber(GMPS_Processor *proc) {
    GMPS_Fiber *fbr;

    /* Try local cache first */
    fbr = proc->freeFiberList;
    if (fbr != NULL) {
        proc->freeFiberList = atomic_load(&(fbr->nextFiber));
        proc->freeFiberCount--;
        atomic_store(&(fbr->nextFiber), NULL);
        return fbr;
    }

    /* Try global cache */
    WXThread_MutexLock(&(scheduler.freeFiberLock));
    fbr = scheduler.freeFiberList;
    if (fbr != NULL) {
        scheduler.freeFiberList = atomic_load(&(fbr->nextFiber));
        scheduler.freeFiberCount--;
    }
    WXThread_MutexUnlock(&(scheduler.freeFiberLock));
    if (fbr != NULL) {
        atomic_store(&(fbr->nextFiber), NULL);
        return fbr;
    }

    /* No luck finding one, make a new one */
    return allocFiber();
}

/* Push the fiber back into the free cache */
static void releaseFiber(GMPS_Processor *proc, GMPS_Fiber *fbr) {
    /* Unregister from epoll if socket is registered */
    if (fbr->waitSocket != INVALID_SOCKET_FD) {
        (void) epoll_ctl(scheduler.epollFd, EPOLL_CTL_DEL,
                         (int) fbr->waitSocket, NULL);
        fbr->waitSocket = INVALID_SOCKET_FD;
    }

    atomic_store(&(fbr->status), SFBR_DEAD);
    fbr->startFn = NULL;
    fbr->fnArg = NULL;
    fbr->waitEvents = 0;
    fbr->readyEvents = 0;
    gmps_ctx_reset(&(fbr->ctx));

    /* Add to local cache, with limits */
    if (proc->freeFiberCount < 64) {
        atomic_store(&(fbr->nextFiber), proc->freeFiberList);
        proc->freeFiberList = fbr;
        proc->freeFiberCount++;
        return;
    }

    /* Add to global cache */
    WXThread_MutexLock(&(scheduler.freeFiberLock));
    atomic_store(&(fbr->nextFiber), scheduler.freeFiberList);
    scheduler.freeFiberList = fbr;
    scheduler.freeFiberCount++;
    WXThread_MutexUnlock(&(scheduler.freeFiberLock));
}

/* Entry point for a fiber, launches the caller provided function/arg */
static void fiberStartFn() {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Processor *proc = thr->currProcessor;
    GMPS_Fiber *fbr = thr->currFiber;

#ifdef SCHEDULER_DEBUG
    (void) fprintf(stderr, "--> Running start function for fiber %lu\n",
                   (long unsigned) fbr->id);
#endif
    if (fbr->startFn != NULL) {
        fbr->startFn(fbr->fnArg);
    }
#ifdef SCHEDULER_DEBUG
    (void) fprintf(stderr, "--> Exit of start function for fiber %lu\n",
                   (long unsigned) fbr->id);
#endif

    /* Note that the user function may have done waits, etc. so stale */
    thr = _tlsThread;
    proc = thr->currProcessor;
    fbr = thr->currFiber;

    /* Nothing to do if this is the g0 scheduling fiber */
    if ((fbr == NULL) || (fbr == thr->g0)) return;

    /* Clear the park callback so schedule() sees completion, not yield */
    thr->parkFn = NULL;
    thr->parkArg = NULL;
    thr->parkFiber = NULL;

    /* Disassociate before release to avoid write-after-free on reuse */
    thr->currFiber = NULL;
    fbr->thread = NULL;

    /* Place fiber on free list for return to scheduler */
    releaseFiber(proc, fbr);

    /* Jump to g0 context without save (we're done here) */
    _gmps_ctx_jump(&(thr->g0->ctx));
}

/********** Processor management functions **********/

static void idleProcPut(GMPS_Processor *proc) {
    atomic_store(&(proc->status), SPRC_IDLE);
    proc->nextProc = atomic_load(&(scheduler.idleProcList));
    atomic_store(&(scheduler.idleProcList), proc);
    (void) atomic_fetch_add(&(scheduler.idleProcCount), 1);
}

static GMPS_Processor *idleProcGet() {
    GMPS_Processor *next, *proc = atomic_load(&(scheduler.idleProcList));
    while (proc != NULL) {
        next = proc->nextProc;
        if (atomic_compare_exchange_weak(&(scheduler.idleProcList),
                                         &proc, next)) {
            atomic_fetch_sub(&(scheduler.idleProcCount), 1);
            proc->nextProc = NULL;
            return proc;
        }

        proc = atomic_load(&(scheduler.idleProcList));
    }

    return NULL;
}

/* This could use _tlsThread (always current) but callers have thread */
static void acquireProc(GMPS_Thread *me, GMPS_Processor *proc) {
    atomic_store(&(proc->thread), me);
    me->currProcessor = proc;
    atomic_store(&(proc->status), SPRC_RUNNING);
}

static GMPS_Processor *releaseProc(GMPS_Thread *me) {
    GMPS_Processor *proc = me->currProcessor;

    if (proc == NULL) return NULL;

    atomic_store(&(proc->thread), NULL);
    me->currProcessor = NULL;
    atomic_store(&(proc->status), SPRC_IDLE);

    return proc;
}

/* Multiple routines to work with the processor run queue */
static int runQPutSlow(GMPS_Processor *proc, GMPS_Fiber *fbr,
                       uint32_t hd, uint32_t tl) {
    GMPS_Fiber *tmp, *batch[LOCAL_RUNQ_SIZE / 2 + 1];
    uint32_t idx, idy, cnt = (tl - hd) / 2;

    /* Steal leading half from the run queue, add incoming */
    if (cnt != (LOCAL_RUNQ_SIZE / 2)) return FALSE;
    for (idx = 0; idx < cnt; idx++) {
        batch[idx] = proc->runQ[(hd + idx) % LOCAL_RUNQ_SIZE];
    }
    if (!atomic_compare_exchange_strong(&(proc->runQHead),
                                        &hd, hd + cnt)) return FALSE;
    batch[cnt] = fbr;

#ifdef SCHEDULER_DEBUG
    (void) fprintf(stderr, "--> Prc-%lu, half to global, now %lu -> %lu?\n",
                   (long unsigned) proc->id, (long unsigned) hd + cnt,
                   (long unsigned) tl);
#endif

    /* Shuffle for fairness */
    for (idx = 1; idx <= cnt; idx++) {
        idy = fastRandN(idx + 1);
        tmp = batch[idx];
        batch[idx] = batch[idy];
        batch[idy] = tmp;
    }

    /* Put on global queue */
    WXThread_MutexLock(&(scheduler.lock));
    for (idx = 0; idx <= cnt; idx++) {
        fiberQueuePush(&(scheduler.runQ), batch[idx]);
    }
    WXThread_MutexUnlock(&(scheduler.lock));

#ifdef SCHEDULER_DEBUG
    (void) fprintf(stderr, "--> Half to global queue, total %lu\n",
                   (long unsigned) scheduler.runQ.size);
#endif

    return TRUE;
}

static void runQPut(GMPS_Processor *proc, GMPS_Fiber *fbr, int next) {
    /* If so indicated, try to swap into the priority slot */
    while (next) {
        GMPS_Fiber *oldNext = atomic_load(&(proc->runNext));
        if (atomic_compare_exchange_strong(&(proc->runNext),
                                           &oldNext, fbr)) {
            /* If there wasn't one, we're done, otherwise bounced... */
#ifdef SCHEDULER_DEBUG
            (void) fprintf(stderr, "--> Fbr-%lu on next prc-%lu, %s\n",
                           (long unsigned) fbr->id, (long unsigned) proc->id,
                           ((oldNext == NULL) ? "all done" :
                                                "bumped existing"));
#endif
            if (oldNext == NULL) return;
            fbr = oldNext;
            break;
        }
    }

    /* Push onto the runqueue, bumping on overflow */
    while (TRUE) {
        uint32_t hd = atomic_load_explicit(&(proc->runQHead),
                                           memory_order_acquire);
        uint32_t tl = atomic_load(&(proc->runQTail));

        if ((tl - hd) < LOCAL_RUNQ_SIZE) {
            proc->runQ[tl % LOCAL_RUNQ_SIZE] = fbr;
            atomic_store_explicit(&(proc->runQTail), tl + 1,
                                  memory_order_release);
#ifdef SCHEDULER_DEBUG
            (void) fprintf(stderr, "--> Fbr-%lu on runq prc-%lu, %lu -> %lu\n",
                           (long unsigned) fbr->id, (long unsigned) proc->id,
                           (long unsigned) hd, (long unsigned) tl + 1);
#endif
            return;
        }

        /* Queue is full, take some with it to the global queue */
        if (runQPutSlow(proc, fbr, hd, tl)) return;
    }
}

static GMPS_Fiber *runQGet(GMPS_Processor *proc, int *fromQueue) {
    /* Grab the priority entry first, if there */
    GMPS_Fiber *next = atomic_load(&(proc->runNext));
    while (next != NULL) {
        if (atomic_compare_exchange_strong(&(proc->runNext), &next, NULL)) {
            *fromQueue = FALSE;
            return next;
        }
        next = atomic_load(&(proc->runNext));
    }

    /* Get from regular queue */
    while (TRUE) {
        uint32_t hd = atomic_load_explicit(&(proc->runQHead),
                                           memory_order_acquire);
        uint32_t tl = atomic_load(&(proc->runQTail));

        if (tl == hd) {
            *fromQueue = TRUE;
            return NULL;
        }

        GMPS_Fiber *fbr = proc->runQ[hd % LOCAL_RUNQ_SIZE];
        if (atomic_compare_exchange_strong(&(proc->runQHead), &hd, hd + 1)) {
            *fromQueue = TRUE;
            return fbr;
        }
    }
}

static int runQIsEmpty(GMPS_Processor *proc) {
    uint32_t hd = atomic_load(&(proc->runQHead));
    uint32_t tl = atomic_load(&(proc->runQTail));
    GMPS_Fiber *next = atomic_load(&(proc->runNext));
    return (tl == hd) && (next == NULL);
}

/* Grab a half of fibers from the processor, including next if indicated */
static uint32_t runQGrab(GMPS_Processor *proc, GMPS_Fiber **batch,
                         uint32_t batchHead, int stealRunNext) {
    GMPS_Fiber *fbr;
    uint32_t idx;

    while (1) {
        uint32_t hd = atomic_load_explicit(&(proc->runQHead),
                                           memory_order_acquire);
        uint32_t tl = atomic_load_explicit(&(proc->runQTail),
                                           memory_order_acquire);
        uint32_t cnt = tl - hd;
        cnt = cnt - cnt / 2;

        /* If empty (and permitted) try to steal the next entry */
        if (cnt == 0) {
            if (stealRunNext) {
                fbr = atomic_load(&(proc->runNext));
                if (fbr != NULL) {
                    if (!atomic_compare_exchange_strong(&(proc->runNext),
                                                        &fbr, NULL)) {
                        /* Try, try again */
                        continue;
                    }
                    batch[batchHead % LOCAL_RUNQ_SIZE] = fbr;
                    return 1;
                }
            }
            return 0;
        }

        if (cnt > (LOCAL_RUNQ_SIZE / 2)) {
            /* Someone else is adjusting the queue, try again */
            continue;
        }

        /* Copy selection and sneak unopposed */
        for (idx = 0; idx < cnt; idx++) {
            fbr = proc->runQ[(hd + idx) % LOCAL_RUNQ_SIZE];
            batch[(batchHead + idx) % LOCAL_RUNQ_SIZE] = fbr;
        }
        if (atomic_compare_exchange_strong(&(proc->runQHead), &hd, hd + cnt)) {
            return cnt;
        }
    }
}

/* Steal from procFrom to fill procTo, returning last stolen (get) */
static GMPS_Fiber *runQSteal(GMPS_Processor *procTo, GMPS_Processor *procFrom,
                             int stealRunNext) {
    uint32_t hd, tl = atomic_load(&(procTo->runQTail));
    uint32_t cnt = runQGrab(procFrom, procTo->runQ, tl, stealRunNext);
    if (cnt == 0) return NULL;

    /* Grab last one, if that was it then tail doesn't change */
    GMPS_Fiber *fbr = procTo->runQ[(tl + (--cnt)) % LOCAL_RUNQ_SIZE];
    if (cnt == 0) return fbr;

    /* Otherwise need to update tail, note we are filling ourselves - no race */
    hd = atomic_load_explicit(&(procTo->runQHead), memory_order_acquire);
    if (tl - hd + cnt >= LOCAL_RUNQ_SIZE) {
        (void) fprintf(stderr, "runQSteal: runQ overflow\n");
        /* Ugh, this is less than ideal */
        abort();
    }
    atomic_store_explicit(&(procTo->runQTail), tl + cnt, memory_order_release);
    return fbr;
}

/* How many times to attempt stealing work */
#define STEAL_TRIES 4

static GMPS_Fiber *stealWork(GMPS_Processor *proc) {
    uint32_t start, idy, procCount = scheduler.procCount;
    int idx, stealRunNext;
    GMPS_Fiber *fbr;

    for (idx = 0; idx < STEAL_TRIES; idx++) {
        stealRunNext = (idx == STEAL_TRIES - 1);

        /* Random starting point for fairness */
        start = fastRandN(procCount);

        for (idy = 0; idy < procCount; idy++) {
            GMPS_Processor *targ =
                     scheduler.processors[(start + idy) % procCount];

            /* Skip if invalid, myself or twiddling its thumbs */
            if ((targ == proc) || (targ == NULL)) continue;
            if (atomic_load(&(targ->status)) == SPRC_IDLE) continue;

            fbr = runQSteal(proc, targ, stealRunNext);
            if (fbr != NULL) return fbr;
        }
    }

    return NULL;
}

static void wakeProc(void) {
    GMPS_Processor *proc;
    int32_t exp = 0;

    /* Be conservative about spinning threads */
    if (atomic_load(&(scheduler.spinningCount)) != 0) return;

    /* Only wake if no threads are spinning, will be one now */
    if (!atomic_compare_exchange_strong(&(scheduler.spinningCount),
                                        &exp, 1)) {
        return;
    }

    /* Grab an idle processor */
    WXThread_MutexLock(&(scheduler.lock));
    proc = idleProcGet();
    if (proc == NULL) {
        /* Processor no longer idle, revert spinning marker and proceed */
        atomic_fetch_sub(&(scheduler.spinningCount), 1);
        WXThread_MutexUnlock(&(scheduler.lock));
        return;
    }
    WXThread_MutexUnlock(&(scheduler.lock));

    /* Launch a spinning thread with the processor */
    startThread(proc, TRUE);
}

static void handoff(GMPS_Processor *proc) {
    /* If local/global queue has work, hand off to another thread */
    if ((!runQIsEmpty(proc)) || (!fiberQueueIsEmpty(&(scheduler.runQ)))) {
        startThread(proc, FALSE);
        return;
    }

    /* If no spinning threads and idle procs, hand off to a spinner */
    if ((atomic_load(&(scheduler.spinningCount)) == 0) &&
            (atomic_load(&(scheduler.idleProcCount)) > 0)) {
        startThread(proc, TRUE);
        return;
    }

    /* No work to do, just put the processor on the idle list */
    WXThread_MutexLock(&(scheduler.lock));
    idleProcPut(proc);
    WXThread_MutexUnlock(&(scheduler.lock));
}

/********** Thread management functions **********/

static GMPS_Thread *allocThread() {
    GMPS_Thread *thr = (GMPS_Thread *) WXCalloc(sizeof(GMPS_Thread));
    if (thr == NULL) return NULL;

    /* Caller will set the osThread instance */
    thr->id = atomic_fetch_add(&(scheduler.tIdGen), 1);

    if (WXThread_MutexInit(&(thr->idleLock), FALSE) != WXTRC_OK) {
        WXFree(thr);
        return NULL;
    }
    if (WXThread_CondInit(&(thr->idleCond)) != WXTRC_OK) {
        WXThread_MutexDestroy(&(thr->idleLock));
        WXFree(thr);
        return NULL;
    }
    atomic_store(&(thr->idle), FALSE);
    atomic_store(&(thr->spinning), FALSE);

    /* Create the g0/scheduling fiber */
    thr->g0 = allocFiber();
    if (thr->g0 == NULL) {
        WXThread_CondDestroy(&(thr->idleCond));
        WXThread_MutexDestroy(&(thr->idleLock));
        WXFree(thr);
        return NULL;
    }
    thr->g0->id = 0;
    atomic_store(&(thr->g0->status), SFBR_RUNNING);

    return thr;
}

static void freeThread(GMPS_Thread *thr) {
    (void) WXThread_MutexDestroy(&(thr->idleLock));
    (void) WXThread_CondDestroy(&(thr->idleCond));
    if (thr->g0 != NULL) {
        freeFiber(thr->g0);
    }
    WXFree(thr);
}

/* Find a runnable fiber instance, from backlog, stealing or waiting */
static GMPS_Fiber *findRunnable(int *fromQueue) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_FiberQueue netList;
    GMPS_Processor *proc;
    GMPS_Fiber *fbr;
    int32_t idx;

top:
    /* Waits and retries below may alter the current processor, start fresh */
    proc = thr->currProcessor;

    /* Periodically check the global queue for fairness */
    /* Note: do not combine, because global queue might empty while looking */
    *fromQueue = FALSE;
    if (((proc->schedTick % 61) == 0) &&
            (!fiberQueueIsEmpty(&(scheduler.runQ)))) {
        WXThread_MutexLock(&(scheduler.lock));
        fbr = globRunQGet(proc, 0);
        WXThread_MutexUnlock(&(scheduler.lock));
        if (fbr != NULL) {
#ifdef SCHEDULER_DEBUG
            (void) fprintf(stderr, "--> findRun: got fbr-%lu from global\n",
                           (long unsigned) fbr->id);
#endif
            return fbr;
        }
    }

    /* Target the local queue first */
    fbr = runQGet(proc, fromQueue);
    if (fbr != NULL) {
#ifdef SCHEDULER_DEBUG
            (void) fprintf(stderr, "--> findRun: got fbr-%lu from local %lu\n",
                           (long unsigned) fbr->id, (long unsigned) proc->id);
#endif
        return fbr;
    }

    /* Look to global queue next */
    if (!fiberQueueIsEmpty(&(scheduler.runQ))) {
        WXThread_MutexLock(&(scheduler.lock));
        fbr = globRunQGet(proc, 0);
        WXThread_MutexUnlock(&(scheduler.lock));
        if (fbr != NULL) {
#ifdef SCHEDULER_DEBUG
            (void) fprintf(stderr, "--> findRun: got fbr-%lu from global\n",
                           (long unsigned) fbr->id);
#endif
            return fbr;
        }
    }

    /* Poll network (non-blocking) */
    if (netpoll(0, &netList)) {
        /* Take the first one for ourselves */
        fbr = fiberQueuePop(&netList);

        /* Put rest on global queue */
        WXThread_MutexLock(&(scheduler.lock));
        while (!fiberQueueIsEmpty(&netList)) {
            GMPS_Fiber *nf = fiberQueuePop(&netList);
            fiberQueuePush(&(scheduler.runQ), nf);
        }
        WXThread_MutexUnlock(&(scheduler.lock));

        return fbr;
    }

    /* Try to steal work, if spinning or less than half busy spinning */
    uint32_t runCount = scheduler.procCount -
                              atomic_load(&(scheduler.idleProcCount));
    if (atomic_load(&(thr->spinning)) ||
            (2 * atomic_load(&(scheduler.spinningCount)) < runCount)) {
        /* Well, we're spinning now */
        if (!atomic_load(&(thr->spinning))) {
            atomic_store(&(thr->spinning), TRUE);
            atomic_fetch_add(&(scheduler.spinningCount), 1);
        }

        fbr = stealWork(proc);
        if (fbr != NULL) return fbr;
    }

    /* Nothing left to do - release processor and park thread */

    /* First check global queue one more time with lock held */
    WXThread_MutexLock(&(scheduler.lock));
    if (!fiberQueueIsEmpty(&(scheduler.runQ))) {
        fbr = globRunQGet(proc, 0);
        if (fbr != NULL) {
            WXThread_MutexUnlock(&(scheduler.lock));
            return fbr;
        }
    }

    /* Release processor */
    GMPS_Processor *oldp = releaseProc(thr);
    idleProcPut(oldp);
    WXThread_MutexUnlock(&(scheduler.lock));

    /* Stop spinning if we were */
    if (atomic_load(&(thr->spinning))) {
        atomic_store(&(thr->spinning), FALSE);
        atomic_fetch_sub(&(scheduler.spinningCount), 1);

        /* Double-check for work before parking */
        for (idx = 0; idx < scheduler.procCount; idx++) {
            GMPS_Processor *targ = scheduler.processors[idx];
            if ((targ != NULL) && (!runQIsEmpty(targ))) {
                WXThread_MutexLock(&(scheduler.lock));
                targ = idleProcGet();
                WXThread_MutexUnlock(&(scheduler.lock));
                if (targ != NULL) {
                    acquireProc(thr, targ);
                    atomic_store(&(thr->spinning), TRUE);
                    atomic_fetch_add(&(scheduler.spinningCount), 1);
                    goto top;
                }
                break;
            }
        }
    }

    /*
     * If all threads park while fibers are waiting on sockets,
     * an external thread must call GMPS_NetPoll() to deliver
     * events and restart scheduling.
     */

    /* Wait for something to do and start again */
    parkThread(thr);
    goto top;
}

/* Post general yield, safely set status and push back onto queue */
/* Avoids race where thread grabs fiber before context switch completes */
static int yieldParkFn(GMPS_Fiber *fbr, void *arg) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Processor *proc = thr->currProcessor;

    atomic_store(&(fbr->status), SFBR_RUNNABLE);
    runQPut(proc, fbr, FALSE);

    return TRUE;
}

/* Post socket yield, set status and register/re-arm epoll */
static int socketParkFn(GMPS_Fiber *fbr, void *arg) {
    WXSocket sock = (WXSocket)(uintptr_t) arg;
    struct epoll_event ev;
    int rc;

    ev.events = fbr->waitEvents | EPOLLET | EPOLLONESHOT;
    ev.data.ptr = fbr;

    /* Safe to set status, socket is not yet armed */
    atomic_store(&(fbr->status), SFBR_WAITING);

    /* Register appropriately based on existing socket */
    if (fbr->waitSocket == INVALID_SOCKET_FD) {
        /* First registration for this fiber */
        rc = epoll_ctl(scheduler.epollFd, EPOLL_CTL_ADD,
                       (int) sock, &ev);
        if (rc == 0) fbr->waitSocket = sock;
    } else if (fbr->waitSocket == sock) {
        /* Re-arm same socket with (possibly new) events */
        rc = epoll_ctl(scheduler.epollFd, EPOLL_CTL_MOD,
                       (int) sock, &ev);
    } else {
        /* Different socket, unregister old and register new */
        (void) epoll_ctl(scheduler.epollFd, EPOLL_CTL_DEL,
                         (int) fbr->waitSocket, NULL);
        rc = epoll_ctl(scheduler.epollFd, EPOLL_CTL_ADD,
                       (int) sock, &ev);
        if (rc == 0) {
            fbr->waitSocket = sock;
        } else {
            fbr->waitSocket = INVALID_SOCKET_FD;
        }
    }

    if (rc != 0) {
        /* Registration failed, fiber will be re-queued */
        fbr->readyEvents = 0;
        return FALSE;
    }

    return TRUE;
}

/* Lots of other stuff is called but here is the heart of the routine */
void schedule() {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Processor *proc;
    int fromQueue;

schedule:
#ifdef SCHEDULER_DEBUG
    (void) fprintf(stderr, "--> Entering schedule() for thread %llu\n",
                   (long long unsigned) thr->id);
#endif

    /* If we have no processor, need to wait to acquire one */
    if (thr->currProcessor == NULL) parkThread(thr);

    /* Find a runnable fiber from current processor context */
    GMPS_Fiber *fbr = findRunnable(&fromQueue);
    proc = thr->currProcessor;

    /* Reset spinning if needed */
    if (atomic_load(&(thr->spinning))) {
        atomic_store(&(thr->spinning), FALSE);
        int32_t cnt = atomic_fetch_sub(&(scheduler.spinningCount), 1) - 1;

        if (cnt < 0) {
            fprintf(stderr, "resetspinning: negative spinningCount\n");
            abort();
        }

        /* If we were the last spinner, wake a new one */
        if ((cnt == 0) && (atomic_load(&(scheduler.idleProcCount)) > 0)) {
            wakeProc();
        }
    }

    /* Execute the retrieved fiber, with tick if pulled from queue */
    thr->currFiber = fbr;
    fbr->thread = thr;
    atomic_store(&(fbr->status), SFBR_RUNNING);
    if (fromQueue) proc->schedTick++;

    /* Switch into the target fiber's context */
    _gmps_ctx_switch(&(thr->g0->ctx), &(fbr->ctx));

    /* If non-completion, call park function to handle context closure */
    if (thr->parkFn != NULL) {
        GMPS_Fiber *pf = thr->parkFiber;
        if (!thr->parkFn(pf, thr->parkArg)) {
            /* Park failed, re-queue the fiber */
            atomic_store(&(pf->status), SFBR_RUNNABLE);
            runQPut(proc, pf, TRUE);
        }
        thr->parkFn = NULL;
        thr->parkArg = NULL;
        thr->parkFiber = NULL;
    }

    /* When it returns, start the process all over again */
    goto schedule;
}

/* Create and schedule a new OS/scheduler thread instance */
static void *threadStartFn(void *arg) {
    GMPS_Thread *thr = (GMPS_Thread *) arg;

    /* Store the thread-local reference and seed the random number generator */
    _tlsThread = thr;
    fastRandInit(thr->id);

    /* If we have a targProcessor, acquire it */
    if (thr->targProcessor != NULL) {
        acquireProc(thr, thr->targProcessor);
        thr->targProcessor = NULL;
    }

    /* Reset context, first schedule call will save current context as g0 */
    gmps_ctx_reset(&(thr->g0->ctx));
    schedule();

    return NULL;
}

static GMPS_Thread *newThread(GMPS_Processor *proc, int spinning) {
    GMPS_Thread *thr = allocThread();
    if (thr == NULL) return NULL;

    /* Set target before thread creation to avoid startup race */
    thr->targProcessor = proc;
    atomic_store(&(thr->spinning), spinning);

    /* Create the thread */
    if (WXThread_Create(&(thr->osThread), threadStartFn, thr) != WXTRC_OK) {
        freeThread(thr);
        return NULL;
    }

    /* Add to full thread list after successful creation */
    thr->allNextThread = atomic_load(&(scheduler.threadList));
    while (!atomic_compare_exchange_weak(&(scheduler.threadList),
                                         &thr->allNextThread, thr)) {}
    atomic_fetch_add(&(scheduler.threadCount), 1);

    return thr;
}

/* Start a thread to run the provided processor */
static void startThread(GMPS_Processor *proc, int spinning) {
    GMPS_Thread *thr;

    /* Grab processor if not provided */
    WXThread_MutexLock(&(scheduler.lock));
    if (proc == NULL) {
        proc = idleProcGet();
        if (proc == NULL) {
            /* No idle processors, something woke up */
            WXThread_MutexUnlock(&(scheduler.lock));
            if (spinning) {
                atomic_fetch_sub(&(scheduler.spinningCount), 1);
            }
            return;
        }
    }

    /* Check for idle thread, start new one if unavailable */
    thr = scheduler.idleThreadList;
    if (thr == NULL) {
        WXThread_MutexUnlock(&(scheduler.lock));

        /* Create new thread instance with target already set */
        thr = newThread(proc, spinning);
        if (thr == NULL) {
            WXThread_MutexLock(&(scheduler.lock));
            idleProcPut(proc);
            WXThread_MutexUnlock(&(scheduler.lock));
            if (spinning) {
                atomic_fetch_sub(&(scheduler.spinningCount), 1);
            }
            return;
        }

        return;
    }

    /* Not so idle anymore */
    scheduler.idleThreadList = thr->idleNextThread;
    thr->idleNextThread = NULL;
    scheduler.idleThreadCount--;
    WXThread_MutexUnlock(&(scheduler.lock));

    thr->targProcessor = proc;
    atomic_store(&(thr->spinning), spinning);

    /* Wake the idle thread instance */
    WXThread_MutexLock(&(thr->idleLock));
    atomic_store(&(thr->idle), FALSE);
    WXThread_CondSignal(&(thr->idleCond));
    WXThread_MutexUnlock(&(thr->idleLock));
}

/* Park the thread in the idle list and wait on condition */
static void parkThread(GMPS_Thread *thr) {
    WXThread_MutexLock(&(scheduler.lock));
    thr->idleNextThread = scheduler.idleThreadList;
    scheduler.idleThreadList = thr;
    scheduler.idleThreadCount++;
    WXThread_MutexUnlock(&(scheduler.lock));

    WXThread_MutexLock(&(thr->idleLock));
    atomic_store(&(thr->idle), TRUE);
    while (atomic_load(&(thr->idle))) {
#ifdef SCHEDULER_DEBUG
        (void) fprintf(stderr, "--> thr-%lu going to sleep (idle)\n",
                           (long unsigned) thr->id);
#endif
        /* Zzzzzzzzzz */
        WXThread_CondWait(&(thr->idleCond), &(thr->idleLock));
#ifdef SCHEDULER_DEBUG
        (void) fprintf(stderr, "--> thr-%lu woken (idle)\n",
                           (long unsigned) thr->id);
#endif
    }
    WXThread_MutexUnlock(&(thr->idleLock));

    /* Awake!  Grab the leading processor */
    acquireProc(thr, thr->targProcessor);
    thr->targProcessor = NULL;
}

/* Park current fiber, return to g0 to schedule with park callback */
static void yieldFiber(GMPS_Fiber *fbr, GMPS_ParkFn parkFn, void *parkArg) {
    GMPS_Thread *thr = _tlsThread;

    /* Set up park callback on the thread for g0 to execute */
    thr->parkFn = parkFn;
    thr->parkArg = parkArg;
    thr->parkFiber = fbr;

    /* Disassociate fiber from thread */
    thr->currFiber = NULL;
    fbr->thread = NULL;

    /* Save current fiber context and switch to g0 */
    _gmps_ctx_switch(&(fbr->ctx), &(thr->g0->ctx));
}

/* Poll for network changes using native epoll */
static int netpoll(int32_t delay, GMPS_FiberQueue *q) {
    struct epoll_event events[64];
    ssize_t idx;
    int cnt, rc = 0;

    cnt = epoll_wait(scheduler.epollFd, events, 64, delay);
    if (cnt <= 0) return 0;

    fiberQueueInit(q);
    for (idx = 0; idx < cnt; idx++) {
        GMPS_Fiber *fbr = (GMPS_Fiber *) events[idx].data.ptr;
        if (fbr == NULL) continue;

        /* With one-shot poll, this shouldn't fail but check anyways */
        if (atomic_load(&(fbr->status)) != SFBR_WAITING) continue;

        /* Store the events that triggered the wakeup */
        fbr->readyEvents = events[idx].events;
        atomic_store(&(fbr->status), SFBR_RUNNABLE);
        fiberQueuePush(q, fbr);
        rc++;
    }

    return rc;
}

/* Leave these to last, the public methods for managing the scheduler */

/**
 * Initialize the global scheduler instance with the provided processor
 * count.  Returns TRUE if successful, FALSE on error (not anticipated but
 * just in case).  Note that this method is only to be called once during
 * process startup and does not clean up on failures.
 */
int GMPS_SchedulerInit(int procCount) {
    GMPS_Processor *lastProc = NULL, *proc;
    int idx;

    /* TODO - may need to look at these limits/defaults */
    if (procCount <= 0) procCount = 4;
    if (procCount > MAXPROCS) procCount = MAXPROCS;
    (void) memset(&scheduler, 0, sizeof(struct GMPS_Sched));

    /* Initialize the global lock and the id generators */
    if (WXThread_MutexInit(&scheduler.lock, FALSE) != WXTRC_OK) return FALSE;
    atomic_store(&(scheduler.fIdGen), 1);
    atomic_store(&(scheduler.tIdGen), 1);

    /* Build the processor array */
    scheduler.procCount = procCount;
    for (idx = 0; idx < procCount; idx++) {
        proc = scheduler.processors[idx] =
            (GMPS_Processor *) WXCalloc(sizeof(GMPS_Processor));
        if (proc == NULL) return FALSE;
        proc->id = idx + 1;
        atomic_store(&(proc->status), SPRC_IDLE);

        /* Prevent schedule() from immediately targetting global queue */
        proc->schedTick = 1;

        /* Zero'th processor will be allocated to current thread */
        if (idx != 0) {
            if (lastProc == NULL) atomic_store(&(scheduler.idleProcList), proc);
            else lastProc->nextProc = proc;
            lastProc = proc;
            scheduler.idleProcCount++;
        }
    }

    /* Initialize the global queue */
    fiberQueueInit(&(scheduler.runQ));

    /* Initialize the auxiliary locks for associated data management */
    WXThread_MutexInit(&(scheduler.freeFiberLock), FALSE);

    /* Create the primary thread instance (attached to this thread) */
    GMPS_Thread *thr = allocThread();
    if (thr == NULL) return FALSE;
    thr->osThread = WXThread_Self();
    atomic_store(&(scheduler.threadList), thr);
    atomic_store(&(scheduler.threadCount), 1);
    _tlsThread = thr;
    fastRandInit(thr->id);

    /* Attach zeroth processor to the current thread */
    acquireProc(thr, scheduler.processors[0]);

    /* Create the epoll instance for network polling */
    scheduler.epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (scheduler.epollFd < 0) return FALSE;

    return TRUE;
}

/**
 * Start the global scheduler.  This method does not return unless something
 * goes wrong or we implement a shutdown model.
 */
void GMPS_SchedulerStart() {
    GMPS_Thread *thr = _tlsThread;

    /* This mimics the thread start, reset the context to reinit and schedule */
    gmps_ctx_reset(&(thr->g0->ctx));
    schedule();
}

/**
 * Launch a new fiber running the provided function/arg combination.  Returns
 * the underlying fiber object for test purposes only.  In Go this is the 'go'
 * keyword.  Note that this can only be called within either the Init thread
 * or another 'go' routine.
 */
struct GMPS_Fiber *GMPS_Start(GMPS_StartFn startFn, void *arg) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Processor *proc = thr->currProcessor;
    GMPS_Fiber *fbr;

    fbr = getFiber(proc);
    if (fbr == NULL) {
        /* Really not sure what we can do here, except be violent */
        abort();
    }

    fbr->id = atomic_fetch_add(&(scheduler.fIdGen), 1);
    fbr->startFn = startFn;
    fbr->fnArg = arg;
    atomic_store(&(fbr->status), SFBR_RUNNABLE);
    fbr->thread = NULL;

    /* Initialize the fiber's context */
    gmps_ctx_init(&(fbr->ctx), fiberStartFn);

    /* Put on local queue */
    runQPut(proc, fbr, TRUE);

    /* Wake up a processor if needed */
    if ((atomic_load(&(scheduler.idleProcCount)) > 0) &&
            (atomic_load(&(scheduler.spinningCount)) == 0)) {
        wakeProc();
    }

    return fbr;
}

/**
 * Yield the current fiber back to the scheduler.
 */
void GMPS_Yield() {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Fiber *fbr = thr->currFiber;

    /* Do nothing if we are the scheduling fiber */
    if ((fbr == NULL) || (fbr == thr->g0)) return;

    /* Yield with callback to manage status/queue */
    yieldFiber(fbr, yieldParkFn, NULL);
}

/**
 * Yield/park the current fiber until the specified events occur on
 * the given socket.  The actual epoll registration happens in the
 * park callback on g0 after the fiber context is fully saved.
 */
uint32_t GMPS_YieldSocket(WXSocket sock, uint32_t events) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Fiber *fbr = thr->currFiber;

    /* This shouldn't do anything on scheduler fiber */
    if ((fbr == NULL) || (fbr == thr->g0)) return 0;

    /* Prepare the wait state, park callback will register */
    fbr->waitEvents = events;
    fbr->readyEvents = 0;

    /* Yield with socket park callback */
    yieldFiber(fbr, socketParkFn, (void *)(uintptr_t) sock);

    /* When resumed, fiber contains event ready flags */
    return fbr->readyEvents;
}

/**
 * Update the events for the specified socket, if socket is invalid
 * use the socket attached to this fiber.  Note: with one-shot mode this
 * re-arms the socket so events may fire for a running fiber (ignored).
 */
int GMPS_SocketUpdate(WXSocket sock, uint32_t events) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Fiber *fbr = thr->currFiber;
    struct epoll_event ev;

    if ((fbr == NULL) || (fbr == thr->g0)) return FALSE;
    if (sock == INVALID_SOCKET_FD) sock = fbr->waitSocket;
    if (sock == INVALID_SOCKET_FD) return FALSE;

    ev.events = events | EPOLLET | EPOLLONESHOT;
    ev.data.ptr = fbr;

    int rc = epoll_ctl(scheduler.epollFd, EPOLL_CTL_MOD,
                       (int) sock, &ev);

    return (rc == 0) ? TRUE : FALSE;
}

/**
 * Unregister the specified socket from the network poller, if socket
 * is invalid use the socket attached to this fiber.
 */
int GMPS_SocketUnregister(WXSocket sock) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Fiber *fbr = thr->currFiber;

    if ((fbr == NULL) || (fbr == thr->g0)) return FALSE;
    if (sock == INVALID_SOCKET_FD) sock = fbr->waitSocket;
    if (sock == INVALID_SOCKET_FD) return FALSE;

    int rc = epoll_ctl(scheduler.epollFd, EPOLL_CTL_DEL,
                       (int) sock, NULL);

    /* Clear tracking if we unregistered the fiber's socket */
    if ((rc == 0) && (sock == fbr->waitSocket)) {
        fbr->waitSocket = INVALID_SOCKET_FD;
    }

    return (rc == 0) ? TRUE : FALSE;
}

/**
 * Poll for network events from an external thread and schedule any
 * fibers that are ready.  Handles processor/thread wake to run them.
 */
int GMPS_NetPoll(int32_t timeout) {
    GMPS_FiberQueue netList;
    GMPS_Fiber *fbr;
    int cnt;

    cnt = netpoll(timeout, &netList);
    if (cnt <= 0) return 0;

    /* Push all ready fibers onto the global run queue */
    WXThread_MutexLock(&(scheduler.lock));
    while (!fiberQueueIsEmpty(&netList)) {
        fbr = fiberQueuePop(&netList);
        fiberQueuePush(&(scheduler.runQ), fbr);
    }
    WXThread_MutexUnlock(&(scheduler.lock));

    /* Wake a processor, it will cascade to others as needed */
    if ((atomic_load(&(scheduler.idleProcCount)) > 0) &&
            (atomic_load(&(scheduler.spinningCount)) == 0)) {
        wakeProc();
    }

    return cnt;
}

/**
 * Enter syscall state before making a blocking system call.  Detaches the
 * processor from this thread so other threads can use it to run fibers.
 * The fiber remains associated with the thread through the syscall.
 */
void GMPS_EnterSyscall(void) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Processor *proc = thr->currProcessor;
    GMPS_Fiber *fbr = thr->currFiber;

    /* Do nothing if we are the scheduling fiber or not on a processor */
    if ((fbr == NULL) || (fbr == thr->g0)) return;
    if (proc == NULL) return;

    /* Mark fiber as in syscall */
    atomic_store(&(fbr->status), SFBR_SYSCALL);

    /* Detach processor from thread, save for fast-path exit */
    atomic_store(&(proc->thread), NULL);
    thr->currProcessor = NULL;
    thr->syscallProc = proc;

    /* Hand off the processor to another thread if there's work */
    handoff(proc);
}

/**
 * Exit syscall state after a blocking system call returns.  Attempts to
 * reacquire a processor to continue running.
 */
void GMPS_ExitSyscall(void) {
    GMPS_Thread *thr = _tlsThread;
    GMPS_Fiber *fbr = thr->currFiber;
    GMPS_Processor *proc;

    /* Do nothing if not in syscall state */
    if ((fbr == NULL) || (fbr == thr->g0)) return;
    if (atomic_load(&(fbr->status)) != SFBR_SYSCALL) return;

    /* Fast path: try to reacquire the same processor */
    proc = thr->syscallProc;
    thr->syscallProc = NULL;
    if (proc != NULL) {
        GMPS_Thread *expected = NULL;
        if (atomic_compare_exchange_strong(&(proc->thread), &expected, thr)) {
            /* Got it back, continue running */
            thr->currProcessor = proc;
            atomic_store(&(proc->status), SPRC_RUNNING);
            atomic_store(&(fbr->status), SFBR_RUNNING);
            return;
        }
    }

    /* Slow path: try to get any idle processor */
    WXThread_MutexLock(&(scheduler.lock));
    proc = idleProcGet();
    WXThread_MutexUnlock(&(scheduler.lock));
    if (proc != NULL) {
        acquireProc(thr, proc);
        atomic_store(&(fbr->status), SFBR_RUNNING);
        return;
    }

    /* No processor available, queue fiber and park thread */
    atomic_store(&(fbr->status), SFBR_RUNNABLE);
    thr->currFiber = NULL;
    fbr->thread = NULL;
    globRunQPut(fbr);

    /* Wake a processor if possible to run the queued fiber */
    if ((atomic_load(&(scheduler.idleProcCount)) > 0) &&
            (atomic_load(&(scheduler.spinningCount)) == 0)) {
        wakeProc();
    }

    /* Park this thread until work is available */
    parkThread(thr);

    /* When we wake up, we're on g0 - jump into schedule loop */
    schedule();
}
