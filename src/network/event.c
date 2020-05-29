/*
 * Platform wrapper to handle network epoll/poll/select event processing.
 *
 * Copyright (C) 2012-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "event.h"
#include "socket.h"
#include "mem.h"
#include <errno.h>

/*
 * Decision tree up front, to determine exactly what underlying system will
 * be used to implement the eventing interface.  Currently supports epoll, poll
 * and select (last for Windows).  Someday could include kqueue and Solaris
 * support, although the latter is Oracle now...
 */
#if defined(HAVE_SYS_EPOLL_H)
#define WXEVENT_USE_EPOLL
#elif defined(HAVE_POLL_H)
#define WXEVENT_USE_POLL
#else
#define WXEVENT_USE_SELECT
#endif

#ifdef WXEVENT_USE_EPOLL
#include <sys/epoll.h>
#endif
#ifdef WXEVENT_USE_POLL
#include <poll.h>
#endif

/* Handle implementation specific extensions of the event object, as needed */
#define WXEVENT_STRUCT WXEvent
static size_t evtStructSize = sizeof(WXEVENT_STRUCT);

/* Internal definition of the event registry object (system dependent) */
struct WXEvent_Registry {
    /* Common to all, the dynamic buffer of event registrations */
    /* Note that entries are ordered by handle, for lookup performance */
    WXEVENT_STRUCT *entries;
    size_t allocEntryCount, entryCount;

    /* Implementation specific details */
#ifdef WXEVENT_USE_EPOLL
    int epollFd;
#endif

#ifdef WXEVENT_USE_POLL
    struct pollfd *fds;
#endif

    /*
     * Note, just because I'm lazy, the SELECT implementation rebuilds the
     * fd_sets on each wait, so there's no persistent data to maintain
     * in most of the calls...
     */
};

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
int WXEvent_CreateRegistry(size_t size, WXEvent_Registry **registryRef) {
    WXEvent_Registry *reg;

    /* Allocate the core/common registration tracking elements */
    reg = (WXEvent_Registry *) WXMalloc(sizeof(struct WXEvent_Registry));
    if (reg == NULL) return WXNRC_MEM_ERROR;
    if (size < 32) size = 32;
    reg->entries = (WXEVENT_STRUCT *) WXMalloc(size * evtStructSize);
    if (reg->entries == NULL) {
        WXFree(reg);
        return WXNRC_MEM_ERROR;
    }
    reg->allocEntryCount = size;
    reg->entryCount = 0;

#ifdef WXEVENT_USE_EPOLL
    reg->epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (reg->epollFd < 0) {
        WXFree(reg->entries);
        WXFree(reg);
        return WXNRC_SYS_ERROR;
    }
#endif

#ifdef WXEVENT_USE_POLL
    reg->fds = (struct pollfd *) WXMalloc(size * sizeof(struct pollfd));
    if (reg->fds == NULL) {
        WXFree(reg->entries);
        WXFree(reg);
        return WXNRC_MEM_ERROR;
    }
#endif

    /* Note sure why someone would allocate but not track, but just in case */
    if (registryRef != NULL) *registryRef = reg;
    return WXNRC_OK;
}

/* Common method to locate a registry entry by handle, NULL if not found */
static WXEVENT_STRUCT *findEventEntry(WXEvent_Registry *registry,
                               uint32_t socketHandle) {
    /* Binary search algorithm, since the insertion is ordered */
    int lt = 0, rt = registry->entryCount - 1, md;
    WXEVENT_STRUCT *trg;

    while (lt <= rt) {
        /* TODO - can we optimize the differential? */
        md = lt + (rt - lt) / 2;
        trg = registry->entries + md;

        /* Three outcomes... */
        if (trg->socketHandle == socketHandle) {
            return trg;
        } else if (trg->socketHandle < socketHandle) {
            lt = md + 1;
        } else {
            rt = md - 1;
        }
    }

    return NULL;
}

/* Translation routines between the various flagsets */
#ifdef WXEVENT_USE_EPOLL
static uint32_t txlateEvents(uint32_t src) {
    uint32_t retval = 0;
    if ((src & WXEVENT_IN) != 0) retval |= EPOLLIN;
    if ((src & WXEVENT_OUT) != 0) retval |= EPOLLOUT;
    return retval;
}
#endif

