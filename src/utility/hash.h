/*
 * Generic hashtable implementation, with pluggable hashing algorithms.
 *
 * Copyright (C) 1999-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_HASHTBL_H
#define WX_HASHTBL_H 1

/**
 * Structural definition of a generalized hashtable for keyed information
 * storage.  Supports variant forms for generating hash elements.  Underlying
 * hashing algorithm is based on the single table/collision model (from X11) as
 * opposed to the usual bucket model.
 */
typedef struct WXHashTable {
    /**
     * The number of available slots in the entries table and the number
     * that are occupied.
     */
    unsigned int entryCount, occupied;

    /**
     * Internal mask used to generate the hash-indexing sequence.
     */
    unsigned int tableMask;

    /**
     * Allocated block of hashtable entries.  Note that this hashtable
     * implementation uses a collision alorithm against a single allocation
     * block.  Size of this block is stored in the entryCount.
     */
    struct WXHashEntry *entries;
} WXHashTable;

/**
 * Hash generation function prototype for use with the hashtable methods
 * below.  This function must always return the same hashcode for keys
 * which are "equal", but unequal keys are allowed to generate the same
 * hashcode (although it is typically better if they don't).
 *
 * @param key The key associated with the object to be hashed.
 * @return The numeric hashcode for the given key instance.
 */
typedef unsigned int (*WXKeyHashFn)(void *key);

/**
 * Comparison function for locating key matches in hashtables for use
 * with the hashtable methods below.  This function may do a simple
 * pointer comparison or a more complex equals comparison based on data
 * contained within the key structures.
 *
 * @param keya The key to compare against.
 * @param keyb The key to compare to.
 * @return TRUE (non-zero) if the values of the two keys are equal, FALSE (zero)
 *         otherwise.
 */
typedef int (*WXKeyEqualsFn)(void *keya, void *keyb);

/**
 * Duplication function to create a new key based on the given key.  This
 * may be a simple pointer copy (easiest case) or a full duplication of the
 * information represented by the key.
 *
 * @param key The key instance to be copied.
 * @return The "copy" of the provided key, or NULL if a memory allocation failed
 *         (will bubble up as a duplication error).  Caller is responsible for
 *         managing key memory instances if allocated.
 */
typedef void *(*WXKeyDupFn)(void *key);

/**
 * Callback method for enumerating entries within a hashtable.  Allows for
 * termination of the scanning through the return code.
 *
 * @param table The hashtable which is currently being scanned.
 * @param key The key associated with the currently scanned entry.
 * @param object The object associated with the currently scanned entry.
 * @param userData The caller provided information attached to the scan request.
 * @return Zero if scan is to continue, non-zero if scan is to be terminated
 *         (error or matching complete, value is returned from scan).
 */
typedef int (*WXHashEntryScanCB)(WXHashTable *table, void *key, void *object,
                                 void *userData);

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
int WXHash_InitTable(WXHashTable *table, int startSize);

/**
 * Reset/empty the contents of the hashtable.  Resets the internal data as
 * if it were a newly allocated hashtable.
 *
 * @param table The hashtable instance to be emptied.  Note that this will not
 *              release any internal references.
 */
void WXHash_Empty(WXHashTable *table);

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
                    WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn);

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
                       WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn);

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
 * @param origObject If this reference is non-NULL and an entry is removed, the
 *                   object associated with the removed entry is returned here.
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return TRUE if an entry was found and removed, FALSE if key is not found.
 */
int WXHash_RemoveEntry(WXHashTable *table, void *key,
                       void **origKey, void **origObject,
                       WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn);

/**
 * Retrieve an object from the hashtable according to the specified key.
 *
 * @param table The hashtable to retrieve the entry from.
 * @param key The key of the object to be obtained.
 * @param keyHashFn A function reference used to generate hashcode values
 *                  from the table keys.
 * @param keyEqualsFn A function reference used to compare keys in the
 *                    hashtable entries.
 * @return NULL if no object entry has a matching key, otherwise the matching
 *         object reference.
 */
void *WXHash_GetEntry(WXHashTable *table, void *key,
                      WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn);

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
                        WXKeyHashFn keyHashFn, WXKeyEqualsFn keyEqualsFn);

