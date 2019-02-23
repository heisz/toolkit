/*
 * Test interface for the array toolkit elements.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "array.h"

/* A structure with all kinds of alignment variance */
typedef struct {
    char ch;
    void *ptr;
    char str[7];
} Misaligned;

static char *one = "one";
static char *two = "two";
static char *three = "three";
static char *four = "four";

static int scanCounter = 1;
static int scanner(WXArray *array, void *object, void *userData) {
    Misaligned *m = (Misaligned *) object;

    if (m->ch != ('0' + scanCounter)) return scanCounter;
    if (strcmp((char *) m->ptr, m->str) != 0) return scanCounter;
    if (scanCounter == 4) return 12;
    scanCounter++;
    return 0;
}

/**
 * Main testing entry point.  Just a bunch of test instances.
 */
int main(int argc, char **argv) {
    Misaligned data[4], tst;
    WXArray array;
    int idx;

    /* At some point, put the MTraq testcase identifiers in here */

    /* Just play with the local buffer first */
    WXArray_InitLocal(&array, Misaligned, data, 4);

    tst.ch = '3';
    tst.ptr = three;
    (void) strcpy(tst.str, three);
    if (WXArray_Push(&array, &tst) == NULL) {
        (void) fprintf(stderr, "Failed to push local\n");
        exit(1);
    }

    tst.ch = '2';
    tst.ptr = two;
    (void) strcpy(tst.str, two);
    if (WXArray_Unshift(&array, &tst) == NULL) {
        (void) fprintf(stderr, "Failed to unshift local\n");
        exit(1);
    }

    tst.ch = '4';
    tst.ptr = four;
    (void) strcpy(tst.str, four);
    if (WXArray_Push(&array, &tst) == NULL) {
        (void) fprintf(stderr, "Failed to push local\n");
        exit(1);
    }

    tst.ch = '1';
    tst.ptr = one;
    (void) strcpy(tst.str, one);
    if (WXArray_Unshift(&array, &tst) == NULL) {
        (void) fprintf(stderr, "Failed to unshift local\n");
        exit(1);
    }

    if (array.length != 4) {
        (void) fprintf(stderr, "Incorrect array length\n");
        exit(1);
    }

    if (WXArray_Scan(&array, scanner, "testing") != 12) {
        (void) fprintf(stderr, "Incorrect scan outcome\n");
        exit(1);
    }

    if ((WXArray_Shift(&array, &tst) == NULL) || (tst.ptr != one)) {
        (void) fprintf(stderr, "Incorrect unshift one\n");
        exit(1);
    }

    if ((WXArray_Pop(&array, &tst) == NULL) || (tst.ptr != four)) {
        (void) fprintf(stderr, "Incorrect pop one\n");
        exit(1);
    }

    if ((WXArray_Pop(&array, &tst) == NULL) || (tst.ptr != three)) {
        (void) fprintf(stderr, "Incorrect pop two\n");
        exit(1);
    }

    if ((WXArray_Shift(&array, &tst) == NULL) || (tst.ptr != two)) {
        (void) fprintf(stderr, "Incorrect unshift two\n");
        exit(1);
    }

    WXArray_Destroy(&array);

    (void) WXArray_Init(&array, Misaligned, 0);

    for (idx = 0; idx < 128; idx++) {
        tst.ch = 0x20 + idx;
        tst.ptr = (void *) (intptr_t) idx;
        (void) sprintf(tst.str, "%d", idx);

        if ((idx % 2) == 0) {
            if (WXArray_Unshift(&array, &tst) == NULL) {
                (void) fprintf(stderr, "Failed to unshift %d\n", idx);
                exit(1);
            }
        } else {
            if (WXArray_Push(&array, &tst) == NULL) {
                (void) fprintf(stderr, "Failed to push %d\n", idx);
                exit(1);
            }
        }
    }

    return 0;
}
