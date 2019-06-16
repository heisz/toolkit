/*
 * Wrapping elements for supporting cross-platform threading functionality.
 *
 * Copyright (C) 2001-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 *
 * Note: this implementation is not a complete replacement for pthreads (or
 *       equivalent.  It contains the threading elements/capabilities that are
 *       needed to support the toolkit.  Refer to the matching C file for more
 *       details...
 */
#ifndef WX_THREAD_H
#define WX_THREAD_H 1

/* Grab the standard definitions */
#include "stdconfig.h"

/* Wrap standard system error codes to common threading-based errors */
#define WXTRC_TIMEOUT -4
#define WXTRC_BUSY -3
#define WXTRC_MEM_ERROR -2
#define WXTRC_SYS_ERROR -1
#define WXTRC_OK 0

/* Load/define the standard structural elements for the threading methods */
/* Note that these try to align within the subsections */
#ifndef _WXWIN_BUILD

/* For the most part, this is still modelled after a subset of pthreads */
#include <pthread.h>
#include <time.h>

typedef struct timespec WXThread_TimeSpec;

typedef pthread_t WXThread;

typedef pthread_once_t WXThread_OnceCtl;
#define WXTHREAD_ONCE_STATIC_INIT PTHREAD_ONCE_INIT

typedef pthread_mutex_t WXThread_Mutex;
#define WXTHREAD_MUTEX_STATIC_INIT PTHREAD_MUTEX_INITIALIZER

typedef pthread_cond_t WXThread_Cond;
#define WXTHREAD_COND_STATIC_INIT PTHREAD_COND_INITIALIZER

typedef pthread_key_t WXThread_TlsKey;

#else

/* Rock my world, because Windows is slow enough to begin with... */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

/* Need to jump through some hoops (but not as many!) to match Windows */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdint.h>
#include <time.h>

/* TODO - is the underlying timespec structure defined on all systems? */
typedef struct {
    time_t tv_sec;
    long int tv_nsec;
} WXThread_TimeSpec;

/* Once becomes available as of Windows 2003 */
typedef INIT_ONCE WXThread_OnceCtl;
#define WXTHREAD_ONCE_STATIC_INIT INIT_ONCE_STATIC_INIT

/* Still need to wrap thread instances to handle join conditions */
typedef struct WXThread {
    /* The external arguments for the thread start call */
    void *(*_startFn)(void *);
    void *_arg;

    /* Underlying Windows entities for both the thread and the join mgmt */
    HANDLE _handle;
    DWORD _state;

    /* Management elements to handle join state and thread return */
    CRITICAL_SECTION _stcs;
    CONDITION_VARIABLE _stcv;
    void *_retval;
} *WXThread;

/* Use critical section for mutex efficiency, as well as recursion */
typedef struct WXThread_Mutex {
    WXThread _owner;
    int32_t _recurse;
    CRITICAL_SECTION _cs;
} *WXThread_Mutex;

#define WXTHREAD_MUTEX_STATIC_INIT ((WXThread_Mutex) -1)

/* Condition variables also make their appearance in Windows 2003 */
typedef CONDITION_VARIABLE WXThread_Cond;
#define WXTHREAD_COND_STATIC_INIT CONDITION_VARIABLE_INIT

typedef struct WXThread_TlsKey {
    DWORD _key;
    void (*_destrFn)(void *);
    struct WXThread_TlsKey *_next;
} *WXThread_TlsKey;

#endif

/**
 * Utility methd to obtain the UTC epoch time as a timespec instance.  Despite
 * the definition of the timespec structure, this might not have nanosecond
 * resolution, depending on the underlying system/library capabilities.
 *
 * @param tmspec Reference to the timespec value to be initialized.
 */
void WXThread_GetEpochTime(WXThread_TimeSpec *tmspec);

/**
 * Utility method to sleep for the indicated number of microseconds.  This
 * method will internally capture interrupts and attempt to come as close
 * to the indicated time as possible (and should always meet it).
 *
 * @param us The number of microseconds to sleep.  Note that this does not
 *           have the 1000000 limit of some usleep implementations.
 */
void WXThread_USleep(uint64_t us);

/**
 * Create/allocate a thread data instance and start an associated system
 * thread of execution.
 *
 * @param thrd Reference to the thread data instance to allocate into.
 * @param startFn Function instance to be executed in the system thread.
 * @param arg Opaque object reference to pass to the start function.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Create(WXThread *thrd, void *(*startFn)(void *), void *arg);

/**
 * Join to the specified thread data instance, returning only when the
 * associated system thread exits, providing the thread return value.
 *
 * @param thrd Reference to the thread data instance to join.
 * @param thrReturn Reference through which the exit/return value of the
 *                  thread function is returned when it exits.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Join(WXThread thrd, void **thrReturn);

/**
 * Detach the thread data instance from the underlying system thread instance.
 * Associated thread data is invalid after this call.
 *
 * @param thrd Reference to the thread data instance to detach from.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Detach(WXThread thrd);

/**
 * Obtain the thread data instance associated to the current system thread.
 *
 * @return The thread data instance for the current system thread.
 */
