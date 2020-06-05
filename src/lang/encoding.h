/*
 * Methods for handling various data encodings (typically language specific).
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_ENCODING_H
#define WX_ENCODING_H 1

#include "buffer.h"

/**
 * Generate an indent of the number of spaces onto the provided buffer.  Not
 * worth another file...
 *
 * @param buffer Buffer to append the requested indent onto.
 * @param indent Number of spaces to indent by.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXIndent(WXBuffer *buffer, unsigned int indent);

/**
 * Escape unsafe JSON character sequences in the provided string.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXJSON_EscapeString(WXBuffer *buffer, char *str, int len);

/**
 * Escape unsafe character sequences for XML attribute value inclusion.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXML_EscapeAttribute(WXBuffer *buffer, char *str, int len);

/**
 * Escape unsafe character sequences for XML content inclusion.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXML_EscapeContent(WXBuffer *buffer, char *str, int len);

/**
 * Escape unsafe characters in an open URI specification.
 *
 * @param buffer Buffer to escape the provided URI onto.
 * @param str URI to be escaped.
 * @param len Length of text to escape, -1 for string length.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXURL_EscapeURI(WXBuffer *buffer, char *str, int len);

#endif
