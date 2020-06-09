/*
 * Dynamic/rolling memory buffer, which supports stream-like data processing.
 *
 * Copyright (C) 1999-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_BUFFER_H
#define WX_BUFFER_H 1

/* Grab the standard definitions plus varargs support */
#include "mem.h"
#include <stdarg.h>

/**
 * Structural definition of a memory buffer that supports a streaming data
 * model (append and consume).  Handles both globally allocated memory or
 * local (stack) allocated storage.
 */
typedef struct WXBuffer {
    /**
     * The allocated length of the buffer content.  If negative, the buffer
     * is locally allocated (of the given size).
     */
    ssize_t allocLength;

    /**
     * The current amount of data available in the buffer.
     */
    size_t length;

    /**
     * For streaming processors, the offset into the front of the buffer
     * for the next byte being read.
     */
    size_t offset;

    /**
     * The actual dynamic buffer content.  Note that this can be a globally
     * allocated memory segment or a stack block, depending on initialization
     * (determined by the sign of the allocLength value).
     */
    uint8_t *buffer;
} WXBuffer;

/**
 * Initialize the provided buffer instance to the indicated size (allocated
 * memory).
 *
 * @param buffer The buffer instance to be initialized.
 * @param size The number of bytes to be preallocated into the buffer.  Note
 *             that a size of zero is allowed but will not allocate a buffer.
 * @return Reference to the internal buffer if successfully allocated or
 *         NULL on a memory allocation failure (if size is non-zero).
 */
uint8_t *WXBuffer_Init(WXBuffer *buffer, size_t size);

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
void WXBuffer_InitLocal(WXBuffer *buffer, uint8_t *data, size_t size);

/**
 * Reset/empty the contents of the provided buffer (convenience function).
 * Resets the length/offset as though it were a newly allocated instance.
 *
 * @param buffer The buffer instance to be emptied.
 */
void WXBuffer_Empty(WXBuffer *buffer);

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
                                 int consume);

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
                         int consume);

/**
 * Append another buffer's content to the specified buffer, expanding the
 * internal buffer as necessary.
 *
 * @param buffer The buffer instance to append to.
 * @param source The buffer to append the contents of.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_AppendBuffer(WXBuffer *buffer, WXBuffer *source, int consume);

/**
 * Print a formatted string into the buffer, resizing the buffer as required.
 *
 * @param buffer The buffer instance to print into.
 * @param format The standard printf format string.
 * @param ... The argument set for the printf, according to the format.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Printf(WXBuffer *buffer, const char *format, ...) 
                                    __attribute__((format(__printf__, 2, 3)));

/**
 * Identical to the above, but print based on an explicit varargs instance.
 *
 * @param buffer The buffer instance to print into.
 * @param format The standard printf format string.
 * @param ap The allocated vararg instance.  Note that the state of this
 *           is indeterminant after the call.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_VPrintf(WXBuffer *buffer, const char *format, va_list ap);

/**
 * Pack a set of values into the buffer according to the (modified) Perl
 * binary pack format.  The packing mechanism recognizes the fixed patterns
 * 'aAbBhHcCsSlLqQnNvVxX', the <> modifiers (not !), the [] and *% length
 * notation and groups ().  Also recognizes z and Z for network and vax ordered
 * 64-bit unsigned values (like nN and vV).  And y and Y for base-128 varints of
 * 32-bit and 64-bit length respectively.
 *
 * @param buffer The buffer instance to pack into.
 * @param format The format to define the packing information.
 * @param ... The argument set for the pack, according to the format.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Pack(WXBuffer *buffer, const char *format, ...);

/**
 * Identical to the above, but pack based on an explicit varargs instance.
 *
 * @param buffer The buffer instance to pack into.
 * @param format The format to define the packing information.
 * @param ap The allocated vararg instance.  Note that the state of this
 *           is indeterminant after the call.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_VPack(WXBuffer *buffer, const char *format, va_list ap);

/**
 * Unpack a set of values into the buffer according to the (modified) Perl
 * binary pack format.  The packing mechanism recognizes the patterns
 * 'aAbBhHcCsSlLqQnNvVxX', the <> modifiers (not !), the [] and *% length
 * notation and groups (), along with the zZ and yY extensions as described
 * in the Pack() method.
 *
 * @param buffer The buffer instance to unpack from.
 * @param format The format to define the packing information.
 * @param ... The argument set for the unpack, according to the format.
 * @return Reference to the internal buffer if successfully parsed or
 *         NULL on a memory allocation failure or packing error.
 */
uint8_t *WXBuffer_Unpack(WXBuffer *buffer, const char *format, ...);

/**
 * Identical to the above, but unpack based on an explicit varargs instance.
 *
 * @param buffer The buffer instance to unpack from.
 * @param format The format to define the packing information.
 * @param ap The allocated vararg instance.  Note that the state of this
 *           is indeterminant after the call.
 * @return Reference to the internal buffer if successfully parsed or
 *         NULL on a memory allocation failure or packing error.
 */
uint8_t *WXBuffer_VUnpack(WXBuffer *buffer, const char *format, va_list ap);

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
ssize_t WXBuffer_ReadFile(WXBuffer *buffer, int fd, size_t length);

/**
 * Write the contents of the buffer to the provided file descriptor, starting
 * from the current buffer offset (which will be adjusted accordingly).
 *
 * @param buffer The buffer instance to write from.
 * @param fd The file descriptor to read to.
 * @return The number of bytes written to the file, -1 on error (partial
 *         contents may be written).
 */
ssize_t WXBuffer_WriteFile(WXBuffer *buffer, int fd);

/**
 * Destroy (free) the contents of the provided buffer.  This does not free
 * the buffer structure itself, only the allocated content.  For locally
 * allocated buffers, only dereferences the local storage block.
 *
 * @param buffer The buffer instance to destroy.
 */
void WXBuffer_Destroy(WXBuffer *buffer);

#endif