WXThread WXThread_Self();

/**
 * Determine whether the two thread data instances are equivalent.
 *
 * @param thra Reference to the thread data instance to be compared.
 * @param thrb Reference to the thread data instance to be compared against.
 * @return TRUE if the threads are equal, FALSE if not.
 */
int WXThread_Equal(WXThread thra, WXThread thrb);

/* TODO - thread kill if required at some point */

/**
 * Yield the processor from the current thread.
 *
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Yield();

/**
 * Perform execution of the provided function through the indicated control
 * exactly once in the process.  onceCtl object must be initialized using the
 * WXTHREAD_ONCE_STATIC_INIT definition.
 *
 * @param onceCtl Controller instance to managed once/singular execution.
 * @param init The initialization function to be called only once.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_Once(WXThread_OnceCtl *onceCtl, void (*init)(void));

/**
 * Initialize a mutex instance, presuming it had not been initialized
 * statically.
 *
 * @param mutex Reference to the mutex instance to be initialized.
 * @param isRecursive TRUE if this is a recursive/reentrant mutex, FALSE for
 *                    a 'normal' mutex.  FALSE recommended...
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexInit(WXThread_Mutex *mutex, int isRecursive);

/**
 * Counterpart to the init function, release the resources associated to the
 * provided mutex instance.  Note that this does *not* release the underlying
 * mutex structure instance.
 *
 * @param mutex Reference to the mutex instance to be released.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexDestroy(WXThread_Mutex *mutex);

/**
 * Lock the provided mutex instance.  This method returns when either the lock
 * is granted or an error occurs, depending on the error code.  Note that
 * recursive locks can be locked by the owning thread instance multiple times.
 *
 * @param mutex Reference to the mutex instance to be locked.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexLock(WXThread_Mutex *mutex);

/**
 * Attempt to lock the provided mutex instance.  Acts similar to the Lock()
 * method, except if the lock is being held by another thread, the method
 * returns immediately with WXTRC_BUSY.
 *
 * @param mutex Reference to the mutex instance to be locked.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexTryLock(WXThread_Mutex *mutex);

/**
 * Unlock a previously locked mutex instance.  Note that, for a recursive
 * mutex, the convention is that an equal number of Lock/Unlock() operations
 * must be taken for another thread to be able to lock the mutex (available).
 *
 * @param mutex Reference to the mutex instance to be unlocked.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_MutexUnlock(WXThread_Mutex *mutex);

/**
 * Initialize a condition variable, presuming it has not been initialized
 * statically.  Note that all condition variables are process private.
 *
 * @param cond Reference to condition variable to be initialized.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondInit(WXThread_Cond *cond);

/**
 * Counterpart to the init function, release the resources associated to the
 * provided condition variable.  Note that this does *not* release the
 * underlying condition structure instance.
 *
 * @param cond Reference to the condition variable to be released.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondDestroy(WXThread_Cond *cond);

/**
 * Issue a signal to one (arbitrary) of the threads waiting on the condition.
 *
 * @param cond Reference to the condition variable to be signalled.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondSignal(WXThread_Cond *cond);

/**
 * Issue a signal to all of the threads waiting on the condition.
 *
 * @param cond Reference to the condition variable to be signalled.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondBroadcast(WXThread_Cond *cond);

/**
 * Wait for a signal on the provided condition variable.  Will return either
 * when signalled by another thread or on error.
 *
 * @param cond Reference to the condition variable to wait against.
 * @param mutex Reference to the associated mutex being held to manage the
 *              wait conditions.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_CondWait(WXThread_Cond *cond, WXThread_Mutex *mutex);

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
                           WXThread_TimeSpec *abstime);

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
int WXThread_TlsCreate(WXThread_TlsKey *key, void (*destrFn)(void *));

/**
 * Set a value into the thread-local storage associated to the key.
 *
 * @param key The allocated key to defined the value for.
 * @param value The value to store into the associate thread-local storage slot.
 * @return One of the WXTRC_* result codes, depending on outcome.
 */
int WXThread_TlsSet(WXThread_TlsKey key, void *value);

/**
 * Retrieve a value from the thread-local storage associated to the key.
 *
 * @param key The allocated key to retrieve the value for.
 * @return The associated value to the key or NULL if the key has not been
 *         set or is invalid.
 */
void *WXThread_TlsGet(WXThread_TlsKey key);

#endif