/**
 * Duplicate the given hashtable.  This will create copies of the internal
 * management structures of the hashtable and may possibly create duplicates
 * of the entry keys, if a duplication function is provided.  It does not
 * duplicate the object instances, only the references to the objects.
 *
 * @param dest The hashtable to copy information into, any entries in this
 *             table will be lost without any sort of cleanup.
 * @param source The hashtable containing the information to be copied.
 * @param keyDupFn If non-NULL, this function will be called to duplicate the
 *                 key instances between the tables, otherwise the original
 *                 key reference will be used.
 * @return TRUE if the hashtable was successfully duplicated, FALSE if a
 *         memory allocation failure occurred.  Note that the hashtable may
 *         be partially filled, if failure occurred during key duplication.
 */
int WXHash_Duplicate(WXHashTable *dest, WXHashTable *source,
                     WXKeyDupFn keyDupFn);

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
int WXHash_Scan(WXHashTable *table, WXHashEntryScanCB entryCB, void *userData);

/**
 * Destroy the internals of the hashtable instance.  This does not free the
 * hashtable structure itself (only the allocated content), nor does it do
 * anything with the key/object instances stored in the table.
 *
 * @param table The hashtable instance to destroy.
 */
void WXHash_Destroy(WXHashTable *table);

/* * * * * * * * * * * Convenience Hash Methods * * * * * * * * * * */

/**
 * Convenience method for hashing null-character terminated string values.
 *
 * @param key The string key to be hashed.
 * @return An appropriate hashcode value for the string.
 */
unsigned int WXHash_StrHashFn(void *key);

/**
 * Convenience method for comparing two null-character terminated strings
 * for exact (case) equality.
 *
 * @param keya The key to compare against.
 * @param keyb The key to compare to.
 * @return TRUE if the values of the two keys are equal, FALSE otherwise.
 */
int WXHash_StrEqualsFn(void *keya, void *keyb);

/**
 * Convenience method for hashing null-character terminated string values
 * without case consideration.
 *
 * @param key The string key to be hashed.
 * @return An appropriate hashcode value for the string.
 */
unsigned int WXHash_StrCaseHashFn(void *key);

/**
 * Convenience method for comparing two null-character terminated strings
 * for caseless equality.
 *
 * @param keya The key to compare against.
 * @param keyb The key to compare to.
 * @return TRUE if the values of the two keys are equal, FALSE otherwise.
 */
int WXHash_StrCaseEqualsFn(void *keya, void *keyb);

/* * * * * * * * * * * Standard Dictionary * * * * * * * * * * */

/**
 * Special form of the hashtable which can only store strings and self-manages
 * allocation of the key/value pairings.
 */
typedef struct WXDictionary {
    /* At the core, it's still a hashtable */
    WXHashTable base;

    /* With just an indicator as to the case-sensitivity of the keys */
    int isCaseSensitive;
} WXDictionary;

/**
 * Initialize a dictionary instance with the given number of base entries.
 *
 * @param dict Reference to an existing instance of the dictionary to be
 *             initialized (already existing entries in the table will not
 *             be cleaned up).
 * @param startSize The number of key entries to initially allocate in the
 *                  dictionary.  If negative, the system default start size
 *                  will be selected.
 * @param isCaseSensitive If TRUE, keys will be matched according to case, if
 *                        FALSE, key matches are case-insensitive (but are *not*
 *                        lower-cased).
 * @return TRUE (non-zero) if initialized, FALSE (zero) if memory error occured.
 */
int WXDict_Init(WXDictionary *dict, int startSize, int isCaseSensitive);

/**
 * Reset/empty the contents of the dictionary.  Resets the internal data as
 * if it were a newly allocated dictionary.  All keys and values will be freed.
 *
 * @param dict The dictionary instance to be emptied.  Note that this will not
 *             release any internal references.
 */
void WXDict_Empty(WXDictionary *dict);

