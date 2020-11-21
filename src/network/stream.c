/*
 * Methods for supporting content streaming on sockets.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "stream.h"

#ifndef _WXWIN_BUILD
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#else
#include "Mstcpip.h"
#endif
#include <errno.h>

/* Secret method from the socket.c wrapper functions */
void _setSockErrNo(int errnum);

/**
 * Wrapper to access the last error number related to the specified stream.
 * If NULL, grabs the last global socket error number, which is constrained
 * to the threading/timing dependencies of the standard errno.
 *
 * @param strm The socket stream instance to get the last error for (or NULL).
 * @return Associated last system error number for the specified stream or the
 *         global error number.
 */
int WXSockStream_GetLastErrNo(WXSocketStream *strm) {
    if (strm != NULL) return strm->lastErrNo;
    return WXSocket_GetLastErrNo();
}

/**
 * Matching wrapper to obtain the last status/response code for the stream.
 * This may be particularly of interest in the read/write cases where an
 * underlying raw processor is returning read/write required responses.  Will
 * return an error if a NULL stream is provided.
 *
 * @param strm The socket stream to get the last response code for.
 * @return The WXNRC_* response code for the last operation on the stream.
 */
int WXSockStream_GetLastRespCode(WXSocketStream *strm) {
    if (strm != NULL) return strm->lastRespCode;
    return WXNRC_SYS_ERROR;
}

/**
 * Method corresponding to the above to set the last system error number and
 * operation response code.  If the specified stream is NULL, simply sets the
 * global error number (see note above).
 *
 * @param strm The socket stream instance to set the error on (or NULL).
 * @param respCode The associated response to record against the stream/global.
 * @return The provided response code value for simplified chaining.
 */
int WXSockStream_Response(WXSocketStream *strm, int respCode) {
    int errnum = 0;

    switch (respCode) {
        case WXNRC_TIMEOUT:
#ifdef _WXWIN_BUILD
            errnum = WSAETIMEDOUT;
#else
            errnum = ETIMEDOUT;
#endif
            break;
        case WXNRC_DISCONNECT:
#ifdef _WXWIN_BUILD
            errnum = WSACONNRESET;
#else
            errnum = ECONNRESET;
#endif
            break;
        case WXNRC_DATA_ERROR:
            errnum = EINVAL;
            break;
        case WXNRC_MEM_ERROR:
            errnum = ENOMEM;
            break;
        case WXNRC_SYS_ERROR:
            /* No really good errno for this one */
            errnum = EPERM;
            break;
        default:
            /* Everything else is not an error... */
            break;
    }

    if (strm != NULL) {
        strm->lastErrNo = errnum;
        strm->lastRespCode = respCode;
    }

    /* Presumption is that a successful action clears the system errno */
    if (errnum != 0) _setSockErrNo(errnum);

    return respCode;
}

/**
 * Initialize a socket stream instance.  Note that this does not allocate the
 * structure, only initializes the internal details (for use in embedded stream
 * handlers).  If raw stream processing methods are required, assign them after
 * calling this method.
 *
 * @param strm The socket stream instance to be initialized.
 * @param socketHandle The associated socket instance to stream against.
 * @param bufferSize The initial buffer size to allocate for the inbound and
 *                   outbound message streams.  If negative, use default.
 * @return WXNRC_OK if successful, suitable WXNRC_* error code on failure.
 */
