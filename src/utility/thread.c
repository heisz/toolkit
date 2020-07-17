/*
 * Wrapping elements for supporting cross-platform threading functionality.
 *
 * Copyright (C) 2001-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 *
 * Long sordid story around this toolkit element.  At one time, this codebase
 * was modelled on pthreads and included a clean-room implementation of a
 * pthreads-compatibility library for Windows that supported legacy Windows
 * API instances (XP and prior).  Alas, there were a bunch of legal adventures
 * with xkoto and the use of the library in that codebase, which led to a loss
 * of ownership/rights for that implementation.  This is a different model,
 * using a common wrapper element on top of both pthreads and Windows, which
 * also uses the newer Windows API instances (Vista/2008+) for the more complex
 * elements like onces and conditionals.
 */
#include "thread.h"
#include "mem.h"
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <errno.h>

/* Common mapping method for standard threading return codes */
static int mapThreadErrorCode(int rc) {
    /* Conceptually a case is more efficient, except we usually expect ok */
    if (rc == 0) return WXTRC_OK;
    if (rc == ETIMEDOUT) return WXTRC_TIMEOUT;
    if (rc == ENOMEM) return WXTRC_MEM_ERROR;
    if (rc == EBUSY) return WXTRC_BUSY;
    return WXTRC_SYS_ERROR;
}

/* Global elements/intiializer for the Windows threading implementation */
#ifdef _WXWIN_BUILD

enum WXThreadState {
    /* Normal running thread, attached to data instance */
    WXTHREAD_STATE_ATTACHED,

    /* Detached system thread from data instance */
    WXTHREAD_STATE_DETACHED,

    /* Another thread is attempting to join this thread */
    WXTHREAD_STATE_JOINING,

    /* Thread is completed, awaiting join or detach action to finish */
    WXTHREAD_STATE_AWAITING_JOIN
};

static INIT_ONCE threadsInitCtl = INIT_ONCE_STATIC_INIT;

static DWORD threadInstanceKey = 0;

static CRITICAL_SECTION tlsKeyCS;
static WXThread_TlsKey tlsKeyList = NULL;

BOOL CALLBACK threadsInitHandler(PINIT_ONCE initOnce, PVOID param,
                                 PVOID *context) {
    WXThread mainThread;

    /* Allocate and initialize the various elements for the threading support */
    if ((threadInstanceKey = TlsAlloc()) == TLS_OUT_OF_INDEXES) {
        /* This is really bad, given that the library is just init'ing */
        abort();
    }
    InitializeCriticalSection(&tlsKeyCS);

    /* This must be the main thread, initialize the threading data structure */
    mainThread = (WXThread) WXMalloc(sizeof(struct WXThread));
    mainThread->_handle = GetCurrentThread();
    mainThread->_state = WXTHREAD_STATE_ATTACHED;
    TlsSetValue(threadInstanceKey, mainThread);

    return TRUE;
}

/* Simplifying wrapper call to the once initializer */
static void threadsInit() {
    (void) InitOnceExecuteOnce(&threadsInitCtl, threadsInitHandler, NULL, NULL);
}

#endif

/**
 * Utility methd to obtain the UTC epoch time as a timespec instance.  Despite
 * the definition of the timespec structure, this might not have nanosecond
 * resolution, depending on the underlying system/library capabilities.
 *
 * @param tmspec Reference to the timespec value to be initialized.
 */
void WXThread_GetEpochTime(WXThread_TimeSpec *tmspec) {
#ifndef _WXWIN_BUILD
    struct timeval tv;
    (void) gettimeofday(&tv, NULL);
    tmspec->tv_sec = (time_t) tv.tv_sec;
    tmspec->tv_nsec = 1000L * ((long int) tv.tv_usec);
#else
    struct timespec ts;
    (void) timespec_get(&ts, TIME_UTC);
    tmspec->tv_sec = ts.tv_sec;
    tmspec->tv_nsec = ts.tv_nsec;
#endif
}

/**
 * Utility method to sleep for the indicated number of microseconds.  This
 * method will internally capture interrupts and attempt to come as close
 * to the indicated time as possible (and should always meet it).
 *
 * @param us The number of microseconds to sleep.  Note that this does not
 *           have the 1000000 limit of some usleep implementations.
 */
