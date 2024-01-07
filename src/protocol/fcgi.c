/*
 * Methods for managing FCGI protocol transport.
 *
 * Copyright (C) 2007-2024 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "fcgi.h"
#include "mem.h"

/* Standard debugging pattern */
#define FCGI_DEBUG 1

/**
 * Allocate an FCGI connection instance for the given connection.
 *
 * @param sockConn The WXSocket instance of the incoming FCGI connection.
 * @return The allocated connection instance (to provide as event data)
 *         or NULL if memory allocation failed.
 */
WXFCGI_Connection *WXFCGI_Allocate(WXSocket sockConn) {
    WXFCGI_Connection *conn;

    conn = (WXFCGI_Connection *) WXMalloc(sizeof(WXFCGI_Connection));
    if (conn == NULL) return NULL;

    conn->sockConn = sockConn;
    conn->recordLength = 0;
    conn->request.requestId = WXFCGI_NULL_REQUEST_ID;
    conn->request.params = NULL;
    conn->request.stdin = NULL;
    conn->request.stdinLen = 0;

    /* Always assume some responses */
    conn->outBuffer = (uint8_t *) WXMalloc(conn->outAllocLen = 1024);
    if (conn->outBuffer == NULL) {
        WXFree(conn);
        return NULL;
    }
    conn->outLen = 0;

    return conn;
}

/* Release a list of name-value pairs */
static void flushNVP(WXFCGI_NameValuePair *pair) {
    WXFCGI_NameValuePair *np;

    while (pair != NULL) {
        np = pair->next;
        if (pair->name != NULL) WXFree(pair->name);
        if (pair->value != NULL) WXFree(pair->value);
        WXFree(pair);
        pair = np;
    }
}

/**
 * Based on an input available signal, perform a read of the given connection.
 *
 * @param conn The connection instance to read from.
 * @return One of the WXNRC return codes.  Note that OK_WITH_DATA means an
 *         incoming FCGI request has been completed.
 */
