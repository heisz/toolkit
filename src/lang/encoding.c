/*
 * Methods for handling various data encodings (typically language specific).
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "encoding.h"

/**
 * Generate an indent of the number of spaces onto the provided buffer.  Not
 * worth another file...
 *
 * @param buffer Buffer to append the requested indent onto.
 * @param indent Number of spaces to indent by.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXIndent(WXBuffer *buffer, int indent) {
    static char *spaces = "                                        "
                          "                                        "
                          "                                        "
                          "                                        ";
    int len;

    while (indent > 0) {
        len = (indent < sizeof(spaces) - 1) ? indent : sizeof(spaces) - 1;
        if (WXBuffer_Append(buffer, spaces, len, TRUE) == NULL) return NULL;
        indent -= len;
    }

    return buffer->buffer;
}

/**
 * Escape unsafe JSON character sequences in the provided string.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXJSON_EscapeString(WXBuffer *buffer, char *str, int len) {
    char  escBuff[16], *blk = str;
    uint32_t uniChar;
    unsigned char ch;
    int l;

    if (len < 0) len = strlen(str);
    escBuff[0] = '\\';
    while (len > 0) {
        ch = (unsigned char) *(str++);
        len--;

        if ((ch & 0x80) != 0) {
            if ((l = (str - blk) - 1) > 0) {
                if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
            }
            if ((len < 1) || ((*str & 0xC0) != 0x80)) {
                (void) strcpy(escBuff, "\\u001A");
            } else {
                if ((ch & 0xE0) == 0xE0) {
                    if ((len < 2) || ((*(str + 1) & 0xC0) != 0x80)) {
                        (void) strcpy(escBuff, "\\u001A");
                    } else {
                        if ((ch & 0xF0) == 0xF0) {
                            /* TODO - support extended (non-BMP) Unicode */
                            (void) strcpy(escBuff, "\\u001A");
                        } else {
                            /* Three-byte blk */
                            uniChar = (((uint32_t) ch) & 0x0F) << 12;
                            uniChar |= (((uint32_t) *(str++)) & 0x3F) << 6;
                            uniChar |= *(str++) & 0x3F;
                            if (uniChar > 0xFFFF) {
                                /* TODO - support extended (non-BMP) Unicode */
                                (void) strcpy(escBuff, "\\u001A");
                            } else {
                                (void) sprintf(escBuff, "\\u%04X", uniChar);
                            }
                            len -= 2;
                        }
                    }
                } else {
                    /* Two-byte blk */
                    uniChar = (((uint32_t) ch) & 0x1F) << 6;
                    uniChar |= *(str++) & 0x3F;
                    (void) sprintf(escBuff, "\\u%04X", uniChar);
                    len--;
                }
            }
            if (WXBuffer_Append(buffer, escBuff, 6, TRUE) == NULL) return NULL;
            blk = str;
        } else if ((ch == '"') || (ch == '\\') || (ch == '/')) {
            if ((l = (str - blk) - 1) > 0) {
                if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
            }
            escBuff[1] = ch;
            if (WXBuffer_Append(buffer, escBuff, 2, TRUE) == NULL) return NULL;
            blk = str;
        } else if (ch < 0x20) {
            if ((l = (str - blk) - 1) > 0) {
                if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
            }
            switch (ch) {
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t':
                    if (ch == '\b') ch = 'b';
                    else if (ch == '\f') ch = 'f';
                    else if (ch == '\n') ch = 'n';
                    else if (ch == '\r') ch = 'r';
                    else if (ch == '\t') ch = 't';
                    escBuff[1] = ch;
                    if (WXBuffer_Append(buffer, escBuff, 2,
                                        TRUE) == NULL) return NULL;
                    break;
                default:
                    (void) sprintf(escBuff, "\\u00%02X", ch);
                    if (WXBuffer_Append(buffer, escBuff, 6,
                                        TRUE) == NULL) return NULL;
                    break;
            }
            blk = str;
        } else {
            /* Just a regular character, track as a block */
        }
    }
    if ((l = (str - blk)) > 0) {
        if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
    }

    return buffer->buffer;
}

/* For performance/simplicity, use a character mapping table and fn */
#define ESCAPE_ATTR 1
#define ESCAPE_CONTENT 2

/* Follow best practice, always escape <&>, escape '" in attributes */
static uint8_t xmlEscFlags[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint8_t *xmlEnc(WXBuffer *buffer, unsigned char ch) {
    switch (ch) {
        case '&':
            return WXBuffer_Append(buffer, "&amp;", 5, TRUE);
        case '<':
            return WXBuffer_Append(buffer, "&lt;", 4, TRUE);
        case '>':
            return WXBuffer_Append(buffer, "&gt;", 4, TRUE);
        case '"':
            return WXBuffer_Append(buffer, "&quot;", 6, TRUE);
        case '\'':
            return WXBuffer_Append(buffer, "&apos;", 6, TRUE);
    }

    return buffer->buffer;
}

/**
 * Escape unsafe character sequences for XML attribute value inclusion.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXML_EscapeAttribute(WXBuffer *buffer, char *str, int len) {
    unsigned char ch;
    char *blk = str;
    int l;

    if (len < 0) len = strlen(str);
    while (len > 0) {
        ch = (unsigned char) *(str++);
        len--;

        if (xmlEscFlags[ch] & ESCAPE_ATTR) {
            if ((l = (str - blk) - 1) > 0) {
                if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
            }
            if (xmlEnc(buffer, ch) == NULL) return NULL;
            blk = str;
        } else {
            /* Just a regular character, track as a block */
        }
    }
    if ((l = (str - blk)) > 0) {
        if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
    }

    return buffer->buffer;
}

/**
 * Escape unsafe character sequences for XML content inclusion.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXML_EscapeContent(WXBuffer *buffer, char *str, int len) {
    unsigned char ch;
    char *blk = str;
    int l;

    if (len < 0) len = strlen(str);
    while (len > 0) {
        ch = (unsigned char) *(str++);
        len--;

        if (xmlEscFlags[ch] & ESCAPE_CONTENT) {
            if ((l = (str - blk) - 1) > 0) {
                if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
            }
            if (xmlEnc(buffer, ch) == NULL) return NULL;
            blk = str;
        } else {
            /* Just a regular character, track as a block */
        }
    }
    if ((l = (str - blk)) > 0) {
        if (WXBuffer_Append(buffer, blk, l, TRUE) == NULL) return NULL;
    }

    return buffer->buffer;
}
