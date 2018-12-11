/*
 * General methods for network socket creation and processing.
 *
 * Copyright (C) 1997-2018 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "socket.h"

#include <errno.h>
#include <sys/types.h>
#ifndef WIN32
#include <sys/socket.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#else
#include "mstcpip.h"
#endif

/* Redirect appropriately for the Windows/Unix based network error handling */
#ifdef WIN32
#define sockErrNo WSAGetLastError()
#else
#define sockErrNo errno
#endif

/* Internal method to manage millisecond timings. */
/* NOTE: on windows, this is *not* epoch time (but don't care for intervals)! */
static int64_t _wxmillitime() {
#ifdef WIN32
    FILETIME fTime;
    ULARGE_INTEGER iTime;

    GetSystemTimeAsFileTime(&fTime);
    (void) memcpy(&iTime, &fTime, sizeof(iTime));
    return (int64_t) (iTime.QuadPart / 10000LL);
#else
    struct timeval tv;

    (void) gettimeofday(&tv, NULL);
    return (((int64_t) tv.tv_sec) * ((int64_t) 1000)) + tv.tv_usec / 1000;
#endif
}

/**
 * Wrapping method to access the system error number as set by the last network
 * operation.  This method accesses the thread-specific value in both the
 * Windows and Unix environments, so local copies are not required.
 *
 * @return The last error number for any network actions (thread-safe).
 */
int WXSocket_GetSystemErrNo() {
    return sockErrNo;
}

/* Internal method to set a specific error number */
void _setSockErrNo(int errnum) {
#ifdef WIN32
    WSASetLastError(errnum);
#else
    errno = errnum;
#endif
}

/* Mapping region for addrinfo errors */
#define EAI_ERROR_OFFSET -10000000
#define EAI_ERROR_LIMIT 1000

/**
 * Obtain the error message/string associated to the provided system error
 * number, handling platform-specific error messaging as well as non-standard
 * extensions.
 *
 * @param serrno The system error number to translate.
 * @return The error string assocated to the provided error number.
 */