/**
 * Store a key/value pair into a dictionary.  Tables will expand as necessary,
 * value will replace an already existing/matching key entry.  Key/value are
 * internally duplicated, existing values will be freed.
 *
 * @param dict The dictionary to put the key->value pair into.
 * @param key The key associated with the entry.
 * @param val The value to store in the dictionary according to the given
 *            key.  Note that this dictionary implementation cannot store
 *            NULL values, use an external fixed/static object to represent
 *            NULL value storage.
 * @return TRUE if the insertion was successful, FALSE if a memory allocation
 *         failure occurred.
 */
int WXDict_PutEntry(WXDictionary *dict, const char *key, const char *val);

/**
 * Almost identical to the PutEntry method, this method stores a value in the
 * dictionary unless there already exists an entry in the dictionary with an
 * "equal" key (i.e. this method will not replace already existing dictionary
 * entries where PutEntry does).  If inserted, key/value pairs are duplicated.
 *
 * @param dict The dictionary to insert the key->value pair into.
 * @param key The key associated with the entry.
 * @param val The value to store in the dictionary according to the given
 *            key.  Note that this dictionary implementation cannot store
 *            NULL values, use an external fixed/static object to represent
 *            NULL value storage.
 * @return 1 if the insertion was successful, 0 if a memory allocation failure
 *         occurred and -1 if an entry already existed for the given key.
 */
int WXDict_InsertEntry(WXDictionary *dict, const char *key, const char *val);

/**
 * Remove an entry from the dictionary.  This will automatically clean up
 * the internal key/value content.  NOTE: this method is safe to be called
 * within the Scan function, as it does not perform hash compaction.
 *
 * @param dict The dictionary to remove the entry from.
 * @param key The key of the pair entry to be removed.  Only has to match
 *            in equality, does not have to be the exact key instance.
 * @return TRUE if an entry was found and removed, FALSE if key is not found.
 */
int WXDict_RemoveEntry(WXDictionary *dict, const char *key);

/**
 * Retrieve a value from the dictionary according to the specified key.
 *
 * @param dict The dictionary to retrieve the entry from.
 * @param key The key of the value to be obtained.
 * @return NULL if no value is found for the matching key, otherwise the 
 *         value entry.  Note that this is a reference to the internally
 *         managed duplicate, consider it read only.
 */
const char *WXDict_GetEntry(WXDictionary *dict, const char *key);

/**
 * Duplicate the given dictionary.  This will completely duplicate the keys,
 * values and internal table instances.
 *
 * @param dest The dictionary to copy information into, any entries in this
 *             dict will be lost without any sort of cleanup.
 * @param source The dictionary containing the information to be copied.
 * @return TRUE if the dictionary was successfully duplicated, FALSE if a
 *         memory allocation failure occurred.  Note that the dictionary may
 *         be partially filled, if failure occurred during duplication.
 */
int WXDict_Duplicate(WXDictionary *dest, WXDictionary *source);

/**
 * Callback method for enumerating entries within a dictionary.  Allows for
 * termination of the scanning through the return code.
 *
 * @param dict The dictionary which is currently being scanned.
 * @param key The key associated with the currently scanned entry.
 * @param val The value associated with the currently scanned entry.
 * @param userData The caller provided information attached to the scan request.
 * @return Zero if scan is to continue, non-zero if scan is to be terminated
 *         (error or matching complete, value is returned from scan).
 */
typedef int (*WXDictionaryScanCB)(WXDictionary *dict, const char *key,
                                  const char *val, void *userData);

/**
 * Scan through all entries in a dictionary, calling the specified callback
 * function for each valid dictionary entry.
 *
 * @param dict The dictionary containing the entries to be scanned.
 * @param entryCB A function reference which is called for each valid entry
 *                in the dictionary.
 * @param userData A caller provided data object which is included in the
 *                 scan callback arguments.
 * @return Zero if the scan was completed, any other value indicates the scan
 *         was interrupted by the callback with the given value.
 */
int WXDict_Scan(WXDictionary *dict, WXDictionaryScanCB entryCB, void *userData);

/**
 * Destroy the internals of the dictionary instance.  This does not free the
 * dictionary structure itself, just the contained key/value/hash content.
 *
 * @param dict The dictionary instance to destroy.
 */
void WXDict_Destroy(WXDictionary *dict);

#endif