int WXSockStream_Init(WXSocketStream *strm, WXSocket socketHandle,
                      int32_t bufferSize) {
    int noDelay = 1;

    /* Store the socket at the end (successful initialization) */
    strm->socketHandle = INVALID_SOCKET_FD;

    /* No handlers, manually set by caller after this method */
    strm->rawReader = strm->rawWriter = NULL;

    /* Safely initialize the buffers so that cleanup can always run */
    WXBuffer_Init(&(strm->readBuffer), 0);
    WXBuffer_Init(&(strm->writeBuffer), 0);

    if (bufferSize < 0) bufferSize = 2048;
    if (WXBuffer_Init(&(strm->readBuffer), bufferSize) == NULL) {
        return WXSockStream_Response(strm, WXNRC_MEM_ERROR);
    }
    if (WXBuffer_Init(&(strm->writeBuffer), bufferSize) == NULL) {
        return WXSockStream_Response(strm, WXNRC_MEM_ERROR);
    }

    /* For streaming (variant packets), disable the Nagle algorithm */
#ifdef _WXWIN_BUILD
    if (setsockopt((SOCKET) socketHandle, IPPROTO_TCP, TCP_NODELAY,
#else
    if (setsockopt((int) socketHandle, IPPROTO_TCP, TCP_NODELAY,
#endif
                   (char *) &noDelay, sizeof(noDelay)) < 0) {
        strm->lastErrNo = WXSocket_GetLastErrNo();
        return (strm->lastRespCode = WXNRC_SYS_ERROR);
    } 
    strm->socketHandle = socketHandle;

    return WXSockStream_Response(strm, WXNRC_OK);
}

/**
 * Read more content into the read buffer, consuming any offset and expanding
 * the buffer according to the desired amount of content (total).  Consumers
 * should manipulate the associated buffer contents directly.
 *
 * @param strm The socket stream to read more content for.
 * @param capacity The desired/target number of bytes to buffer/read from the
 *                 stream in total (includes existing unconsumed content).  Note
 *                 that the stream can/will read more if available.
 * @return A suitable WXNRC_* response code based on the outcome of the read
 *         operation, OK_WITH_DATA indicating that more data was read into the
 *         buffer.  Note that READ and/or WRITE_REQUIRED will be returned for
 *         non-blocking sockets, the latter indicating an underlying raw
 *         protocol exchange being required.
 */
int WXSockStream_Read(WXSocketStream *strm, size_t capacity) {
    WXBuffer *rd = &(strm->readBuffer);
    uint8_t *buffer;
    size_t len;
    ssize_t l;
    int rc;

    /* Like ensure capacity but force consumption and size by total */
    if (rd->offset != 0) {
        rd->length -= rd->offset;
        (void) memmove(rd->buffer, rd->buffer + rd->offset, rd->length);
        rd->offset = 0;
    }
    if ((capacity > rd->allocLength ) || (rd->length >= rd->allocLength)) {
        /* Note: second condition is to ensure we actually read something */
        len = (rd->allocLength << 1);
        if (capacity > len) len = capacity;
        buffer = (uint8_t *) WXMalloc(len);
        if (buffer == NULL) return WXSockStream_Response(strm, WXNRC_MEM_ERROR);
        if (rd->length != 0) (void) memcpy(buffer, rd->buffer, rd->length);
        WXFree(rd->buffer);
        rd->buffer = buffer;
        rd->allocLength = len;
    }

    /* Direct appropriately for read handling */
    if (strm->rawReader == NULL) {
        l = WXSocket_Recv(strm->socketHandle, rd->buffer + rd->length,
                          rd->allocLength - rd->length, 0);
        if (l > 0) {
            rd->length += l;
            rc = WXNRC_OK_WITH_DATA;
        } else if (l == 0) {
            rc = WXNRC_READ_REQUIRED;
        } else {
            rc = (int) l;
        }
    } else {
        rc = (*(strm->rawReader))(strm);
    }

    return rc;
}

/**
 * Write more buffered outbound content to the underlying socket, updating the
 * buffer accordingly.  Producers should have already staged content in the
 * write buffer instance, although it will not be an error to issue a write
 * with no content to be written.
 *
 * @param strm The socket stream to write more content for.
 * @return A suitable WXNRC_* response code based on the outcome of the write
 *         operation, OK indicating that all pending data was written from the
 *         buffer.  Note that WRITE and/or READ_REQUIRED will be returned for
 *         non-blocking sockets, the latter indicating an underlying raw
 *         protocol exchange being required.
 */
int WXSockStream_Write(WXSocketStream *strm) {
    WXBuffer *wr = &(strm->writeBuffer);
    ssize_t l;
    int rc;

    if (wr->offset >= wr->length) return WXNRC_OK;

    if (strm->rawWriter == NULL) {
        l = WXSocket_Send(strm->socketHandle, wr->buffer + wr->offset,
                          wr->length - wr->offset, 0);
        if (l > 0) {
            wr->offset += l;
            if (wr->offset >= wr->length) {
                /* All written, reset the buffer */
                wr->length = wr->offset = 0;
                rc = WXNRC_OK;
            } else {
                /* Still more to go */
                rc = WXNRC_WRITE_REQUIRED;
            }
        } else if (l == 0) {
            rc = WXNRC_WRITE_REQUIRED;
        } else {
            rc = (int) l;
        }
    } else {
        rc = (*(strm->rawWriter))(strm);

        /* Save work in commons, flush stream on writer's behalf */
        if (wr->offset >= wr->length) {
            wr->length = wr->offset = 0;
        }
    }

    return rc;
}

/**
 * Synchronously flush any pending outbound content for the indicated stream.
 *
 * @param strm The socket stream to flush write content for.
 * @return Either WXNRC_OK to indicated that all pending data has been flushed
 *         or a suitable WXNRC_* error code for failure.
 */
int WXSockStream_Flush(WXSocketStream *strm) {
    int rc = WXNRC_WRITE_REQUIRED;

    while (rc != WXNRC_OK) {
        /* Even though last return may be read, raw handler keeps track */
        rc = WXSockStream_Write(strm);
        if (rc < 0) return rc;

        if (rc != WXNRC_OK) {
            rc = WXSocket_Wait(strm->socketHandle, rc, NULL);
            if (rc < 0) return rc;
        }
    }

    return rc;
}

/**
 * Destroy a socket stream instance.  Releases internal resources of the stream
 * but does not related the instance itself.  Will automatically close the
 * associated socket instance as well for convenience.
 *
 * @param strm The stream to be destroyed.
 */
void WXSockStream_Destroy(WXSocketStream *strm) {
    /* Flush the buffers, note that create is done to support error cleanup  */
    WXBuffer_Destroy(&(strm->readBuffer));
    WXBuffer_Destroy(&(strm->writeBuffer));

    /* Nuke and clear the socket */
    if (strm->socketHandle != INVALID_SOCKET_FD) {
        WXSocket_Close(strm->socketHandle);
        strm->socketHandle = INVALID_SOCKET_FD;
    }
}
