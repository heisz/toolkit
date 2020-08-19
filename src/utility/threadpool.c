/*
 * Tooling for managing a 'generic' worker/thread pool system.
 *
 * Copyright (C) 2003-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 *
 * Like the sad thread story, this code has been bantered around for a long
 * time, I can find various forms of it in archived/abandoned personal projects.
 * I think the original code was based on an example from Solaris but that
 * was a long time ago.  And the code has been revised to align with the
 * changes in the underlying threading library (see that file for the other
 * half of the story)...
 */
#include "threadpool.h"
#include "mem.h"

/* Need this definitions now */
struct WXThreadPoolQueueItem {
    /* Calling arguments for processing the work item */
    void *(*execFn)(void *);
    void *arg;

    /* Tracking count for waiters on outcome */
    int hasWaiters;

    /* For slicing efficiency, list points in both directions */
    WXThreadPoolQueueItem *prev, *next;
};

/* Special elements for pool termination */
static void *termFn(void *arg) { abort(); }
static WXThreadPoolQueueItem terminator = { termFn, NULL };

/* Primary thread method for a running worker */
static void *worker(void *arg) {
    WXThreadPool *pool = (WXThreadPool *) arg;
    WXThreadPoolQueueItem *item;
    WXThread_TimeSpec ts;
    int running = TRUE;

    /* Pay close attention to use of locks in loop, held on boundaries... */
    (void) WXThread_MutexLock(&(pool->mutex));

    /* Work forever until something signals otherwise */
    while (running) {
        /* Wait/idle until there is work in the queue */
        pool->idleCount++;
        while (pool->nextQueue == NULL) {
            /* Zzzzzzz.... */
            if (pool->workerCount <= pool->minWorkers) {
                (void) WXThread_CondWait(&(pool->workCond), &(pool->mutex));
            } else {
                WXThread_GetEpochTime(&ts);
                ts.tv_sec += pool->lingerIntvl;
                if ((WXThread_CondTimedWait(&(pool->workCond), &(pool->mutex),
                                            &ts) == WXTRC_TIMEOUT) &&
                        (pool->workerCount > pool->minWorkers)) {
                        running = FALSE;
                    break;
                }
            }
        }
        pool->idleCount--;

        /* Exit if idle timeout occurred or termination signalled */
        if ((!running) || (pool->nextQueue == &terminator)) break;

        /* Extract the next work item */
        item = pool->nextQueue;
        pool->nextQueue = item->next;

        /* Execute outside of lock boundaries */
        (void) WXThread_MutexUnlock(&(pool->mutex));
        (void) (item->execFn)(item->arg);
        (void) WXThread_MutexLock(&(pool->mutex));

        /* Slice item and signal appropriately */
        if (pool->queue == item) pool->queue = item->next;
        if (pool->lastQueue == item) pool->lastQueue = item->prev;
        if (item->next != NULL) item->next->prev = item->prev;
        if (item->prev != NULL) item->prev->next = item->next;
        if (item->hasWaiters) WXThread_CondBroadcast(&(pool->waitCond));
        WXFree(item);
    }

    /* Mark end, signal for termination and clean up */
    pool->workerCount--;
    if (pool->workerCount == 0) {
        WXThread_CondBroadcast(&(pool->waitCond));
    }
    (void) WXThread_MutexUnlock(&(pool->mutex));
}

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
                      size_t lingerIntvl) {
    WXThread thrd;
    int rc;

    /* Initialize the pool structure first */
    pool->queue = pool->nextQueue = pool->lastQueue = NULL;
    pool->minWorkers = minWorkers;
    pool->maxWorkers = maxWorkers;
    pool->lingerIntvl = lingerIntvl;
    pool->workerCount = pool->idleCount = 0;
    if (((rc = WXThread_MutexInit(&pool->mutex, FALSE)) != WXTRC_OK) ||
            ((rc = WXThread_CondInit(&pool->workCond)) != WXTRC_OK) ||
            ((rc = WXThread_CondInit(&pool->waitCond)) != WXTRC_OK)) return rc;

    /* Spawn the workers, all are idle at present */
    while (minWorkers > 0) {
        if (WXThread_Create(&thrd, worker, pool) == WXTRC_OK) {
            pool->workerCount++;
        }
        minWorkers--;
    }

    return WXTRC_OK;
}

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
                         void *arg) {
    WXThreadPoolQueueItem *item;
    WXThread thrd;
    int rc;

    /* Immediate error if someone is enqueueing against a terminating pool */
    if (pool->lastQueue == &terminator) return WXTRC_SYS_ERROR;

    /* Create the queue item for handoff to the workers */
    item = (WXThreadPoolQueueItem *) WXMalloc(sizeof(WXThreadPoolQueueItem));
    if (item == NULL) return WXTRC_MEM_ERROR;
    item->execFn = execFn;
    item->arg = arg;
    item->hasWaiters = FALSE;

    /* Enqueue the item, marking active point and moving end of chain */
    if ((rc = WXThread_MutexLock(&(pool->mutex))) != WXTRC_OK) {
        WXFree(item);
        return rc;
    }
    if (pool->queue == NULL) {
        pool->queue = item;
        item->prev = item->next = NULL;
    } else {
        pool->lastQueue->next = item;
        item->prev = pool->lastQueue;
        item->next = NULL;
    }
    if (pool->nextQueue == NULL) pool->nextQueue = item;
    pool->lastQueue = item;

    /* Wake an idle thread, start a new one or just leave for later */
    /* In both cases, ignore error, item will be picked up anyways */
    if (pool->idleCount != 0) {
        (void) WXThread_CondSignal(&(pool->workCond));
    } else if (pool->workerCount < pool->maxWorkers) {
        if (WXThread_Create(&thrd, worker, pool) == WXTRC_OK) {
            pool->workerCount++;
        }
    }

    /* Not much we can do here if the unlock fails... */
    (void) WXThread_MutexUnlock(&(pool->mutex));

    return WXTRC_OK;
}

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
int WXThreadPool_Wait(WXThreadPool *pool, void *(*execFn)(void *), void *arg) {
    WXThreadPoolQueueItem *item;
    int rc;

    /* Search queue, mark and wait if found (in loop for parallel waiters) */
    if ((rc = WXThread_MutexLock(&(pool->mutex))) != WXTRC_OK) return rc;
    item = pool->queue;
    while (item != NULL) {
        item = pool->queue;
        while (item != NULL) {
            if ((item->execFn == execFn) && (item->arg == arg)) {
                item->hasWaiters = TRUE;
                (void) WXThread_CondWait(&(pool->waitCond), &(pool->mutex));
                break;
            }
            item = item->next;
        }
    }
    (void) WXThread_MutexUnlock(&(pool->mutex));

    return WXTRC_OK;
}

