/*
 * General methods for network socket creation and processing.
 *
 * Copyright (C) 1997-2026 J.M. Heisz.  All Rights Reserved.
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
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#else
#include <mstcpip.h>
#endif

/* Redirect appropriately for the Windows/Unix based network error handling */
#ifdef _WXWIN_BUILD
#define sockErrNo WSAGetLastError()
#else
#define sockErrNo errno
#endif

/**
 * Used in various points in the networking code, globalize it.  Returns
 * a consistent millisecond timing amount (not necessarily epoch time) for
 * handling timeout determination.
 */
int64_t WXSocket_MilliTime() {
#ifdef _WXWIN_BUILD
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
int WXSocket_GetLastErrNo() {
    return sockErrNo;
}

/* Internal method to set a specific error number */
void _setSockErrNo(int errnum) {
#ifdef _WXWIN_BUILD
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
#ifdef _WXWIN_BUILD
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

/* Not pretty but easiest way to be thread safe for the bitmask */
static char *stateBitStr[] = {
    "ok", "ok with data",
    "read required", "read required with data",
    "write required", "write required with data",
    "read/write required", "read/write required with data",
    "wait required", "wait required with data",
    "read/wait required", "read/wait required with data",
    "write/wait required", "write/wait required with data",
    "read/write/wait required", "read/write/wait required with data"
};

/**
 * Similar to the above, but for the WXNRC_* response codes (note that these
 * are somewhat common across the toolkit).
 *
 * @param respCode The WXNRC_* response code to translate.
 * @return The associated message string for the response code.
 */
const char *WXSocket_GetRespCodeStr(int respCode) {
    switch (respCode) {
        case WXNRC_TIMEOUT:
            return "timeout";
        case WXNRC_DISCONNECT:
            return "disconnect";
        case WXNRC_DATA_ERROR:
            return "data error";
        case WXNRC_MEM_ERROR:
            return "memory error";
        case WXNRC_SYS_ERROR:
            return "system error";
        default:
            if ((respCode >= 0) && (respCode <= 15)) {
                return stateBitStr[respCode];
            }
            break;
    }

    return "unknown";
}

/* Common method for addrinfo wrapping with error translation */
static int _addrinfo(const char *host, const char *service,
                     struct addrinfo *hints, struct addrinfo **res) {
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
#ifdef _WXWIN_BUILD
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
 *         WXSocket_GetLastErrNo to retrieve the failure condition.
 */
int WXSocket_ValidateHostIpAddr(const char *hostIpAddr) {
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
 * Determine if the provided target is a numeric IPv4 or IPv6 hostname.
 *
 * @param hostIpAddr The target address to check.
 * @return TRUE if the address is a numeric IPv4 or IPv6 address.  False for
 *         a 'hostname' (not validated as proper DNS hostname).
 */
int WXSocket_IsIpAddr(const char *hostIpAddr) {
    char tmp[sizeof(struct in6_addr)];

    if (inet_pton(AF_INET, hostIpAddr, tmp)) return TRUE;
    if (inet_pton(AF_INET6, hostIpAddr, tmp)) return TRUE;
    return FALSE;
}

/**
 * Common method to allocate socket based on an address info definition.
 *
 * @param addrInfo The address (host, port, protocol) of the target to
 *                 connect to, opaque instance of struct addrinfo.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_AllocateSocket(void *addrInfo, WXSocket *socketRef) {
    struct addrinfo *addressInfo = (struct addrinfo *) addrInfo;
#ifdef _WXWIN_BUILD
    SOCKET socketHandle;
#else
    int32_t socketHandle;
#endif

    /* Create the socket instance */
    socketHandle = socket(addressInfo->ai_family, addressInfo->ai_socktype,
                          addressInfo->ai_protocol);
#ifdef _WXWIN_BUILD
    if (socketHandle == INVALID_SOCKET) {
#else
    if (socketHandle < 0) {
#endif
        if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
        return WXNRC_SYS_ERROR;
    }
    if (socketRef != NULL) *socketRef = socketHandle;

    return WXNRC_OK;
}

/**
 * General method to open a TCP client socket to the specified address target,
 * supporting both synchronous and asynchronous connection models.
 *
 * @param addrInfo The address (host, port, protocol) of the target to
 *                 connect to, opaque instance of struct addrinfo.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @param timeoutRef If NULL, perform a synchronous connection (method will not
 *                   return until connection is established or an error occurs).
 *                   If non-NULL, a synchronous connection will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenTCPClientByAddr(void *addrInfo, WXSocket *socketRef,
                                 int32_t *timeoutRef) {
    struct addrinfo *addressInfo = (struct addrinfo *) addrInfo;
#ifdef _WXWIN_BUILD
    SOCKET socketHandle;
    int optLen;
#else
    int32_t socketHandle;
    socklen_t optLen;
#endif
    int64_t startTime = WXSocket_MilliTime();
    int rc, errnum;

    /* Create the socket instance */
    rc = WXSocket_AllocateSocket(addrInfo, socketRef);
    if (rc != WXNRC_OK) return rc;
#ifdef _WXWIN_BUILD
    socketHandle = (SOCKET) *socketRef;
#else
    socketHandle = (int32_t) *socketRef;
#endif

    /* Synchronous connect sequence if timeout unspecified */
    if (timeoutRef == NULL) {
        if (connect(socketHandle, addressInfo->ai_addr,
                    addressInfo->ai_addrlen) < 0) {
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
    } else {
        /* Force non-blocking connect to detect timeout */
        rc = WXSocket_SetNonBlockingState((WXSocket) socketHandle, TRUE);
        if (rc) {
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return rc;
        }

        /* Attempt connection, will most likely need wait */
        if (connect(socketHandle, addressInfo->ai_addr,
                    addressInfo->ai_addrlen) < 0) {
#ifdef _WXWIN_BUILD
            if (sockErrNo != WSAEWOULDBLOCK) {
#else
            if (sockErrNo != EINPROGRESS) {
#endif
                /* Ooops, this is a real error condition */
                errnum = sockErrNo;
                WXSocket_Close((WXSocket) socketHandle);
                if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
                _setSockErrNo(errnum);
                return WXNRC_SYS_ERROR;
            }
        }

        /* Wait for activity */
        *timeoutRef -= WXSocket_MilliTime() - startTime;
        rc = WXSocket_Wait((WXSocket) socketHandle, WXNRC_WRITE_REQUIRED,
                           timeoutRef);
        if (rc < 0) {
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return rc;
        }
        startTime = WXSocket_MilliTime();

        /* Cannot explicitly trust return from the wait states on connect */
        optLen = sizeof(errnum);
        if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                       (uint8_t *) &errnum, &optLen) < 0) {
            /* This is unexpected */
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
        if (errnum != 0) {
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }

        /* We now return you to your regular programming */
        rc = WXSocket_SetNonBlockingState((WXSocket) socketHandle, FALSE);
        if (rc) {
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return rc;
        }
    }

    /* Either we've gotten a connection or are still waiting... */
    if (timeoutRef != NULL) *timeoutRef -= WXSocket_MilliTime() - startTime;
    if (socketRef != NULL) *socketRef = (WXSocket) socketHandle;

    return WXNRC_OK;
}

/**
 * Special form of the above method to support connection start in fully
 * asynchronous environments.
 *
 * @param addrInfo The address (host, port, protocol) of the target to
 *                 connect to, opaque instance of struct addrinfo.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenTCPClientByAddrAsync(void *addrInfo, WXSocket *socketRef) {
    struct addrinfo *addressInfo = (struct addrinfo *) addrInfo;
#ifdef _WXWIN_BUILD
    SOCKET socketHandle;
    int optLen;
#else
    int32_t socketHandle;
    socklen_t optLen;
#endif
    int rc, errnum;

    /* Create the socket instance */
    rc = WXSocket_AllocateSocket(addrInfo, socketRef);
    if (rc != WXNRC_OK) return rc;
#ifdef _WXWIN_BUILD
    socketHandle = (SOCKET) *socketRef;
#else
    socketHandle = (int32_t) *socketRef;
#endif

    /* Force non-blocking connect for async handling */
    rc = WXSocket_SetNonBlockingState((WXSocket) socketHandle, TRUE);
    if (rc) {
        errnum = sockErrNo;
        WXSocket_Close((WXSocket) socketHandle);
        if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
        _setSockErrNo(errnum);
        return rc;
    }

    /* Attempt connection, will most likely need wait */
    if (connect(socketHandle, addressInfo->ai_addr,
                addressInfo->ai_addrlen) < 0) {
#ifdef _WXWIN_BUILD
        if (sockErrNo != WSAEWOULDBLOCK) {
#else
        if (sockErrNo != EINPROGRESS) {
#endif
            /* Ooops, this is a real error condition */
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            if (socketRef != NULL) *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
    }

    return WXNRC_OK;
}

/**
 * Convenience wrapper to open a 'standard' TCP client connection to a named
 * target instance with full resolution.
 *
 * @param hostIpAddr Hostname or IP address of the target system to connect to.
 * @param service Either the service name or the port number for the connection.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @param timeoutRef If NULL, perform a synchronous connection (method will not
 *                   return until connection is established or an error occurs).
 *                   If non-NULL, a synchronous connection will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenTCPClient(const char *hostIpAddr, const char *service,
                           WXSocket *socketRef, int32_t *timeoutRef) {
    struct addrinfo hints, *addrInfo = NULL;
    WXSocket socketHandle;
    int rc;

    /* Retrieve target address information */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    rc = _addrinfo(hostIpAddr, service, &hints, &addrInfo);
    if (rc != WXNRC_OK) return rc;

    rc = WXSocket_OpenTCPClientByAddr(addrInfo, &socketHandle, timeoutRef);
    freeaddrinfo(addrInfo);
    if (socketRef != NULL) *socketRef = socketHandle;
    return rc;
}

/**
 * Allocate a UDP socket instance with target address resolution.
 *
 * @param hostIpAddr Hostname or IP address of the target system to message.
 * @param service Either the service name or the port number for the connection.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @param addrInfoRef Opaque reference to the resolved address info structure
 *                    for the target address, if non-NULL.  Must be freed if
 *                    returned.
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenUDPClient(const char *hostIpAddr, const char *service,
                           WXSocket *socketRef, void **addrInfoRef) {
    struct addrinfo hints, *addrInfo = NULL;
    int rc;

    /* Retrieve target address information */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    rc = _addrinfo(hostIpAddr, service, &hints, &addrInfo);
    if (rc != WXNRC_OK) return rc;

    /* And create the socket */
    rc = WXSocket_AllocateSocket(addrInfo, socketRef);
    if (rc != WXNRC_OK) {
        freeaddrinfo(addrInfo);
        return rc;
    }

    /* Assign address info if reference provided, otherwise set it free */
    if (addrInfoRef != NULL) *addrInfoRef = addrInfo;
    else freeaddrinfo(addrInfo);

    return WXNRC_OK;
}

/* Common method for the binding sequence for the following methods */
static int WXSocket_BindServer(struct addrinfo *addrInfo, uint32_t *portRef,
                               WXSocket *socketRef) {
#ifdef _WXWIN_BUILD
    SOCKET socketHandle;
    int localAddrLen;
#else
    int32_t socketHandle;
    socklen_t localAddrLen;
#endif
    struct sockaddr_in localAddr;
    int optVal, rc, errnum;

    /* Create the socket instance */
    rc = WXSocket_AllocateSocket(addrInfo, socketRef);
    if (rc != WXNRC_OK) return rc;
#ifdef _WXWIN_BUILD
    socketHandle = (SOCKET) *socketRef;
#else
    socketHandle = (int32_t) *socketRef;
#endif

    /* Indicate socket reusability (rebind assistance) */
    optVal = 1;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR,
                   (const char *) &optVal, sizeof(optVal)) < 0) {
        errnum = sockErrNo;
        WXSocket_Close((WXSocket) socketHandle);
        *socketRef = INVALID_SOCKET_FD;
        _setSockErrNo(errnum);
        return WXNRC_SYS_ERROR;
    }

    /* Bind to the target instance */
    if (bind(socketHandle, addrInfo->ai_addr, addrInfo->ai_addrlen) < 0) {
        errnum = sockErrNo;
        WXSocket_Close((WXSocket) socketHandle);
        *socketRef = INVALID_SOCKET_FD;
        _setSockErrNo(errnum);
        return WXNRC_SYS_ERROR;
    }

    /* Automatically set listen queue for TCP instances */
    if ((addrInfo->ai_family == AF_INET) || (addrInfo->ai_family == AF_INET6)) {
        if (listen(socketHandle, SOMAXCONN) < 0) {
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
    }

    /* Retrieve the ephemeral port number, where applicable */
    if (portRef != NULL) {
        localAddrLen = sizeof(struct sockaddr_in);
        if (getsockname(socketHandle, (struct sockaddr *) &localAddr,
                        &localAddrLen) < 0) {
            errnum = sockErrNo;
            WXSocket_Close((WXSocket) socketHandle);
            *socketRef = INVALID_SOCKET_FD;
            _setSockErrNo(errnum);
            return WXNRC_SYS_ERROR;
        }
        *portRef = ntohs(localAddr.sin_port);
    }

    return WXNRC_OK;
}

/**
 * Allocate and bind a TCP server socket on the indicated address/port
 * instance.
 *
 * @param hostIpAddr Hostname or IP address of the target network to bind
 *                   to.  If NULL, binds to all networks (INADDR_ANY).
 * @param service Either the service name or the port number to bind to.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenTCPServer(const char *hostIpAddr, const char *service,
                           WXSocket *socketRef) {
    struct addrinfo hints, *addrInfo = NULL;
    WXSocket socketHandle;
    int rc;

    /* Retrieve target address information */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    rc = _addrinfo(hostIpAddr, service, &hints, &addrInfo);
    if (rc != WXNRC_OK) return rc;

    /* And open/bind the socket instance */
    rc = WXSocket_BindServer(addrInfo, NULL, &socketHandle);
    freeaddrinfo(addrInfo);
    if (socketRef != NULL) *socketRef = socketHandle;
    return rc;
}

/**
 * Allocate and bind an ephemeral (indeterminant port) TCP server socket on
 * the indicated address.
 *
 * @param hostIpAddr Hostname or IP address of the target network to bind
 *                   to.  If NULL, binds to all netowkrs (INADDR_ANY).
 * @param portRef Pointer through which ephemeral port number is returned.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenEphemeralServer(const char *hostIpAddr, uint32_t *portRef,
                                 WXSocket *socketRef) {
    struct addrinfo hints, *addrInfo = NULL;
    uint32_t ephemPort = 0;
    WXSocket socketHandle;
    int rc;

    /* Retrieve target address information */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    rc = _addrinfo(hostIpAddr, "0", &hints, &addrInfo);
    if (rc != WXNRC_OK) return rc;

    /* And open/bind the socket instance */
    rc = WXSocket_BindServer(addrInfo, &ephemPort, &socketHandle);
    freeaddrinfo(addrInfo);
    if (portRef != NULL) *portRef = ephemPort;
    if (socketRef != NULL) *socketRef = socketHandle;
    return rc;
}

