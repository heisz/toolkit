/*
 * Generic array/list implementation for arbitrary data objects.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_ARRAY_H
#define WX_ARRAY_H 1

#include "mem.h"

/* Note: if you think the operators are modelled after Perl, you'd be correct */

/**
 * This is very similar to WXBuffer but instead of a byte-oriented packing
 * model, it uses a more traditional array allocation model supporting datatypes
 * and structures.  Callers can access the internal array with suitable cast
 * or use the modifier methods.
 */
typedef struct WXArray {
    /**
     * The allocated length of the array content.  If negative, the buffer
     * is locally allocated (of the given size).  Note that this is measured
     * in array object units, not bytes.
     */
    ssize_t allocLength;

    /**
     * The current number of object records in the array.
     */
    size_t length;

    /**
     * The incremental byte size of each object in the array (sizeof).
     */
    size_t objectSize;

    /**
     * The actual array content.  Note that this can be a globally allocated
     * memory segment or a stack block, depending on initialization (determined
     * by the sign of the allocLength value).
     */
    void *array;
} WXArray;

/**
 * Initialize the provided array instance to the indicated size (allocated
 * memory).
 *
 * @param array The array instance to be initialized.
 * @param type Type of object to be stored in the array, to determine sizing.
 * @param size Number of elements to preallocate in the array.  Note that a
 *             size of zero is allowed but will not allocate the array.
 * @return Reference to the internal array if successfully allocated or
 *         NULL on a memory allocation failure (if size is non-zero).
 */
void *_WXArray_Init(WXArray *array, size_t _objSize, size_t size);
#define WXArray_Init(array, type, size) _WXArray_Init(array, sizeof(type), size)

/**
 * Initialize the provided array instance using a local (alloca) or static
 * data block for the storage.  If required, a content change will dynamically
 * allocate a new data block so the WXArray_Destroy() method should be used
 * to ensure cleanup.
 *
 * @param array The array instance to be initialized.
 * @param type Type of object to be stored in the array, to determine sizing.
 * @param data Reference to the local memory block to use for initialization.
 * @param size The total size (in number of objects, not bytes) of the data
 *             block.
 */
void _WXArray_InitLocal(WXArray *array, size_t _objSize, void *data,
                        size_t size);
#define WXArray_InitLocal(array, type, data, size) \
                  _WXArray_InitLocal(array, sizeof(type), data, size)

/**
 * Reset/empty the contents of the provided array (convenience function).
 * Resets the length/offset as though it were a newly allocated instance.
 *
 * @param array The array instance to be initialized.  Note that this will not
 *              release any internal references.
 */
void WXArray_Empty(WXArray *array);

/**
 * Push an object onto the end of the array instance, expanding the internal
 * array as needed.
 *
 * @param array The array to push the object onto.
 * @param object The opaque object instance to push onto the array.
 * @return Reference to the internal array if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
void *WXArray_Push(WXArray *array, void *object);

/**
 * Pull the last object from the array, reducing the array size by one.
 *
 * @param array The array to pop the last value from.
 * @param object Reference to an externally provided memory block to populate
 *               the popped object into.
 * @return The last object in the array (reference to the provided storage
 *         block) or NULL if the array is empty.
 */
void *WXArray_Pop(WXArray *array, void *object);

/**
 * Insert an object into the beginning of the array instance, expanding the
 * internal array as needed.
 *
 * @param array The array to insert the object into.
 * @param object The opaque object instance to insert into the array.
 * @return Reference to the internal array if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
void *WXArray_Unshift(WXArray *array, void *object);

/**
 * Pull the first object from the array, reducing the array size by one.
 *
 * @param array The array to shift the first value from.
 * @param object Reference to an externally provided memory block to populate
 *               the shifted object into.
 * @return The first object in the array (refernce to the provided storage
 *         block) or NULL if the array is empty.
 */
void *WXArray_Shift(WXArray *array, void *object);

/**
 * Callback method for enumerating entries within an array.  Allows for
 * termination of the scanning through the return code.  
 *
 * @param array The array which is currently being scanned.
 * @param object The object associated with the currently scanned entry.
 * @param userData The caller provided information attached to the scan request.
 * @return Zero if scan is to continue, non-zero if scan is to be terminated
 *         (error or matching complete, value is returned from scan).
 */
typedef int (*WXArrayEntryScanCB)(WXArray *array, void *object,
                                  void *userData);

/**
 * Scan through all entries in an array, calling the specified callback
 * function for each valid array entry.  Really this could just be done using
 * an external for loop but retain for consistency with hashscan.
 *
 * @param array The array instance to be scanned.
 * @param entryCB A function reference which is called for each array entry.
 * @param userData A caller provided data object which is included in the
 *                 scan callback arguments.
 * @return Zero if the scan was completed, any other value indicates the scan
 *         was interrupted by the callback with the given value.
 */
int WXArray_Scan(WXArray *array, WXArrayEntryScanCB entryCB, void *userData);

/**
 * Destroy the internals of the array instance.  This does not free the array
 * structure itself (only the allocated content), nor does it do anything with
 * the object instances stored in the array.
 *
 * @param array The array instance to destroy.
 */
void WXArray_Destroy(WXArray *table);

#endif
