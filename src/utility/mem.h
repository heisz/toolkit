/*
 * Standard definitions and toolkits/wrappers for memory processing.
 *
 * Copyright (C) 1999-2024 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_MEM_H
#define WX_MEM_H 1

/* Grab the standard definitions */
#include "stdconfig.h"

/**
 * Common wrapper for memory allocation operations.  Allows for failure
 * debugging (automatic source wrapping) along with support for replacable
 * tracking/allocation mechanisms.
 *
 * @param size The number of bytes to be allocated from the heap.
 * @return Reference to the allocated block of memory or NULL if heap
 *         allocation fails.
 */
#define WXMalloc(size) _WXMalloc(size, __LINE__, __FILE__)
void *_WXMalloc(size_t size, int line, char *file);

/**
 * Identical to WXMalloc except for the automatica initialization of the
 * memory to 0/null.  Not entirely consistent with calloc().
 *
 * @param size The number of bytes to be allocated from the heap.
 * @return Reference to the allocated block of memory or NULL if heap
 *         allocation fails.
 */
#define WXCalloc(size) _WXCalloc(size, __LINE__, __FILE__)
void *_WXCalloc(size_t size, int line, char *file);

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
#define WXRealloc(original, size) _WXRealloc(original, size, __LINE__, __FILE__)
void *_WXRealloc(void *original, size_t size, int line, char *file);

/**
 * Common wrapper for freeing memory obtained through the WXAlloc or WXRealloc
 * methods above.  Allows for tracing and replacable allocation mechanisms.
 *
 * @param original The originally allocated block of memory to be freed.
 */
#define WXFree(original) _WXFree(original, __LINE__, __FILE__)
void _WXFree(void *original, int line, char *file);

#endif
