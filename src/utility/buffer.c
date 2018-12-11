/*
 * Dynamic/rolling memory buffer, which supports stream-like data processing.
 *
 * Copyright (C) 1999-2018 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "buffer.h"

#include <errno.h>

/**
 * Initialize the provided buffer instance to the indicated size (allocated
 * memory).
 *
 * @param buffer The buffer instance to be initialized.
 * @param size The number of bytes to be preallocated into the buffer.  Note 
 *             that a size of zero will generate an empty buffer.
 * @return Reference to the internal buffer if successfully allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Init(WXBuffer *buffer, size_t size) {
    if (size > 0) {
        buffer->buffer = (uint8_t *) WXMalloc(size);
        if (buffer->buffer == NULL) return NULL;
    } else {
        buffer->buffer = NULL;
    }
    buffer->length = buffer->offset = 0;
    buffer->allocLength = (ssize_t) size;

    return buffer->buffer;
}

/**
 * Initialize the provided buffer instance using a local (alloca) or static
 * data block for the storage.  If required, a data resize will dynamically
 * allocate a new data block so the WXBuffer_Destroy() method should be used
 * to ensure cleanup.
 *
 * @param buffer The buffer instance to be initialized.
 * @param data Reference to the local memory block to use for initialization.
 * @param size The total size (in bytes) of the data block.
 */
void WXBuffer_InitLocal(WXBuffer *buffer, uint8_t *data, size_t size) {
    buffer->buffer = data;
    buffer->length = buffer->offset = 0;
    buffer->allocLength = -((ssize_t) size);
}

/**
 * Reset/empty the contents of the provided buffer (convenience function).
 * Resets the length/offset as though it were a newly allocated instance.
 *
 * @param buffer The buffer instance to be emptied.
 */
void WXBuffer_Empty(WXBuffer *buffer) {
    buffer->length = buffer->offset = 0;
}

/**
 * Resize the buffer if necessary to ensure that the required capacity is
 * available.
 *
 * @param buffer The buffer instance to be (potentially) resized.
 * @param capacity The number of bytes that need to be made available.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_EnsureCapacity(WXBuffer *buffer, size_t capacity,
                                 int consume) {
    size_t reqLength = buffer->length + capacity;
    ssize_t allocLength = buffer->allocLength;
    uint8_t *newBuffer;

    if (allocLength < 0) allocLength = -allocLength;
    if (reqLength > allocLength) {
        /* Attempt to consume first, if allowed */
        if ((consume) && (buffer->offset != 0)) {
            buffer->length -= buffer->offset;
            (void) memmove(buffer->buffer,
                           buffer->buffer + buffer->offset,
                           buffer->length);
            reqLength -= buffer->offset;
            buffer->offset = 0;
        }

        /* Still need more room? */
        if (reqLength > allocLength) {
            allocLength <<= 1;
            if (reqLength > allocLength) allocLength = reqLength + 1;
            newBuffer = (uint8_t *) WXMalloc(allocLength);
            if (newBuffer == NULL) return NULL;
            if (buffer->length != 0) {
                if (buffer->buffer != NULL) {
                    (void) memcpy(newBuffer, buffer->buffer, buffer->length);
                }
            }
            if (buffer->allocLength >= 0) {
                if (buffer->buffer != NULL) {
                    WXFree(buffer->buffer);
                }
            }
            buffer->buffer = newBuffer;
            buffer->allocLength = allocLength;
        }
    }

    return buffer->buffer;
}