int WXFCGI_Read(WXFCGI_Connection *conn) {
    WXFCGI_NameValuePair *param;
    WXFCGI_Request *request;
    int32_t nameLen, valLen;
    uint16_t requestId;
    uint8_t *ptr;
    int rc, len;

    /* Complete the header if needed */
    if (conn->recordLength <= 0) {
        rc = WXSocket_Recv(conn->sockConn,
                           ((uint8_t *) &(conn->header)) - conn->recordLength,
                           sizeof(WXFCGI_Header) + conn->recordLength, 0);
        if (rc < 0) return rc;
        conn->recordLength -= rc;
        if (conn->recordLength > - (int32_t) sizeof(WXFCGI_Header))
                                              return WXNRC_READ_REQUIRED;
        conn->recordLength = (((uint32_t) conn->header.contentLengthB1) << 8) |
                                                 conn->header.contentLengthB0;
        conn->recordOffset = 0;

#ifdef FCGI_DEBUG
        (void) fprintf(stdout, "*** FCGI Header Read\n");
        (void) fprintf(stdout, "    Version: %d Type: %d RequestId: %d\n",
                       conn->header.version, conn->header.type,
                       (((uint16_t) conn->header.requestIdB1) << 8) |
                                                 conn->header.requestIdB0);
        (void) fprintf(stdout, "    ContentLength: %d Padding: %d\n",
                       conn->recordLength, conn->header.paddingLength);
#endif
    }

    /* Note: empty parcels are used for end-of-stream indication */
    len = conn->recordLength + conn->header.paddingLength -
              conn->recordOffset;
    if (len != 0) {
        rc = WXSocket_Recv(conn->sockConn,
                           conn->recordBuffer + conn->recordOffset, len, 0);
        if (rc < 0) return rc;
        conn->recordOffset += rc;
        if (conn->recordOffset <
                    (conn->recordLength + conn->header.paddingLength))
                                                    return WXNRC_READ_REQUIRED;
    }

    /* TODO - handle management requests for a server that sends them */

    /* Process appropriately, easier without multiplexing */
    requestId = (((uint16_t) conn->header.requestIdB1) << 8) |
                                             conn->header.requestIdB0;
    if ((requestId != conn->request.requestId) ||
            (conn->request.phase == WXFCGI_PHASE_RESP_DONE)) {
        if (conn->header.type != WXFCGI_BEGIN_REQUEST) {
            return WXNRC_DATA_ERROR;
        }
        conn->request.requestId = requestId;
        /* Do we really care about the role, it can only be RESPONDER */
        conn->request.flags =
                 ((WXFCGI_BeginRequestBody *) conn->recordBuffer)->flags;
        conn->request.phase = WXFCGI_PHASE_BEGIN;
        if (conn->request.params != NULL) flushNVP(conn->request.params);
        conn->request.params = NULL;
        if (conn->request.stdin != NULL) WXFree(conn->request.stdin);
        conn->request.stdin = NULL;
        conn->request.stdinLen = 0;
    } else if (conn->header.type == WXFCGI_PARAMS) {
        if ((conn->request.phase != WXFCGI_PHASE_BEGIN) &&
                (conn->request.phase != WXFCGI_PHASE_PARAMS)) {
            return WXNRC_DATA_ERROR;
        }
        if (conn->recordLength == 0) {
            ptr = conn->request.stdin;
            len = conn->request.stdinLen;
            while (len > 0) {
                if ((*ptr & 0x80) == 0) {
                    nameLen = *(ptr++);
                    len--;
                } else {
                    if (len < 4) break;
                    nameLen = (((int32_t) (*ptr & 0x7F)) << 24) |
                              (((int32_t) *(ptr + 1)) << 16) |
                              (((int32_t) *(ptr + 2)) << 8) |
                              ((int32_t) *(ptr + 3));
                    ptr += 4; len -= 4;
                }
                if ((*ptr & 0x80) == 0) {
                    valLen = *(ptr++);
                    len--;
                } else {
                    if (len < 4) break;
                    valLen = (((int32_t) (*ptr & 0x7F)) << 24) |
                             (((int32_t) *(ptr + 1)) << 16) |
                             (((int32_t) *(ptr + 2)) << 8) |
                             ((int32_t) *(ptr + 3));
                    ptr += 4; len -= 4;
                }
                if ((nameLen + valLen) > len) break;

                param = (WXFCGI_NameValuePair *)
                                WXCalloc(sizeof(WXFCGI_NameValuePair));
                if (param == NULL) return WXNRC_MEM_ERROR;

                param->name = (uint8_t *) WXMalloc(nameLen + 1);
                if (param->name == NULL) {
                    WXFree(param);
                    return WXNRC_MEM_ERROR;
                }
                (void) memcpy(param->name, ptr, nameLen);
                param->name[nameLen] = '\0';

                param->value = (uint8_t *) WXMalloc(valLen + 1);
                if (param->value == NULL) {
                    WXFree(param->name);
                    WXFree(param);
                    return WXNRC_MEM_ERROR;
                }
                (void) memcpy(param->value, ptr + nameLen, valLen);
                param->value[valLen] = '\0';

                param->next = conn->request.params;
                conn->request.params = param;

                ptr += nameLen + valLen; len -= nameLen + valLen;
            }
            conn->request.phase = WXFCGI_PHASE_PARAMS_DONE;

            if (conn->request.stdin != NULL) WXFree(conn->request.stdin);
            conn->request.stdin = NULL;
            conn->request.stdinLen = 0;
        } else {
            if (conn->request.stdin == NULL) {
                conn->request.stdin = (uint8_t *) WXMalloc(conn->recordLength);
                if (conn->request.stdin == NULL) return WXNRC_MEM_ERROR;
                (void) memcpy(conn->request.stdin, conn->recordBuffer,
                              conn->recordLength);
                conn->request.stdinLen = conn->recordLength;
            } else {
                conn->request.stdin =
                    (uint8_t *) WXRealloc(conn->request.stdin,
                                          conn->request.stdinLen +
                                                          conn->recordLength);
                if (conn->request.stdin == NULL) return WXNRC_MEM_ERROR;
                (void) memcpy(conn->request.stdin,
                              conn->recordBuffer + conn->request.stdinLen,
                              conn->recordLength);
                conn->request.stdinLen += conn->recordLength;
            }
            conn->request.phase = WXFCGI_PHASE_PARAMS;
        }
    } else if (conn->header.type == WXFCGI_STDIN) {
        if ((conn->request.phase != WXFCGI_PHASE_BEGIN) &&
                (conn->request.phase != WXFCGI_PHASE_PARAMS_DONE) &&
                (conn->request.phase != WXFCGI_PHASE_STDIN)) {
            return WXNRC_DATA_ERROR;
        }
        if (conn->recordLength == 0) {
            conn->request.phase = WXFCGI_PHASE_REQ_DONE;
        } else {
            if (conn->request.stdin == NULL) {
                conn->request.stdin = (uint8_t *) WXMalloc(conn->recordLength);
                if (conn->request.stdin == NULL) return WXNRC_MEM_ERROR;
                (void) memcpy(conn->request.stdin, conn->recordBuffer,
                              conn->recordLength);
                conn->request.stdinLen = conn->recordLength;
            } else {
                conn->request.stdin =
                    (uint8_t *) WXRealloc(conn->request.stdin,
                                          conn->request.stdinLen +
                                                          conn->recordLength);
                if (conn->request.stdin == NULL) return WXNRC_MEM_ERROR;
                (void) memcpy(conn->request.stdin,
                              conn->recordBuffer + conn->request.stdinLen,
                              conn->recordLength);
                conn->request.stdinLen += conn->recordLength;
            }
            conn->request.phase = WXFCGI_PHASE_STDIN;
        }
    }

    conn->recordLength = 0;
    conn->recordOffset = 0;
    return (conn->request.phase == WXFCGI_PHASE_REQ_DONE) ?
                                       WXNRC_OK_WITH_DATA : WXNRC_READ_REQUIRED;
}

