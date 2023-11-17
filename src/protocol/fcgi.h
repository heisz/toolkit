/*
 * Structures and methods for managing FastCGI (FCGI) protocol transport.
 *
 * Copyright (C) 2007-2023 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_FCGI_H
#define WX_FCGI_H 1

#include <stdint.h>
#include "socket.h"

/* Pulled from the FCGI specification, with notations and modern types */

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t requestIdB1;
    uint8_t requestIdB0;
    uint8_t contentLengthB1;
    uint8_t contentLengthB0;
    uint8_t paddingLength;
    uint8_t reserved;
} WXFCGI_Header;

/*
 * Number of bytes in a WXFCGI_Header.  Future versions of the protocol
 * will not reduce this number.
 */
#define WXFCGI_HEADER_LEN 8

/*
 * Value for version component of WXFCGI_Header
 */
#define WXFCGI_VERSION_1 1 

/*
 * Values for type component of WXFCGI_Header
 */
#define WXFCGI_BEGIN_REQUEST     1
#define WXFCGI_ABORT_REQUEST     2
#define WXFCGI_END_REQUEST       3
#define WXFCGI_PARAMS            4
#define WXFCGI_STDIN             5
#define WXFCGI_STDOUT            6
#define WXFCGI_STDERR            7
#define WXFCGI_DATA              8
#define WXFCGI_GET_VALUES        9
#define WXFCGI_GET_VALUES_RESULT 10
#define WXFCGI_UNKNOWN_TYPE      11
#define WXFCGI_MAXTYPE (WXFCGI_UNKNOWN_TYPE)

/*
 * Value for requestId component of WXFCGI_Header
 */
#define WXFCGI_NULL_REQUEST_ID 0

typedef struct {
    uint8_t roleB1;
    uint8_t roleB0;
    uint8_t flags;
    uint8_t reserved[5];
} WXFCGI_BeginRequestBody;

typedef struct {
    WXFCGI_Header header;
    WXFCGI_BeginRequestBody body;
} WXFCGI_BeginRequestRecord;

/*
 * Mask for flags component of WXFCGI_BeginRequestBody
 */
#define WXFCGI_KEEP_CONN 1

/*
 * Values for role component of WXFCGI_BeginRequestBody
 */
#define WXFCGI_RESPONDER  1
#define WXFCGI_AUTHORIZER 2
#define WXFCGI_FILTER     3

typedef struct {
    uint8_t appStatusB3;
    uint8_t appStatusB2;
    uint8_t appStatusB1;
    uint8_t appStatusB0;
    uint8_t protocolStatus;
    uint8_t reserved[3];
} WXFCGI_EndRequestBody;

typedef struct {
    WXFCGI_Header header;
    WXFCGI_EndRequestBody body;
} WXFCGI_EndRequestRecord;

/*
 * Values for protocolStatus component of WXFCGI_EndRequestBody
 */
#define WXFCGI_REQUEST_COMPLETE 0
#define WXFCGI_CANT_MPX_CONN    1
#define WXFCGI_OVERLOADED       2
#define WXFCGI_UNKNOWN_ROLE     3

/*
 * Variable names for WXFCGI_GET_VALUES / WXFCGI_GET_VALUES_RESULT records
 */
#define WXFCGI_MAX_CONNS  "WXFCGI_MAX_CONNS"
#define WXFCGI_MAX_REQS   "WXFCGI_MAX_REQS"
#define WXFCGI_MPXS_CONNS "WXFCGI_MPXS_CONNS"

typedef struct {
    uint8_t type;
    uint8_t reserved[7];
} WXFCGI_UnknownTypeBody;

typedef struct {
    WXFCGI_Header header;
    WXFCGI_UnknownTypeBody body;
} WXFCGI_UnknownTypeRecord;

/**** Spec ends here, additional structures for implementation ***/

/* Special return code/flag for write to indicate completion with close */
#define WXNRC_COMPLETE_CLOSE -100

