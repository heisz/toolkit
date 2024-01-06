/*
 * Test methods for the platform-independent thread wrapping functions.
 *
 * Copyright (C) 2003-2024 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "thread.h"
#include "threadpool.h"
#include "log.h"

/* Two elements to handle thread testing and static initializers */
static WXThread_Mutex globalLock = WXTHREAD_MUTEX_STATIC_INIT;
static WXThread_Cond globalCond = WXTHREAD_COND_STATIC_INIT;

/* Once upon a time... */
static WXThread_OnceCtl onceCtl = WXTHREAD_ONCE_STATIC_INIT;
static int onceCounter = 0;
static void onceInitFn() {
    if (++onceCounter != 1) {
        (void) fprintf(stderr, "Multiple calls to the once() function\n");
        exit(1);
    }
}

#define INCREMENT 123456

/* High conflict thread routine with mutex safety */
static int conflictCounter = 0;
static void *conflictThreadHandler(void *arg) {
    int idx, loop, tmp;

    (void) fprintf(stdout, "Increment thread started\n");
    for (idx = 0; idx < INCREMENT; idx++) {
        if (WXThread_MutexLock(&globalLock) != WXTRC_OK) {
            (void) fprintf(stderr, "Failed to obtain global lock\n");
            exit(1);
        }
        tmp = conflictCounter;
        for (loop = 0; loop < 5; loop++) {
            if (loop == 2) {
                if (WXThread_Yield() != WXTRC_OK) {
                    (void) fprintf(stderr, "Failed to yield the thread\n");
                    exit(1);
                }
                loop++;
                tmp++;
            }
        }
        conflictCounter = tmp;
        if (WXThread_MutexUnlock(&globalLock) != WXTRC_OK) {
            (void) fprintf(stderr, "Failed to release global lock\n");
            exit(1);
        }
    }

    (void) fprintf(stdout, "Conflict thread completed\n");
    *((int *) arg) = 0;
    if (WXThread_CondSignal(&globalCond) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to issue global signal\n");
        exit(1);
    }

    return NULL;
}

/* Combine TLS into broadcast handler */
static WXThread_TlsKey tlsKey;

/* Handle a sequence of conditional/broadcast actions */
static int broadcastCounter = 0, broadcastTotal = 0;
static void *broadcastThreadHandler(void *arg) {
    int varg = (int) (intptr_t) arg;

    /* Store the value into the key */
    WXThread_TlsSet(tlsKey, arg);

    (void) fprintf(stdout, "Broadcast thread %d started\n", varg);
    if (WXThread_MutexLock(&globalLock) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to obtain global broadcast lock\n");
        exit(1);
    }

    broadcastCounter += varg;
    (void) fprintf(stdout, "Broadcast thread %d entering wait\n", varg);
    if (WXThread_CondWait(&globalCond, &globalLock) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to wait on global condition\n");
        exit(1);
    }
    broadcastCounter -= varg;
    broadcastTotal += varg;

    if (WXThread_MutexUnlock(&globalLock) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to release global broadcast lock\n");
        exit(1);
    }
    (void) fprintf(stdout, "Broadcast thread %d finished\n", varg);

    /* Check the local storage value */
    if (WXThread_TlsGet(tlsKey) != arg) {
        (void) fprintf(stderr, "Incorrect response for TLS key value\n");
        exit(1);
    }

    return arg;
}

static void *wakeupThreadHandler(void *arg) {
    (void) fprintf(stdout, "Wakeup thread started, sleeping 1 second\n");
    WXThread_USleep(1000000);

    (void) fprintf(stdout, "Issuing wakeup broadcast\n");
    if (WXThread_CondBroadcast(&globalCond) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to issue broadcast signal\n");
        exit(1);
    }

    return NULL;
}

/* Just track destructor call instances (should only happen from broadcast) */
static int destructs[5] = { FALSE, FALSE, FALSE, FALSE, FALSE };
static void keyDestructor(void *arg) {
    int slot = ((int) (intptr_t) arg) - 1;

    if ((slot < 0) || (slot > 4)) {
        (void) fprintf(stderr, "Invalid/unexpected call for destructor\n");
        exit(1);
    }
    if (destructs[slot]) {
        (void) fprintf(stderr, "Multiple destructs for common slot %d\n", slot);
        exit(1);
    }

    destructs[slot] = TRUE;
}