void WXThread_USleep(uint64_t us) {
#ifndef _WXWIN_BUILD
    struct timespec interval;
    int rc;

    interval.tv_sec = us / 1000000L;
    interval.tv_nsec = (us % 1000000L) * 1000L;

    do {
        errno = 0;
        rc = nanosleep(&interval, &interval);
    } while ((rc != 0) && (errno == EINTR));
#else
    /* Per docs, might be less by 1ms but not interruptable */
    Sleep(us / 1000L);
#endif
}

#ifdef _WXWIN_BUILD
/* Common method to release all resources associated to a thread */
static void threadCleanup(WXThread thr) {
    /* Clean up the join coordinate elements (note, nothing for condvar) */
    DeleteCriticalSection(&(thr->_stcs));

    /* Release the threading handle and the data instance */
    CloseHandle(thr->_handle);
    WXFree(thr);
}

/* Wrapper method for the underlying system call */
static DWORD threadStartFn(LPVOID arg) {
    struct WXThread *thr = (struct WXThread *) arg;
    int inJoiningState = FALSE;
    WXThread_TlsKey tlsKey;
    void *tlsVal;

    /* Record the threading data into the associate key slot */
    if (threadInstanceKey == 0) threadsInit();
    (void) TlsSetValue(threadInstanceKey, arg);

    /* Call the associated start function */
    thr->_retval = (*thr->_startFn)(thr->_arg);

    /* Clean up thread-local storage elements */
    EnterCriticalSection(&tlsKeyCS);
    tlsKey = tlsKeyList;
    while (tlsKey != NULL) {
        tlsVal = TlsGetValue(tlsKey->_key);
        if ((tlsVal != NULL) && (tlsKey->_destrFn != NULL)) {
            tlsKey->_destrFn(tlsVal);
        }
        tlsKey = tlsKey->_next;
    }
    LeaveCriticalSection(&tlsKeyCS);

    /* Notify waiting thread instances depending on state */
    EnterCriticalSection(&(thr->_stcs));
    if (thr->_state == WXTHREAD_STATE_DETACHED) {
        /* Already detached, normal thread exit and cleanup */
    } else if (thr->_state == WXTHREAD_STATE_JOINING) {
        /* Someone is waiting for us to join them, signal them */
        WakeConditionVariable(&(thr->_stcv));

        /* Set flag to prevent local cleanup */
        inJoiningState = TRUE;
    } else {
        /* Ok, now we wait for someone to see what happened */
        thr->_state = WXTHREAD_STATE_AWAITING_JOIN;
        (void) SleepConditionVariableCS(&(thr->_stcv),
                                        &(thr->_stcs), INFINITE);
    }
    LeaveCriticalSection(&(thr->_stcs));

    /* If no other thread has joined, we can release thread data now */
    if (!inJoiningState) threadCleanup(thr);

    /* Thread needs explicity exit on top of return */
    ExitThread(0);
    return 0;
}
#endif

/**
 * Create/allocate a thread data instance and start an associated system
 * thread of execution.
 *
 * @param thrd Reference to the thread data instance to allocate into.
 * @param startFn Function instance to be executed in the system thread.
 * @param arg Opaque object reference to pass to the start function.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Create(WXThread *thrd, void *(*startFn)(void *), void *arg) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_create(thrd, NULL, startFn, arg));
#else
    /* Create the thread instance object */
    WXThread thr = (WXThread) WXMalloc(sizeof(struct WXThread));
    if (thr == NULL) return mapThreadErrorCode(errno = ENOMEM);

    thr->_startFn = startFn;
    thr->_arg = arg;
    thr->_state = WXTHREAD_STATE_ATTACHED;
    InitializeCriticalSection(&(thr->_stcs));
    InitializeConditionVariable(&(thr->_stcv));
    thr->_retval = NULL;

    /* Fire thread in suspended state to complete setup */
    thr->_handle = (HANDLE) _beginthreadex(NULL, 0, threadStartFn, thr,
                                           CREATE_SUSPENDED, NULL);
    if (thr->_handle == NULL) {
        WXFree(thr);
        *thrd = NULL;
        return mapThreadErrorCode(errno = ENOMEM);
    }

    /* Initiate thread operation */
    (void) ResumeThread(thr->_handle);
    *thrd = thr;

    return WXTRC_OK;
#endif
}