/**
 * Wait for all worker threads in the thread pool to become idle.  Similar to
 * calling WXThread_Join on all worker threads.  This does *not* block
 * enqueue(), so this method might never return if another thread is still
 * queueing work to be done.
 *
 * @param pool The thread pool to wait for all work item from.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_WaitAll(WXThreadPool *pool) {
    WXThreadPoolQueueItem *item;
    int rc;

    /* Similar to above, but mark all and wait until queue is empty */
    if ((rc = WXThread_MutexLock(&(pool->mutex))) != WXTRC_OK) return rc;
    while (pool->queue != NULL) {
        item = pool->queue;
        while (item != NULL) {
            item->hasWaiters = TRUE;
            item = item->next;
        }
        (void) WXThread_CondWait(&(pool->waitCond), &(pool->mutex));
    }
    (void) WXThread_MutexUnlock(&(pool->mutex));

    return WXTRC_OK;
}

/**
 * Signal termination to all workers in the provided thread pool and wait
 * for all active workers to exit.  This method also blocks any further
 * enqueue() calls and cleans up the thread pool instance (destroys all 
 * internally allocated resources but not the structure itself).
 *
 * @param pool The thread pool to be terminated.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThreadPool_Terminate(WXThreadPool *pool) {
    int rc;

    /* Again, same idea but in this case push the terminator onto the queue */
    if ((rc = WXThread_MutexLock(&(pool->mutex))) != WXTRC_OK) return rc;

    if (pool->queue == NULL) {
        pool->queue = &terminator;
    } else {
        pool->lastQueue->next = &terminator;
    }
    if (pool->nextQueue == NULL) pool->nextQueue = &terminator;
    pool->lastQueue = &terminator;

    /* Wake all workers, if only to terminate */
    (void) WXThread_CondBroadcast(&(pool->workCond));

    /* Wait until the queue has drained */
    while (pool->workerCount != 0) {
        (void) WXThread_CondWait(&(pool->waitCond), &(pool->mutex));
    }
    if (pool->queue != &terminator) abort();
    (void) WXThread_MutexUnlock(&(pool->mutex));

    /* Clean up the threading elements */
    (void) WXThread_MutexDestroy(&(pool->mutex));
    (void) WXThread_CondDestroy(&(pool->workCond));
    (void) WXThread_CondDestroy(&(pool->waitCond));

    return WXTRC_OK;
}
