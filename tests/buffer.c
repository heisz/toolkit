/*
 * Test interface for the buffer toolkit elements.
 *
 * Copyright (C) 1999-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "buffer.h"

/* Forward declarations */
static void testBasics();
static void testPack();
static void testUnpack();

/**
 * Main testing entry point.  Just a bunch of manipulations of the dynamic
 * buffer toolset.
 */
int main(int argc, char **argv) {
    /* At some point, put the MTraq testcase identifiers in here */

    testBasics();
    testPack();
    testUnpack();

    return 0;
}

/* Basic test elements for the buffer (most of what it does) */
static void testBasics() {
    uint8_t localBuffer[64];
    WXBuffer buffer;
    char *bigValue = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                     "sed do eiusmod tempor incididunt ut labore et dolore "
                     "magna aliqua. Ut enim ad minim veniam, quis nostrud "
                     "exercitation ullamco laboris nisi ut aliquip ex ea "
                     "commodo consequat. Duis aute irure dolor in "
                     "reprehenderit in voluptate velit esse cillum dolore eu "
                     "fugiat nulla pariatur. Excepteur sint occaecat cupidatat "
                     "non proident, sunt in culpa qui officia deserunt mollit "
                     "anim id est laborum.";

    /* Local->allocated sequences */
    WXBuffer_InitLocal(&buffer, localBuffer, sizeof(localBuffer));
    WXBuffer_Destroy(&buffer);

    WXBuffer_InitLocal(&buffer, localBuffer, sizeof(localBuffer));
    if (WXBuffer_Append(&buffer, (uint8_t *) bigValue, strlen(bigValue),
                        FALSE) == NULL) {
       (void) fprintf(stderr, "Unexpected memory error on big append\n");
       exit(1);
    }
    if (WXBuffer_Append(&buffer, (uint8_t *) bigValue, strlen(bigValue),
                        FALSE) == NULL) {
       (void) fprintf(stderr, "Unexpected memory error on 2nd big append\n");
       exit(1);
    }
    if (buffer.length != 2 * strlen(bigValue)) {
       (void) fprintf(stderr, "Incorrect length for expanded allocation\n");
       exit(1);
    }
    if (memcmp(buffer.buffer, bigValue, strlen(bigValue)) != 0) {
       (void) fprintf(stderr, "Incorrect initial append value\n");
       exit(1);
    }
    if (memcmp(buffer.buffer + strlen(bigValue), bigValue,
               strlen(bigValue)) != 0) {
       (void) fprintf(stderr, "Incorrect second append value\n");
       exit(1);
    }
    WXBuffer_Destroy(&buffer);
}

#define BCHK(bffr, compare, case) \
    if ((bffr).length != sizeof(compare)){ \
        (void) fprintf(stderr, \
                       "ERROR: Size diff for " case " %li vs %li\n", \
                       (bffr).length, sizeof(compare)); \
        exit(1); \
    } \
    if (memcmp((bffr).buffer, compare, sizeof(compare)) != 0) { \
        (void) fprintf(stderr, \
                       "ERROR: Compare failure for " case "\n"); \
        exit(1); \
    }

#define BPCK(bffr, content) \
    WXBuffer_Empty(&buffer); \
    if (WXBuffer_Append(&buffer, content, sizeof(content), FALSE) == NULL) { \
       (void) fprintf(stderr, "Unexpected memory error on buffer setup\n"); \
    }

static void dump(WXBuffer *buffer) {
    int idx;

    for (idx = 0; idx < buffer->length; idx++) {
        if (idx != 0) {
            (void) fprintf(stderr, (((idx % 8) == 0) ? ",\n" : ", "));
        }
        (void) fprintf(stderr, "0x%02X", *(buffer->buffer + idx));
    }
    (void) fprintf(stderr, "\n");
}

