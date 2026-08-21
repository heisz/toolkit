/**
 * Exposed interfaces for asynchronous M:N scheduler interactions.
 *
 * Copyright (C) 2024-2026 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */

#ifndef GMP_SCHEDULER_H
#define GMP_SCHEDULER_H 1

#include "socket.h"

/* Start/entry function type for fibers (taking user object) */
typedef void (*GMPS_StartFn)(void *arg);

/* Socket event types for wait operations (match Linux epoll values) */
#define GMPS_EVT_IN  0x001
#define GMPS_EVT_OUT 0x004
#define GMPS_EVT_ERR 0x008
#define GMPS_EVT_HUP 0x010

/**
 * Initialize the global scheduler instance with the provided processor
 * count.  Returns TRUE if successful, FALSE on error (not anticipated but
 * just in case).  Note that this method is only to be called once during
 * process startup and does not clean up on failures.
 */
int GMPS_SchedulerInit(int procCount);

/**
 * Start the global scheduler.  This method does not return unless something
 * goes wrong or we implement a shutdown model.
 */
void GMPS_SchedulerStart();

/**
 * Launch a new fiber running the provided function/arg combination.  Returns
 * the underlying fiber object for test purposes only.  In Go this is the 'go'
 * keyword.  Note that this can only be called within either the Init thread
 * or another 'go' routine.
 */
struct GMPS_Fiber *GMPS_Start(GMPS_StartFn startFn, void *arg);

/**
 * Determine whether the caller is currently running on a fiber.
 *
 * @return TRUE if an active fiber, FALSE if a thread or scheduling fiber.
 */
int GMPS_OnFiber();

/**
 * Yield the current fiber back to the scheduler.
 */
void GMPS_Yield();

/**
 * Yield/park the current fiber until one of the specified events occurs on
 * the given socket.  Uses poll descriptor to track until explicitly detached
 * via closure.  Returns event mask of triggered wakeup, EVT_ERR or zero for
 * any invalid registration conditions (e.g. non-fiber, bad socket, etc.)
 *
 * NOTE: callers are responsible to ensure socket association rules are 
 *       followed.  Namely a) only one fiber of read or write is permitted at
 *       one time (can be different) and b) a combined read/write registration
 *       must be the only fiber registered.
 */
uint32_t GMPS_YieldSocket(WXSocket sock, uint32_t events);

/**
 * Explicitly register a socket with the network poller, for giggles.
 */
int GMPS_SocketRegister(WXSocket sock);

/**
 * Unregister the specified socket from the network poller.  Any waiters will
 * receive an error condition.  This MUST be called prior to socket close to
 * clean up the polldesc relationship.
 */
int GMPS_SocketUnregister(WXSocket sock);

/**
 * Two utility methods for supporting external sync/async models based on
 * network wait states, based on the networking definitions and state models
 * but integrated with the scheduler systems.
 */

/**
 * Await network socket conditions based on the WXNRC_READ/WRITE_REQUIRED
 * flags.  Handles synchronous (non-fiber) and asynchronous (fiber) context.
 * Refer to the YieldSocket method above for notes on fiber/socket rules.
 *
 * @param sock The descriptor to await on.
 * @param flags A mixture of WXNRC_READ_REQUIRED and WXNRC_WRITE_REQUIRED.
 * @return The subset of conditions encountered or zero on error.
 */
uint32_t GMPS_SocketWait(WXSocket sock, uint32_t flags);

/**
 * Detach a socket from the scheduler (if applicable) when registered by the
 * previous method.
 *
 * @param sock The descriptor to detach.
 */
void GMPS_SocketRelease(WXSocket sock);

/**
 * Poll for network events and schedule any ready fibers.  Intended to
 * be called from an external (non-scheduler) thread to avoid stalls
 * when all scheduler threads are idle.  The timeout is in milliseconds
 * (-1 blocks indefinitely, 0 returns immediately).  Returns the number
 * of fibers that were made runnable.
 */
int GMPS_NetPoll(int32_t timeout);

/**
 * Enter syscall state before making a blocking system call.  Detaches the
 * processor from this thread so other threads can use it to run fibers.
 * The fiber remains associated with the thread through the syscall.
 */
void GMPS_EnterSyscall(void);

/**
 * Exit syscall state after a blocking system call returns.  Attempts to
 * reacquire a processor to continue running.  Fast path reuses the same
 * processor if still idle, slow path tries any idle processor.  If no
 * processor is available, the fiber is queued and this thread parks.
 */
void GMPS_ExitSyscall(void);

/* Fiber-local storage key type and invalid marker */
typedef uint32_t GMPS_FlsKey;
#define GMPS_FLS_INVALID_KEY ((GMPS_FlsKey) -1)

/* Destructor callback prototype for cleaning up entries on fiber exit */
typedef void (*GMPS_FlsDestructor)(void *value);

/**
 * Allocate a fiber-local storage key in the global context, for storing
 * values within a fiber instance.  Provide a non-NULL destructor for value
 * cleanup on fiber exit.  Returns GMPS_FLS_INVALID_KEY on failure.
 */
GMPS_FlsKey GMPS_FlsKeyCreate(GMPS_FlsDestructor destrFn);

/**
 * Set a value in the current fiber local storage for the specified key.  Will
 * overwrite existing value without cleanup.
 */
int GMPS_FlsSet(GMPS_FlsKey key, void *value);

/**
 * Get the value from the current fiber local storage for the specified key.
 *
 * @param key The FLS key (from GMPS_FlsKeyCreate).
 * @return The stored value, or NULL if key is invalid or not set.
 */
void *GMPS_FlsGet(GMPS_FlsKey key);

#endif