#ifdef WXEVENT_USE_POLL
static short txlateEvents(uint32_t src) {
    short retval = 0;
    if ((src & WXEVENT_IN) != 0) retval |= POLLIN;
    if ((src & WXEVENT_OUT) != 0) retval |= POLLOUT;
    return retval;
}
#endif

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
                          uint32_t events, WXEvent_UserData userData) {
    size_t allocCount = registry->allocEntryCount;
    WXEVENT_STRUCT *entries, *entry, src;
#ifdef WXEVENT_USE_EPOLL
    struct epoll_event evt;
#endif
#ifdef WXEVENT_USE_POLL
    struct pollfd *fds;
#endif
    uint32_t idx;

    /* First, reallocate the array for room, if needed */
    if (registry->entryCount >= allocCount) {
        if (allocCount < 1024) allocCount <<= 1;
        else allocCount = allocCount + (allocCount >> 1);

        entries = (WXEVENT_STRUCT *) WXRealloc(registry->entries,
                                               allocCount * evtStructSize);
        if (entries == NULL) return WXNRC_MEM_ERROR;
        registry->entries = entries;

#ifdef WXEVENT_USE_POLL
        fds = (struct pollfd *) WXRealloc(registry->fds,
                                          allocCount * sizeof(struct pollfd));
        if (fds == NULL) return WXNRC_MEM_ERROR;
        registry->fds = fds;
#endif

        registry->allocEntryCount = allocCount;
    }

    /* Handle this up front, to avoid shifting array on registration error */
#ifdef WXEVENT_USE_EPOLL
    (void) memset(&evt, 0, sizeof(evt));
    evt.events = txlateEvents(events);
    evt.data.fd = socketHandle;
    if (epoll_ctl(registry->epollFd, EPOLL_CTL_ADD, (int) socketHandle,
                  &evt) < 0) {
        return WXNRC_SYS_ERROR;
    }
#endif

    /* Determine the target slot, capturing duplicate entries */
    for (idx = 0, entry = registry->entries;
                                idx < registry->entryCount; idx++, entry++) {
        if (entry->socketHandle == socketHandle) {
#ifdef WXEVENT_USE_EPOLL
            /* Should have failed above, but just in case... */
            (void) epoll_ctl(registry->epollFd, EPOLL_CTL_DEL,
                             (int) socketHandle, &evt);
#endif
            return WXNRC_DATA_ERROR;
        }
        if (entry->socketHandle > socketHandle) break;
    }

    /* Build the entry, with system elements */
    src.socketHandle = socketHandle;
    src.events = events;
    src.userData = userData;

    /* Slice in the entry */
    if (idx >= registry->entryCount) {
        idx = registry->entryCount;
    } else {
        (void) memmove(registry->entries + idx + 1, registry->entries + idx,
                       (registry->entryCount - idx) * evtStructSize);
    }

    /* And build the associated data elements */
    registry->entries[idx] = src;

#ifdef WXEVENT_USE_POLL
    fds = &(registry->fds[idx]);
    fds->fd = (int) socketHandle;
    fds->events = txlateEvents(events);
#endif
    registry->entryCount++;

    return WXNRC_OK;
}

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
                        uint32_t events) {
#ifdef WXEVENT_USE_EPOLL
    struct epoll_event evt;
#endif
#ifdef WXEVENT_USE_POLL
    struct pollfd *fds;
#endif
    WXEVENT_STRUCT *entry = findEventEntry(registry, socketHandle);
    if (entry == NULL) return WXNRC_DATA_ERROR;

#ifdef WXEVENT_USE_EPOLL
    (void) memset(&evt, 0, sizeof(evt));
    evt.events = txlateEvents(events);
    evt.data.fd = entry->socketHandle;
    if (epoll_ctl(registry->epollFd, EPOLL_CTL_MOD, (int) entry->socketHandle,
                  &evt) < 0) {
        return WXNRC_SYS_ERROR;
    }
#endif

#ifdef WXEVENT_USE_POLL
    fds = &(registry->fds[entry - registry->entries]);
    fds->events = txlateEvents(events);
#endif

    /* At the most basic level, just set the new event tracking detail */
    entry->events = events;

    return WXNRC_OK;
}

