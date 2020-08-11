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
    if ((WXJSON_EscapeString(&buffer, "abc", 3) == NULL) ||
            (buffer.length != 3) ||
            (strncmp((char *) buffer.buffer, "abc", 3) != 0)) {
        (void) fprintf(stderr, "Incorrect JSON encoding of standard text\n");
        exit(1);
    }
    buffer.length = 0;
    if ((WXJSON_EscapeString(&buffer, "\"\\/\b\f\n\r\t", -1) == NULL) ||
            (buffer.length != 16) ||
            (strncmp((char *) buffer.buffer,
                      "\\\"\\\\\\/\\b\\f\\n\\r\\t", 16) != 0)) {
        (void) fprintf(stderr, "Incorrect JSON encoding of control text\n");
        exit(1);
    }
    buffer.length = 0;
    if ((WXJSON_EscapeString(&buffer,
                             "\x07\xD1\xB2\xE4\xB8\x9D", -1) == NULL) ||
            (buffer.length != 18) ||
            (strncmp((char *) buffer.buffer,
                      "\\u0007\\u0472\\u4E1D", 18) != 0)) {
        (void) fprintf(stderr, "Incorrect JSON encoding of unicode text\n");
        exit(1);
    }

    /* XML */
    buffer.length = 0;
    if ((WXML_EscapeAttribute(&buffer, "a<b&c>d'e\"f", -1, FALSE) == NULL) ||
            (buffer.length != 31) ||
            (strncmp((char *) buffer.buffer,
                     "a&lt;b&amp;c&gt;d&apos;e&quot;f", 31) != 0)) {
        (void) fprintf(stderr, "Incorrect XML encoding of attribute text\n");
        exit(1);
    }
    buffer.length = 0;
    if ((WXML_EscapeContent(&buffer, "a<b&c>d'e\"f", -1, FALSE) == NULL) ||
            (buffer.length != 21) ||
            (strncmp((char *) buffer.buffer,
                     "a&lt;b&amp;c&gt;d'e\"f", 21) != 0)) {
        (void) fprintf(stderr, "Incorrect XML encoding of content\n");
        exit(1);
    }

    /* URL */
    buffer.length = 0;
    if ((WXURL_EscapeURI(&buffer, "?a-z%A_Z!0.9 ", -1) == NULL) ||
            (buffer.length != 21) ||
            (strncmp((char *) buffer.buffer,
                     "%3Fa-z%25A_Z%210.9%20", 21) != 0)) {
        (void) fprintf(stderr, "Incorrect URI encoding of special chars\n");
        exit(1);
    }
}
