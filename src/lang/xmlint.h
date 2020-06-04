/*
 * Internal elements for the XML data parser.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_XMLINT_H
#define WX_XMLINT_H 1

#include "xml.h"

/* Note: internals of parser are exposed for test but not for general use! */

/*
 * Enumeration of the lexical token types.
 */
typedef enum {
    WXMLTK_ERROR = -2,
    WXMLTK_EOF = -1,
    WXMLTK_DOC_START = 0,
    WXMLTK_PI_START = 10,
    WXMLTK_PI_END = 11,

    WXMLTK_ELMNT_TAG_START = 20,
    WXMLTK_CLOSE_ELMNT_TAG_START = 21,
    WXMLTK_ELMNT_TAG_END = 22,
    WXMLTK_EMPTY_ELMNT_TAG_END = 23,

    WXMLTK_IDENTIFIER = 30,
    WXMLTK_ATTR_EQ = 31,
    WXMLTK_ATTR_VALUE = 32,

    WXMLTK_CONTENT = 40
} WXMLTokenType;

/* Lexically parsed token definition, see comments above */
typedef struct {
    WXMLTokenType type;
    char *val;
} WXMLToken;

/* And the corresponding lexical tracking object for parsing content */
typedef struct {
    const char *content;
    unsigned int offset, lineNumber;
    int ignoreWhitespace, inElementTag;
    WXMLToken lastToken;
} WXMLLexer;

/**
 * Initialize a lexer instance for the provided content.  Just an internal
 * setup wrapper.
 *
 * @param lexer The lexer instance to initialize.
 * @param content The string XML content to be parsed.
 */
void WXMLLexerInit(WXMLLexer *lexer, const char *content);

/**
 * Obtain the next lexical token for the parser.  Some prevalidations occur
 * here, based on the associated grammar details.
 *
 * @param lexer The lexer instance to retrieve the token from.
 * @param errorMsg External buffer for returning parsing error details.
 * @param errorMsgLen Length of provided buffer.
 * @return Token identifier for the retrieved token (lastToken contains data).
 */
WXMLTokenType WXMLLexerNext(WXMLLexer *lexer, char *errorMsg, int errorMsgLen);

/**
 * For closure, destruction method for lexer content.  Does not release the
 * object itself and really only resets internal parsing details.
 *
 * @param lexer The lexer instance to be destroyed.
 */
void WXMLLexerDestroy(WXMLLexer *lexer);

#endif