/**
 * Unregister an interest in events for the given socket instance.
 *
 * @param registry The associated registry for managing events.
 * @param socketHandle The socket handle/file descriptor to remove.
 * @return WXNRC_OK on success, WXNRC_DATA_ERROR for unrecognized socket and
 *         WXNRC_SYS_ERROR on underlying system error.
 */
int WXEvent_UnregisterEvent(WXEvent_Registry *registry, uint32_t socketHandle) {
    unsigned int entryIdx;
#ifdef WXEVENT_USE_EPOLL
    struct epoll_event evt;
#endif
    WXEVENT_STRUCT *entry = findEventEntry(registry, socketHandle);
    if (entry == NULL) return WXNRC_DATA_ERROR;

#ifdef WXEVENT_USE_EPOLL
    /* Note, should be NULL but kernel < 2.6.9 has a bug... */
    (void) memset(&evt, 0, sizeof(evt));
    if (epoll_ctl(registry->epollFd, EPOLL_CTL_DEL,
                  (int) entry->socketHandle, &evt) < 0) {
        return WXNRC_SYS_ERROR;
    }
#endif

    /* Slice out the entry */
    if ((entryIdx = entry - registry->entries) < registry->entryCount - 1) {
        (void) memmove(entry, entry + 1,
                       (registry->entryCount - entryIdx - 1) * evtStructSize);
#ifdef WXEVENT_USE_POLL
        (void) memmove(registry->fds + entryIdx, registry->fds + entryIdx + 1,
                       (registry->entryCount - entryIdx - 1) *
                                                   sizeof(struct pollfd));
#endif
    }
    registry->entryCount--;

    return WXNRC_OK;
}