/* Common method to append to the outbound connection buffer */
static int appendOutput(WXFCGI_Connection *conn, void *data, uint32_t len) {
    uint32_t newLen;

    /* Might need a bit more buffer */
    if ((conn->outLen + len) > conn->outAllocLen - 8) {
        /* Leave a little extra for padding and the end stream/request bits */
        newLen = conn->outLen + len + 32;
        conn->outBuffer = (uint8_t *) WXRealloc(conn->outBuffer, newLen);
        if (conn->outBuffer == NULL) return FALSE;
        conn->outAllocLen = newLen;
    }

    (void) memcpy(conn->outBuffer + conn->outLen, data, len);
    conn->outLen += len;
    return TRUE;
}

#ifdef FCGI_DEBUG
static void dumpRespHeader(WXFCGI_Header *header) {
        (void) fprintf(stdout, "*** FCGI Response Header\n");
        (void) fprintf(stdout, "    Version: %d Type: %d RequestId: %d\n",
                       header->version, header->type,
                       (((uint16_t) header->requestIdB1) << 8) |
                                                 header->requestIdB0);
        (void) fprintf(stdout, "    ContentLength: %d Padding: %d\n",
                       (((uint32_t) header->contentLengthB1) << 8) |
                                                 header->contentLengthB0,
                       header->paddingLength);
}
#endif

/**
 * Write a response to a given request instance.  Encodes in the connection
 * outgoing buffer and attempts a write operation.
 *
 * @param conn The connection instance to write to.
 * @param requestId The identifier of the request being responded to.
 * @param isStdout TRUE if this is a standard output response message, FALSE
 *                 for a standard error response.
 * @param response The buffer of the response to be written.
 * @param length The number of bytes contained in the response, -1 for strlen.
 * @return Suitable WXNRC return codes, for memory or write operations.
 */