/**
 * Join to the specified thread data instance, returning only when the
 * associated system thread exits, providing the thread return value.
 *
 * @param thrd Reference to the thread data instance to join.
 * @param thrReturn Reference through which the exit/return value of the
 *                  thread function is returned when it exits.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Join(WXThread thrd, void **thrReturn) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_join(thrd, thrReturn));
#else
    /* Lock and mark for join */
    EnterCriticalSection(&(thrd->_stcs));
    if ((thrd->_state == WXTHREAD_STATE_DETACHED) ||
            (thrd->_state == WXTHREAD_STATE_JOINING)) {
        LeaveCriticalSection(&(thrd->_stcs));
        return mapThreadErrorCode(errno = EINVAL);
    } else if (thrd->_state == WXTHREAD_STATE_AWAITING_JOIN) {
        /* Thread finished before we got here, grab value and let it close */
        if (thrReturn != NULL) *thrReturn = thrd->_retval;
        WakeAllConditionVariable(&(thrd->_stcv));
        LeaveCriticalSection(&(thrd->_stcs));
        return WXTRC_OK;
    }

    /* Remaining outcome is to wait for thread completion */
    thrd->_state = WXTHREAD_STATE_JOINING;
    (void) SleepConditionVariableCS(&(thrd->_stcv),
                                    &(thrd->_stcs), INFINITE);
    if (thrReturn != NULL) *thrReturn = thrd->_retval;
    LeaveCriticalSection(&(thrd->_stcs));

    /* Cleanup is our problem now */
    threadCleanup(thrd);

    return WXTRC_OK;
#endif
}

/**
 * Detach the thread data instance from the underlying system thread instance.
 * Associated thread data is invalid after this call.
 *
 * @param thrd Reference to the thread data instance to detach from.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Detach(WXThread thrd) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_detach(thrd));
#else
    /* Check and detach under concurrency control */
    EnterCriticalSection(&(thrd->_stcs));
    if (thrd->_state != WXTHREAD_STATE_AWAITING_JOIN) {
        /* Thread has already finished, notify and let it clean up */
        WakeAllConditionVariable(&(thrd->_stcv));
    } else if (thrd->_state != WXTHREAD_STATE_ATTACHED) {
        LeaveCriticalSection(&(thrd->_stcs));
        return mapThreadErrorCode(errno = EINVAL);
    }
    thrd->_state = WXTHREAD_STATE_DETACHED;
    LeaveCriticalSection(&(thrd->_stcs));

    return WXTRC_OK;
#endif
}

/**
 * Obtain the thread data instance associated to the current system thread.
 *
 * @return The thread data instance for the current system thread.
 */
WXThread WXThread_Self() {
#ifndef _WXWIN_BUILD
    return pthread_self();
#else
    if (threadInstanceKey == 0) threadsInit();

    /* Work with the associated threading data instance, not the handle */
    return TlsGetValue(threadInstanceKey);
#endif
}

/**
 * Determine whether the two thread data instances are equivalent.
 *
 * @param thra Reference to the thread data instance to be compared.
 * @param thrb Reference to the thread data instance to be compared against.
 * @return TRUE if the threads are equal, FALSE if not.
 */
int WXThread_Equal(WXThread thra, WXThread thrb) {
#ifndef _WXWIN_BUILD
    return pthread_equal(thra, thrb);
#else
    /* Compare the two thread instance references */
    return (thra == thrb) ? TRUE : FALSE;
#endif
}

/**
 * Yield the processor from the current thread.
 *
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Yield() {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(sched_yield());
#else
    /* A sleep of zero time is just a release of current scheduler */
    Sleep(0);
    return WXTRC_OK;
#endif
}

#ifdef _WXWIN_BUILD
/* Windows model is interesting but not useful to model (yet) */
static BOOL CALLBACK InitHandleFunction(PINIT_ONCE initOnce, PVOID param,
                                        PVOID *context) {
   /* Just blindly call the provided once function */
   ((void (*)(void)) param)();
   return TRUE;
}
#endif

/**
 * Perform execution of the provided function through the indicated control
 * exactly once in the process.  onceCtl object must be initialized using the
 * WXTHREAD_ONCE_STATIC_INIT definition.
 *
 * @param onceCtl Controller instance to managed once/singular execution.
 * @param init The initialization function to be called only once.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Once(WXThread_OnceCtl *onceCtl, void (*init)(void)) {
    /* Not sure what can really break here, aside from handle/system error */
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_once(onceCtl, init));
#else
    return InitOnceExecuteOnce(onceCtl, InitHandleFunction,
                               init, NULL) ? WXTRC_OK : WXTRC_SYS_ERROR;
#endif
}