/* Need a sorting method for epoll ordering handling below */
#ifdef WXEVENT_USE_EPOLL
int qsortEpollEvt(const void *a, const void *b) {
    int fda = ((struct epoll_event *) a)->data.fd;
    int fdb = ((struct epoll_event *) b)->data.fd;

    if (fda < fdb) return -1;
    if (fda > fdb) return 1;
    return 0;
}
#endif

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
                     size_t maxEvents, int32_t *timeoutRef) {
    ssize_t retCount = 0;
    int64_t startTime = WXSocket_MilliTime();

#ifdef WXEVENT_USE_EPOLL
    struct epoll_event *epevt;
    WXEvent *pevt, *rpevt, evt;
    uint32_t socketFd, evts, revts;

    /* This optimization relies on WXEvent being bigger than epoll_event! */
    if (timeoutRef == NULL) {
        retCount = epoll_wait(registry->epollFd, (struct epoll_event *) events,
                              maxEvents, -1);
    } else {
        retCount = epoll_wait(registry->epollFd, (struct epoll_event *) events,
                              maxEvents, *timeoutRef);
    }
    if (retCount < 0) return WXNRC_SYS_ERROR;
    if (retCount == 0) return WXNRC_TIMEOUT;

    /* Order the entries by fd and reverse scan to match against src events */
    qsort(events, retCount, sizeof(struct epoll_event), qsortEpollEvt);

    /* And reverse scan the entries, matching to source */
    pevt = registry->entries + registry->entryCount - 1;
    epevt = ((struct epoll_event *) events) + retCount - 1;
    rpevt = events + retCount - 1;
    while (((void *) epevt >= (void *) events) && (rpevt >= events)) {
        socketFd = epevt->data.fd;
        while (pevt->socketHandle > socketFd) pevt--;
        if (pevt->socketHandle < socketFd) {
            /* This should not happen, event for unregistered descriptor */
#ifdef _WXWIN_BUILD
            WSASetLastError(EINVAL);
#else
            errno = EINVAL;
#endif
            return WXNRC_SYS_ERROR;
        }

        /* Matched, carefully update record and move on */
        evt.socketHandle = (uint32_t) socketFd;
        revts = epevt->events;
        evts = 0;
        if ((revts & EPOLLIN) != 0) evts |= WXEVENT_IN;
        if ((revts & EPOLLOUT) != 0) evts |= WXEVENT_OUT;
        if ((revts & EPOLLERR) != 0) evts |= WXEVENT_ERR;
        evt.events = evts;
        evt.userData = pevt->userData;

        *(rpevt--) = evt;
        epevt--; pevt--;
    }
#endif

#ifdef WXEVENT_USE_POLL
    struct pollfd *fds = registry->fds;
	WXEVENT_STRUCT *entry;
    uint32_t revents, evt;
    int32_t rc, idx;

    /* Just call the target poll method with the preallocated fds array */
    if (timeoutRef == NULL) {
        rc = poll(fds, registry->entryCount, -1);
    } else {
        rc = poll(fds, registry->entryCount, *timeoutRef);
    }
    if (rc < 0) return WXNRC_SYS_ERROR;
    if (rc == 0) return WXNRC_TIMEOUT;

    /* Translate responses (TODO - rotating responses for subset?) */
    for (idx = 0, entry = registry->entries;
                          idx < registry->entryCount; idx++, entry++, fds++) {
        revents = fds->revents;
        evt = 0;
        if ((revents & POLLIN) != 0) evt |= WXEVENT_IN;
        if ((revents & POLLOUT) != 0) evt |= WXEVENT_OUT;
        if ((revents & POLLERR) != 0) evt |= WXEVENT_ERR;

        /* If we have a poll result, it turns into the event */
        if (evt != 0) {
            events[retCount] = *entry;
            events[retCount].events = evt;
            if (++retCount == maxEvents) break;
        }
    }
#endif

#ifdef WXEVENT_USE_SELECT
    /* All of the laziness above costs us now... */
    int32_t rc, fd, idx, maxSelectFd, timeout;
    fd_set readSet, writeSet, exceptionSet;
	WXEVENT_STRUCT *entry;
    struct timeval tv;
    uint32_t evt;

    /* Build the select markers, capturing overflow */
    FD_ZERO(&readSet); FD_ZERO(&writeSet); FD_ZERO(&exceptionSet);
    maxSelectFd = -1;
    for (idx = 0, entry = registry->entries;
                          idx < registry->entryCount; idx++, entry++) {
        fd = (int32_t) entry->socketHandle;
        if ((fd < 0) || ((fd = (int32_t) entry->socketHandle) >= FD_SETSIZE)) {
#ifdef _WXWIN_BUILD
            WSASetLastError(EINVAL);
#else
            errno = EINVAL;
#endif
            /* TODO - individual kaboom instead of global kaboom? */
            return WXNRC_SYS_ERROR;
        }
        if (fd > maxSelectFd) maxSelectFd = fd;

        evt = entry->events;
        if ((evt & WXEVENT_IN) != 0) {
            FD_SET(fd, &readSet);
        }
        if ((evt & WXEVENT_OUT) != 0){
            FD_SET(fd, &writeSet);
        }
        FD_SET(fd, &exceptionSet);
    }

    /* Note: a Windows select() will fail with no descriptors, don't do that */

    /* Perform the appropriate select */
    if (timeoutRef == NULL) {
        rc = select(maxSelectFd + 1, &readSet, &writeSet, &exceptionSet, NULL);
    } else {
        timeout = *timeoutRef;
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        rc = select(maxSelectFd + 1, &readSet, &writeSet, &exceptionSet, &tv);
    }
    if (rc < 0) return WXNRC_SYS_ERROR;
    if (rc == 0) return WXNRC_TIMEOUT;

    /* And translate the result */
    for (idx = 0, entry = registry->entries;
                          idx < registry->entryCount; idx++, entry++) {
        fd = entry->socketHandle;
        evt = 0;
        if (FD_ISSET(fd, &readSet)) evt |= WXEVENT_IN;
        if (FD_ISSET(fd, &writeSet)) evt |= WXEVENT_OUT;
        if (FD_ISSET(fd, &exceptionSet)) evt |= WXEVENT_ERR;

        /* If we have a select result, it turns into the event */
        if (evt != 0) {
            events[retCount] = *entry;
            events[retCount].events = evt;
            if (++retCount == maxEvents) break;
        }
    }

#endif

    if (timeoutRef != NULL) *timeoutRef -= WXSocket_MilliTime() - startTime;
    return retCount;
}

/**
 * Destroy the provided registry instance, which releases associated system
 * resources and frees all memory allocated for the registry.
 *
 * @param registry The event registry to be destroyed.
 */
void WXEvent_DestroyRegistry(WXEvent_Registry *registry) {
#ifdef WXEVENT_USE_EPOLL
    close(registry->epollFd);
#endif

#ifdef WXEVENT_USE_POLL
    WXFree(registry->fds);
#endif

    WXFree(registry->entries);
    WXFree(registry);
}