/* This is a bit more complicated, there's a lot of pack options... */
static void testPack() {
    uint8_t localBuffer[64];
    uint16_t endTstVal = 0x1234;
    WXBuffer buffer;

    WXBuffer_InitLocal(&buffer, localBuffer, sizeof(localBuffer));

    /* Standard text encodings */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "aa4", "abc", "defghi");
    BCHK(buffer, ((uint8_t[]) { 0x61, 0x64, 0x65, 0x66, 0x67 }),
         "truncated text packing");
    WXBuffer_Pack(&buffer, "a4a", "abc", "defghi");
    BCHK(buffer, ((uint8_t[]) { 0x61, 0x64, 0x65, 0x66, 0x67,
                                0x61, 0x62, 0x63, 0x00, 0x64 }),
         "null padded text packing");
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "A%A", 5, "abc", "defghi");
    BCHK(buffer, ((uint8_t[]) { 0x61, 0x62, 0x63, 0x20, 0x20, 0x64 }),
         "null padded text packing");

    /* Characters are really simple */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "c2C2", 12, -12, 100, -100);
    BCHK(buffer, ((uint8_t[]) { 0x0c, 0xf4, 0x64, 0x9c }),
         "character packing");

    /* Short items are the start of the ordering elements */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "sSs<S>nv",
                  0x1234, 0x5678, 0x4321, 0x8765, 0x1357, 0x8642);
    if (*((uint8_t *) &endTstVal) == 0x34) {
        BCHK(buffer, ((uint8_t[]) { 0x34, 0x12, 0x78, 0x56, 0x21, 0x43,
                                    0x87, 0x65, 0x13, 0x57, 0x42, 0x86 }),
             "short value packing");
    } else {
        BCHK(buffer, ((uint8_t[]) { 0x12, 0x34, 0x56, 0x78, 0x21, 0x43,
                                    0x87, 0x65, 0x13, 0x57, 0x42, 0x86 }),
             "short value packing");
    }
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "s<S>", -12, -22222);
    BCHK(buffer, ((uint8_t[]) { 0xf4, 0xff, 0xa9, 0x32 }),
         "negative short packing");

    /* Long is just longer, more bits to test against */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "lLl<L>NV",
                  0x01234567, 0x89abcdef, 0x76543210, 0xfedcba98,
                  0x13579bdf, 0xeca86420);
    if (*((uint8_t *) &endTstVal) == 0x34) {
        BCHK(buffer, ((uint8_t[]) { 0x67, 0x45, 0x23, 0x01,
                                    0xef, 0xcd, 0xab, 0x89,
                                    0x10, 0x32, 0x54, 0x76,
                                    0xfe, 0xdc, 0xba, 0x98,
                                    0x13, 0x57, 0x9b, 0xdf,
                                    0x20, 0x64, 0xa8, 0xec }),
             "long value packing");
    } else {
        BCHK(buffer, ((uint8_t[]) { 0x01, 0x23, 0x45, 0x67,
                                    0x89, 0xab, 0xcd, 0xef,
                                    0x10, 0x32, 0x54, 0x76,
                                    0xfe, 0xdc, 0xba, 0x98,
                                    0x13, 0x57, 0x9b, 0xdf,
                                    0x20, 0x64, 0xa8, 0xec }),
             "long value packing");
    }
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "l<L>", -22222, -222222222);
    BCHK(buffer, ((uint8_t[]) { 0x32, 0xa9, 0xff, 0xff,
                                0xf2, 0xc1, 0x28, 0x72 }),
         "negative long packing");

    /* Long long is just getting to be a pain... */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "qQq<Q>zZ",
                  (uint64_t) 0x0123456789abcdef,
                  (uint64_t) 0xfedcba9876543210,
                  (uint64_t) 0x0123456789abcdef,
                  (uint64_t) 0xfedcba9876543210,
                  (uint64_t) 0x0123456789abcdef,
                  (uint64_t) 0xfedcba9876543210);
    if (*((uint8_t *) &endTstVal) == 0x34) {
        BCHK(buffer,
             ((uint8_t[]) { 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
                            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
                            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
                            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe }),
             "long long value packing");
    } else {
        BCHK(buffer,
             ((uint8_t[]) { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
                            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe }),
             "long long value packing");
    }
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "q<Q>", -2222222222, -2222222222222222222L);
    BCHK(buffer,
         ((uint8_t[]) { 0x72, 0x94, 0x8b, 0x7b, 0xff, 0xff, 0xff, 0xff,
                        0xe1, 0x29, 0x14, 0xa9, 0xa8, 0x77, 0x1c, 0x72 }),
         "negative long long packing");

    /* Byte positioning (x and X) */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "x[5]x3x");
    BCHK(buffer, ((uint8_t[]) { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }),
         "empty (x) packing");
    WXBuffer_Pack(&buffer, "XX4");
    BCHK(buffer, ((uint8_t[]) { 0x0, 0x0, 0x0, 0x0 }), "rollback (X) packing");
    WXBuffer_Pack(&buffer, "X[24]");
    BCHK(buffer, ((uint8_t[]) { }), "rollback (X) overflow");

    /* Easiest case to test variant lengths */
    WXBuffer_Pack(&buffer, "x[%]x%x", 3, 2);
    BCHK(buffer, ((uint8_t[]) { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }),
         "variant empty (x) packing");

    /* Bits and hex left to end as not entirely common */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "b6", "010111");
    BCHK(buffer, ((uint8_t[]) { 0x3a }),
         "base ascending bit packing");
    WXBuffer_Pack(&buffer, "B6", "010111");
    BCHK(buffer, ((uint8_t[]) { 0x3a, 0x5c }),
         "base ascending/descending bit packing");
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "b*B*", "0101110110010100111",
                                   "0101110110010100111");
    BCHK(buffer,
         ((uint8_t[]) { 0xba, 0x29, 0x07, 0x5d, 0x94, 0xe0 }),
         "multi-byte ascending/descending bit packing");

    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "h3", "5ae95c");
    BCHK(buffer, ((uint8_t[]) { 0xa5, 0x0e }),
         "truncated low-nybble hex packing");
    WXBuffer_Pack(&buffer, "H7", "5ae95c");
    BCHK(buffer, ((uint8_t[]) { 0xa5, 0x0e, 0x5a, 0xe9, 0x5c, 0x00 }),
         "extended high-nybble hex packing");
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "h*H*", "3ae46", "f294d3");
    BCHK(buffer,
         ((uint8_t[]) { 0xa3, 0x4e, 0x06, 0xf2, 0x94, 0xd3 }),
         "multi-byte mixed hex packing");

    /* Well, groups actually are last... */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "(ss)<(s)>2n2",
                  0x1234, 0x5678, 0x4321, 0x8765, 0x1357, 0x8642);
    BCHK(buffer, ((uint8_t[]) { 0x34, 0x12, 0x78, 0x56, 0x43, 0x21,
                                0x87, 0x65, 0x13, 0x57, 0x86, 0x42 }),
         "group packing");

    /* Until we add other stuff */
    WXBuffer_Empty(&buffer);
    WXBuffer_Pack(&buffer, "yYyY", 300, (uint64_t) 0, 12, 1234567);
    BCHK(buffer, ((uint8_t[]) { 0xAC, 0x02, 0x00, 0x0C, 0x87, 0xAD, 0x4B }),
         "varint packing");
}

