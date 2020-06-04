/*
 * Method implementations for parsing, representing and generating XML data.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "xmlint.h"
#include <ctype.h>

/* The original MiniXML was all regex, then I learned about lexical parsing */

static char *_memFail = "Internal error: memory allocation failure";

/**
 * Initialize a lexer instance for the provided content.  Just an internal
 * setup wrapper.
 *
 * @param lexer The lexer instance to initialize.
 * @param content The string XML content to be parsed.
 */
void WXMLLexerInit(WXMLLexer *lexer, const char *content) {
    /* Really just a setup for the real work */
    lexer->content = content;
    lexer->offset = 0;
    lexer->lineNumber = 1;
    lexer->lastToken.type = WXMLTK_DOC_START;
    /* Note: whitespace at the start of the document is ignorable */
    lexer->ignoreWhitespace = TRUE;
    lexer->inElementTag = FALSE;
}

/* Because there are several consumers, commons to track line processing */
static void _munch(WXMLLexer *lexer, char *ptr, char *eptr) {
    char ch;

    /* Just count newlines up to but not included the end pointer */
    while (ptr < eptr) {
        ch = *(ptr++);
        if ((ch == '\r') && (ptr < eptr) && (*ptr == '\n')) {
            /* Collapse LF-CR into single CR (avoid double line count) */
            ch = *(ptr++);
        }
        if ((ch == '\r') || (ch == '\n')) lexer->lineNumber++;
    }
}

/* For speed, use a static table for matching identifier characters */
/* TODO - are the upper ASCII ranges truly valid XML identifier characters? */
#define WXML_ID_START 1
#define WXML_ID_CHAR 2

static uint8_t xmlIdFlags[]= {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 0, 0, 0, 0, 0,
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,
    0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
};

/* Common method to allocate/copy content from source, with collapse */
static WXMLTokenType _allocTextToken(WXMLLexer *lexer, WXMLTokenType type,
                                     char *ptr, int len, int condense,
                                     char *errorMsg, int errorMsgLen) {
    char *str, *enc, *eptr;
    int l, ch;

    /* Clone from source */
    lexer->lastToken.val = WXCalloc(len + 1);
    if (lexer->lastToken.val == NULL) {
        (void) snprintf(errorMsg, errorMsgLen, "%s", _memFail);
        return (lexer->lastToken.type = WXMLTK_ERROR);
    }
    (void) memcpy(lexer->lastToken.val, ptr, len);
    lexer->lastToken.val[len] = '\0';

    /* Condense character entities if so indicated */
    if (condense) {
        str = lexer->lastToken.val;
        while (str != NULL) {
            str = strchr(str, '&');
            if (str != NULL) {
                l = strlen(enc = str + 1);
                if (strncmp(enc, "amp;", 4) == 0) {
                    *(str++) = '&';
                    (void) memmove(enc, enc + 4, l - 3);
                } else if (strncmp(enc, "apos;", 5) == 0) {
                    *(str++) = '\'';
                    (void) memmove(enc, enc + 5, l - 4);
                } else if (strncmp(enc, "lt;", 3) == 0) {
                    *(str++) = '<';
                    (void) memmove(enc, enc + 3, l - 2);
                } else if (strncmp(enc, "gt;", 3) == 0) {
                    *(str++) = '>';
                    (void) memmove(enc, enc + 3, l - 2);
                } else if (strncmp(enc, "quot;", 5) == 0) {
                    *(str++) = '"';
                    (void) memmove(enc, enc + 5, l - 4);
                } else if ((*(enc++) == '#') &&
                               (isdigit(*enc) || (*enc == 'x'))) {
                    if (*enc == 'x') {
                        ch = strtol(enc + 1, &eptr, 16);
                    } else {
                        ch = strtol(enc, &eptr, 10);
                    }
                    if (*eptr != ';') {
                        (void) snprintf(errorMsg, errorMsgLen,
                                       "Invalid numeric character entity ref"
                                       "erence (line %d)", lexer->lineNumber);
                        WXFree(lexer->lastToken.val);
                        lexer->lastToken.val = NULL;
                        return (lexer->lastToken.type = WXMLTK_ERROR);
                    }
                    /* TODO - this does not support full Unicode properly! */
                    *(str++) = (ch & 0xFF);
                    (void) memmove(enc - 1, eptr + 1, l - (eptr - enc) + 2);
                } else {
                    (void) snprintf(errorMsg, errorMsgLen,
                                   "Invalid character entity reference "
                                   "(line %d)", lexer->lineNumber);
                    WXFree(lexer->lastToken.val);
                    lexer->lastToken.val = NULL;
                    return (lexer->lastToken.type = WXMLTK_ERROR);
                }
            }
        }
    }

    return (lexer->lastToken.type = type);
}

