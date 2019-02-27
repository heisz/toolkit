/*
 * Standard definitions and toolkits/wrappers for memory processing.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "mem.h"

/**
 * Common wrapper for memory allocation operations.  Allows for failure
 * debugging (automatic source wrapping) along with support for replacable
 * tracking/allocation mechanisms.
 *
 * @param size The number of bytes to be allocated from the heap.
 * @return Reference to the allocated block of memory or NULL if heap 
 *         allocation fails.
 */
void *_WXMalloc(size_t size, int line, char *file) {
    return malloc(size);
}

/**
 * Identical to WXMalloc except for the automatica initialization of the
 * memory to 0/null.  Not entirely consistent with calloc().
 * 
 * @param size The number of bytes to be allocated from the heap.
 * @return Reference to the allocated block of memory or NULL if heap
 *         allocation fails.
 */
void *_WXCalloc(size_t size, int line, char *file) {
    return calloc(1, size);
}

/**
 * Common wrapper for memory reallocation operations.  Allows for failure
 * debugging (automatic source wrapping) along with support for replacable
 * tracking/allocation mechanisms.
 *
 * @param original The originally allocated block of memory to be resized.
 * @param size The number of bytes to be allocated from the heap.
 * @return Reference to the reallocated block of memory or NULL if heap
 *         allocation fails.
 */
void *_WXRealloc(void *original, size_t size, int line, char *file) {
    return realloc(original, size);
}

/**
 * Common wrapper for freeing memory obtained through the WXAlloc or WXRealloc
 * methods above.  Allows for tracing and replacable allocation mechanisms.
 *
 * @param original The originally allocated block of memory to be freed.
 */
void _WXFree(void *original, int line, char *file) {
    free(original);
}