/**
 * Append a block of binary data to the contents of the buffer, expanding the
 * internal buffer as necessary.
 *
 * @param buffer The buffer instance to append to.
 * @param data Reference to the block of binary data to be appended.
 * @param length Length (in bytes) of the block to append.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Append(WXBuffer *buffer, uint8_t *data, size_t length,
                         int consume) {
    if (WXBuffer_EnsureCapacity(buffer, length, consume) == NULL) return NULL;
    (void) memcpy(buffer->buffer + buffer->length, data, length);
    buffer->length += length;
    return buffer->buffer;
}

/**
 * Append another buffer's content to the specified buffer, expanding the
 * internal buffer as necessary.
 *
 * @param buffer The buffer instance to append to.
 * @param data The buffer to append the contents of.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_AppendBuffer(WXBuffer *buffer, WXBuffer *source,
                               int consume) {
    uint64_t len = source->length;
    if (WXBuffer_EnsureCapacity(buffer, len, consume) == NULL) return NULL;
    (void) memcpy(buffer->buffer + buffer->length, source->buffer, len);
    buffer->length += len;
    return buffer->buffer;
}

/**
 * Print a formatted string into the buffer, resizing the buffer as required.
 *
 * @param buffer The buffer instance to print into.
 * @param format The standard printf format string.
 * @param ... The argument set for the printf, according to the format.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Printf(WXBuffer *buffer, const char *format, ...) {
    ssize_t reqSize;
    va_list ap;
    int len;

    /* WAG, select double format length for initial target buffer size */
    reqSize = (strlen(format) << 1) + 1;

    while (reqSize > 0) {
        /* Create buffer space for target */
        if (WXBuffer_EnsureCapacity(buffer, reqSize,
                                    FALSE) == NULL) return NULL;

        /* Try to print in the allocated space */
        va_start(ap, format);
        len = vsnprintf(buffer->buffer + buffer->length, reqSize, format, ap);
        va_end(ap);

        /* If conversion was not truncated, all complete */
        if ((len >= 0) && (len < reqSize)) {
            buffer->length += len;
            return buffer->buffer;
        }

        /* Otherwise, determine appropriate size */
        if (len >= 0) {
            /* Standard form, vsnprintf returned actual number of characters */
            reqSize = len + 1;
        } else {
            /* Older printf model, exact size unknown, double until failure */
            reqSize <<= 1;
        } 
    }

    /* (Unexpected) overflow condition, indicate failure */
    return NULL;
}

/**
 * Read the contents of the provided file descriptor into the buffer
 * (appended to the end of the buffer).
 *
 * @param buffer The buffer instance to read into.
 * @param fd The file descriptor to read from.
 * @param length The (maximum) number of bytes to read from the file, zero
 *               to read the remainder.
 * @return The number of bytes read from the file, -1 on error (partial 
 *         contents may be read).
 */
ssize_t WXBuffer_ReadFile(WXBuffer *buffer, int fd, size_t length) {
    ssize_t len, count = 0, block = 8192;
    uint8_t *ptr;

    if (length == 0) length = (size_t) -1;
    while (length > 0) {
        /* Attempt to allocate and read a (remaining) chunk */
        if (length < block) block = length;
        if (WXBuffer_EnsureCapacity(buffer, block, TRUE) == NULL) return -1;
        ptr = buffer->buffer + buffer->length;
#ifdef WIN32
        len = _read(fd, ptr, block);
#else
        len = read(fd, ptr, block);
#endif

        /* Handle the result appropriately */
        if (len < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) continue;
            return -1;
        }
        if (len == 0) break;
        count += len;
        buffer->length += len;
        length -= len;
    }

    return count;
}

/**
 * Write the contents of the buffer to the provided file descriptor, starting
 * from the current buffer offset (which will be adjusted accordingly).
 *
 * @param buffer The buffer instance to write from.
 * @param fd The file descriptor to read to.
 * @return The number of bytes written to the file, -1 on error (partial
 *         contents may be read).
 */
ssize_t WXBuffer_WriteFile(WXBuffer *buffer, int fd) {
    ssize_t len, count = 0, length = buffer->length - buffer->offset;
    uint8_t *ptr = buffer->buffer + buffer->offset;

    while (length > 0) {
        /* Attempt/issue the remainder write */
#ifdef WIN32
        len = _write(fd, ptr, length);
#else
        len = write(fd, ptr, length);
#endif

        /* Handle the result appropriately */
        if (len < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) continue;
            return -1;
        }
        count += len;
        buffer->offset += len;
        length -= len;
    }

    return count;
}

/**
 * Destroy (free) the contents of the provided buffer.  This does not free
 * the buffer structure itself, only the allocated content.  For locally
 * allocated buffers, only dereferences the local storage block.
 *
 * @param buffer The buffer instance to destroy.
 */
void WXBuffer_Destroy(WXBuffer *buffer) {
    if (buffer->allocLength >= 0) {
        WXFree(buffer->buffer);
    }
    buffer->buffer = NULL;
    buffer->allocLength = 0;
    buffer->offset = buffer->length = 0;
}