/**
 * Obtain the next lexical token for the parser.  Some prevalidations occur
 * here, based on the associated grammar details.
 *
 * @param lexer The lexer instance to retrieve the token from.
 * @param errorMsg External buffer for returning parsing error details.
 * @param errorMsgLen Length of provided buffer.
 * @return Token identifier for the retrieved token (lastToken contains data).
 */
WXMLTokenType WXMLLexerNext(WXMLLexer *lexer, char *errorMsg, int errorMsgLen) {
    char ch, *start, *ptr = (char *) (lexer->content + lexer->offset), *eptr;

    /* Flush prior content */
    lexer->lastToken.val = NULL;

    /* Read to end of file (or a token occurs) */
    start = ptr;
    while ((ch = *(ptr++)) != '\0') {
        /* Consume white space if in ignore context (element tag) */
        if (isspace(ch) && (lexer->ignoreWhitespace)) {
            while (isspace(*ptr)) ptr++;
            _munch(lexer, start, ptr);
            start = ptr;
            continue;
        }

        /* Direct appropriately, note that < is the real star of the show */
        if (ch == '<') {
            if (strncmp(ptr, "!--", 3) == 0) {
                eptr = strstr(ptr + 3, "-->");
                if (eptr == NULL) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated comment "
                                    "(line %d)", lexer->lineNumber);
                    return (lexer->lastToken.type = WXMLTK_ERROR);
                } else {
                    _munch(lexer, start + 4, eptr);
                    start = ptr = eptr + 3;
					continue;
                }
            } else if (strncmp(ptr, "![CDATA[", 8) == 0) {
                eptr = strstr(ptr + 8, "]]>");
                if (eptr == NULL) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated CDATA content "
                                    "(line %d)", lexer->lineNumber);
                    return (lexer->lastToken.type = WXMLTK_ERROR);
                } else {
                    _munch(lexer, start, eptr);
                    lexer->offset = eptr + 3 - lexer->content;
                    return _allocTextToken(lexer, WXMLTK_CONTENT, ptr + 8,
                                           eptr - ptr - 8, FALSE, errorMsg,
                                           errorMsgLen);
                }
            } else if (*ptr == '!') {
                eptr = strchr(ptr + 1, '>');
                if (eptr == NULL) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated DTD directive  "
                                    "(line %d)", lexer->lineNumber);
                    return (lexer->lastToken.type = WXMLTK_ERROR);
                } else {
                    /* Just ignore DTD elements */
                    _munch(lexer, start, eptr);
                    start = ptr = eptr + 1;
					continue;
                }
            } else if (*ptr == '?') {
                lexer->inElementTag = lexer->ignoreWhitespace = TRUE;
                lexer->offset = ptr - lexer->content + 1;
                return (lexer->lastToken.type = WXMLTK_PI_START);
            } else if (*ptr == '/') {
                lexer->inElementTag = lexer->ignoreWhitespace = TRUE;
                lexer->offset = ptr - lexer->content + 1;
                return (lexer->lastToken.type = WXMLTK_CLOSE_ELMNT_TAG_START);
            } else {
                /* All that's left is just the < element tag start */
                lexer->inElementTag = lexer->ignoreWhitespace = TRUE;
                lexer->offset = ptr - lexer->content;
                return (lexer->lastToken.type = WXMLTK_ELMNT_TAG_START);
            }
        }

        /* Intelligent lex, certain elements only match in tag context */
        if (lexer->inElementTag) {
            if ((ch == '?') && (*ptr == '>')) {
                lexer->inElementTag = lexer->ignoreWhitespace = FALSE;
                lexer->offset = ptr - lexer->content + 1;
                return (lexer->lastToken.type = WXMLTK_PI_END);
            } else if ((ch == '/') && (*ptr == '>')) {
                lexer->inElementTag = lexer->ignoreWhitespace = FALSE;
                lexer->offset = ptr - lexer->content + 1;
                return (lexer->lastToken.type = WXMLTK_EMPTY_ELMNT_TAG_END);
            } else if (ch == '>') {
                lexer->inElementTag = lexer->ignoreWhitespace = FALSE;
                lexer->offset = ptr - lexer->content;
                return (lexer->lastToken.type = WXMLTK_ELMNT_TAG_END);
            } else if (xmlIdFlags[ch] & WXML_ID_START) {
                while (xmlIdFlags[*ptr] & WXML_ID_CHAR) ptr++;
                lexer->offset = ptr - lexer->content;
                return _allocTextToken(lexer, WXMLTK_IDENTIFIER, start,
                                       ptr - start, FALSE, errorMsg,
                                       errorMsgLen);
            } else if (ch == '=') {
                lexer->offset = ptr - lexer->content;
                return (lexer->lastToken.type = WXMLTK_ATTR_EQ);
            } else if (ch == '\'') {
                eptr = strchr(ptr, '\'');
                if (eptr == NULL) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated attr 'value' "
                                    "(line %d)", lexer->lineNumber);
                    return (lexer->lastToken.type = WXMLTK_ERROR);
                } else {
                    _munch(lexer, ptr, eptr);
                    lexer->offset = eptr - lexer->content + 1;
                    return _allocTextToken(lexer, WXMLTK_ATTR_VALUE, ptr,
                                           eptr - ptr, TRUE, errorMsg,
                                           errorMsgLen);
                }
            } else if (ch == '"') {
                eptr = strchr(ptr, '"');
                if (eptr == NULL) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated attr \"value\" "
                                    "(line %d)", lexer->lineNumber);
                    return (lexer->lastToken.type = WXMLTK_ERROR);
                } else {
                    _munch(lexer, start, eptr);
                    lexer->offset = eptr - lexer->content + 1;
                    return _allocTextToken(lexer, WXMLTK_ATTR_VALUE, ptr,
                                           eptr - ptr, TRUE, errorMsg,
                                           errorMsgLen);
                }
            } else {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: invalid text in element tag "
                                "(line %d)", lexer->lineNumber);
                return (lexer->lastToken.type = WXMLTK_ERROR);
            }
        }

        /* If we get to here, it's just content until the next tag */
        eptr = strchr(start, '<');
        if (eptr == NULL) eptr = start + strlen(start);
        _munch(lexer, start, eptr);
        lexer->offset = eptr - lexer->content;
        return _allocTextToken(lexer, WXMLTK_CONTENT, start, eptr - start,
                               TRUE, errorMsg, errorMsgLen);
    }

    lexer->offset = ptr - lexer->content - 1;
    return (lexer->lastToken.type = WXMLTK_EOF);
}