/**
 * Allocate and bind a UDP server socket on the indicated address/port
 * instance.
 *
 * @param hostIpAddr Hostname or IP address of the target network to bind
 *                   to.  If NULL, binds to all netowkrs (INADDR_ANY).
 * @param service Either the service name or the port number to bind to.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_OpenUDPServer(const char *hostIpAddr, const char *service,
                           WXSocket *socketRef) {
    struct addrinfo hints, *addrInfo = NULL;
    WXSocket socketHandle;
    int rc;

    /* Retrieve target address information */
    (void) memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    rc = _addrinfo(hostIpAddr, service, &hints, &addrInfo);
    if (rc != WXNRC_OK) return rc;

    /* And open/bind the socket instance */
    rc = WXSocket_BindServer(addrInfo, NULL, &socketHandle);
    freeaddrinfo(addrInfo);
    if (socketRef != NULL) *socketRef = socketHandle;
    return rc;
}

/**
 * Wrapper around the server socket accept() call that includes support for
 * determination of the originating client IP address.  This method should 
 * only be called where a read indication from the bound socket exists for 
 * the queued connection instances.
 *
 * @param serverSocket The bound server socket to accept the connection from.
 * @param socketRef Pointer through which the accepted socket is returned,
 *                  where successful.
 * @param origin If non-NULL, a buffer to store the originating address
 *               information into.
 * @param originLen The length of the previous buffer, where appropriate.
 * @return WXNRC_OK if accept was successful, suitable WXNRC_* error code on
 *         failure.  Will return TIMEOUT for a non-blocking socket that no
 *         longer has a queue.
 */
