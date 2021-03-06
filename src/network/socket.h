/*
 * General methods for network socket creation and processing.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_SOCKET_H
#define WX_SOCKET_H 1

/* Grab the standard definitions */
#include "stdconfig.h"

/* Standardized error codes and signalling for networking actions */
/* Error codes are mutually exclusive and must be negative for tests */
#define WXNRC_TIMEOUT -5
#define WXNRC_DISCONNECT -4
#define WXNRC_DATA_ERROR -3
#define WXNRC_MEM_ERROR -2
#define WXNRC_SYS_ERROR -1

/* State response codes are a bitset (dependent on context) */
#define WXNRC_OK 0
#define WXNRC_OK_WITH_DATA 1
#define WXNRC_READ_REQUIRED 2
#define WXNRC_WRITE_REQUIRED 4
#define WXNRC_WAIT_REQUIRED 8

/*
 * Integration type definition to hold a socket descriptor across platforms
 * (contains a standard Unix file descriptor or a windows socket handle).
 */
typedef uint32_t WXSocket;

/* Special socket number (equivalent to INVALID_SOCKET) for "unused/error" */
#define INVALID_SOCKET_FD ((WXSocket) 0xFFFFFFFF)

/**
 * Used in various points in the networking code, globalize it.  Returns
 * a consistent millisecond timing amount (not necessarily epoch time) for
 * handling timeout determination.
 */
int64_t WXSocket_MilliTime();

/**
 * Wrapping method to access the system error number as set by the last network
 * operation.  This method accesses the thread-specific value in both the
 * Windows and Unix environments, so local copies are not required.
 *
 * @return The last error number for any network actions (thread-safe).
 */
int WXSocket_GetLastErrNo();

/**
 * Obtain the error message/string associated to the provided system error
 * number, handling platform-specific error messaging as well as non-standard
 * extensions.
 *
 * @param serrno The system error number to translate.
 * @return The error string assocated to the provided error number.
 */
const char *WXSocket_GetErrorStr(int serrno);

/**
 * Similar to the above, but for the WXNRC_* response codes (note that these
 * are somewhat common across the toolkit).
 *
 * @param respCode The WXNRC_* response code to translate.
 * @return The associated message string for the response code.
 */
const char *WXSocket_GetRespCodeStr(int respCode);

/**
 * Standardized method to validate a provided hostname or IP address host
 * specification.
 *
 * @param hostIpAddr Hostname or IP address to validate.
 * @return WXNRC_OK if the hostname is valid, WXNRC_DATA_ERROR otherwise.  Use
 *         WXSocket_GetLastErrNo to retrieve the failure condition.
 */
int WXSocket_ValidateHostIpAddr(const char *hostIpAddr);

/**
 * Determine if the provided target is a numeric IPv4 or IPv6 hostname.
 *
 * @param hostIpAddr The target address to check.
 * @return TRUE if the address is a numeric IPv4 or IPv6 address.  False for
 *         a 'hostname' (not validated as proper DNS hostname).
 */
int WXSocket_IsIpAddr(const char *hostIpAddr);

/**
 * Common method to allocate socket based on an address info definition.
 *
 * @param addrInfo The address (host, port, protocol) of the target to
 *                 connect to, opaque instance of struct addrinfo.
 * @param socketRef Pointer through which the created socket instance is
 *                  returned (if applicable, depending on error conditions).
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSocket_AllocateSocket(void *addrInfo, WXSocket *socketRef);

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
                                 int32_t *timeoutRef);

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
int WXSocket_OpenTCPClientByAddrAsync(void *addrInfo, WXSocket *socketRef);

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
                           WXSocket *socketRef, int32_t *timeoutRef);

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
                           WXSocket *socketRef, void **addrInfoRef);

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
                           WXSocket *socketRef);

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
                                 WXSocket *socketRef);

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
                           WXSocket *socketRef);

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
                    char *origin, uint32_t originLen);

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
 *         (depending on input) if the wait condition is valid, a WXNRC_* error  *         code otherwise.
 */
int WXSocket_Wait(WXSocket socketHandle, int condition, int32_t *timeoutRef);

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
                      int flags);

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
                          int flags, void *srcAddr, socklen_t *addrLen);

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
                      int flags);

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
                        int flags, void *destAddr, socklen_t addrLen);

/**
 * Manage the blocking state of the socket.  Determines whether data access
 * operations (including connect) behave synchronously or asynchronously.
 *
 * @param socketHandle The handle of the socket to manipulate.
 * @param isNonBlocking If TRUE, set the socket to non-blocking.  If FALSE,
 *                      set it to blocking.
 * @return WXNRC_OK if successful, WXNRC_SYS_ERROR on failure (check system
 *         error number for more information).
 */
int WXSocket_SetNonBlockingState(WXSocket socketHandle, int setNonBlocking);

/**
 * General method to close the provided socket instance.
 *
 * @param socketHandle The handle of the socket to close.
 */
void WXSocket_Close(WXSocket socketHandle);

#endif