const char *WXSocket_GetErrorStr(int serrno) {
#ifdef WIN32
    static struct wErrData {
        int serrno;
        char *errorDesc;
    } werrs[] = {
        { WSA_E_CANCELLED, "Call was cancelled" },
        { WSA_E_NO_MORE, "No more results" },
        { WSA_INVALID_HANDLE, "Specified event object handle is invalid" },
        { WSA_INVALID_PARAMETER, "One or more parameters are invalid" },
        { WSA_IO_INCOMPLETE, "Overlapped I/O event not in signalled state" },
        { WSA_IO_PENDING, "Overlapped operations will complete later" },
        { WSA_NOT_ENOUGH_MEMORY, "Insufficient memory available" },
        { WSA_OPERATION_ABORTED, "Overlapped operation aborted" },
        /* WSA_QOS_* errors could go here... */
        { WSABASEERR, "No error" },
        { WSAEACCES, "Permission denied" },
        { WSAEADDRINUSE, "Address already in use" },
        { WSAEADDRNOTAVAIL, "Cannot assign requested address" },
        { WSAEAFNOSUPPORT, "Address family not supported by protocol family" },
        { WSAEALREADY, "Operation already in progress" },
        { WSAEBADF, "File handle is not valid" },
        { WSAECANCELLED, "Call has been cancelled" },
        { WSAECONNABORTED, "Software caused connection abort" },
        { WSAECONNREFUSED, "Connection refused" },
        { WSAECONNRESET, "Connection reset by peer" },
        { WSAEDESTADDRREQ, "Destination address required" },
        { WSAEDISCON, "Graceful shutdown in progress" },
        { WSAEDQUOT, "Disc quota exceeded" },
        { WSAEFAULT, "Bad address" },
        { WSAEHOSTDOWN, "Host is down" },
        { WSAEHOSTUNREACH, "No route to host" },
        { WSAEINPROGRESS, "Operation now in progress" },
        { WSAEINTR, "Interrupted function call" },
        { WSAEINVAL, "Invalid argument" },
        { WSAEINVALIDPROCTABLE, "Procedure call table is invalid" },
        { WSAEINVALIDPROVIDER, "Service provider is invalid" },
        { WSAEISCONN, "Socket is already connected" },
        { WSAELOOP, "Cannot translate name" },
        { WSAEMFILE, "Too many open files/sockets" },
        { WSAEMSGSIZE, "Message too long" },
        { WSAENAMETOOLONG, "Name too long" },
        { WSAENETDOWN, "Network is down" },
        { WSAENETRESET, "Network dropped connection on reset" },
        { WSAENETUNREACH, "Network is unreachable" },
        { WSAENOBUFS, "No buffer space available" },
        { WSAENOMORE, "No more results" },
        { WSAENOPROTOOPT, "Bad protocol option" },
        { WSAENOTCONN, "Socket is not connected" },
        { WSAENOTEMPTY, "Directory not empty" },
        { WSAENOTSOCK, "Socket operation on nonsocket" },
        { WSAEOPNOTSUPP, "Operation not supported" },
        { WSAEPFNOSUPPORT, "Protocol family not supported" },
        { WSAEPROCLIM, "Too many processes" },
        { WSAEPROTONOSUPPORT, "Protocol not supported" },
        { WSAEPROTOTYPE, "Protocol wrong type for socket" },
        { WSAEPROVIDERFAILEDINIT, "Service provider failed to initialize" },
        { WSAEREFUSED, "Database query refused" },
        { WSAEREMOTE, "Item is remote" },
        { WSAESHUTDOWN, "Cannot send after socket shutdown" },
        { WSAESOCKTNOSUPPORT, "Socket type not supported" },
        { WSAESTALE, "Stale file handle reference" },
        { WSAETIMEDOUT, "Connection timed out" },
        { WSAETOOMANYREFS, "Too many references" },
        { WSAEUSERS, "User quota exceeded" },
        { WSAEWOULDBLOCK, "Resource temporarily unavailable" },
        { WSAHOST_NOT_FOUND, "Host not found" },
        { WSANO_DATA, "Valid name, no data record of requested type" },
        { WSANO_RECOVERY, "This is a non-recoverable error" },
        { WSANOTINITIALISED, "Successful WSAStartup() not yet performed" },
        { WSASERVICE_NOT_FOUND, "Service not found" },
        { WSASYSCALLFAILURE, "System call failure" },
        { WSASYSNOTREADY, "Network subsystem is unavailable" },
        { WSATRY_AGAIN, "Nonauthoritative host not found" },
        { WSATRY_AGAIN, "Nonauthoritative host not found" },
        { WSATYPE_NOT_FOUND, "Class type not found" },
        { WSAVERNOTSUPPORTED, "Winsock DLL Version out of range" },
     };
     int idx;

     for (idx = 0; idx < (sizeof(werrs) / sizeof(struct wErrData)); idx++) {
         if (werrs[idx].serrno == serrno) return werrs[idx].errorDesc;
     }
#endif

     if ((serrno > EAI_ERROR_OFFSET - EAI_ERROR_LIMIT) &&
             (serrno < EAI_ERROR_OFFSET + EAI_ERROR_LIMIT)) {
         return gai_strerror(serrno - EAI_ERROR_OFFSET);
     }

#ifndef WIN32
     /* Different handling for errors from gethostbyname() in non-win */
     if (serrno < 0) {
         switch (-serrno) {
             case HOST_NOT_FOUND: return "Unknown host";
             case TRY_AGAIN:      return "Host name lookup failure";
             case NO_RECOVERY:    return "Unknown server error";
             case NO_DATA:        return "No address associated with name";
             default:             return "Unknown error";
         }
     }
#endif

     /* Not found (or not windows), fallback to strerror */
     return strerror(serrno);
}