/**
 * For closure, destruction method for lexer content.  Does not release the
 * object itself and really only resets internal parsing details.
 *
 * @param lexer The lexer instance to be destroyed.
 */
void WXMLLexerDestroy(WXMLLexer *lexer) {
    /* Guess I have this for consistency but boooorrrrriiiinnnnngggg.... */
    lexer->content = NULL;
    lexer->offset = 0;
}

/**
 * Parse/decode XML text, returning a corresponding document representation.
 *
 * @param content The XML document/content to be parsed.
 * @param errorMsg External buffer for returning parsing error details.
 * @param errorMsgLen Length of provided buffer.
 * @return The document root instance, or NULL on parsing or memory failure.
 */
WXMLElement *WXML_Decode(const char *content, char *errorMsg, int errorMsgLen) {
    WXMLElement *retval = NULL, *current = NULL, *wkg;
    WXMLAttribute *attr = NULL;
    unsigned int lineNo;
    WXMLTokenType type;
    WXMLNamespace *ns;
    WXMLLexer lexer;
    char *nm, *tmp;

    /* Parse away, use stack instead of recursive descent */
    *errorMsg = '\0';
    WXMLLexerInit(&lexer, content);
    while ((lexer.lastToken.type != WXMLTK_ERROR) &&
                       (lexer.lastToken.type != WXMLTK_EOF)) {
        /* Top level has a limited set of possbilities */
        type = WXMLLexerNext(&lexer, errorMsg, errorMsgLen);
        if ((type == WXMLTK_ERROR) || (type == WXMLTK_EOF)) break;

        /* Use if instead of cases here so that 'break's exit the full loop */
        if (type == WXMLTK_PI_START) {
            /* At some point, might act on content, consume for now */
            lineNo = lexer.lineNumber;
            while (type != WXMLTK_PI_END) {
                type = WXMLLexerNext(&lexer, errorMsg, errorMsgLen);
                if (type == WXMLTK_ERROR) break;
                if (type == WXMLTK_EOF) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated processing "
                                    "instruction (line %d)", lineNo);
                    if (retval != NULL) WXML_Destroy(retval);
                    retval = current = NULL;
                    break;
                }
                if (lexer.lastToken.val != NULL) {
                    WXFree(lexer.lastToken.val);
                    lexer.lastToken.val = NULL;
                }
            }

            /* Reset the whitespace if we aren't in element context */
            if (current == NULL) lexer.ignoreWhitespace = TRUE;

        } else if (type == WXMLTK_ELMNT_TAG_START) {
            /* First it needs a name */
            type = WXMLLexerNext(&lexer, errorMsg, errorMsgLen);
            if (type == WXMLTK_ERROR) break;
            if (type != WXMLTK_IDENTIFIER) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: Missing name in opening tag "
                                "(line %d)", lexer.lineNumber);
                if (retval != NULL) WXML_Destroy(retval);
                retval = current = NULL;
                break;
            }

            /* At this point, we can create the element in the stack */
            wkg = (WXMLElement *) WXCalloc(sizeof(WXMLElement));
            if (wkg == NULL) goto memfail;
            wkg->name = lexer.lastToken.val;
            lexer.lastToken.val = NULL;

            if (current != NULL) {
                /* Work into the chain */
                if (current->children == NULL) {
                    current->children = current->lastChild = wkg;
                } else {
                    current->lastChild->next = wkg;
                    current->lastChild = wkg;
                }
                wkg->parent = current;
                /* Inherits namespace from parent */
                wkg->namespaceSet = current->namespaceSet;
                current = wkg;
            } else {
                /* This is the root node, but there can only be one! */
                if (retval != NULL) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: Multiple root elements are "
                                    "defined (line %d)", lexer.lineNumber);
                    WXML_Destroy(retval);
                    WXML_Destroy(wkg);
                    retval = current = NULL;
                    break;
                } else {
                    retval = current = wkg;
                }
            }

            /* Process the remaining tag elements (attributes) */
            lineNo = lexer.lineNumber;
            while (type != WXMLTK_EOF) {
                type = WXMLLexerNext(&lexer, errorMsg, errorMsgLen);
                if ((type == WXMLTK_ERROR) ||
                        (type == WXMLTK_ELMNT_TAG_END) ||
                        (type == WXMLTK_EMPTY_ELMNT_TAG_END)) break;
                if (type == WXMLTK_EOF) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: unterminated element tag "
                                    "(line %d)", lineNo);
                    if (retval != NULL) WXML_Destroy(retval);
                    retval = current = NULL;
                    break;
                }

                if (type == WXMLTK_IDENTIFIER) {
                    /* This can be an attribute or a namespace definition */
                    attr = NULL;
                    nm = lexer.lastToken.val;
                    if ((strncmp(nm, "xmlns", 5) == 0) &&
                              ((nm[5] == ':') || (nm[5] == '\0'))) {
                        /* URI must be provided */
                        if ((WXMLLexerNext(&lexer, errorMsg,
                                           errorMsgLen) != WXMLTK_ATTR_EQ) ||
                              (WXMLLexerNext(&lexer, errorMsg,
                                             errorMsgLen) != WXMLTK_CONTENT)) {
                            if (lexer.lastToken.type != WXMLTK_ERROR) {
                                (void) snprintf(errorMsg, errorMsgLen,
                                                "Syntax error: namespaces "
                                                "require URI value (line %d)",
                                                lexer.lineNumber);
                            }
                            WXFree(nm);
                            if (retval != NULL) WXML_Destroy(retval);
                            retval = current = NULL;
                            type = WXMLTK_ERROR;
                            break;
                        }
                        
                        /* Hook it all together, local precedes inherited */
                        ns = (WXMLNamespace *) WXCalloc(sizeof(WXMLNamespace));
                        if (ns == NULL) goto memfail;
                        ns->prefix = nm;
                        /* TODO - truncate prefix */
                        ns->href = lexer.lastToken.val;
                        lexer.lastToken.val = NULL;
                        ns->origin = current;
                        ns->next = current->namespaceSet;
                        current->namespaceSet = ns;
                    } else {
                        /* Create the attribute, empty shell ready for assign */
                        attr = (WXMLAttribute *)WXCalloc(sizeof(WXMLAttribute));
                        if (attr == NULL) goto memfail;
                        attr->name = lexer.lastToken.val;
                        lexer.lastToken.val = NULL;
                        if (current->attributes == NULL) {
                            current->attributes = current->lastAttribute = attr;
                        } else {
                            current->lastAttribute->next = attr;
                            current->lastAttribute = attr;
                        }
                    }
                } else if (type == WXMLTK_ATTR_EQ) {
                    /* Only assignable from an attribute definition */
                    if (attr == NULL) {
                        (void) snprintf(errorMsg, errorMsgLen,
                                        "Syntax error: missing identifier for "
                                        "attribute (line %d)", lineNo);
                        if (retval != NULL) WXML_Destroy(retval);
                        retval = current = NULL;
                        break;
                    }

                    /* Requires associated value, transfer to attribute */
                    if (WXMLLexerNext(&lexer, errorMsg,
                                      errorMsgLen) != WXMLTK_ATTR_VALUE) {
                        if (lexer.lastToken.type != WXMLTK_ERROR) {
                            (void) snprintf(errorMsg, errorMsgLen,
                                            "Syntax error: attribute assign"
                                            "ment requires value (line %d)",
                                            lexer.lineNumber);
                        }
                        if (retval != NULL) WXML_Destroy(retval);
                        retval = current = NULL;
                        type = WXMLTK_ERROR;
                        break;
                    }
                    attr->value = lexer.lastToken.val;
                    lexer.lastToken.val = NULL;

                    /* Reset identifier marker */
                    attr = NULL;
                } else {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: invalid text in element tag "
                                    "(line %d)", lineNo);
                    if (retval != NULL) WXML_Destroy(retval);
                    retval = current = NULL;
                    break;
                }
            }

            /* Bail from any errors inside the loop */
            if ((type != WXMLTK_ELMNT_TAG_END) &&
                        (type != WXMLTK_EMPTY_ELMNT_TAG_END)) break;

            /* Element definition is complete, process namespacing */

            /* If the final token was empty close, then pop it */
            if (type == WXMLTK_EMPTY_ELMNT_TAG_END) current = current->parent;

            /* Reset the whitespace if we aren't in element context */
            if (current == NULL) lexer.ignoreWhitespace = TRUE;

        } else if (type == WXMLTK_CLOSE_ELMNT_TAG_START) {
            /* Very linear validation sequence for this element */
            if (current == NULL) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: Unexpected end tag "
                                "encountered (line %d)", lexer.lineNumber);
                if (retval != NULL) WXML_Destroy(retval);
                retval = current = NULL;
                break;
            }

            type = WXMLLexerNext(&lexer, errorMsg, errorMsgLen);
            if (type == WXMLTK_ERROR) break;
            if (type != WXMLTK_IDENTIFIER) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: Missing name in closing tag "
                                "(line %d)", lexer.lineNumber);
                if (retval != NULL) WXML_Destroy(retval);
                retval = current = NULL;
                break;
            }
            if (strcmp(lexer.lastToken.val, current->name) != 0) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: Unmatched closing tag, expected "
                                "</%s> (line %d)", current->name,
                                lexer.lineNumber);
                if (retval != NULL) WXML_Destroy(retval);
                retval = current = NULL;
                break;
            }
            WXFree(lexer.lastToken.val);
            lexer.lastToken.val = NULL;

            type = WXMLLexerNext(&lexer, errorMsg, errorMsgLen);
            if (type == WXMLTK_ERROR) break;
            if (type == WXMLTK_EOF) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: Missing end of closing "
                                "tag (line %d)", lexer.lineNumber);
                if (retval != NULL) WXML_Destroy(retval);
                retval = current = NULL;
                break;
            }
            if (type != WXMLTK_ELMNT_TAG_END) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Syntax error: Extraneous content in closing "
                                "tag (line %d)", lexer.lineNumber);
                if (retval != NULL) WXML_Destroy(retval);
                retval = current = NULL;
                break;
            }

            /* After all that, it's pretty easy to close an element! */
            current = current->parent;

            /* Reset the whitespace if we aren't in element context */
            if (current == NULL) lexer.ignoreWhitespace = TRUE;

        } else if ((type == WXMLTK_CONTENT) && (current != NULL)) {
            /* Just append to what's there (or just initialize) */
            if (current->content == NULL) {
                /* Just steal the allocated value */
                current->content = lexer.lastToken.val;
                lexer.lastToken.val = NULL;
            } else {
                tmp = WXMalloc(strlen(current->content) +
                                   strlen(lexer.lastToken.val) + 1);
                if (tmp == NULL) goto memfail;
                (void) strcpy(tmp, current->content);
                (void) strcat(tmp, lexer.lastToken.val);
                WXFree(current->content);
                current->content = tmp;

                WXFree(lexer.lastToken.val);
                lexer.lastToken.val = NULL;
            }

        } else {
            /* AFAIK, the lexer will not generate other cases, so... */
            (void) snprintf(errorMsg, errorMsgLen,
                            "Syntax error: extraneous content encountered "
                            "(line %d)", lexer.lineNumber);
            if (retval != NULL) WXML_Destroy(retval);
            retval = current = NULL;
            break;
        }
    }

    /* Clean up/error if parse incomplete, dangling content or nothing */
    if (lexer.lastToken.type != WXMLTK_EOF) {
        if (retval != NULL) WXML_Destroy(retval);
        retval = NULL;
    } else if ((retval != NULL) && (current != NULL)) {
        (void) snprintf(errorMsg, errorMsgLen,
                        "End of document reached, unclosed element '%s'",
                        current->name);
        WXML_Destroy(retval);
        retval = NULL;
    } else if ((retval == NULL) && (*errorMsg == '\0')) {
        (void) snprintf(errorMsg, errorMsgLen,
                        "Empty document, no root element found");
    }
    if (lexer.lastToken.val != NULL) WXFree(lexer.lastToken.val);

    return retval;

    /* Ugh, I usually hate goto's but this is the perfect usage */