static int lastTmExit = 0;

/* Worker thread waits for the arg-indicated amount of time */
static void *worker(void *arg) {
    int tm = (int) (intptr_t) arg;

    WXLog_Info("Worker (%d) starting", tm);
    WXThread_USleep(tm * 1000000);
    WXLog_Info("Worker (%d) finished", tm);
    lastTmExit = tm;

    return NULL;
}

/* Just so I can embed in the if() */
static int blip() {
    WXThread_USleep(100000);
    return 0;
}

/**
 * Main testing entry point.
 */
int main(int argc, char **argv) {
    int idx, thrstata, thrstatb, thrstatc;
    WXThread tid, tida, tidb, tidc, btids[5];
    WXThread_TimeSpec start, end;
    WXThread_Mutex mutex;
    WXThread_Cond cond;
    WXThreadPool pool;
    int64_t net;
    void *ret;

    /* Generate a TLS key up front */
    if (WXThread_TlsCreate(&tlsKey, keyDestructor) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create TLS key instance\n");
        exit(1);
    }

    /* Play with some timing methods */
    WXThread_GetEpochTime(&start);
    (void) fprintf(stdout, "Snoozing for 2.5 seconds\n");
    WXThread_USleep(2500000L);
    WXThread_GetEpochTime(&end);
    net = (end.tv_sec - start.tv_sec) * 1000000000L +
                                   end.tv_nsec - start.tv_nsec;
    (void) fprintf(stdout, "Actual: %d:%09lld\n\n", (int) (net / 1000000000L),
                   (long long int) (net % 1000000000L));
    /* Should at least resolve to 10ms intervals */
    if ((net < 2500000000L) || ((net / 10000000L) != 250)) {
        (void) fprintf(stderr, "Wait interval out of bounds...\n");
        exit(1);
    }

    /* Am I talking to myself?  Yes, I am.... */
    if (!WXThread_Equal(WXThread_Self(), WXThread_Self())) {
        (void) fprintf(stderr, "Hmmmm, I'm not equivalent with myself...\n");
        exit(1);
    }

    /* ???? upon a time, they lived happily ever after... */
    if (WXThread_Once(&onceCtl, onceInitFn) != WXTRC_OK) {
        (void) fprintf(stderr, "Error on first once call\n");
        exit(1);
    }
    if (WXThread_Once(&onceCtl, onceInitFn) != WXTRC_OK) {
        (void) fprintf(stderr, "Error on second once call\n");
        exit(1);
    }
    if (WXThread_Once(&onceCtl, onceInitFn) != WXTRC_OK) {
        (void) fprintf(stderr, "Error on third once call\n");
        exit(1);
    }
    if (onceCounter != 1) {
        (void) fprintf(stderr, "Once called did not happen just once\n");
        exit(1);
    }

    /* Play with some mutex as well */
    if (WXThread_MutexInit(&mutex, FALSE) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to initialize the mutex\n");
        exit(1);
    }
    if (WXThread_MutexLock(&mutex) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to lock the mutex\n");
        exit(1);
    }
    if (WXThread_MutexTryLock(&mutex) != WXTRC_BUSY) {
        (void) fprintf(stderr, "Not busy for non-recursive try\n");
        exit(1);
    }
    if (WXThread_MutexUnlock(&mutex) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to unlock the mutex\n");
        exit(1);
    }
    if (WXThread_MutexDestroy(&mutex) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to destroy the mutex\n");
        exit(1);
    }

    /* Increment test using a bunch of threads */
    thrstata = thrstatb = thrstatc = 1;
    if (WXThread_MutexLock(&globalLock) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to lock the global mutex\n");
        exit(1);
    }

    /* Spawn three threads and wait for condition exit */
    if (WXThread_Create(&tida, conflictThreadHandler, &thrstata) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to start thread A\n");
        exit(1);
    }
    if (WXThread_Detach(tida) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to detach thread A\n");
        exit(1);
    }
    if (WXThread_Create(&tidb, conflictThreadHandler, &thrstatb) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to start thread B\n");
        exit(1);
    }
    if (WXThread_Detach(tidb) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to detach thread B\n");
        exit(1);
    }
    if (WXThread_Create(&tidc, conflictThreadHandler, &thrstatc) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to start thread C\n");
        exit(1);
    }
    if (WXThread_Detach(tidc) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to detach thread C\n");
        exit(1);
    }

    while ((thrstata == 1) || (thrstatb == 1) || (thrstatc == 1)) {
        if (WXThread_CondWait(&globalCond, &globalLock) != WXTRC_OK) {
            (void) fprintf(stderr, "Error on global condition wait\n");
            exit(1);
        }
    }
    if (WXThread_MutexUnlock(&globalLock) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to unlock the global lock\n");
        exit(1);
    }

    if (conflictCounter != (3 * INCREMENT)) {
        (void) fprintf(stderr, "Incorrect result for conflict counting\n");
        exit(1);
    }
    (void) fprintf(stdout, "Thread/mutex threads completed\n\n");

    /* Test broadcasting between threads */
    if (WXThread_Create(&btids[0], broadcastThreadHandler,
                        (void *) 1) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create broadcast thread 1\n");
        exit(1);
    }
    if (WXThread_Create(&btids[1], broadcastThreadHandler,
                        (void *) 2) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create broadcast thread 2\n");
        exit(1);
    }
    if (WXThread_Create(&btids[2], broadcastThreadHandler,
                        (void *) 3) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create broadcast thread 3\n");
        exit(1);
    }
    if (WXThread_Create(&btids[3], broadcastThreadHandler,
                        (void *) 4) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create broadcast thread 4\n");
        exit(1);
    }
    if (WXThread_Create(&btids[4], broadcastThreadHandler,
                        (void *) 5) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create broadcast thread 5\n");
        exit(1);
    }

    WXThread_USleep(2000000);

    /* Wake two, call me in the morning... */
    if (WXThread_CondSignal(&globalCond) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to issue single signal\n");
        exit(1);
    }
    if (WXThread_CondSignal(&globalCond) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to issue single signal\n");
        exit(1);
    }

    /* Start the remainder wakeup method */
    if (WXThread_Create(&tid, wakeupThreadHandler, NULL) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to create broadcast wakeup thread\n");
        exit(1);
    }

    /* Join to the first non-woken thread (1 second delay in wakeup) */
    for (int idx = 0; idx < 5; idx++) {
        if (!destructs[idx]) {
            (void) fprintf(stderr, "Joining broadcast thread %d\n", idx + 1);
            if (WXThread_Join(btids[idx], &ret) != WXTRC_OK) {
                (void) fprintf(stderr, "Could not join bcast wait thread\n");
                exit(1);
            }
            (void) fprintf(stderr, "Join returned %p\n", ret);
            if (idx + 1 != (int) (intptr_t) ret) {
                (void) fprintf(stderr, "Incorrect wait return value\n");
                exit(1);
            }
            break;
        }
    }

    /* Condition timeout test */
    if (WXThread_MutexInit(&mutex, FALSE) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to initialize the mutex\n");
        exit(1);
    }
    if (WXThread_MutexLock(&mutex) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to lock the mutex\n");
        exit(1);
    }
    if (WXThread_CondInit(&cond) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to initialize timeout condition\n");
        exit(1);
    }

    (void) fprintf(stdout, "Entering timed wait for 3 seconds\n");
    WXThread_GetEpochTime(&end);
    end.tv_sec += 3;
    if (WXThread_CondTimedWait(&cond, &mutex, &end) != WXTRC_TIMEOUT) {
        (void) fprintf(stderr, "Unexpected return from condwait-timeout\n");
        exit(1);
    }
    (void) fprintf(stdout, "Timeout exit\n");
    if (WXThread_MutexUnlock(&mutex) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to unlock timeout mutex\n");
        exit(1);
    }
    if (WXThread_MutexDestroy(&mutex) != WXTRC_OK){
        (void) fprintf(stderr, "Unable to destroy timeout mutex\n");
        exit(1);
    }
    if (WXThread_CondDestroy(&cond) != WXTRC_OK) {
        (void) fprintf(stderr, "Unable to destroy timeout condition\n");
        exit(1);
    }

    /* Check this here (use the timeout delay to our advantage) */
    if (broadcastCounter != 0) {
        (void) fprintf(stderr, "Incorrect end count result for broadcast\n");
        exit(1);
    }
    if (broadcastTotal != 15) {
        (void) fprintf(stderr, "Incorrect total result for broadcast\n");
        exit(1);
    }

    for (idx = 0; idx < 5; idx++) {
        if (!destructs[idx]) {
            (void) fprintf(stderr, "Failed to destroy key slot %d\n", idx);
            exit(1);
        }
    }

    /* Might as well test thread-pooling in here too */

    WXLog_Info("Starting threadpool tests");

    if (WXThreadPool_Init(&pool, 2, 10, 10) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed to initialize thread pool\n");
        exit(1);
    }

    /* Let the threads catch up */
    WXThread_USleep(500000L);

    WXLog_Info("Queueing 4 jobs for processing");

    /* First test, mixture of jobs to validate the queue-linkages */
    if ((WXThreadPool_Enqueue(&pool, worker,
                              (void *) (intptr_t) 4) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 2) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 8) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 6) != WXTRC_OK)) {
        (void) fprintf(stderr, "Failed to populate worker queue\n");
        exit(1);
    }

    WXLog_Info("Entering wait all on four");

    /* Wait for all */
    if (WXThreadPool_WaitAll(&pool) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed in first waitall case\n");
        exit(1);
    }

    WXLog_Info("Exited wait all on four");

    /* Pool should still have 4 workers hot and ready to go */
    if (pool.workerCount != 4) {
        (void) fprintf(stderr, "Incorrect worker count on first wait %ld\n",
                               pool.workerCount);
        exit(1);
    }

    /* And nothing to do */
    if (pool.queue != NULL) {
        (void) fprintf(stderr, "Queue left after waitAll()?\n");
        exit(1);
    }

    /* Wait for idle threads to cycle down */
    WXLog_Info("Entering lingering wait");
    WXThread_USleep(15000000L);
    if (pool.workerCount != 2) {
        (void) fprintf(stderr, "Incorrect worker count after linger %ld\n",
                               pool.workerCount);
        exit(1);
    }

    WXLog_Info("Queueing 2 jobs for processing");

    /* Test the specific wait functionality */
    if ((WXThreadPool_Enqueue(&pool, worker,
                              (void *) (intptr_t) 4) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 2) != WXTRC_OK)) {
        (void) fprintf(stderr, "Failed to populate worker wait queue\n");
        exit(1);
    }

    WXLog_Info("Entering wait on specific");

    if (WXThreadPool_Wait(&pool, worker, (void *) (intptr_t) 2) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed in wait case\n");
        exit(1);
    }

    WXLog_Info("Exiting wait on specific");
    (void) blip();

    /* Should be one item in the queue, last item was the 2 */
    if ((pool.queue == NULL) || (lastTmExit != 2)) {
        (void) fprintf(stderr, "Incorrect working state on wait() result\n");
        exit(1);
    }

    /* And one idle worker */
    if (pool.idleCount != 1) {
        (void) fprintf(stderr, "Incorrect idle count on wait() result\n");
        exit(1);
    }

    WXLog_Info("Entering wait all on remainder");

    /* Wait for all */
    if (WXThreadPool_WaitAll(&pool) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed in second waitall case\n");
        exit(1);
    }

    WXLog_Info("Exited wait all on remainder");

    /* Mess with limits */
    pool.maxWorkers = 2;

    WXLog_Info("Queueing 4 jobs for processing (blocking) ");

    /* First test, mixture of jobs to validate the queue-linkages */
    if ((WXThreadPool_Enqueue(&pool, worker,
                              (void *) (intptr_t) 4) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 2) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 8) != WXTRC_OK) ||
            (blip()) ||
            (WXThreadPool_Enqueue(&pool, worker,
                                  (void *) (intptr_t) 6) != WXTRC_OK)) {
        (void) fprintf(stderr, "Failed to populate worker queue\n");
        exit(1);
    }

    WXLog_Info("Terminating pool");

    if (WXThreadPool_Terminate(&pool) != WXTRC_OK) {
        (void) fprintf(stderr, "Failed in pool termination\n");
        exit(1);
    }

    WXLog_Info("Termination complete");

    if (pool.workerCount != 0) {
        (void) fprintf(stderr, "Leftover workers after terminate? %ld\n",
                               pool.workerCount);
        exit(1);
    }

    (void) fprintf(stderr, "All tests passed\n");
}