/* Common method for addrinfo wrapping with error translation */
static int _addrinfo(char *host, char *service, struct addrinfo *hints,
                     struct addrinfo **res) {
    /* Wrap getaddrinfo() call with error handling */
    int rc = getaddrinfo(host, service, hints, res);
    if (rc != 0) {
        /* Ideally, this would translate into other error conditions */
        /* Assume it failed overall (rc) due to invalid data condition */
        _setSockErrNo(EAI_ERROR_OFFSET + rc);
        *res = NULL;
        return WXNRC_DATA_ERROR;
    }

    /* Also verify base address compatibility */
    if (((*res)->ai_family != AF_INET) && ((*res)->ai_family != AF_INET6)) {
#ifdef WIN32
        WSASetLastError(WSAESOCKTNOSUPPORT);
#else
#ifdef ESOCKTNOSUPPORT
        errno = ESOCKTNOSUPPORT;
#else
        errno = EINVAL;
#endif
#endif
        freeaddrinfo(*res);
        *res = NULL;
        return WXNRC_DATA_ERROR;
    }

    return WXNRC_OK;
}

/**
 * Standardized method to validate a provided hostname or IP address host
 * specification.
 *
 * @param hostIpAddr Hostname or IP address to validate.
 * @return WXNRC_OK if the hostname is valid, WXNRC_DATA_ERROR otherwise.  Use 
 *         WXSocket_GetSystemErrNo to retrieve the failure condition.
 */
int WXSocket_ValidateHostIpAddr(char *hostIpAddr) {
    struct addrinfo hints, *hostInfo;
    int rc;

    /* Use addrinfo instead of gethostbyname, better IPv6 support */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    /* Only checking hostname, TCP/UDP targets are irrelevant */
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG;

    /* Use the common lookup method */
    rc = _addrinfo(hostIpAddr, NULL, &hints, &hostInfo);
    if (rc == WXNRC_OK) freeaddrinfo(hostInfo);
    return rc;
}

/**
 * General method to open a client socket to the specified address target,
 * supporting both synchronous and asynchronous connection models.
 *
 * @param addrInfo The address (host, port, protocol) of the target to
 *                 connect to, opaque instance of struct addrinfo.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @param timeoutRef If NULL, perform a synchronous connection (method will not
 *                   return until connection is established or an error occurs).
 *                   If non-NULL, an asynchronous connection will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenClient(void *addrInfo, wxsocket_t *socketRef,
                        int32_t *timeoutRef) {
    struct addrinfo *addressInfo = (struct addrinfo *) addrInfo;
#ifdef WIN32
    SOCKET socketHandle;
    int optLen;
#else
    int32_t socketHandle;
    socklen_t optLen;
#endif
    int64_t startTime = _wxmillitime();
    int rc, errnum;

    /* Create the socket instance */
    socketHandle = socket(addressInfo->ai_family, addressInfo->ai_socktype,
                          addressInfo->ai_protocol);
