/*
 * Tooling for managing a 'generic' worker/thread pool system.
 *
 * Copyright (C) 2003-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 *
 * Note: this implementation is not a complete replacement for pthreads (or
 *       equivalent.  It contains the threading elements/capabilities that are
 *       needed to support the toolkit.  Refer to the matching C file for more
 *       details...
 */
#ifndef WX_THREADPOOL_H
#define WX_THREADPOOL_H 1

/* Well, it's built on the threading wrapper library */
#include "thread.h"

/* The pool is exposed (alloc and diags) but the internals are opaque */
typedef struct WXThreadPoolQueueItem WXThreadPoolQueueItem;

typedef struct WXThreadPool {
    /* Multi-threading elements to support parallel worker access/messaging */
    WXThread_Mutex mutex;
    WXThread_Cond workCond;
    WXThread_Cond waitCond;

    /* Linked list of queued items (with edge tracking) */
    WXThreadPoolQueueItem *queue, *nextQueue, *lastQueue;

    /* Provided configuration parameters for the pool instance */
    size_t minWorkers, maxWorkers, lingerIntvl;

    /* For efficiency, the counts of running workers and external waiters */
    size_t workerCount, idleCount;
} WXThreadPool;

/**
 * Initialize a thread pool instance.  Baseline set of threads will be started
 * on return of this method (if successful).  Pool will automatically grow and
 * shrink according to provided limits.  Note that the pool might return
 * success even if only partially started, on the presumption that something is
 * better than nothing...
 *
 * @param pool Reference to the pool management structure to initialize.
 * @param minWorkers Minimum number of workers/threads to start/run for
 *                   processing queued items.
 * @param maxThreads Maximum limit of workers/threads to start before incoming
 *                   queues start to block.
 * @param lingerIntvl Number of seconds any 'reserve' workers should wait before
 *                    exiting.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_Init(WXThreadPool *pool, size_t minWorkers, size_t maxWorkers,
                      size_t lingerIntvl);

/**
 * Enqueue a work item onto the thread pool, which might run immediately if
 * idle workers are available, start a new worker if not at the maximum limit
 * or just park until a worker becomes available.  Note that the execution
 * function signature matches the standalone thread signature for portability.
 *
 * @param pool The thread pool to enqueue the work item onto.
 * @param execFn Function instance to be executed in the allocated worker.
 * @param arg Opaque object reference to pass to the exec function.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_Enqueue(WXThreadPool *pool, void *(*execFn)(void *),
                         void *arg);

/**
 * Wait for the completion of a specific enqueued item, if it is not already
 * complete.  Similar to WXThread_Join for the specific enqueued item.  Note
 * that this is matched according to the original arguments from the enqueue()
 * method, so there is an implication of uniqueness in those arguments.  
 *
 * @param pool The thread pool to wait for the work item from.
 * @param execFn Original execution function as passed to the enqueue() method.
 * @param arg Original data argument as passed to the enqueue() method.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_Wait(WXThreadPool *pool, void *(*execFn)(void *), void *arg);

/**
 * Wait for all worker threads in the thread pool to become idle.  Similar to
 * calling WXThread_Join on all worker threads.  This does *not* block
 * enqueue(), so this method might never return if another thread is still
 * queueing work to be done.
 *
 * @param pool The thread pool to wait for all work item from.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_WaitAll(WXThreadPool *pool);

/**
 * Signal termination to all workers in the provided thread pool and wait
 * for all active workers to exit.  This method also blocks any further
 * enqueue() calls and cleans up the thread pool instance (destroys all 
 * internally allocated resources but not the structure itself).
 *
 * @param pool The thread pool to be terminated.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_Terminate(WXThreadPool *pool);

#endif
