/*
 * Test interface for the character encoding toolkit.
 *
 * Copyright (C) 1999-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "encoding.h"
#include "log.h"

/**
 * Main testing entry point.  Just a bunch of test instances.
 */
int main(int argc, char **argv) {
    WXBuffer buffer;

    /* At some point, put the MTraq testcase identifiers in here */
    WXBuffer_Init(&buffer, 0);

    /* JSON */
    buffer.length = 0;
    if ((WXJSON_EscapeString(&buffer, "abc") == NULL) ||
            (buffer.length != 3) ||
            (strncmp((char *) buffer.buffer, "abc", 3) != 0)) {
        (void) fprintf(stderr, "Incorrect JSON encoding of standard text\n");
        exit(1);
    }
    buffer.length = 0;
    if ((WXJSON_EscapeString(&buffer, "\"\\/\b\f\n\r\t") == NULL) ||
            (buffer.length != 16) ||
            (strncmp((char *) buffer.buffer,
                      "\\\"\\\\\\/\\b\\f\\n\\r\\t", 16) != 0)) {
        (void) fprintf(stderr, "Incorrect JSON encoding of control text\n");
        exit(1);
    }
    buffer.length = 0;
    if ((WXJSON_EscapeString(&buffer, "\x07\xD1\xB2\xE4\xB8\x9D") == NULL) ||
            (buffer.length != 18) ||
            (strncmp((char *) buffer.buffer,
                      "\\u0007\\u0472\\u4E1D", 18) != 0)) {
        (void) fprintf(stderr, "Incorrect JSON encoding of unicode text\n");
        exit(1);
    }
}
