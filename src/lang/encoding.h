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
uint8_t *WXIndent(WXBuffer *buffer, int indent);

/**
 * Escape unsafe JSON character sequences in the provided string.
 *
 * @param buffer Buffer to escape the provided text onto the end of.
 * @param str Text to be encoded.
 * @return Pointer to the buffer contents or NULL on memory error.
 */
uint8_t *WXJSON_EscapeString(WXBuffer *buffer, char *str);

#endif