/**
 * Initialize a mutex instance, presuming it had not been initialized
 * statically.
 *
 * @param mutex Reference to the mutex instance to be initialized.
 * @param isRecursive TRUE if this is a recursive/reentrant mutex, FALSE for
 *                    a 'normal' mutex.  FALSE recommended...
 * @param One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexInit(WXThread_Mutex *mutex, int isRecursive) {
    int rc;
#ifndef _WXWIN_BUILD
    pthread_mutexattr_t attr;
    rc = pthread_mutexattr_init(&attr);
    if (rc != 0) return mapThreadErrorCode(rc);
    if (pthread_mutexattr_settype(&attr,
            (isRecursive) ? PTHREAD_MUTEX_RECURSIVE :
                            PTHREAD_MUTEX_NORMAL) != 0) return WXTRC_SYS_ERROR;
    rc = pthread_mutex_init(mutex, &attr);
    (void) pthread_mutexattr_destroy(&attr);
    return mapThreadErrorCode(rc);
#else
    WXThread_Mutex mtx =
          (WXThread_Mutex) WXMalloc(sizeof(struct WXThread_Mutex));
    if (mtx == NULL) return mapThreadErrorCode(errno = ENOMEM);

    mtx->_owner = (WXThread) NULL;
    mtx->_recurse = (isRecursive) ? 0 : -999;
    InitializeCriticalSection(&(mtx->_cs));
    *mutex = mtx;

    return WXTRC_OK;
#endif
}


/**
 * Counterpart to the init function, release the resources associated to the
 * provided mutex instance.  Note that this does *not* release the underlying
 * mutex structure instance.
 *
 * @param mutex Reference to the mutex instance to be released.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexDestroy(WXThread_Mutex *mutex) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_mutex_destroy(mutex));
#else
    if (*mutex == WXTHREAD_MUTEX_STATIC_INIT) return WXTRC_OK;

    DeleteCriticalSection(&((*mutex)->_cs));
    WXFree(*mutex);
    *mutex = NULL;

    return WXTRC_OK;
#endif
}


#ifdef _WXWIN_BUILD
/* Common internal method to intialize a static mutex safely */
static int initStaticMutex(WXThread_Mutex *mutex) {
    static WXThread_Mutex init = WXTHREAD_MUTEX_STATIC_INIT;
    WXThread_Mutex mtx;
    int rc;

    rc = WXThread_MutexInit(&mtx, FALSE);
    if (rc != WXTRC_OK) return rc;

    if (InterlockedCompareExchangePointer(mutex, mtx, init) != init) {
        /* Someone has gotten there before us, clean up */
        (void) WXThread_MutexDestroy(&mtx);
    }

    return WXTRC_OK;
}

/* Lock completion method for both standard and try lock cases */
static int completeMutexLock(WXThread_Mutex mutex, int isTry) {
    WXThread me = WXThread_Self();

    /* Capture re-entrant conditions */
    if (mutex->_owner == me) {
        if ((mutex->_recurse)++ > 0) {
            /* Recursive entry condition */
            LeaveCriticalSection(&(mutex->_cs));
        } else {
            /* Thread is deadlocked, unless we are just trying it out */
            if (isTry) return WXTRC_BUSY;
            while (1) Sleep(1000);
        }

        return WXTRC_OK;
    }

    mutex->_owner = me;
    (mutex->_recurse)++;

    return WXTRC_OK;
}

#endif

/**
 * Lock the provided mutex instance.  This method returns when either the lock
 * is granted or an error occurs, depending on the error code.  Note that
 * recursive locks can be locked by the owning thread instance multiple times.
 *
 * @param mutex Reference to the mutex instance to be locked.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexLock(WXThread_Mutex *mutex) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_mutex_lock(mutex));
#else
    int rc;

    /* Capture static initialization condition */
    if (threadInstanceKey == 0) threadsInit();
    if (*mutex == WXTHREAD_MUTEX_STATIC_INIT) {
        rc = initStaticMutex(mutex);
        if (rc != 0) return rc;
    }

    /* Grab control of the section, which is re-entrant in Windows */
    EnterCriticalSection(&((*mutex)->_cs));

    /* Finish defining the lock state */
    return completeMutexLock(*mutex, FALSE);
#endif
}

