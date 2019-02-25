/*
 * Generic hashtable implementation, with pluggable hashing algorithms.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "hash.h"
#include "mem.h"

/* Internal object to represent a hashtable entry */
struct WXHashEntry {
    /* The hashcode as computed from key, save recomputation */
    unsigned int hashCode;

    /* Associated key and object instances for the entry */
    void *key, *object;
};

/* Convenience macro for (re)allocation */
#define ENTRY_ALLOC(cnt) \
    (struct WXHashEntry *) WXMalloc((cnt) * sizeof(struct WXHashEntry))

/* Marker for filled but not occupied slots in the table */
static void *ResidualMarker = (void *) "xyzzy";

#define HASHSTART(table, index) ((index) & table->tableMask)
#define HASHJUMP(table, index) ((((index) % (table->tableMask - 2)) + 2) | 1)
#define HASHNEXT(table, index, jump) (((index) + (jump)) & table->tableMask)

/**
 * Initialize a hash table instance to the given number of base hash points.
 *
 * @param table Reference to an existing instance of the hashtable to be
 *              initialized (already existing entries in the table will not
 *              be cleaned up).
 * @param startSize The number of hash blocks to initially allocate in the
 *                  table.  If negative, the system default start size will be
 *                  selected.
 * @return TRUE (non-zero) if initialized, FALSE (zero) if memory error occured.
 */
int WXHash_InitTable(WXHashTable *table, int startSize) {
    unsigned int nByTwo = 1;

    /* Zero or minimal size, must be 2^n - 1 */
    if (startSize != 0) {
        if (startSize < 0x1F) startSize = 0x1F;
        while (nByTwo <= startSize) nByTwo = nByTwo << 1;
        startSize = nByTwo - 1;
    }

    /* Initialize the hash specific details */
    if (startSize != 0) {
        table->entries = ENTRY_ALLOC(startSize + 1);
        if (table->entries == NULL) return FALSE;
    } else {
        table->entries = NULL;
    }
    table->tableMask = startSize;
    table->entryCount = 0;
    table->occupied = 0;

    return TRUE;
}

/**
 * Destroy the internals of the hashtable instance.  This does not free the
 * hashtable structure itself (only the allocated content), nor does it do
 * anything with the key/object instances stored in the table.
 *
 * @param table The hashtable instance to destroy.
 */
void WXHash_Destroy(WXHashTable *table) {
    if (table->entries != NULL) WXFree(table->entries);
    table->entries = NULL;
    table->tableMask = 0;
    table->entryCount = 0;
    table->occupied = 0;
}

/**
 * Reset/empty the contents of the hashtable.  Resets the internal data as
 * if it were a newly allocated hashtable.
 *
 * @param table The hashtable instance to be emptied.  Note that this will not
 *              release any internal references.
 */
void WXHash_Empty(WXHashTable *table) {
    table->entryCount = 0;
    table->occupied = 0;
    if (table->entries != NULL) {
        (void) memset(table->entries, 0, table->tableMask + 1);
    }
}

/*
 * Internal routine to check table fill and expand if necessary.  Returns
 * TRUE or FALSE depending on allocation outcome.
 */
static int checkTableOccupancy(WXHashTable *table) {
    unsigned int idx, index, jump, origMask;
    struct WXHashEntry *newEntries, *entry = NULL;

    /* First time caller on an empty allocated hashtable */
    if (table->entries == NULL) {
        table->entries = ENTRY_ALLOC(0x20);
        if (table->entries == NULL) return FALSE;
        table->tableMask = 0x1F;
        return TRUE;
    }

    /* Check for less than optimal collision fill */
    if ((table->occupied + (table->occupied >> 1)) <= table->tableMask) {
        /* No need for expansion this time */
        return TRUE;
    }
    origMask = table->tableMask;
    table->tableMask = (table->tableMask << 1) | 1;

    /* Allocate and repopulate (flushes debris automatically) */
    newEntries = ENTRY_ALLOC(table->tableMask + 1);
    if (newEntries == NULL) {
        table->tableMask = origMask;
        return FALSE;
    }
    table->occupied = table->entryCount;
    for (idx = 0; idx <= origMask; idx++) {
         entry = &(table->entries[idx]);
         if ((entry->object != NULL) && (entry->object != ResidualMarker)) {
            index = HASHSTART(table, entry->hashCode);
            if (newEntries[index].object != NULL) {
                jump = HASHJUMP(table, entry->hashCode);
                do {
                    index = HASHNEXT(table, index, jump);
                } while (newEntries[index].object != NULL);
            }
            newEntries[index] = *entry;
        }
    }
    WXFree(table->entries);
    table->entries = newEntries;

    return TRUE;
}