#ifdef WIN32
    if (socketHandle == INVALID_SOCKET) {
#else
    if (socketHandle < 0) {
#endif
        if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
        return WXNRC_SYS_ERROR;
    }

    /* Synchronous connect sequence if timeout unspecified */
    if (timeoutRef == NULL) {
        if (connect(socketHandle, addressInfo->ai_addr,
                    addressInfo->ai_addrlen) < 0) {
            errnum = sockErrNo;
            WXSocket_Close((wxsocket_t) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
    } else {
        /* Force non-blocking connect to detect timeout */
        rc = WXSocket_SetNonBlockingState((wxsocket_t) socketHandle, TRUE);
        if (rc) {
            errnum = sockErrNo;
            WXSocket_Close((wxsocket_t) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return rc;
        }

        /* Attempt connection, will most likely need wait */
        if (connect(socketHandle, addressInfo->ai_addr,
                    addressInfo->ai_addrlen) < 0) {
#ifdef WIN32
            if (sockErrNo != WSAEWOULDBLOCK) {
#else
            if (sockErrNo != EINPROGRESS) {
#endif
                /* Ooops, this is a real error condition */
                errnum = sockErrNo;
                WXSocket_Close((wxsocket_t) socketHandle);
                if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
                _setSockErrNo(errnum);
                return WXNRC_SYS_ERROR;
            }
        }

        /* Wait for activity */
        *timeoutRef -= _wxmillitime() - startTime;
        rc = WXSocket_Wait((wxsocket_t) socketHandle, WXNRC_WRITE_REQUIRED,
                           timeoutRef);
        if (rc < 0) {
            errnum = sockErrNo;
            WXSocket_Close((wxsocket_t) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return rc;
        }
        startTime = _wxmillitime();

        /* Cannot explicitly trust return from the wait states on connect */
        optLen = sizeof(errnum);
        if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                       (uint8_t *) &errnum, &optLen) < 0) {
            /* This is unexpected */
            errnum = sockErrNo;
            WXSocket_Close((wxsocket_t) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
        if (errnum != 0) {
            WXSocket_Close((wxsocket_t) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }

        /* We now return you to your regular programming */
        rc = WXSocket_SetNonBlockingState((wxsocket_t) socketHandle, FALSE);
        if (rc) {
            errnum = sockErrNo;
            WXSocket_Close((wxsocket_t) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return rc;
        }
    }

    /* Either we've gotten a connection or are still waiting... */
    if (timeoutRef != NULL) *timeoutRef -= _wxmillitime() - startTime;
    if (socketRef != NULL) *socketRef = (wxsocket_t) socketHandle;

    return WXNRC_OK;
}

/**
 * Convenience wrapper to open a 'standard' client connection to the specified
 * target (wraps the more generic OpenClient call).
 *
 * @param hostIpAddr Hostname or IP address of the target system to connect to.
 * @param service Either the service name or the port number for the connection.
 * @param isTCP TRUE if this should be a TCP-based connection, FALSE for UDP.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @param timeoutRef If NULL, perform a synchronous connection (method will not
 *                   return until connection is established or an error occurs).
 *                   If non-NULL, an asynchronous connection will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenStdClient(char *hostIpAddr, char *service, int isTCP,
                           wxsocket_t *socketRef, int32_t *timeoutRef) {
    struct addrinfo hints, *addrInfo = NULL;
    wxsocket_t socketHandle;
    int rc;

    /* Retrieve target address information */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = (isTCP) ? SOCK_STREAM : SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    rc = _addrinfo(hostIpAddr, service, &hints, &addrInfo);
    if (rc != WXNRC_OK) return rc;

    rc = WXSocket_OpenClient(addrInfo, &socketHandle, timeoutRef);
    freeaddrinfo(addrInfo);
    if (socketRef != NULL) *socketRef = socketHandle;
    return rc;
}

/**
 * Method to wait for read/write availability on the given socket handle.
 * Uses select/poll as required, not event dispatching like the server engine.
 *
 * @param socketHandle The handle of the socket to wait on.
 * @param condition A mixture of WXNRC_READ_REQUIRED and/or WXNRC_WRITE_REQUIRED
 *                  depending on the read/write conditions.
 * @param timeoutRef If NULL, perform a synchronous wait (method will not
 *                   return until condition is reached or an error occurs).
 *                   If non-NULL, an asynchronous wait will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return A mixture of WXNRC_READ_REQUIRED and/or WXNRC_WRITE_REQUIRED
 *         (depending on input) if the wait condition is valid, a WXNRC_* error
 *         code otherwise.
 */
int WXSocket_Wait(wxsocket_t socketHandle, int condition, int32_t *timeoutRef) {
#ifdef WIN32
    fd_set readSet, writeSet, excSet;
    struct timeval tv;
#else
    struct pollfd connPollFds[1];
#endif
    int64_t delay, startTime = _wxmillitime();
    int rc, result = WXNRC_OK;

    /* Perform select/polling operations to wait for outcome */
    rc = -1;
#ifdef WIN32
    while (rc < 0) {
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&excSet);
        if (condition & WXNRC_READ_REQUIRED) FD_SET(socketHandle, &readSet);
        if (condition & WXNRC_WRITE_REQUIRED) FD_SET(socketHandle, &writeSet);
        FD_SET(socketHandle, &excSet);

        if (timeoutRef == NULL) {
            rc = select(socketHandle + 1, &readSet, &writeSet,
                        &excSet, NULL);
        } else {
            delay = *timeoutRef - (_wxmillitime() - startTime);
            if (delay > 0) {
                tv.tv_sec = delay / 1000;
                tv.tv_usec = (delay % 1000) * 1000;
                rc = select(socketHandle + 1, %readSet, &writeSet,
                            &excSet, &tv);
            } else {
                rc = 0;
            }
        }
        if (rc > 0) {
            if (FD_ISSET(socketHandle, &readSet))
                                result |= WXNRC_READ_REQUIRED;
            if (FD_ISSET(socketHandle, &writeSet))
                                result |= WXNRC_WRITE_REQUIRED;
        }
#else
    connPollFds[0].fd = socketHandle;
    connPollFds[0].events = POLLERR;
    if (condition & WXNRC_READ_REQUIRED) connPollFds[0].events |= POLLIN;
    if (condition & WXNRC_WRITE_REQUIRED) connPollFds[0].events |= POLLOUT;
    while (rc < 0) {
        if (timeoutRef == NULL) {
            rc = poll(connPollFds, 1, -1);
        } else {
            delay = *timeoutRef - (_wxmillitime() - startTime);
            if (delay > 0) {
                rc = poll(connPollFds, 1, delay);
            } else {
                rc = 0;
            }
        }
        if (rc > 0) {
            if (connPollFds[0].revents & POLLIN)
                                result |= WXNRC_READ_REQUIRED;
            if (connPollFds[0].revents & POLLOUT)
                                result |= WXNRC_WRITE_REQUIRED;
        }
#endif

        /* Check for failure and timeout */
        if (rc < 0) {
            if (sockErrNo == EINTR) continue;
#ifdef EAGAIN
            if (sockErrNo == EAGAIN) continue;
#endif
            return WXNRC_SYS_ERROR;
        }

        if ((timeoutRef != NULL) && (rc == 0)) {
#ifdef WIN32
            WSASetLastError(WSAETIMEDOUT);
#else
            errno = ETIMEDOUT;
#endif
            *timeoutRef -= _wxmillitime() - startTime;
            if (*timeoutRef >= 0) *timeoutRef = -1;
            return WXNRC_TIMEOUT;
        }
    }

    /* Record the final time remaining */
    if (timeoutRef != NULL) *timeoutRef -= _wxmillitime() - startTime;

    return result;
}

/**
 * Manage the blocking state of the socket.  Determines whether data access
 * operations (including connect) behave synchronously or asynchronously.
 *
 * @param socketHandle The handle of the socket to manipulate.
 * @param isNonBlocking If TRUE, set the socket to non-blocking.  If FALSE,
 *                       set it to blocking.
 * @return TRUE if manipulation was successful, FALSE on error (check system
 *         error number).
 */
int WXSocket_SetNonBlockingState(wxsocket_t socketHandle, int isNonBlocking) {
#ifdef WIN32
    ULONG nonBlock = (isNonBlocking) ? 1 : 0;

    if (ioctlsocket((SOCKET) socketHandle, FIONBIO, &nonBlock) != 0) {
        return FALSE;
    }
#else
    int sockFlags = fcntl((int) socketHandle, F_GETFL, 0);
    if (isNonBlocking) {
#ifdef O_NONBLOCK
        sockFlags |= O_NONBLOCK;
#else
#ifdef O_NDELAY
        sockFlags |= O_NDELAY;
#endif
#endif
    } else {
#ifdef O_NONBLOCK
        sockFlags &= ~O_NONBLOCK;
#else
#ifdef O_NDELAY
        sockFlags &= ~O_NODELAY;
#endif
#endif
    }
    (void) fcntl((int) socketHandle, F_SETFL, sockFlags);
#endif

    return TRUE;
}

/**
 * General method to close the provided socket instance.
 *
 * @param socketHandle The handle of the socket to close.
 */
void WXSocket_Close(wxsocket_t socketHandle) {
#ifdef WIN32
    closesocket((SOCKET) socketHandle);
#else
    close((int) socketHandle);
#endif
}
