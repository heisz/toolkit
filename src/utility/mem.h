/*
 * Standard definitions and toolkits/wrappers for memory processing.
 *
 * Copyright (C) 1999-2018 J.M. Heisz.  All Rights Reserved.
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
 */
#define WXMalloc(size) _WXMalloc(size, __LINE__, __FILE__)
uint8_t *_WXMalloc(size_t size, int line, char *file);

/**
 * Common wrapper for memory reallocation operations.  Allows for failure
 * debugging (automatic source wrapping) along with support for replacable
 * tracking/allocation mechanisms.
 *
 * @param original The originally allocated block of memory to be resized.
 * @param size The number of bytes to be allocated from the heap.
 */
#define WXRealloc(original, size) _WXRealloc(original, size, __LINE__, __FILE__)
uint8_t *_WXRealloc(uint8_t * original, size_t size, int line, char *file);

/**
 * Common wrapper for freeing memory obtained through the WXAlloc or WXRealloc
 * methods above.  Allows for tracing and replacable allocation mechanisms.
 *
 * @param original The originally allocated block of memory to be freed.
 */
#define WXFree(original) _WXFree(original, __LINE__, __FILE__);
void _WXFree(uint8_t *original, int line, char *file);

#endif
