/*
 * General methods for network socket creation and processing.
 *
 * Copyright (C) 1997-2018 J.M. Heisz.  All Rights Reserved.
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

/* State codes are a bitset (dependent on context) */
#define WXNRC_OK 0
#define WXNRC_OK_WITH_DATA 1
#define WXNRC_READ_REQUIRED 2
#define WXNRC_WRITE_REQUIRED 4
#define WXNRC_WAIT_REQUIRED 8

/*
 * Integration type definition to hold a socket descriptor across platforms
 * (contains a standard Unix file descriptor or a windows socket handle).
 * Defined as a standard data type as the functional wrappings use the
 * "object oriented" WXSocket_ notation.
 */
typedef uint32_t wxsocket_t;

/* Special socket number (equivalent to INVALID_SOCKET) for "unused/error" */
#define INVALID_SOCKET_FD ((wxsocket_t) 0xFFFFFFFF)

/**
 * Wrapping method to access the system error number as set by the last network
 * operation.  This method accesses the thread-specific value in both the
 * Windows and Unix environments, so local copies are not required.
 *
 * @return The last error number for any network actions (thread-safe).
 */
int WXSocket_GetSystemErrNo();

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
 * Standardized method to validate a provided hostname or IP address host
 * specification.
 *
 * @param hostIpAddr Hostname or IP address to validate.
 * @return WXNRC_OK if the hostname is valid, WXNRC_DATA_ERROR otherwise.  Use
 *         WXSocket_GetSystemErrNo to retrieve the failure condition.
 */
int WXSocket_ValidateHostIpAddr(char *hostIpAddr);

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
                        int32_t *timeoutRef);

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
                           wxsocket_t *socketRef, int32_t *timeoutRef);

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
 *         (depending on input) if the wait condition is valid, a WXNRC_* error  *         code otherwise.
 */
int WXSocket_Wait(wxsocket_t socketHandle, int condition, int32_t *timeoutRef);

/**
 * Manage the blocking state of the socket.  Determines whether data access
 * operations (including connect) behave synchronously or asynchronously.
 *
 * @param socketHandle The handle of the socket to manipulate.
 * @param isNonBlocking If TRUE, set the socket to non-blocking.  If FALSE,
 *                      set it to blocking.
 * @return TRUE if manipulation was successful, FALSE on error (check system
 *         error number).
 */
int WXSocket_SetNonBlockingState(wxsocket_t socketHandle, int setNonBlocking);

/**
 * General method to close the provided socket instance.
 *
 * @param socketHandle The handle of the socket to close.
 */
void WXSocket_Close(wxsocket_t socketHandle);

#endif
