/*
 * Generic array/list implementation for arbitrary data objects.
 *
 * Copyright (C) 1999-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "array.h"
#include "mem.h"

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
void *_WXArray_Init(WXArray *array, size_t _objSize, size_t size) {
    array->objectSize = _objSize;
    if (size > 0) {
        array->array = WXMalloc(size * _objSize);
        if (array->array == NULL) return NULL;
    } else {
        array->array = NULL;
    }
    array->allocLength = (ssize_t) size;
    array->length = 0;

    return array->array;
}

/**
 * Initialize the provided array instance using a local (alloca) or static
 * data block for the storage.  If required, a content change will dynamically
 * allocate a new data block so the WXArray_Destroy() method should be used
 * to ensure cleanup.
 *
 * @param array The array instance to be initialized.
 * @param _objSize Size of the object storage (defined by type-based macro).
 * @param data Reference to the local memory block to use for initialization.
 * @param size The total size (in number of objects, not bytes) of the data 
 *             block.
 */
void _WXArray_InitLocal(WXArray *array, size_t _objSize, void *data, 
                        size_t size) {
    array->objectSize = _objSize;
    array->allocLength = -((ssize_t) size);
    array->array = data;
    array->length = 0;
}

/**
 * Reset/empty the contents of the provided array (convenience function).
 * Resets the length/offset as though it were a newly allocated instance.
 *
 * @param array The array instance to be initialized.  Note that this will not
 *              release any internal references.
 */
void WXArray_Empty(WXArray *array) {
    /* In this case, the function is really just for readability... */
    array->length = 0;
}

/**
 * Internal method to verify capacity in the array.  Note that the capacity
 * is defined as available slots of the object size.
 */
static void *WXArray_EnsureCapacity(WXArray *array, size_t capacity) {
    size_t reqLength = array->length + capacity;
    size_t allocLength = (array->allocLength < 0) ? -array->allocLength :
                                                    array->allocLength;
    void *newArray;

    if (reqLength > allocLength) {
        /* TODO - look at the doubling algorithm in array and buffer... */
        allocLength <<= 1;
        if (reqLength > allocLength) allocLength = reqLength + 1;
        newArray = WXMalloc(allocLength * array->objectSize);
        if (newArray == NULL) return NULL;
        if (array->length != 0) {
            if (array->array != NULL) {
                (void) memcpy(newArray, array->array,
                              array->length * array->objectSize);
            }
        }
        if (array->allocLength >= 0) {
            if (array->array != NULL) {
                WXFree(array->array);
            }
        }
        array->array = newArray;
        array->allocLength = allocLength;
    }

    return array->array;
}

/**
 * Push an object onto the end of the array instance, expanding the internal
 * array as needed.
 *
 * @param array The array to push the object onto.
 * @param object The opaque object instance to push onto the array.
 * @return Reference to the pushed *record* on the internal array if
 *         successfully (re)allocated or NULL on a memory allocation failure.
 */
void *WXArray_Push(WXArray *array, void *object) {
    uint8_t *endPtr;

    if (WXArray_EnsureCapacity(array, 1) == NULL) return NULL;
    endPtr = ((uint8_t *) array->array) +
                              ((array->length++) * array->objectSize);
    (void) memcpy(endPtr, object, array->objectSize);
    return endPtr;
}

/**
 * Pull the last object from the array, reducing the array size by one.
 *
 * @param array The array to pop the last value from.
 * @param object Reference to an externally provided memory block to populate
 *               the popped object into.
 * @return The last object in the array (reference to the provided storage
 *         block) or NULL if the array is empty.
 */
void *WXArray_Pop(WXArray *array, void *object) {
    if (array->length == 0) return NULL;
    (void) memcpy(object, ((uint8_t *) array->array) +
                                     ((--array->length) * array->objectSize),
                  array->objectSize);
    return object;
}

/**
 * Insert an object into the beginning of the array instance, expanding the 
 * internal array as needed.
 *
 * @param array The array to insert the object into.
 * @param object The opaque object instance to insert into the array.
 * @return Reference to the internal array if successfully (re)allocated or
 *         NULL on a memory allocation failure.  Note that this is also a 
 *         a reference to the pushed object as well.
 */        
void *WXArray_Unshift(WXArray *array, void *object) {
    if (WXArray_EnsureCapacity(array, 1) == NULL) return NULL;
    (void) memmove(((uint8_t *) array->array) + array->objectSize,
                   array->array, (array->length++) * array->objectSize);
    (void) memcpy(array->array, object, array->objectSize);
    return array->array;
}

/**         
 * Pull the first object from the array, reducing the array size by one.
 *
 * @param array The array to shift the first value from.
 * @param object Reference to an externally provided memory block to populate
 *               the shifted object into.
 * @return The first object in the array (refernce to the provided storage
 *         block) or NULL if the array is empty.
 */
void *WXArray_Shift(WXArray *array, void *object) {
    if (array->length == 0) return NULL;
    (void) memcpy(object, array->array, array->objectSize);
    (void) memmove(array->array, ((uint8_t *) array->array) + array->objectSize,
                   (--array->length) * array->objectSize);
    return object;
}

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
int WXArray_Scan(WXArray *array, WXArrayEntryScanCB entryCB, void *userData) {
    uint8_t *ptr = (uint8_t *) array->array;
    unsigned int idx;
    int rc;

    for (idx = 0; idx < array->length; idx++) {
        rc = (*entryCB)(array, ptr, userData);
        if (rc != 0) return rc;
        ptr += array->objectSize;
    }

    return 0;
}

/**
 * Destroy the internals of the array instance.  This does not free the array
 * structure itself (only the allocated content), nor does it do anything with
 * the object instances stored in the array.
 *
 * @param array The array instance to destroy.
 */
void WXArray_Destroy(WXArray *array) {
    if (array->allocLength >= 0) {
        WXFree(array->array);
    }
    array->array = NULL;
    array->allocLength = 0;
    array->length = 0;
}