/**
 * Attempt to lock the provided mutex instance.  Acts similar to the Lock()
 * method, except if the lock is being held by another thread, the method
 * returns immediately with WXTRC_BUSY.
 *
 * @param mutex Reference to the mutex instance to be locked.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexTryLock(WXThread_Mutex *mutex) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_mutex_trylock(mutex));
#else
    int rc;

    /* Capture static initialization condition */
    if (threadInstanceKey == 0) threadsInit();
    if (*mutex == WXTHREAD_MUTEX_STATIC_INIT) {
        rc = initStaticMutex(mutex);
        if (rc != 0) return rc;
    }

    /* Grab control of the section, which is re-entrant in Windows */
    if (TryEnterCriticalSection(&((*mutex)->_cs)) == 0) {
        return WXTRC_BUSY;
    }

    /* Finish defining the lock state, with self-avoidance */
    return completeMutexLock(*mutex, TRUE);
#endif
}

/**
 * Unlock a previously locked mutex instance.  Note that, for a recursive
 * mutex, the convention is that an equal number of Lock/Unlock() operations
 * must be taken for another thread to be able to lock the mutex (available).
 *
 * @param mutex Reference to the mutex instance to be unlocked.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexUnlock(WXThread_Mutex *mutex) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_mutex_unlock(mutex));
#else
    /* Just presume error check, for simplicity */
    if (*mutex == WXTHREAD_MUTEX_STATIC_INIT) return WXTRC_SYS_ERROR;
    if ((*mutex)->_owner != WXThread_Self()) return WXTRC_SYS_ERROR;

    /* Quick return from inner recursion */
    if (--((*mutex)->_recurse) > 0) return WXTRC_OK;

    /* Fully unlocked, release and cleanup */
    (*mutex)->_owner = (WXThread) NULL;
    LeaveCriticalSection(&((*mutex)->_cs));

    return WXTRC_OK;
#endif
}

/**
 * Initialize a condition variable, presuming it has not been initialized
 * statically.  Note that all condition variables are process private.
 *
 * @param cond Reference to condition variable to be initialized.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondInit(WXThread_Cond *cond) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_cond_init(cond, NULL));
#else
    /* Sooooo tidy once it's supported in the native Windows API */
    InitializeConditionVariable(cond);
    return WXTRC_OK;
#endif
}

/**
 * Counterpart to the init function, release the resources associated to the
 * provided condition variable.  Note that this does *not* release the
 * underlying condition structure instance.
 *
 * @param cond Reference to the condition variable to be released.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondDestroy(WXThread_Cond *cond) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_cond_destroy(cond));
#else
    /* No corresponding destructor for Windows */
    return WXTRC_OK;
#endif
}

/**
 * Issue a signal to one (arbitrary) of the threads waiting on the condition.
 *
 * @param cond Reference to the condition variable to be signalled.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondSignal(WXThread_Cond *cond) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_cond_signal(cond));
#else
    WakeConditionVariable(cond);
    return WXTRC_OK;
#endif
}

/**
 * Issue a signal to all of the threads waiting on the condition.
 *
 * @param cond Reference to the condition variable to be signalled.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondBroadcast(WXThread_Cond *cond) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_cond_broadcast(cond));
#else
    WakeAllConditionVariable(cond);
    return WXTRC_OK;
#endif
}

/**
 * Wait for a signal on the provided condition variable.  Will return either
 * when signalled by another thread or on error.
 *
 * @param cond Reference to the condition variable to wait against.
 * @param mutex Reference to the associated mutex being held to manage the
 *              wait conditions.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondWait(WXThread_Cond *cond, WXThread_Mutex *mutex) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_cond_wait(cond, mutex));
#else
    WXThread_Mutex mtx = *mutex;
    int32_t recurse;

    /* Just presume error check, for simplicity */
    if (mtx == WXTHREAD_MUTEX_STATIC_INIT) return WXTRC_SYS_ERROR;
    if (mtx->_owner != WXThread_Self()) return WXTRC_SYS_ERROR;

    /* Reset lock state (but not critical section) for other thread */
    recurse = mtx->_recurse;
    mtx->_owner = NULL;
    mtx->_recurse = 0;

    /* In theory, check mutex lock, but nah... */
    if (SleepConditionVariableCS(cond, &(mtx->_cs), INFINITE) == 0) {
        return WXTRC_SYS_ERROR;
    }

    /* We've awakened, restore mutex state */
    mtx->_owner = WXThread_Self();
    mtx->_recurse = recurse;

    return WXTRC_OK;
#endif
}

