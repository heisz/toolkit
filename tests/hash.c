/*
 * Test interface for the hashtable toolkit elements.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "hash.h"
#include "mem.h"

/**
 * Main testing entry point.  Just a bunch of manipulations of the hashtable
 * codepoints.
 */
int main(int argc, char **argv) {
    /* At some point, put the MTraq testcase identifiers in here */

    /* Snag some bits from the original JEMCC implementation */
    char bigTable[1024][64], *origKey, *origObj;
    WXHashTable hashTable, dupHashTable;
    int idx, idy, isSet[1024];

    /* Really fill a hashtable */
    if (!WXHash_InitTable(&hashTable, -1)) {
        (void) fprintf(stderr, "Error: unexpected memory failure\n");
        exit(1);
    }
    for (idx = 0; idx < 1024; idx++) {
        (void) sprintf(bigTable[idx], "entry-%d", idx);

        if (!WXHash_InsertEntry(&hashTable, bigTable[idx], bigTable[idx],
                                NULL, NULL, WXHash_StrHashFn,
                                WXHash_StrEqualsFn)) {
            (void) fprintf(stderr, "Big table insert failure\n");
            exit(1);
        }
        isSet[idx] = 1;
    }

    /* Random removals (sparse population) */
    for (idx = 0; idx < 512; idx++) {
        idy = rand() & 1023;
        if (WXHash_RemoveEntry(&hashTable, bigTable[idy],
                               (void **) &origKey, (void **) &origObj,
                               WXHash_StrHashFn, WXHash_StrEqualsFn)) {
            if (!isSet[idy]) {
                (void) fprintf(stderr, "Remove for non-existent entry?\n");
                exit(1);
            }
            if ((origKey != bigTable[idy]) || (origObj != bigTable[idy])) {
                (void) fprintf(stderr, "Incorrect orig remove return\n");
                exit(1);
            }
            isSet[idy] = 0;
        } else {
            if (isSet[idy]) {
                (void) fprintf(stderr, "No remove for existent entry?\n");
                exit(1);
            }
        }
    }

    /* Refill with replace... */
    for (idx = 0; idx < 1024; idx++) {
        if (!WXHash_PutEntry(&hashTable, bigTable[idx], bigTable[idx],
                             (void **) &origKey, (void **) &origObj,
                             WXHash_StrHashFn, WXHash_StrEqualsFn)) {
            (void) fprintf(stderr, "Big table (partial) put failed\n");
            exit(1);
        }
        if ((origKey == NULL) && (origObj == NULL)) {
            if (isSet[idx]) {
                (void) fprintf(stderr, "No return for entry replacement?\n");
                exit(1);
            }
            isSet[idx] = 1;
        } else if ((origKey != NULL) && (origObj != NULL)) {
            if (!isSet[idx]) {
                (void) fprintf(stderr, "Replacement for empty slot?\n");
                exit(1);
            }
            if ((origKey != bigTable[idx]) || (origObj != bigTable[idx])) {
                (void) fprintf(stderr, "Mismatch in replacement value?\n");
                exit(1);
            }
        } else {
            (void) fprintf(stderr, "Partial return for replacement?\n");
            exit(1);
        }
    }

    /* And random removals again */
    for (idx = 0; idx < 512; idx++) {
        idy = rand() & 1023;
        if (WXHash_RemoveEntry(&hashTable, bigTable[idy],
                               (void **) &origKey, (void **) &origObj,
                               WXHash_StrHashFn, WXHash_StrEqualsFn)) {
            if (!isSet[idy]) {
                (void) fprintf(stderr, "Remove for non-existent entry?\n");
                exit(1);
            }
            if ((origKey != bigTable[idy]) || (origObj != bigTable[idy])) {
                (void) fprintf(stderr, "Incorrect orig remove return\n");
                exit(1);
            }
            isSet[idy] = 0;
        } else {
            if (isSet[idy]) {
                (void) fprintf(stderr, "No remove for existent entry?\n");
                exit(1);
            }
        }
    }

    /* Duplicate the table */
    if (!WXHash_Duplicate(&dupHashTable, &hashTable, NULL)) {
        (void) fprintf(stderr, "Unexpected memory failure on duplicate\n");
        exit(1);
    }

    /* Walk the tables */
    for (idx = 0; idx < 1024; idx++) {
        if (WXHash_GetFullEntry(&hashTable, bigTable[idx],
                                (void *) &origKey, (void *) &origObj,
                                WXHash_StrHashFn, WXHash_StrEqualsFn)) {
            if (!isSet[idx]) {
                (void) fprintf(stderr, "Return for non-existent entry?\n");
                exit(1);
            }
            if ((origKey != bigTable[idx]) || (origObj != bigTable[idx])) {
                (void) fprintf(stderr, "Incorrect full get return\n");
                exit(1);
            }
            if (WXHash_GetEntry(&hashTable, bigTable[idx],
                                WXHash_StrHashFn,
                                WXHash_StrEqualsFn) != origObj) {
                (void) fprintf(stderr, "Orig table get mismatch?\n");
                exit(1);
            }
            if (WXHash_GetEntry(&dupHashTable, bigTable[idx],
                                WXHash_StrHashFn,
                                WXHash_StrEqualsFn) != origObj) {
                (void) fprintf(stderr, "Dup table get mismatch?\n");
                exit(1);
            }
        } else {
            if (isSet[idy]) {
                (void) fprintf(stderr, "No return for existent entry?\n");
                exit(1);
            }
            if ((origKey != NULL) || (origObj != NULL)) {
                (void) fprintf(stderr, "Data return for non-entry?\n");
                exit(1);
            }
            if (WXHash_GetEntry(&hashTable, bigTable[idx],
                                WXHash_StrHashFn, WXHash_StrEqualsFn) != NULL) {
                (void) fprintf(stderr, "Table get data for non-entry?\n");
                exit(1);
            }
            if (WXHash_GetEntry(&dupHashTable, bigTable[idx],
                                WXHash_StrHashFn, WXHash_StrEqualsFn) != NULL) {
                (void) fprintf(stderr, "Duplicate get data for non-entry?\n");
                exit(1);
            }
        }
    }
    WXHash_Destroy(&hashTable);
    WXHash_Destroy(&dupHashTable);
    exit(0);
}

int hashEntryCB(WXHashTable *table, void *key, void *obj, void *userData) {
    WXHash_RemoveEntry(table, key, NULL, NULL, WXHash_StrHashFn, WXHash_StrEqualsFn);
    return ((userData == (void *) 0) ? 0 : 1);
}

#if 0
    WXHashScan(NULL, &dupHashTable, hashEntryCB, (void *) 1);
    WXHashScan(NULL, &dupHashTable, hashEntryCB, (void *) 0);
#endif
