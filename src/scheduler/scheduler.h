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
 * Yield the current fiber back to the scheduler.
 */
void GMPS_Yield();

/**
 * Yield/park the current fiber until the specified events occur on the
 * given socket.  Uses epoll in edge-triggered one-shot mode: the socket
 * stays registered between calls but is only armed while parked.  Returns
 * the event mask that triggered the wakeup, or 0 on error.
 */
uint32_t GMPS_YieldSocket(WXSocket sock, uint32_t events);

/**
 * Update the events for the specified socket, if socket is invalid use
 * the socket attached to this fiber.  Note: with one-shot mode this
 * re-arms the socket so events may fire for a running fiber (ignored).
 */
int GMPS_SocketUpdate(WXSocket sock, uint32_t events);

/**
 * Unregister the specified socket from the network poller, if socket
 * is invalid use the socket attached to this fiber.
 */
int GMPS_SocketUnregister(WXSocket sock);

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

#endif