memfail:
    if (lexer.lastToken.val != NULL) WXFree(lexer.lastToken.val);
    (void) snprintf(errorMsg, errorMsgLen, "%s", _memFail);
    if (retval != NULL) WXML_Destroy(retval);
    return NULL;
}

/**
 * Converse to the above, translate the XML document to text format.
 *
 * @param buffer Buffer into which the XML data should be encoded.
 * @param root The XML document (root) to be encoded.
 * @param prettyPrint If TRUE (non-zero) , pretty-print the XML output,
 *                    otherwise (false/zero) output in compact format.
 * @return The buffer area containing the output document (null terminated)
 *         or NULL if memory allocation failure occurred.
 */
char *WXML_Encode(WXBuffer *buffer, WXMLElement *root, int prettyPrint) {

    return NULL;
}

/**
 * Destroy/release the contents of the provided node/document (and all nested
 * content).  This method will also free the value itself (consistent with
 * the allocated return from the parse method).
 *
 * @param root The XML node/tree to be destroyed/freed.
 */
void WXML_Destroy(WXMLElement *root) {
    WXMLNamespace *ns = root->namespaceSet, *nextNs;
    WXMLAttribute *attr = root->attributes, *nextAttr;
    WXMLElement *child = root->children, *nextChild;

    /* Recursively deal with children (do this first due to NS climbing) */
    while (child != NULL) {
        nextChild = child->next;
        WXML_Destroy(child);
        child = nextChild;
    }

    /* Release associated attributes and namespaces owned by this node */
    while (attr != NULL) {
        nextAttr = attr->next;
        WXFree(attr->name);
        if (attr->value != NULL) WXFree(attr->value);
        WXFree(attr);
        attr = nextAttr;
    }
    while (ns != NULL) {
        nextNs = ns->next;
        if (ns->origin == root) {
            WXFree(ns->prefix);
            WXFree(ns->href);
            WXFree(ns);
        }
    }

    /* Finally, discard associated details and the node itself */
    WXFree(root->name);
    if (root->content != NULL) WXFree(root->content);
    WXFree(root);
}