/**
 * Wait for a signal on the provided condition variable for a specific time.
 * Will return when signalled by another thread, the absolute time is reached
 * or an error occurs.
 *
 * @param cond Reference to the condition variable to wait against.
 * @param mutex Reference to the associated mutex being held to manage the
 *              wait conditions.
 * @param abstime Absolute (epoch) time at which the wait condition should
 *                time out.
 * @return One of the WXTRC_* result codes, depending on outcome (note that
 *         WXTRC_TIMEOUT will be returned if the absolute time passes while in
 *         the wait condtion).
 */
int WXThread_CondTimedWait(WXThread_Cond *cond, WXThread_Mutex *mutex,
                           WXThread_TimeSpec *abstime) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_cond_timedwait(cond, mutex, abstime));
#else
    WXThread_TimeSpec now;
    WXThread_Mutex mtx = *mutex;
    int rc = WXTRC_OK;
    int32_t recurse;
    DWORD waitMs;

    /* Just presume error check, for simplicity */
    if (mtx == WXTHREAD_MUTEX_STATIC_INIT) return WXTRC_SYS_ERROR;
    if (mtx->_owner != WXThread_Self()) return WXTRC_SYS_ERROR;

    /* Reset lock state (but not critical section) for other thread */
    recurse = mtx->_recurse;
    mtx->_owner = NULL;
    mtx->_recurse = 0;

    /* Compute time difference, in this case it's not an infinite wait */
    WXThread_GetEpochTime(&now);
    waitMs = (abstime->tv_sec - now.tv_sec) * 1000 +
             (abstime->tv_nsec - now.tv_nsec) / 1000000L;
    if (SleepConditionVariableCS(cond, &(mtx->_cs), waitMs) == 0) {
        rc = (GetLastError() == ERROR_TIMEOUT) ? WXTRC_TIMEOUT :
                                                 WXTRC_SYS_ERROR;
    }

    /* We've awakened, restore mutex state */
    mtx->_owner = WXThread_Self();
    mtx->_recurse = recurse;

    return rc;
#endif
}

/**
 * Allocate a key for a thread-local storage slot.  Note that there is no
 * key destructor at present, generally used in a business model capacity.
 *
 * @param key Reference through which the allocated key is returned.
 * @param destrFn If non-NULL, a function to release the resource bound to the
 *                key on thread exit.  This is executed in the key allocation
 *                lock, take appropriate precautions for deadlock.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_TlsCreate(WXThread_TlsKey *key, void (*destrFn)(void *)) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_key_create(key, destrFn));
#else
    WXThread_TlsKey lastKey, tlsKey =
                (WXThread_TlsKey) WXMalloc(sizeof(struct WXThread_TlsKey));
    if (tlsKey == NULL) {
        *key = NULL;
        return mapThreadErrorCode(errno = ENOMEM);
    }

    if ((tlsKey->_key = TlsAlloc()) == TLS_OUT_OF_INDEXES) {
        *key = NULL;
        WXFree(tlsKey);
        return mapThreadErrorCode(errno = ENOMEM);
    }
    tlsKey->_destrFn = destrFn;
    tlsKey->_next = NULL;

    /* Append to chain for thread cleanup handling */
    if (threadInstanceKey == 0) threadsInit();
    EnterCriticalSection(&tlsKeyCS);
    lastKey = tlsKeyList;
    while ((lastKey != NULL) && (lastKey->_next != NULL)) {
        lastKey = lastKey->_next;
    }
    if (lastKey == NULL) tlsKeyList = tlsKey;
    else lastKey->_next = tlsKey;
    LeaveCriticalSection(&tlsKeyCS);

    *key = tlsKey;

    return WXTRC_OK;
#endif
}

/**
 * Set a value into the thread-local storage associated to the key.
 *
 * @param key The allocated key to defined the value for.
 * @param value The value to store into the associate thread-local storage slot.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_TlsSet(WXThread_TlsKey key, void *value) {
#ifndef _WXWIN_BUILD
    return mapThreadErrorCode(pthread_setspecific(key, value));
#else
    return (TlsSetValue(key->_key, value) == 0) ? WXTRC_OK : WXTRC_SYS_ERROR;
#endif
}

/**
 * Retrieve a value from the thread-local storage associated to the key.
 *
 * @param key The allocated key to retrieve the value for.
 * @return The associated value to the key or NULL if the key has not been
 *         set or is invalid.
 */
void *WXThread_TlsGet(WXThread_TlsKey key) {
#ifndef _WXWIN_BUILD
    return pthread_getspecific(key);
#else
    return TlsGetValue(key->_key);
#endif
}