/*
 * Core method to handle the two models of hashtable entry insertion
 * (replace or collide).  See methods below for more information and
 * parameter/return code descriptions.
 */
static int pushHashEntry(WXHashTable *table, void *key, void *object,
                         void **lastKey, void **lastObject,
                         WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn,
                         int replaceFlag) {
    unsigned int index, jump, hashCode = 0;
    struct WXHashEntry *entry = NULL;
    int firstResidualIndex;

    /* Deferred initialization on first entry */
    if (table->entries == NULL) {
        if (!checkTableOccupancy(table)) return FALSE;
    }

    /* First, find a slot to be used or replaced */
    firstResidualIndex = -1;
    hashCode = (*keyHashFn)(key);
    index = HASHSTART(table, hashCode);
    if ((entry = &(table->entries[index]))->object != NULL) {
        jump = HASHJUMP(table, hashCode);
        do {
            if (entry->object == ResidualMarker) {
                if (firstResidualIndex < 0) firstResidualIndex = index;
            } else {
                if ((entry->hashCode == hashCode) &&
                    ((*keyEqualsFn)(entry->key, key))) break;
            }
            index = HASHNEXT(table, index, jump);
        } while ((entry = &(table->entries[index]))->object != NULL);
    }

    if (entry->object == NULL) {
        /* Either insert here or replace a prior residual record placeholder */
        if (firstResidualIndex < 0) {
            table->occupied++;
            entry->hashCode = hashCode;
            entry->key = key;
            entry->object = object;
            if (!checkTableOccupancy(table)) {
                /* The tables are not modified if realloc failed */
                table->occupied--;
                entry->hashCode = 0;
                entry->key = entry->object = NULL;
                return FALSE;
            }
        } else {
            entry = &(table->entries[firstResidualIndex]);
            entry->hashCode = hashCode;
            entry->key = key;
            entry->object = object;
        }
        table->entryCount++;

        /* No collision here */
        if (lastKey != NULL) *lastKey = NULL;
        if (lastObject != NULL) *lastObject = NULL;
    } else {
        /* Collision, potentially replace an already existing hash entry */
        if (lastKey != NULL) *lastKey = entry->key;
        if (lastObject != NULL) *lastObject = entry->object;
        if (replaceFlag) {
            entry->key = key;
            entry->object = object;
        } else {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * Store an object into a hashtable.  Hashtable will expand as necessary,
 * and object will replace an already existing object with an equal key
 * according to the provided hash/comparison functions.  If an existing object
 * is replaced, the associated key/object instances are not destroyed but the
 * pair is returned to allow the caller to clean up.
 *
 * @param table The hashtable to put the key->value pair into.
 * @param key The key associated with the entry.
 * @param object The object to store in the hashtable according to the given
 *               key.  Note that this hashtable implementation cannot store
 *               NULL objects, use an external fixed/static object to represent
 *               NULL value storage.
 * @param lastKey If this reference is non-NULL, the previous key is returned
 *                if the put entry replaces one in the hashtable (NULL is
 *                returned if the entry is new).
 * @param lastObject If this reference is non-NULL, the previous object is
 *                   returned if the put entry replaces one in the hashtable
 *                   (NULL is returned if the entry is new).
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return TRUE if the insertion was successful, FALSE if a memory allocation
 *         failure occurred.
 */
int WXHash_PutEntry(WXHashTable *table, void *key, void *object,
                    void **lastKey, void **lastObject,
                    WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn) {
    return pushHashEntry(table, key, object, lastKey, lastObject,
                         keyHashFn, keyEqualsFn, TRUE);
}

/**
 * Almost identical to the PutEntry method, this method stores an object
 * entry into a hashtable unless there already exists an entry in the
 * hashtable with an "equal" key (i.e. this method will not replace already
 * existing hashtable entries where PutEntry does).
 *
 * @param table The hashtable to insert the key->value pair into.
 * @param key The key associated with the entry.
 * @param object The object to store in the hashtable according to the given
 *               key.  Note that this hashtable implementation cannot store
 *               NULL objects, use an external fixed/static object to represent
 *               NULL value storage.
 * @param lastKey If this reference is non-NULL, the existing key is returned
 *                if the insert did not happen (no replace) (NULL is
 *                returned if the entry is new or a memory error occurred).
 * @param lastObject If this reference is non-NULL, the existing object is
 *                   returned if the insert did not happen (no replace) (NULL is
 *                   returned if the entry is new or a memory error occurred).
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return TRUE if the insertion was successful, FALSE if a memory allocation
 *         failure occurred or an entry already existed for the given key (check
 *         the lastKey/lastObject references to differentiate).
 */
int WXHash_InsertEntry(WXHashTable *table, void *key, void *object,
                       void **lastKey, void **lastObject,
                       WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn) {
    return pushHashEntry(table, key, object, lastKey, lastObject,
                         keyHashFn, keyEqualsFn, FALSE);
}

/* Repeated method to find the entry (or the NULL next instance) */
static struct WXHashEntry *findEntry(WXHashTable *table, void *key,
                                     WXKeyHashFn keyHashFn,
                                     WXKeyEqualsFn keyEqualsFn) {
    unsigned int index, jump, hashCode;
    struct WXHashEntry *entry = NULL;

    /* See if we can find the record in question */
    if (table->entries == NULL) return NULL;
    hashCode = (*keyHashFn)(key);
    index = HASHSTART(table, hashCode);
    if ((entry = &(table->entries[index]))->object != NULL) {
        jump = HASHJUMP(table, hashCode);
        do {
            if ((entry->object != ResidualMarker) &&
                (entry->hashCode == hashCode) &&
                ((*keyEqualsFn)(entry->key, key))) break;
            index = HASHNEXT(table, index, jump);
        } while ((entry = &(table->entries[index]))->object != NULL);
    }

    /* Note, should never be NULL but core dump immediately if it is... */
    return entry;
}

/**
 * Remove an entry from the hashtable.  This does not destroy the removed
 * object/key, only the reference to them.  The original key/object pair
 * can be returned to the caller for cleanup purposes.  NOTE: this method
 * is safe to be called within the HashScan function, as it does not perform
 * hash compaction.
 *
 * @param table The hashtable to remove the entry from.
 * @param key The key of the pair entry to be removed.  Only has to match
 *            in equality, does not have to be the exact key instance.
 * @param origKey If this reference is non-NULL and an entry is removed, the
 *                original key of the removed entry is returned here.
 * @param origObject If this reference is non-NULL and an entry is removed, the  *                   object associated with the removed entry is returned here.
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return TRUE if an entry was found and removed, FALSE if key is not found.
 */
int WXHash_RemoveEntry(WXHashTable *table, void *key,
                       void **origKey, void **origObject,
                       WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn) {
    struct WXHashEntry *entry = findEntry(table, key, keyHashFn, keyEqualsFn);

    /* Process appropriately based on discovery */
    if ((entry != NULL) && (entry->object != NULL)) {
        if (origKey != NULL) *origKey = entry->key;
        if (origObject != NULL) *origObject = entry->object;

        entry->key = NULL;
        entry->object = ResidualMarker;
        table->entryCount--;
    } else {
        if (origKey != NULL) *origKey = NULL;
        if (origObject != NULL) *origObject = NULL;
        return FALSE;
    }

    return TRUE;
}

/**
 * Retrieve an object from the hashtable according to the specified key.
 *
 * @param table The hashtable to retrieve the entry from.
 * @param key The key of the object to be obtained
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return NULL if no object entry has a matching key, otherwise the matching
 *         object reference.
 */
void *WXHash_GetEntry(WXHashTable *table, void *key,
                      WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn) {
    /* Easy with common find */
    struct WXHashEntry *entry = findEntry(table, key, keyHashFn, keyEqualsFn);

    return (entry == NULL) ? NULL : entry->object;
}

/**
 * Similar to the HashGetEntry method, this retrieves entry information
 * for the provided key, but obtains both the object and the key associated
 * with the hashtable entry.
 *
 * @param table The hashtable to retrieve the entry from.
 * @param key The key of the object to be obtained.
 * @param retKey If non-NULL, the entry key is returned through this reference
 *               if a matching entry was found, otherwise it is set to NULL.
 * @param retObject If non-NULL, the entry object is returned through this
 *                  reference if a  matching entry was found, otherwise it is
 *                  set to NULL.
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return TRUE if an entry was found (and data returned), FALSE if key is not
 *         found.
 */
int WXHash_GetFullEntry(WXHashTable *table, void *key,
                        void **retKey, void **retObject,
                        WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn) {
    /* Easy with common find */
    struct WXHashEntry *entry = findEntry(table, key, keyHashFn, keyEqualsFn);

    if (entry != NULL) {
        if (retKey != NULL) *retKey = entry->key;
        if (retObject != NULL) *retObject = entry->object;
        return ((entry->object != NULL) ? TRUE : FALSE);
    } else {
        if (retKey != NULL) *retKey = NULL;
        if (retObject != NULL) *retObject = NULL;
        return FALSE;
    }
}

/**
 * Duplicate the given hashtable.  This will create copies of the internal
 * management structures of the hashtable and may possibly create duplicates
 * of the entry keys, if a duplication function is provided.  It does not
 * duplicate the object instances, only the references to the objects.
 *
 * @param dest The hashtable to copy information into, any entries in this
 *             table will be lost without any sort of cleanup.
 * @param source The hashtable containing the information to be copied
 * @param keyDupFn If non-NULL, this function will be called to duplicate the
 *                 key instances between the tables, otherwise the original
 *                 key reference will be used.
 * @return TRUE if the hashtable was successfully duplicated, FALSE if a
 *         memory allocation failure occurred.  Note that the hashtable may
 *         be partially filled, if failure occurred during key duplication.
 */
int WXHash_Duplicate(WXHashTable *dest, WXHashTable *source,
                     WXKeyDupFn keyDupFn) {
    struct WXHashEntry *srcEntry = NULL, *dstEntry = NULL;
    unsigned int idx;

    /* TODO - should this compact the hash? */

    /* Duplicate the hash count information */
    dest->tableMask = source->tableMask;
    dest->entryCount = source->entryCount;
    dest->occupied = source->occupied;

    /* Pretty easy if duplicating empty */
    if (source->entries == NULL) {
        dest->entries = NULL;
        return TRUE;
    }

    /* Duplicate the hash record information */
    dest->entries = ENTRY_ALLOC(dest->tableMask + 1);
    if (dest->entries == NULL) return FALSE;
    srcEntry = source->entries;
    dstEntry = dest->entries;
    for (idx = 0; idx <= dest->tableMask; idx++) {
         if (srcEntry->object == ResidualMarker) {
             dstEntry->object = ResidualMarker;
         } else if (srcEntry->object != NULL) {
             if (keyDupFn != NULL) {
                 dstEntry->key = (*keyDupFn)(srcEntry->key);
             } else {
                 dstEntry->key = srcEntry->key;
             }
             if (dstEntry->key == NULL) return FALSE;
             dstEntry->object = srcEntry->object;
             dstEntry->hashCode = srcEntry->hashCode;
         }

         srcEntry++;
         dstEntry++;
    }

    return TRUE;
}

/**
 * Scan through all entries in a hashtable, calling the specified callback
 * function for each valid hashtable entry.
 *
 * @param table The hashtable containing the entries to be scanned.
 * @param entryCB A function reference which is called for each valid entry
 *                in the hashtable.
 * @param userData A caller provided data object which is included in the
 *                 scan callback arguments.
 * @return Zero if the scan was completed, any other value indicates the scan
 *         was interrupted by the callback with the given value.
 */
int WXHash_Scan(WXHashTable *table, WXHashEntryScanCB entryCB, void *userData) {
    struct WXHashEntry *entry = table->entries;
    unsigned int tblSize = table->tableMask + 1;
    int rc;

    if (entry == NULL) return 0;
    while (tblSize > 0) {
        if ((entry->object != NULL) && (entry->object != ResidualMarker)) {
            rc = (*entryCB)(table, entry->key, entry->object, userData);
            if (rc != 0) return rc;
        }
        entry++;
        tblSize--;
    }

    return 0;
}

/* * * * * * * * * * * Convenience Hash Methods * * * * * * * * * * */

/**
 * Convenience method for hashing null-character terminated string values.
 *
 * @param key The string key to be hashed.
 * @return An appropriate hashcode value for the string.
 */
unsigned int WXHash_StrHashFn(void *key) {
    /* Yup, this comes straight from the Perl hashing algorithm */
    uint8_t *ptr = (uint8_t *) key;
    unsigned int hashCode = 0;

    while (*ptr != 0) {
        hashCode = hashCode * 33 + *(ptr++);
    }

    return hashCode;
}

/**
 * Convenience method for comparing two null-character terminated strings
 * for exact (case) equality.
 *
 * @param keya The key to compare against.
 * @param keyb The key to compare to.
 * @return TRUE if the values of the two keys are equal, FALSE otherwise.
 */
int WXHash_StrEqualsFn(void *keya, void *keyb) {
    char *ptra = (char *) keya, *ptrb = (char *) keyb;

    while (*ptra != '\0') {
        if (*(ptra++) != *(ptrb++)) return FALSE;
    }
    if (*ptrb != '\0') return FALSE;

    return TRUE;
}
