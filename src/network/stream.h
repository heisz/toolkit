/*
 * Structures and methods for supporting content streaming on sockets.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_SOCKSTREAM_H
#define WX_SOCKSTREAM_H 1

/* Grab the standard definitions */
#include "socket.h"
#include "buffer.h"

/* Forward declaration for recursive reference */
typedef struct WXSocketStream WXSocketStream;

/**
 * Declaration of the indirect content read/write prototype for processing raw
 * socket data encoding for the stream instance below.  Note that the same
 * prototype is used for the two flow directions (but treat the buffers
 * differently).
 *
 * @param strm The socket stream instance to read/write content from.
 * @return WXNRC_OK_WITH_DATA if read has occurred (buffer is updated),
 *         WXNRC_OK for write completion, one of the WXNRC_* error codes on
 *         error.  For non-blocking streams, can also return either READ or
 *         WRITE_REQUIRED, depending on stream status (note that the raw
 *         protocol could generate a write from a read and vice-versa).
 */
typedef int (*WXSocketStream_Encoder)(WXSocketStream *strm);

/**
 * A standard wrapper for the bare socket instance, the stream instance
 * encapsulates optimized reading and writing operations to the socket for use
 * in higher-level protocol constructs.  The stream also includes extensibility
 * for use with raw encoding systems like wire encryption.
 */
struct WXSocketStream {
    /* The underlying 'native' socket instance */
    WXSocket socketHandle;

    /* Tracking detail for the last error/return that occurred on this stream */
    int lastErrNo, lastRespCode;

    /* The raw content reader and writer - if NULL, direct pass-through */
    WXSocketStream_Encoder rawReader, rawWriter;

    /* Buffering elements for inbound and outbound messaging */
    /* Note that locking must be managed by external handlers */
    WXBuffer readBuffer, writeBuffer;
};

/**
 * Wrapper to access the last error number related to the specified stream.
 * If NULL, grabs the last global socket error number, which is constrained
 * to the threading/timing dependencies of the standard errno.
 *
 * @param strm The socket stream instance to get the last error for (or NULL).
 * @return Associated last system error number for the specified stream or the
 *         global error number.
 */
int WXSockStream_GetLastErrNo(WXSocketStream *strm);

/**
 * Matching wrapper to obtain the last status/response code for the stream.
 * This may be particularly of interest in the read/write cases where an
 * underlying raw processor is returning read/write required responses.  Will
 * return an error if a NULL stream is provided.
 *
 * @param strm The socket stream to get the last response code for.
 * @return The WXNRC_* response code for the last operation on the stream.
 */
int WXSockStream_GetLastRespCode(WXSocketStream *strm);

/**
 * Method corresponding to the above to set the last system error number and
 * operation response code.  If the specified stream is NULL, simply sets the
 * global error number (see note above).
 *
 * @param strm The socket stream instance to set the error on (or NULL).
 * @param respCode The associated response to record against the stream/global.
 * @return The provided response code value for simplified chaining.
 */
int WXSockStream_Response(WXSocketStream *strm, int respCode);

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
                      int32_t bufferSize);

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
int WXSockStream_Read(WXSocketStream *strm, size_t capacity);

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
int WXSockStream_Write(WXSocketStream *strm);

/**
 * Synchronously flush any pending outbound content for the indicated stream.
 *
 * @param strm The socket stream to flush write content for.
 * @return Either WXNRC_OK to indicated that all pending data has been flushed
 *         or a suitable WXNRC_* error code for failure.
 */
int WXSockStream_Flush(WXSocketStream *strm);

/**
 * Destroy a socket stream instance.  Releases internal resources of the stream
 * but does not related the instance itself.  Will automatically close the
 * associated socket instance as well for convenience.
 *
 * @param strm The stream to be destroyed.
 */
void WXSockStream_Destroy(WXSocketStream *strm);

/* TODO - merge WXPacketStream code here */

#endif