/* Usually only use for params but make it generic */
typedef struct WXFCGI_NameValuePair {
    uint8_t *name, *value;
    struct WXFCGI_NameValuePair *next;
} WXFCGI_NameValuePair;

/* Not in the spec but track for processing */
#define WXFCGI_PHASE_BEGIN       1
#define WXFCGI_PHASE_PARAMS      2
#define WXFCGI_PHASE_PARAMS_DONE 3
#define WXFCGI_PHASE_STDIN       4
#define WXFCGI_PHASE_REQ_DONE    5
#define WXFCGI_PHASE_RESP_DONE   6

/* Container for the data associated to a standard request */
typedef struct {
    uint16_t requestId;
    uint8_t flags;
    uint8_t phase;
    WXFCGI_NameValuePair *params;
    uint8_t *stdin;
    uint32_t stdinLen;
} WXFCGI_Request;

/* (Event) container for all FCGI request/response processing */
typedef struct {
    /* The underlying socket instance */
    WXSocket sockConn;

    /* Header area for inbound record reads */
    WXFCGI_Header header;
    int32_t recordLength;

    /* Protocol defines an upper limit on record size */
    uint8_t recordBuffer[65536 + 256];
    int32_t recordOffset;

    /*
     * NOTE: per the specification, one can choose to multiplex or only
     *       support single concurrent request per connection.  PHP chose to
     *       do the latter and upstream providers like NGINX do not support
     *       multiplexing either.  So there is only one request, ever.
     */
    WXFCGI_Request request;

    /* Outbound content queueing */
    uint8_t *outBuffer;
    uint32_t outLen, outAllocLen;
} WXFCGI_Connection;

/**
 * Allocate an FCGI connection instance for the given connection.
 *
 * @param sockConn The WXSocket instance of the incoming FCGI connection.
 * @return The allocated connection instance (to provide as event data)
 *         or NULL if memory allocation failed.
 */
WXFCGI_Connection *WXFCGI_Allocate(WXSocket sockConn);

/**
 * Based on an input available signal, perform a read of the given connection.
 *
 * @param conn The connection instance to read from.
 * @return One of the WXNRC return codes.  Note that OK_WITH_DATA means an
 *         incoming FCGI request has been completed.
 */
int WXFCGI_Read(WXFCGI_Connection *conn);

/**
 * Write a response to a given request instance.  Encodes in the connection
 * outgoing buffer and attempts a write operation.
 *
 * @param conn The connection instance to write to.
 * @param requestId The identifier of the request being responded to.
 * @param isStdout TRUE if this is a standard output response message, FALSE
 *                 for a standard error response.
 * @param response The buffer of the response to be written.
 * @param length The number of bytes contained in the response.
 * @return Suitable WXNRC return codes, for memory or write operations.
 */
int WXFCGI_WriteResponse(WXFCGI_Connection *conn, uint16_t requestId,
                         int isStdout, uint8_t *response, int32_t length);

/**
 * Close out a request, either successfully or with an error code.  Like
 * response, it will perform a write attempt and return the associated code.
 *
 * @param conn The connection instance to write to.
 * @param requestId The identifier of the request being responded to.
 * @param appStatus The application status code associated to the result.
 * @param protoStatus The associated WXFCGI response status code.
 * @return Suitable WXNRC return codes, for memory or write operations.
 */
int WXFCGI_WriteEndRequest(WXFCGI_Connection *conn, uint16_t requestId,
                           uint32_t appStatus, uint8_t protoStatus);

/**
 * Convenience method to continue an outstanding/pending write operation.
 *
 * @param conn The connection instance to perform the writes on.
 * @return Associated WXNRC code for the required write operation.
 */
int WXFCGI_Write(WXFCGI_Connection *conn);
 

/**
 * Release an allocated FCGI connection instance and associated elements.
 *
 * @param conn The conn to be released.
 */
void WXFCGI_Release(WXFCGI_Connection *conn);

#endif
