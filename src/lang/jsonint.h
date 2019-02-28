/*
 * Structures and methods for parsing, representing and generating JSON data.
 *
 * Copyright (C) 2015-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_JSONINT_H
#define WX_JSONINT_H 1

#include "json.h"

/* Note: internals of parser are exposed for test but not for general use! */

/*
 * Enumeration of lexical token types.  Note that the lexer isn't entirely
 * agnostic of the language, JSON is so simply defined that the lexer can
 * partially validate the language.
 */
typedef enum {
    WXJSONTK_ERROR = -99,
    WXJSONTK_EOF = -1,
    WXJSONTK_START = 0x00,
    WXJSONTK_OBJ_START = 0x01,
    WXJSONTK_ARR_START = 0x02,
    WXJSONTK_COLON = 0x03,
    WXJSONTK_COMMA = 0x04,

    WXJSONTK_VALUE = 0x100,
    WXJSONTK_VALUE_TRUE = 0x101,
    WXJSONTK_VALUE_FALSE = 0x102,
    WXJSONTK_VALUE_NULL = 0x103,
    WXJSONTK_VALUE_INT = 0x104,
    WXJSONTK_VALUE_DBL = 0x105,
    WXJSONTK_VALUE_STR = 0x106,
    WXJSONTK_VALUE_OBJ_END = 0x107,
    WXJSONTK_VALUE_ARR_END = 0x108
} WXJSONTokenType;

/* Lexically parsed token definition, see comment above */
typedef struct {
    WXJSONTokenType type;

    union {
        long long int ival;
        double dval;
        char *sval;
        WXJSONErrorCode errorCode;
    } value;
} WXJSONToken;

/* And the corresponding lexical tracking object for parsing content */
typedef struct {
    const char *content;
    unsigned int offset, lineNumber;
    WXJSONToken lastToken;
} WXJSONLexer;

/**
 * Initialize a lexer instance for the provided content.  Just an internal
 * setup wrapper.
 *
 * @param lexer The lexer instance to initialize.
 * @param content The string JSON content to be parsed.
 */
void WXJSONLexerInit(WXJSONLexer *lexer, const char *content);

/**
 * Internal method (except for testing) to obtain the next token from the
 * lexer.
 *
 * @param lexer The lexer instance to retrieve the token from.
 * @return Pointer to the next token (reference to the internal lastToken
 *         element).
 */
WXJSONToken *WXJSONLexerNext(WXJSONLexer *lexer);

/**
 * For closure, destruction method for lexer content.  Does not release the
 * object itself and really only resets internal parsing details.
 *
 * @param lexer The lexer instance to be destroyed.
 */
void WXJSONLexerDestroy(WXJSONLexer *lexer);

#endif