int WXFCGI_WriteResponse(WXFCGI_Connection *conn, uint16_t requestId,
                         int isStdout, uint8_t *response, int32_t length) {
    WXFCGI_Header header;
    uint16_t wlen;
    int pad;

    /* Set the common header elements */
    (void) memset(&header, 0, sizeof(header));
    header.version = WXFCGI_VERSION_1;
    header.type = (isStdout) ? WXFCGI_STDOUT : WXFCGI_STDERR;
    header.requestIdB1 = requestId >> 8;
    header.requestIdB0 = requestId & 0xFF;

    /* Might be distributed over multiple fragments */
    if (length < 0) length = strlen((char *) response);
    while (length > 0) {
        wlen = length;
        if (wlen > 65528) wlen = 65528;

        header.contentLengthB1 = wlen >> 8;
        header.contentLengthB0 = wlen & 0xFF;
        pad = 8 - (wlen & 0x07);
        if (pad == 8) pad = 0;
        header.paddingLength = pad;

#ifdef FCGI_DEBUG
        dumpRespHeader(&header);
#endif
        if (!appendOutput(conn, &header,
                          sizeof(header))) return WXNRC_MEM_ERROR;
        if (!appendOutput(conn, response, wlen)) return WXNRC_MEM_ERROR;
        if (pad != 0) {
            (void) memset(conn->outBuffer + conn->outLen, 0, pad);
            conn->outLen += pad;
        }

        response += wlen;
        length -= wlen;
    }

    /* End the stream */
    header.contentLengthB1 = header.contentLengthB0 = header.paddingLength = 0;
#ifdef FCGI_DEBUG
    dumpRespHeader(&header);
#endif
    (void) memcpy(conn->outBuffer + conn->outLen, &header, sizeof(header));
    conn->outLen += sizeof(header);

    /* Used to write but always ends with end request */
    return WXNRC_OK;
}

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
                           uint32_t appStatus, uint8_t protoStatus) {
    WXFCGI_EndRequestRecord content;

    /* Keep valgrind happy */
    (void) memset(&content, 0, sizeof(content));

    content.header.version = WXFCGI_VERSION_1;
    content.header.type = WXFCGI_END_REQUEST;
    content.header.requestIdB1 = requestId >> 8;
    content.header.requestIdB0 = requestId & 0xFF;
    content.header.contentLengthB1 = 0;
    content.header.contentLengthB0 = 8;
    content.header.paddingLength = 0;
#ifdef FCGI_DEBUG
    dumpRespHeader(&content.header);
#endif

    content.body.appStatusB3 = appStatus >> 24;
    content.body.appStatusB2 = (appStatus >> 16) & 0xFF;
    content.body.appStatusB1 = (appStatus >> 8) & 0xFF;
    content.body.appStatusB0 = appStatus & 0xFF;
    content.body.protocolStatus = protoStatus;

    if (!appendOutput(conn, &content, sizeof(content))) return WXNRC_MEM_ERROR;
    conn->request.phase = WXFCGI_PHASE_RESP_DONE;
    return WXFCGI_Write(conn);
}

/**
 * Counterpart to above, when there is enqueued response data for the 
 * connection.
 *
 * @param conn The connection instance to write from.
 * @return One of the WXNRC return codes, typically either OK or WRITE_REQUIRED.
 */
int WXFCGI_Write(WXFCGI_Connection *conn) {
    int rc;

    rc = WXSocket_Send(conn->sockConn, conn->outBuffer, conn->outLen, 0);
    if (rc < 0) return rc;

    if (rc == conn->outLen) {
        conn->outLen = 0;
    } else {
        (void) memmove(conn->outBuffer, conn->outBuffer + rc,
                       conn->outLen - rc);
        conn->outLen -= rc;
    }

    if (conn->outLen != 0) return WXNRC_WRITE_REQUIRED;
    if ((conn->request.phase == WXFCGI_PHASE_RESP_DONE) &&
                  (!(conn->request.flags & WXFCGI_KEEP_CONN)))
                                          return WXNRC_COMPLETE_CLOSE;
    return WXNRC_OK;
}

/**
 * Release an allocated FCGI connection instance and associated elements.
 *
 * @param conn The conn to be released.
 */
void WXFCGI_Release(WXFCGI_Connection *conn) {
    if (conn->request.params != NULL) flushNVP(conn->request.params);
    if (conn->request.stdin != NULL) WXFree(conn->request.stdin);
    if (conn->outBuffer != NULL) WXFree(conn->outBuffer);
    WXSocket_Close(conn->sockConn);
    WXFree(conn);
}