/* Ditto... */
static void testUnpack() {
    uint8_t localBuffer[64];
    WXBuffer buffer;
    uint16_t endTstVal = 0x1234;
    char *sptr, *sptrb, *sptrc;
    uint8_t cha, chb, chc, chd;
    uint16_t sha, shb, shc, shd, she, shf;
    int16_t ssha, sshb;
    uint32_t lda, ldb, ldc, ldd, lde, ldf;
    int32_t slda, sldb;
    uint64_t llda, lldb, lldc, lldd, llde, lldf;
    int64_t sllda, slldb;

    WXBuffer_InitLocal(&buffer, localBuffer, sizeof(localBuffer));

    /* Character is just plain copy extraction */
    BPCK(buffer, ((uint8_t[]) { 0x3a, 0x3b, 0x3c, 0x3d }));
    if ((WXBuffer_Unpack(&buffer, "a10", &sptr) == NULL) ||
                                          (strcmp(sptr, ":;<=") != 0)) {
       (void) fprintf(stderr, "ERROR: invalid extended char unpack\n");
       exit(1);
    }
    WXFree(sptr);
    buffer.offset = 0;
    if ((WXBuffer_Unpack(&buffer, "A3", &sptr) == NULL) ||
                                          (strcmp(sptr, ":;<") != 0)) {
       (void) fprintf(stderr, "ERROR: invalid bounded char unpack\n");
       exit(1);
    }
    WXFree(sptr);

    /* Bit sequences have lots of fractional elements */
    BPCK(buffer, ((uint8_t[]) { 0x3a, 0x3b, 0x3c, 0x3d, 0x00, 0x3a, 0x3b }));
    if ((WXBuffer_Unpack(&buffer, "b123", &sptr) == NULL) ||
           (strcmp(sptr, "010111001101110000111100101111000000000"
                         "00101110011011100") != 0)) {
       (void) fprintf(stderr, "ERROR: invalid extended bit unpack\n");
       exit(1);
    }
    WXFree(sptr);
    buffer.offset = 0;
    if ((WXBuffer_Unpack(&buffer, "b11B*b", &sptr, &sptrb, &sptrc) == NULL) ||
           (strcmp(sptr, "01011100110") != 0) ||
           (strcmp(sptrb, "0011110000111101000000000011101000111011") != 0) ||
           (strcmp(sptrc, "") != 0)) {
       (void) fprintf(stderr, "ERROR: invalid combined bit unpack\n");
       exit(1);
    }
    WXFree(sptr);
    WXFree(sptrb);
    WXFree(sptrc);

    /* Hex not so much */
    buffer.offset = 0;
    if ((WXBuffer_Unpack(&buffer, "h123", &sptr) == NULL) ||
           (strcmp(sptr, "A3B3C3D300A3B3") != 0)) {
       (void) fprintf(stderr, "ERROR: invalid extended hex unpack\n");
       exit(1);
    }
    WXFree(sptr);
    buffer.offset = 0;
    if ((WXBuffer_Unpack(&buffer, "h3H*h", &sptr, &sptrb, &sptrc) == NULL) ||
           (strcmp(sptr, "A3B") != 0) || (strcmp(sptrb, "3C3D003A3B") != 0) ||
           (strcmp(sptrc, "") != 0)) {
       (void) fprintf(stderr, "ERROR: invalid combined hex unpack\n");
       exit(1);
    }
    WXFree(sptr);
    WXFree(sptrb);
    WXFree(sptrc);

    /* Character pretty straightforward */
    buffer.offset = 0;
    buffer.length = 3;
    cha = chb = chc = chd = 12;
    if ((WXBuffer_Unpack(&buffer, "cC2c4", &cha, &chb, &chc, &chd) == NULL) ||
            (cha != 58) || (chb != 59) || (chc != 60) || (chd != 12)) {
       (void) fprintf(stderr, "ERROR: invalid char unpack\n");
       exit(1);
    }

    /* Byte re-positioning (x and X) */
    BPCK(buffer, ((uint8_t[]) { 0x0, 0x0, 0x0, 0x0 }));
    if (WXBuffer_Unpack(&buffer, "x22") != NULL) {
       (void) fprintf(stderr, "Unexpected success on consumption\n");
       exit(1);
    }
    if ((WXBuffer_Unpack(&buffer, "x2") == NULL) || (buffer.offset != 2)) {
       (void) fprintf(stderr, "Unexpected success on unpack consumption\n");
       exit(1);
    }
    if ((WXBuffer_Unpack(&buffer, "X4") == NULL) || (buffer.offset != 0)) {
       (void) fprintf(stderr, "Unexpected success on unpack windback\n");
       exit(1);
    }

    /* Here come the numbers (reverse of the pack conditions) */

    /* Short items are the start of the ordering elements */
    if (*((uint8_t *) &endTstVal) == 0x34) {
        BPCK(buffer, ((uint8_t[]) { 0x34, 0x12, 0x78, 0x56, 0x21, 0x43,
                                    0x87, 0x65, 0x13, 0x57, 0x42, 0x86 }));
    } else {
        BPCK(buffer, ((uint8_t[]) { 0x12, 0x34, 0x56, 0x78, 0x21, 0x43,
                                    0x87, 0x65, 0x13, 0x57, 0x42, 0x86 }));
    }
    if ((WXBuffer_Unpack(&buffer, "sSs<S>nvs22",
                         &sha, &shb, &shc, &shd, &she, &shf) == NULL) ||
            (sha != 0x1234) || (shb != 0x5678) || (shc != 0x4321) ||
            (shd != 0x8765) || (she != 0x1357) || (shf != 0x8642)) {
       (void) fprintf(stderr, "ERROR: invalid combined short unpack\n");
       exit(1);
    }
    BPCK(buffer, ((uint8_t[]) { 0xf4, 0xff, 0xa9, 0x32 }));
    if ((WXBuffer_Unpack(&buffer, "s<S>",&ssha, &sshb) == NULL) ||
            (ssha != -12) || (sshb != -22222)) {
       (void) fprintf(stderr, "ERROR: invalid signed short unpack\n");
       exit(1);
    }

    /* Long is just longer, more bits to test against */
    if (*((uint8_t *) &endTstVal) == 0x34) {
        BPCK(buffer, ((uint8_t[]) { 0x67, 0x45, 0x23, 0x01,
                                    0xef, 0xcd, 0xab, 0x89,
                                    0x10, 0x32, 0x54, 0x76,
                                    0xfe, 0xdc, 0xba, 0x98,
                                    0x13, 0x57, 0x9b, 0xdf,
                                    0x20, 0x64, 0xa8, 0xec }));
    } else {
        BPCK(buffer, ((uint8_t[]) { 0x01, 0x23, 0x45, 0x67,
                                    0x89, 0xab, 0xcd, 0xef,
                                    0x10, 0x32, 0x54, 0x76,
                                    0xfe, 0xdc, 0xba, 0x98,
                                    0x13, 0x57, 0x9b, 0xdf,
                                    0x20, 0x64, 0xa8, 0xec }));
    }
    if ((WXBuffer_Unpack(&buffer, "lLl<L>NV",
                         &lda, &ldb, &ldc, &ldd, &lde, &ldf) == NULL) ||
            (lda != 0x01234567) || (ldb != 0x89abcdef) ||
            (ldc != 0x76543210) || (ldd != 0xfedcba98) ||
            (lde != 0x13579bdf) || (ldf != 0xeca86420)) {
       (void) fprintf(stderr, "ERROR: invalid combined long unpack\n");
       exit(1);
    }
    BPCK(buffer, ((uint8_t[]) { 0x32, 0xa9, 0xff, 0xff,
                                0xf2, 0xc1, 0x28, 0x72 }));
    if ((WXBuffer_Unpack(&buffer, "l<L>*", &slda, &sldb) == NULL) ||
            (slda != -22222) || (sldb != -222222222)) {
       (void) fprintf(stderr, "ERROR: invalid signed long unpack\n");
       exit(1);
    }

    /* Long long is just getting to be a pain... */
    if (*((uint8_t *) &endTstVal) == 0x34) {
        BPCK(buffer,
             ((uint8_t[]) { 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
                            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
                            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
                            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe }));
    } else {
        BPCK(buffer,
             ((uint8_t[]) { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                            0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
                            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
                            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                            0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe }));
    }
    if ((WXBuffer_Unpack(&buffer, "qQq<Q>zZ",
                         &llda, &lldb, &lldc, &lldd, &llde, &lldf) == NULL) ||
            (llda != (uint64_t) 0x0123456789abcdef) ||
            (lldb != (uint64_t) 0xfedcba9876543210) ||
            (lldc != (uint64_t) 0x0123456789abcdef) ||
            (lldd != (uint64_t) 0xfedcba9876543210) ||
            (llde != (uint64_t) 0x0123456789abcdef) ||
            (lldf != (uint64_t) 0xfedcba9876543210)) {
       (void) fprintf(stderr, "ERROR: invalid combined long long unpack\n");
       exit(1);
    }
    BPCK(buffer,
         ((uint8_t[]) { 0x72, 0x94, 0x8b, 0x7b, 0xff, 0xff, 0xff, 0xff,
                        0xe1, 0x29, 0x14, 0xa9, 0xa8, 0x77, 0x1c, 0x72 }));
    if ((WXBuffer_Unpack(&buffer, "q<Q>", &sllda, &slldb) == NULL) ||
            (sllda != -2222222222) || (slldb != -2222222222222222222L)) {
       (void) fprintf(stderr, "ERROR: invalid signed long long unpack\n");
       exit(1);
    }

    BPCK(buffer, ((uint8_t[]) { 0x34, 0x12, 0x78, 0x56, 0x43, 0x21,
                                0x87, 0x65, 0x13, 0x57, 0x86, 0x42 }));
    if ((WXBuffer_Unpack(&buffer, "(ss)<(s)>2n2",
                         &sha, &shb, &shc, &shd, &she, &shf) == NULL) ||
            (sha != 0x1234) || (shb !=  0x5678) || (shc !=  0x4321) ||
            (shd != 0x8765) || (she !=  0x1357) || (shf !=  0x8642)) {
       (void) fprintf(stderr, "ERROR: invalid group unpack\n");
       exit(1);
    }

    /* Until we add other stuff */
    BPCK(buffer, ((uint8_t[]) { 0xAC, 0x02, 0x02, 0x0C, 0x87, 0xAD, 0x4B }));
    if ((WXBuffer_Unpack(&buffer, "yYy*", &lda, &lldb, &ldc, &ldd) == NULL) ||
            (lda != 300) || (lldb != 2) || (ldc != 12) || (ldd != 1234567)) {
       (void) fprintf(stderr, "ERROR: invalid varint unpack\n");
       exit(1);
    }
}
