/*
 * Platform wrapper to handle network epoll/poll/select event processing.
 *
 * Copyright (C) 2012-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_EVENT_H
#define WX_EVENT_H 1

/* Grab the standard definitions (acknowledge socket-only implementation) */
#include "stdconfig.h"
#include "socket.h"

/*
 * To clarify the top-line comment, this is modelled as a platform-independent
 * implementation of an epoll()-like interface.  At least, as much of it as is
 * able to be consistently supported, in particular this implementation only
 * provides level-triggered notifications.  And only works properly for sockets.
 */

/* There are only three supported event masks, due to LCD ... */

/* Flag for inbound event occurrences (ready for read()) */
#define WXEVENT_IN 0x01

/* Flag for outbound event occurrences (ready for write()) */
#define WXEVENT_OUT 0x02

/* Flag for error event occurrences (e.g. write hangup) */
#define WXEVENT_ERR 0x04

/* Special flag for closure/deletion signalling between handlers */
#define WXEVENT_CLOSE 0x08

/**
 * Data union for attaching arbitrary user data to an event, for use in
 * wait notification.  Not entirely compliant with the epoll form, just the
 * datatypes that are really commonly used by the toolkit extensions.
 */
typedef union WXEvent_UserData {
    void *ptr;
    uint32_t index;
} WXEvent_UserData;

/**
 * Structure for returning event instances to the caller from a wait() call
 * (along with internal tracking of registered event instances).
 */
typedef struct WXEvent {
    /* The underlying socket instance to monitor events on */
    WXSocket socketHandle;

    /* Mask for registering event interest or event occurence for the socket */
    uint32_t events;

    /* Associated data object, passed in registration and returned in wait */
    WXEvent_UserData userData;
} WXEvent;

/* Opaque (to external users) structure for the event registry */
typedef struct WXEvent_Registry WXEvent_Registry;

/**
 * Allocate a central event handler object, used to register/track socket
 * event interests and obtain event occurences.
 *
 * @param size Estimated initial allocation for the tracking arrays.  Will
 *             automatically expand as required.
 * @param registryRef Pointer through which the allocated registry instance
 *                    is returned.
 * @return WXNRC_OK if successful, WXNRC_MEM_ERROR on memory allocation failure
 *         or WXNRC_SYS_ERROR on an underlying system error.
 */
int WXEvent_CreateRegistry(size_t size, WXEvent_Registry **registryRef);

/**
 * Register an interest in events for the given socket instance.
 *
 * @param registry The associated registry for managing events.
 * @param socketHandle The socket handle/file descriptor to register against.
 * @param events Bitset of socket events to register an interest in.
 * @param userData Associated data element to track with the event.
 * @return WXNRC_OK on success, otherwise WXNRC_MEM_ERROR on (re)allocation
 *         failure, WXNRC_DATA_ERROR for duplicate error or WXNRC_SYS_ERROR
 *         on underlying system error.
 */
int WXEvent_RegisterEvent(WXEvent_Registry *registry, uint32_t socketHandle,
                          uint32_t events, WXEvent_UserData userData);

/**
 * Update the event set for a given socket instance.
 *
 * @param registry The associated registry for managing events.
 * @param socketHandle The socket handle/file descriptor to update.
 * @param events Bitset of new socket events to register an interest in.
 * @return WXNRC_OK on success, WXNRC_DATA_ERROR for unrecognized socket and
 *         WXNRC_SYS_ERROR on underlying system error.
 */
int WXEvent_UpdateEvent(WXEvent_Registry *registry, uint32_t socketHandle,
                        uint32_t events);

/**
 * Unregister an interest in events for the given socket instance.
 *
 * @param registry The associated registry for managing events.
 * @param socketHandle The socket handle/file descriptor to remove.
 * @return WXNRC_OK on success, WXNRC_DATA_ERROR for unrecognized socket and
 *         WXNRC_SYS_ERROR on underlying system error.
 */
int WXEvent_UnregisterEvent(WXEvent_Registry *registry, uint32_t socketHandle);

/**
 * Perform a wait operation for available events against the provided registry,
 * with or without timeout.
 *
 * @param registry The associated registry to await events from.
 * @param events Array of externally allocated event indicators to use for
 *               occurrence return.
 * @param maxEvents Number of event entries available in the previous array.
 * @param timeoutRef If NULL, perform a synchronous wait (method will not
 *                   return until events occur or a signal interrupts it).
 *                   If non-NULL, an asynchronous wait will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return The number of events that are available or a suitable WXNRC_* error
 *         code (including WXNRC_TIMEOUT if the wait timeout has expired).
 */
ssize_t WXEvent_Wait(WXEvent_Registry *registry, WXEvent *events,
                     size_t maxEvents, int32_t *timeoutRef);

/**
 * Destroy the provided registry instance, which releases associated system
 * resources and frees all memory allocated for the registry.
 *
 * @param registry The event registry to be destroyed.
 */
void WXEvent_DestroyRegistry(WXEvent_Registry *registry);

#endif