int WXSocket_Accept(WXSocket serverSocket, WXSocket *socketRef,
                    char *origin, uint32_t originLen) {
#ifdef _WXWIN_BUILD
    SOCKET socketHandle;
    int srcAddrLen;
#else
    int32_t socketHandle;
    socklen_t srcAddrLen;
#endif
    struct sockaddr_storage srcAddrInfo;
    struct sockaddr_in6 *srcAddrIN6;
    struct sockaddr_in srcAddrIN4;
    int errnum;
    
    /* Perform the accept operation */
    srcAddrLen = sizeof(srcAddrInfo);
    (void) memset(&srcAddrInfo, 0, srcAddrLen);
    socketHandle = accept(serverSocket, (struct sockaddr *) &srcAddrInfo,
                          &srcAddrLen);
#ifdef _WXWIN_BUILD
    if (socketHandle == INVALID_SOCKET) {
        errnum = sockErrNo;
        if (errnum == WSAEWOULDBLOCK) {
#else
    if (socketHandle < 0) {
        errnum = sockErrNo;
        if (errnum == EAGAIN) {
#endif
            return WXNRC_TIMEOUT;
        }
        return WXNRC_SYS_ERROR;
    }

    /* Obtain source address if so desired */
    if (origin != NULL) {
        /* Remap IPv6 encoded IPv4 addresses for nicer display */
        srcAddrIN6 = (struct sockaddr_in6 *) &srcAddrInfo;
        if (IN6_IS_ADDR_V4MAPPED(&srcAddrIN6->sin6_addr)) {
            (void) memset(&srcAddrIN4, 0, sizeof(srcAddrIN4));
            srcAddrIN4.sin_family = AF_INET;
            srcAddrIN4.sin_port = srcAddrIN6->sin6_port;
            (void) memcpy(&srcAddrIN4.sin_addr.s_addr,
                          srcAddrIN6->sin6_addr.s6_addr + 12, 4);
            (void) memcpy(&srcAddrInfo, &srcAddrIN4, sizeof(srcAddrIN4));
            srcAddrLen = sizeof(srcAddrIN4);
        }

        /* And translate */
        if (getnameinfo((struct sockaddr *) &srcAddrInfo, srcAddrLen,
                        origin, originLen, 0, 0, NI_NUMERICHOST) != 0) {
            (void) strcpy(origin, "Unknown");
        }
    }

    if (socketRef != NULL) *socketRef = socketHandle;
    return WXNRC_OK;
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
 *                   If non-NULL, a synchronous wait will be made using
 *                   the referenced value as the timeout in milliseconds.  The
 *                   time remaining will be returned through the same reference,
 *                   (a negative result indicates a connection timeout).
 * @return A mixture of WXNRC_READ_REQUIRED and/or WXNRC_WRITE_REQUIRED
 *         (depending on input) if the wait condition is valid, a WXNRC_* error
 *         code otherwise.
 */
int WXSocket_Wait(WXSocket socketHandle, int condition, int32_t *timeoutRef) {
#ifdef _WXWIN_BUILD
    fd_set readSet, writeSet, excSet;
    struct timeval tv;
#else
    struct pollfd connPollFds[1];
#endif
    int64_t delay, startTime = WXSocket_MilliTime();
    int rc, result = WXNRC_OK;

    /* Perform select/polling operations to wait for outcome */
    rc = -1;
#ifdef _WXWIN_BUILD
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
            delay = *timeoutRef - (WXSocket_MilliTime() - startTime);
            if (delay > 0) {
                tv.tv_sec = delay / 1000;
                tv.tv_usec = (delay % 1000) * 1000;
                rc = select(socketHandle + 1, &readSet, &writeSet,
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
            delay = *timeoutRef - (WXSocket_MilliTime() - startTime);
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
#ifdef _WXWIN_BUILD
            WSASetLastError(WSAETIMEDOUT);
#else
            errno = ETIMEDOUT;
#endif
            *timeoutRef -= WXSocket_MilliTime() - startTime;
            if (*timeoutRef >= 0) *timeoutRef = -1;
            return WXNRC_TIMEOUT;
        }
    }

    /* Record the final time remaining */
    if (timeoutRef != NULL) *timeoutRef -= WXSocket_MilliTime() - startTime;

    return result;
}

/**
 * Manage the blocking state of the socket.  Determines whether data access
 * operations (including connect) behave synchronously or asynchronously.
 *
 * @param socketHandle The handle of the socket to manipulate.
 * @param isNonBlocking If TRUE, set the socket to non-blocking.  If FALSE,
 *                       set it to blocking.
 * @return WXNRC_OK if successful, WXNRC_SYS_ERROR on failure (check system
 *         error number for more information).
 */
int WXSocket_SetNonBlockingState(WXSocket socketHandle, int isNonBlocking) {
#ifdef _WXWIN_BUILD
    ULONG nonBlock = (isNonBlocking) ? 1 : 0;

    if (ioctlsocket((SOCKET) socketHandle, FIONBIO, &nonBlock) != 0) {
        return WXNRC_SYS_ERROR;
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
    if (fcntl((int) socketHandle, F_SETFL, sockFlags) < 0) {
        return WXNRC_SYS_ERROR;
    }
#endif

    return WXNRC_OK;
}

/* All of the following methods have a common set of error codes */
static ssize_t txlateError(ssize_t errorCode) {
#ifdef _WXWIN_BUILD
    if (errorCode == WSAEWOULDBLOCK) return 0;
    if (errorCode == WSAETIMEDOUT) return WXNRC_TIMEOUT;
    if (errorCode == WSAECONNRESET) return WXNRC_DISCONNECT;
#endif
#ifdef ECONNRESET
    if (errorCode == ECONNRESET) return WXNRC_DISCONNECT;
#endif
#ifdef EPIPE
    if (errorCode == EPIPE) return WXNRC_DISCONNECT;
#endif
#ifdef ENOTCONN
    if (errorCode == ENOTCONN) return WXNRC_DISCONNECT;
#endif
#ifdef EWOULDBLOCK
    if (errorCode == EWOULDBLOCK) return 0;
#endif
#ifdef EAGAIN
    if (errorCode == EAGAIN) return 0;
#endif

    return WXNRC_SYS_ERROR;
}

/**
 * Wrapping method to read from a TCP socket instance (wrapper around recv()).
 * Automatically consumes interrupts (EINTR).  Calling this method with a
 * flagset of zero is equivalent to read().
 *
 * @param socketHandle The handle of the socket to read from.
 * @param buf Reference to block of memory to be read into.
 * @param len Number of bytes to attempt to read from the socket.
 * @param flags Bitset of flags to control the read operation, setting to
 *              zero is equivalent to read().
 * @return Number of bytes read from the socket or one of the WXNRC_ error
 *         codes.  Zero indicates a non-blocking wait condition with no
 *         bytes read.
 */
ssize_t WXSocket_Recv(WXSocket socketHandle, void *buf, size_t len,
                      int flags) {
    int errorCode;
    ssize_t rc;

    do {
#ifdef _WXWIN_BUILD
        rc = recv((SOCKET) socketHandle, buf, len, flags);
#else
        rc = recv((int) socketHandle, buf, len, flags);
#endif
        errorCode = sockErrNo;
    } while ((rc < 0) && (errorCode == EINTR));

    if (rc == 0) return WXNRC_DISCONNECT;
    return (rc >= 0) ? rc : txlateError(errorCode);
}

/**
 * Wrapping method to read from a UDP socket instance (wrapper around
 * recvfrom()).  Automatically consumes interrupts (EINTR).
 *
 * @param socketHandle The handle of the socket to read from.
 * @param buf Reference to block of memory to be read into.
 * @param len Number of bytes to attempt to read from the socket.
 * @param flags Bitset of flags to control the read operation.
 * @param srcAddr Reference to sockaddr block to store message source address.
 * @param addrLen Reference to return length of previous address block.
 * @return Number of bytes read from the socket or one of the WXNRC_ error
 *         codes.  Zero indicates a non-blocking wait condition with no
 *         bytes read.
 */
ssize_t WXSocket_RecvFrom(WXSocket socketHandle, void *buf, size_t len,
                          int flags, void *srcAddr, socklen_t *addrLen) {
    struct sockaddr *addr = (struct sockaddr *) srcAddr;
    int errorCode;
    ssize_t rc;

    do {
#ifdef _WXWIN_BUILD
        rc = recvfrom((SOCKET) socketHandle, buf, len, flags, addr, addrLen);
#else
        rc = recvfrom((int) socketHandle, buf, len, flags, addr, addrLen);
#endif
        errorCode = sockErrNo;
    } while ((rc < 0) && (errorCode == EINTR));

    /* Here, UDP allows zero length datagrams (not indended for TCP) */
    return (rc >= 0) ? rc : txlateError(errorCode);
}

/**
 * Wrapping method to write to a TCP socket instance (wrapper around send()).
 * Automatically consumes interrupts (EINTR).  Calling this method with a
 * flagset of zero is equivalent to write().
 *
 * @param socketHandle The handle of the socket to write to.
 * @param buf Reference to block of memory to be written.
 * @param len Number of bytes to attempt to write to the socket.
 * @param flags Bitset of flags to control the write operation, setting to
 *              zero is equivalent to write().
 * @return Number of bytes written to the socket or one of the WXNRC_ error
 *         codes.  Zero indicates a non-blocking wait condition with no
 *         bytes written.
 */
ssize_t WXSocket_Send(WXSocket socketHandle, const void *buf, size_t len,
                      int flags) {
    int errorCode;
    ssize_t rc;

    do {
#ifdef _WXWIN_BUILD
        rc = send((SOCKET) socketHandle, buf, len, flags);
#else
        rc = send((int) socketHandle, buf, len, flags);
#endif
        errorCode = sockErrNo;
    } while ((rc < 0) && (errorCode == EINTR));

    return (rc >= 0) ? rc : txlateError(errorCode);
}

/**
 * Wrapping method to write to a UDP socket instance (wrapper around sendto()).
 * Automatically consumes interrupts (EINTR).
 *
 * @param socketHandle The handle of the socket to write to.
 * @param buf Reference to block of memory to be written.
 * @param len Number of bytes to attempt to write to the socket.
 * @param flags Bitset of flags to control the write operation.
 * @param destAddr Destination address to send the write to.
 * @param addrLen Length of previous address information block.
 * @return Number of bytes written to the socket or one of the WXNRC_ error
 *         codes.  Zero indicates a non-blocking wait condition with no
 *         bytes written.
 */
ssize_t WXSocket_SendTo(WXSocket socketHandle, void *buf, size_t len,
                        int flags, void *destAddr, socklen_t addrLen) {
    struct sockaddr *addr = (struct sockaddr *) destAddr;
    int errorCode;
    ssize_t rc;

    do {
#ifdef _WXWIN_BUILD
        rc = sendto((SOCKET) socketHandle, buf, len, flags, addr, addrLen);
#else
        rc = sendto((int) socketHandle, buf, len, flags, addr, addrLen);
#endif
        errorCode = sockErrNo;
    } while ((rc < 0) && (errorCode == EINTR));

    return (rc >= 0) ? rc : txlateError(errorCode);
}

/**
 * General method to close the provided socket instance.
 *
 * @param socketHandle The handle of the socket to close.
 */
void WXSocket_Close(WXSocket socketHandle) {
#ifdef _WXWIN_BUILD
    (void) closesocket((SOCKET) socketHandle);
#else
    (void) close((int) socketHandle);
#endif
}
