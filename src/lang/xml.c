/*
 * Method implementations for parsing, representing and generating XML data.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "xmlint.h"
#include "encoding.h"
#include <ctype.h>

/* The original MiniXML was all regex, then I learned about lexical parsing */

/* These appear up front so the parser can use them... */

/* Wrap with duplication and NULL handling */
static char *_xmlStrDup(const char *val) {
    char *retval;

    if (val == NULL) return NULL;
    retval = (char *) WXMalloc(strlen(val) + 1);
    if (retval != NULL) (void) strcpy(retval, val);
    return retval;
}

/**
 * Utility method for allocating XML element instances, for manually creating
 * a DOM tree.
 *
 * @param parent The parent element, NULL for allocating a root element.
 * @param name The name of the element, may be duplicated based on flag.
 * @param namespace Reference to the namespace associated to this element, if
 *                  applicable.  If the origin of the provided namespace is
 *                  NULL (locally allocated), a namespace is created for this
 *                  element instead, obeying the duplicate flag.
 * @param content Optional content for this element, may be duplicated based
 *                on flag.
 * @param duplicate If FALSE, *all* provided information is allocated and
 *                  belongs to the element.  If TRUE, name, content and
 *                  namespace details are duplicated.
 * @return The element instance or NULL on memory allocation failure.
 */
WXMLElement *WXML_AllocateElement(WXMLElement *parent, const char *name,
                                  WXMLNamespace *ns, const char *content,
                                  int duplicate) {
    WXMLElement *elmnt = (WXMLElement *) WXCalloc(sizeof(WXMLElement));
    if (elmnt == NULL) return NULL;

    if (!duplicate) {
        elmnt->name = (char *) name;
        elmnt->content = (char *) content;
    } else {
        elmnt->name = (name != NULL) ? _xmlStrDup(name) : NULL;
        if (content != NULL) elmnt->content = _xmlStrDup(content);
        if (((name != NULL) && (elmnt->name == NULL)) ||
                ((content != NULL) && (elmnt->content == NULL))) {
            if (elmnt->content != NULL) WXFree(elmnt->content);
            if (elmnt->name != NULL) WXFree(elmnt->name);
            WXFree(elmnt);
            return NULL;
        }
    }

    /* Create the local element namespace if unassociated */
    if (ns != NULL) {
        if (ns->origin == NULL) {
            elmnt->namespace = WXML_AllocateNamespace(elmnt, ns->prefix,
                                                      ns->href, duplicate);
            if (elmnt->namespace == NULL) {
                if (elmnt->content != NULL) WXFree(elmnt->content);
                if (elmnt->name != NULL) WXFree(elmnt->name);
                WXFree(elmnt);
                return NULL;
            }
        } else {
            elmnt->namespace = ns;
        }
    }

    /* Connect to parent/child list, watch namespace inheritance with local */
    elmnt->parent = parent;
    if (parent != NULL) {
        if (parent->children == NULL) {
            parent->children = parent->lastChild = elmnt;
        } else {
            parent->lastChild->next = elmnt;
            parent->lastChild = elmnt;
        }

        if ((ns != NULL) && (ns->origin == NULL)) {
            elmnt->namespaceSet->next = parent->namespaceSet;
        } else {
            elmnt->namespaceSet = parent->namespaceSet;
        }
    }

    return elmnt;
}

/**
 * Utility method for allocating XML namespace instances, for manually creating
 * a DOM tree.
 *
 * @param origin The element to which this namespace is associated (scope).
 * @param prefix The prefix identifier for the namespace, may be duplicated.
 * @param href The URI associated to the namespace, may be duplicated.
 * @param duplicate If FALSE, *all* provided information is allocated and
 *                  belongs to the namespace.  If TRUE, prefix and href details
 *                  are duplicated.
 * @return The namespace instance or NULL on memory allocation failure.
 */
WXMLNamespace *WXML_AllocateNamespace(WXMLElement *origin, const char *prefix,
                                      const char *href, int duplicate) {
    WXMLNamespace *ns = (WXMLNamespace *) WXCalloc(sizeof(WXMLNamespace));
    if (ns == NULL) return NULL;

    if (!duplicate) {
        ns->prefix = (char *) prefix;
        ns->href = (char *) href;
    } else {
        ns->prefix = _xmlStrDup(prefix);
        ns->href = _xmlStrDup(href);
        if ((ns->prefix == NULL) || (ns->href == NULL)) {
            if (ns->href != NULL) WXFree(ns->href);
            if (ns->prefix != NULL) WXFree(ns->prefix);
            WXFree(ns);
            return NULL;
        }
    }

    /* Here, namespace is injected to head to handle inheritance */
    ns->origin = origin;
    ns->next = origin->namespaceSet;
    origin->namespaceSet = ns;

    return ns;
}

/**
 * Utility method for allocating XML attribute instances, for manually creating
 * a DOM tree.
 *
 * @param elmnt The element to which this attribute is attached.
 * @param name The name/identifier of the attribute, may be duplicated
 * @param namespace Reference to the namespace associated to this attribute, if
 *                  applicable.  Pay attention to the origin/scoping of the
 *                  namespace and related elements.
 * @param value The optional associated attribute value, may be duplicated.
 * @param duplicate If FALSE, *all* provided information is allocated and
 *                  belongs to the namespace.  If TRUE, name and value details
 *                  are duplicated.
 * @return The attribute instance or NULL on memory allocation failure.
 */
WXMLAttribute *WXML_AllocateAttribute(WXMLElement *elmnt, const char *name,
                                      WXMLNamespace *namespace, const char *val,
                                      int duplicate) {
    WXMLAttribute *attr = (WXMLAttribute *) WXCalloc(sizeof(WXMLAttribute));
    if (attr == NULL) return NULL;

    if (!duplicate) {
        attr->name = (char *) name;
        attr->value = (char *) val;
    } else {
        attr->name = _xmlStrDup(name);
        if (val != NULL) attr->value = _xmlStrDup(val);
        if ((attr->name == NULL) || ((val != NULL) && (attr->value == NULL))) {
            if (attr->value != NULL) WXFree(attr->value);
            if (attr->name != NULL) WXFree(attr->name);
            WXFree(attr);
            return NULL;
        }
    }

    /* Just initialize or append to list */
    if (elmnt->attributes == NULL) {
        elmnt->attributes = elmnt->lastAttribute = attr;
    } else {
        elmnt->lastAttribute->next = attr;
        elmnt->lastAttribute = attr;
    }

    return attr;
}

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
                l = strlen(enc = (str + 1));
                if (strncmp(enc, "amp;", 4) == 0) {
                    *(str++) = '&';
                    (void) memmove(str, enc + 4, l - 3);
                } else if (strncmp(enc, "apos;", 5) == 0) {
                    *(str++) = '\'';
                    (void) memmove(str, enc + 5, l - 4);
                } else if (strncmp(enc, "lt;", 3) == 0) {
                    *(str++) = '<';
                    (void) memmove(str, enc + 3, l - 2);
                } else if (strncmp(enc, "gt;", 3) == 0) {
                    *(str++) = '>';
                    (void) memmove(str, enc + 3, l - 2);
                } else if (strncmp(enc, "quot;", 5) == 0) {
                    *(str++) = '"';
                    (void) memmove(str, enc + 5, l - 4);
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
                    (void) memmove(str, eptr + 1, l - (eptr - enc) - 1);
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
            } else if (xmlIdFlags[(int) ch] & WXML_ID_START) {
                while (xmlIdFlags[(int) *ptr] & WXML_ID_CHAR) ptr++;
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

/* Used in several places below */
static WXMLNamespace *_assignNS(WXMLElement *elmnt, char *name,
                                WXMLNamespace *dflt) {
    WXMLNamespace *ns = elmnt->namespaceSet;
    char *colon = strchr(name, ':');

    if (colon != NULL) {
        *colon = '\0';
        while (ns != NULL) {
            if (strcmp(ns->prefix, name) == 0) {
                break;
            }
            ns = ns->next;
        }

        if (ns == NULL) {
            /* In the interest of utility, ignore unmatched namespace */
            /* Technically correct, colons are not formally reserved in spec */
            *colon = ':';
            return dflt;
        } else {
            /* Strip the prefix */
            (void) memmove(name, colon + 1, strlen(colon + 1) + 1);
            return ns;
        }
    }

    return dflt;
}

/**
 * Parse/decode XML text, returning a corresponding document representation.
 *
 * @param content The XML document/content to be parsed.
 * @param retainTextFragments If TRUE (non-zero), text fragments are retained
 *                            (NULL-named children) and consolidated in parent
 *                            element.  If FALSE, only consolidated content is
 *                            captured.
 * @param errorMsg External buffer for returning parsing error details.
 * @param errorMsgLen Length of provided buffer.
 * @return The document root instance, or NULL on parsing or memory failure.
 */
WXMLElement *WXML_Decode(const char *content, int retainTextFragments,
                         char *errorMsg, int errorMsgLen) {
    WXMLElement *retval = NULL, *current = NULL;
    WXMLNamespace *ns, *dfltNs;
    WXMLAttribute *attr = NULL;
    unsigned int lineNo;
    WXMLTokenType type;
    WXMLLexer lexer;
    char *nm, *tmp;
    int offset;

    /* Parse away, use stack instead of recursive descent */
    *errorMsg = '\0';
    WXMLLexerInit(&lexer, content);
    while ((lexer.lastToken.type != WXMLTK_ERROR) &&
                       (lexer.lastToken.type != WXMLTK_EOF)) {
        /* Top level has a limited set of possibilities */
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

            /* There can only be one root */
             if ((current == NULL) && (retval != NULL)) {
                 (void) snprintf(errorMsg, errorMsgLen,
                                 "Syntax error: Multiple root elements are "
                                 "defined (line %d)", lexer.lineNumber);
                 WXML_Destroy(retval);
                 retval = current = NULL;
                 break;
            }

            /* At this point, we can create the element in the stack */
            current = WXML_AllocateElement(current, lexer.lastToken.val,
                                           NULL, NULL, FALSE);
            if (current == NULL) goto memfail;
            lexer.lastToken.val = NULL;
            if (retval == NULL) retval = current;

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
                                           errorMsgLen) != WXMLTK_ATTR_VALUE)) {
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

                        /* Hook it all together, remove identifier marker */
                        offset = (nm[5] == ':') ? 6 : 5;
                        (void) memmove(nm, nm + offset,
                                       strlen(nm + offset) + 1);
                        ns = WXML_AllocateNamespace(current, nm,
                                                    lexer.lastToken.val, FALSE);
                        if (ns == NULL) goto memfail;
                        lexer.lastToken.val = NULL;
                    } else {
                        /* Create the attribute, empty shell ready for assign */
                        attr = WXML_AllocateAttribute(current,
                                                      lexer.lastToken.val,
                                                      NULL, NULL, FALSE);
                        if (attr == NULL) goto memfail;
                        lexer.lastToken.val = NULL;
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
            dfltNs = current->namespaceSet;
            while (dfltNs != NULL) {
                if (*(dfltNs->prefix) == '\0') break;
                dfltNs = dfltNs->next;
            }
            current->namespace = _assignNS(current, current->name, dfltNs);
            attr = current->attributes;
            while (attr != NULL) {
                attr->namespace = _assignNS(current, attr->name, dfltNs);
                attr = attr->next;
            }

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
            nm = lexer.lastToken.val;
            if ((current->namespace == NULL) ||
                        (*(current->namespace->prefix) == '\0')) {
                /* Non or default namespace, just the standard name */
                if (strcmp(nm, current->name) != 0) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: Unmatched closing tag, "
                                    "expected </%s> (line %d)", current->name,
                                    lexer.lineNumber);
                    if (retval != NULL) WXML_Destroy(retval);
                    retval = current = NULL;
                    break;
                }
            } else {
                offset = strlen(current->namespace->prefix);
                if ((strncmp(nm, current->namespace->prefix, offset) != 0) ||
                        (nm[offset] != ':') ||
                        (strcmp(nm + offset + 1, current->name) != 0)) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Syntax error: Unmatched closing tag, "
                                    "expected </%s:%s> (line %d)",
                                    current->namespace->prefix, current->name,
                                    lexer.lineNumber);
                    if (retval != NULL) WXML_Destroy(retval);
                    retval = current = NULL;
                    break;
                }
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
            /* Allocate text fragment child if so requested */
            if (retainTextFragments) {
                if (WXML_AllocateElement(current, NULL, NULL,
                                         lexer.lastToken.val, TRUE) == NULL) {
                    goto memfail;
                }
            }

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

/* Comparator functions for canonical namespace/attribute sorting */
static int _nsCompar(const void *a, const void *b) {
    WXMLNamespace *org = *((WXMLNamespace **) a);
    WXMLNamespace *cmp = *((WXMLNamespace **) b);

    /* Default namespace always comes first */
    if (*(org->prefix) == '\0') return -1;
    if (*(cmp->prefix) == '\0') return 1;

    /* Otherwise it's in order of prefix */
    return strcmp(org->prefix, cmp->prefix);
}
static int _attrCompar(const void *a, const void *b) {
    WXMLAttribute *org = *((WXMLAttribute **) a);
    WXMLAttribute *cmp = *((WXMLAttribute **) b);
    int orgHasNs = ((org->namespace != NULL) &&
                        (*(org->namespace->prefix) != '\0')) ? TRUE : FALSE;
    int cmpHasNs = ((cmp->namespace != NULL) &&
                        (*(cmp->namespace->prefix) != '\0')) ? TRUE : FALSE;
    int cval;

    /* Unqualified attributes come first and are ordered by name */
    if ((!orgHasNs) && (!cmpHasNs)) {
        return strcmp(org->name, cmp->name);
    }
    if (!orgHasNs) return -1;
    if (!cmpHasNs) return 1;

    /* Both have namespaces, c14n orders by *URI* then name */
    cval = strcmp(org->namespace->href, cmp->namespace->href);
    if (cval != 0) return cval;
    return strcmp(org->name, cmp->name);
}

/* Common methods for writing namespace and attribute entries */
static char *_encodeNamespace(WXBuffer *buffer, WXMLNamespace *ns,
                              int isCanonical) {
    if (WXBuffer_Append(buffer, " xmlns", 6, TRUE) == NULL) return NULL;
    if (*(ns->prefix) != '\0') {
        if (WXBuffer_Append(buffer, ":", 1, TRUE) == NULL) return NULL;
        if (WXBuffer_Append(buffer, ns->prefix, strlen(ns->prefix),
                            TRUE) == NULL) return NULL;
    }

    if (WXBuffer_Append(buffer, "=\"", 2, TRUE) == NULL) return NULL;
    if (WXML_EscapeAttribute(buffer, ns->href, -1,
                             isCanonical) == NULL) return NULL;
    if (WXBuffer_Append(buffer, "\"", 1, TRUE) == NULL) return NULL;

    return buffer->buffer;
}
static char *_encodeAttribute(WXBuffer *buffer, WXMLAttribute *attr,
                              int isCanonical) {
    if (WXBuffer_Append(buffer, " ", 1, TRUE) == NULL) return NULL;
    if ((attr->namespace != NULL) && (*(attr->namespace->prefix) != '\0')) {
        if (WXBuffer_Append(buffer, attr->namespace->prefix,
                            strlen(attr->namespace->prefix),
                            TRUE) == NULL) return NULL;
        if (WXBuffer_Append(buffer, ":", 1, TRUE) == NULL) return NULL;
    }
    if (WXBuffer_Append(buffer, attr->name, strlen(attr->name),
                        TRUE) == NULL) return NULL;

    if (attr->value != NULL) {
        if (WXBuffer_Append(buffer, "=\"", 2, TRUE) == NULL) return NULL;
        if (WXML_EscapeAttribute(buffer, attr->value, -1,
                                 isCanonical) == NULL) return NULL;
        if (WXBuffer_Append(buffer, "\"", 1, TRUE) == NULL) return NULL;
    }

    return buffer->buffer;
}

/* Internal recursion method for encoding */
/* Format is 1 for pretty, 0 for standard, -1/-2 for canonical inc/exc*/
static char *_encodeElement(WXBuffer *buffer, WXMLElement *elmnt,
                            WXMLElement *skip, int format, int indent) {
    int idx, l, cnt, isFirst = TRUE, leader, hasChildElement, isDup;
    WXMLNamespace *ns = elmnt->namespaceSet, **nsSort, *nsChk;
    WXMLAttribute *attr = elmnt->attributes, **attrSort;
    int isCanonical = (format < 0) ? TRUE : FALSE;
    WXMLElement *child = elmnt->children;
    char *start, *end;

    leader = 4 * indent + 1;
    if (WXBuffer_Append(buffer, "<", 1, TRUE) == NULL) return NULL;
    if ((elmnt->namespace != NULL) && (*(elmnt->namespace->prefix) != '\0')) {
        if (WXBuffer_Append(buffer, elmnt->namespace->prefix,
                            l = strlen(elmnt->namespace->prefix),
                            TRUE) == NULL) return NULL;
        if (WXBuffer_Append(buffer, ":", 1, TRUE) == NULL) return NULL;
        leader += l + 1;
    }
    if (WXBuffer_Append(buffer, elmnt->name, l = strlen(elmnt->name),
                        TRUE) == NULL) return NULL;
    leader += l;

    /* Owned namespaces appear first or more complex c14n ordering */
    if (!isCanonical) {
        while (ns != NULL) {
            if (ns->origin != elmnt) break;

            if ((format > 0) && (!isFirst)) {
                if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return NULL;
                if (WXIndent(buffer, leader) == NULL) return NULL;
            }

            if (_encodeNamespace(buffer, ns, FALSE) == NULL) return NULL;

            isFirst = FALSE;
            ns = ns->next;
        }
    } else {
        /* First count namespaces, including inheritance if top-encode */
        /* Note that this may overallocate for exclusive c14n, whatever... */
        cnt = 0;
        while (ns != NULL) {
            if ((ns->origin != elmnt) && (indent != 0)) break;
            cnt++;
            ns = ns->next;
        }

        /* Allocate the sorting array and populate, without duplicates */
        nsSort = (WXMLNamespace **) WXCalloc(cnt * sizeof(WXMLNamespace *));
        if (nsSort == NULL) return NULL;

        ns = elmnt->namespaceSet;
        cnt = 0;
        while (ns != NULL) {
            if (format == -1) {
                /* Inherit parent namespaces for the top encode node */
                if ((ns->origin != elmnt) && (indent != 0)) break;
            } else {
                /* This relies on truly exclusive and self-contained XML */
                /* Does not follow the xml-enc-c14n spec for rendering (3.1) */
                if (ns->origin != elmnt) break;
            }

            /* Scan the higher namespace set for a superfluous instance */
            isDup = FALSE;
            nsChk = ns->next;
            while (nsChk != NULL) {
                if (strcmp(ns->prefix, nsChk->prefix) == 0) {
                    if (strcmp(ns->href, nsChk->href) == 0) {
                        /* Exact match, this namespace is superfluous */
                        isDup = TRUE;
                    }
                    /* Matching prefix, stop looking */
                    break;
                }
                nsChk = nsChk->next;
            }

            /* Special case, no duplicate but top-level blank default ns */
            if ((nsChk == NULL) &&
                 (*(ns->prefix) == '\0') && (*(ns->href) == '\0')) isDup = TRUE;

            /* Other special case, do not include already rendered ns */
            if (ns->origin != elmnt) {
                for (idx = 0; idx < cnt; idx++) {
                    if (strcmp(nsSort[idx]->prefix, ns->prefix) == 0) {
                        /* Namespace prefix is already rendered */
                        isDup = TRUE;
                        break;
                    }
                }
            }

            if (!isDup) nsSort[cnt++] = ns;
            ns = ns->next;
        }

        /* Then sort */
        qsort(nsSort, cnt, sizeof(WXMLNamespace *), _nsCompar);

        /* And output */
        for (idx = 0; idx < cnt; idx++) {
            ns = nsSort[idx];
            if (_encodeNamespace(buffer, ns, TRUE) == NULL) return NULL;
        }
        WXFree(nsSort);
    }

    /* For attributes, similar idea */
    if (!isCanonical) {
        while (attr != NULL) {
            if ((format > 0) && (!isFirst)) {
                if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return NULL;
                if (WXIndent(buffer, leader) == NULL) return NULL;
            }

            if (_encodeAttribute(buffer, attr, FALSE) == NULL) return NULL;

            isFirst = FALSE;
            attr = attr->next;
        }
    } else {
        /* Allocate a sorting array of attribute values */
        cnt = 0;
        while (attr != NULL) {
            cnt++;
            attr = attr->next;
        }

        attrSort = (WXMLAttribute **) WXCalloc(cnt * sizeof(WXMLAttribute *));
        if (attrSort == NULL) return NULL;

        attr = elmnt->attributes;
        cnt = 0;
        while (attr != NULL) {
            attrSort[cnt++] = attr;
            attr = attr->next;
        }

        /* Then sort */
        qsort(attrSort, cnt, sizeof(WXMLAttribute *), _attrCompar);

        /* And output */
        for (idx = 0; idx < cnt; idx++) {
            attr = attrSort[idx];
            if (_encodeAttribute(buffer, attr, TRUE) == NULL) return NULL;
        }
        WXFree(attrSort);
    }

    /* Immediate closure if no content and not canonical */
    if ((format >= 0) && (elmnt->children == NULL) &&
                 ((elmnt->content == NULL) || (*(elmnt->content) == '\0'))) {
        if (WXBuffer_Append(buffer, "/>", 2, TRUE) == NULL) return NULL;
        return buffer->buffer;
    } else {
        if (WXBuffer_Append(buffer, ">", 1, TRUE) == NULL) return NULL;
    }

    /* Otherwise, children, content (encoded) and closing tag */
    hasChildElement = FALSE;
    while (child != NULL) {
        /* Output explicit canonical text entries, skip otherwise (collected) */
        if (child->name == NULL) {
            if (format < 0) {
                /* TODO - various text encoding filters here */
                if (WXML_EscapeContent(buffer, child->content, -1,
                                       TRUE) == NULL) return NULL;
            }
            child = child->next;
            continue;
        }
        hasChildElement = TRUE;

        /* Of course, skip encoding elements that are to be skipped */
        if ((skip == NULL) || (child != skip)) {
            if (format > 0) {
                if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return NULL;
                if (WXIndent(buffer, (indent + 1) * 4) == NULL) return NULL;
            }

            if (_encodeElement(buffer, child, skip, format,
                               indent + 1) == NULL) return NULL;
        }

        child = child->next;
    }

    if ((format > 0) && hasChildElement) {
        /* At this point, nested content is not identical, reformat it */
        if (elmnt->content != NULL) {
            start = elmnt->content;
            while (isspace(*start)) start++;
            if (*start != '\0') {
                end = start + strlen(start) - 1;
                while (isspace(*end)) end--;
                if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return NULL;
                if (WXIndent(buffer, (indent + 1) * 4) == NULL) return NULL;
                if (WXML_EscapeContent(buffer, start, end - start + 1,
                                       FALSE) == NULL) return NULL;
            }
        }

        if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return NULL;
        if (WXIndent(buffer, indent * 4) == NULL) return NULL;
    } else if (format >= 0) {
        if (elmnt->content != NULL) {
            if (WXML_EscapeContent(buffer, elmnt->content, -1,
                                   FALSE) == NULL) return NULL;
        }
    }

    if (WXBuffer_Append(buffer, "</", 2, TRUE) == NULL) return NULL;
    if ((elmnt->namespace != NULL) && (*(elmnt->namespace->prefix) != '\0')) {
        if (WXBuffer_Append(buffer, elmnt->namespace->prefix,
                            strlen(elmnt->namespace->prefix),
                            TRUE) == NULL) return NULL;
        if (WXBuffer_Append(buffer, ":", 1, TRUE) == NULL) return NULL;
    }
    if (WXBuffer_Append(buffer, elmnt->name, strlen(elmnt->name),
                        TRUE) == NULL) return NULL;
    if (WXBuffer_Append(buffer, ">", 1, TRUE) == NULL) return NULL;

    return buffer->buffer;
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
    if (_encodeElement(buffer, root, NULL,
                       (prettyPrint) ? 1 : 0, 0) == NULL) return NULL;
    if (WXBuffer_Append(buffer, "\0", 1, TRUE) == NULL) return NULL;
    return buffer->buffer;
}

/**
 * Similar to the previous, but encode the content according to published
 * specifications for canonicalized XML.  Note that the toolkit XML parser
 * already discards a lot of elements, so PI's, DOCTYPES's and comments are
 * never going to appear in the canonicalized form.  Source document must
 * be parsed with retainTextFragments set to TRUE.
 *
 * @param buffer Buffer into which the XML data should be canonicalized.
 * @param root The XML document (root) to be canonicalized.
 * @param skip If non-NULL, do not include this node in the canonicalized form
 *             (signature).
 * @param isInclusive If TRUE (non-zero), canonicalization will be inclusive
 *                    for enveloped namespaces.  If FALSE, the canonicalization
 *                    will be exclusive (localized).
 * @return The buffer area containing the output document (null terminated)
 *         or NULL if memory allocation failure occurred.
 */
char *WXML_Canonicalize(WXBuffer *buffer, WXMLElement *root,
                        WXMLElement *skip, int isInclusive) {
    if (_encodeElement(buffer, root, skip,
                       (isInclusive) ? -1 : -2, 0) == NULL) return NULL;
    if (WXBuffer_Append(buffer, "\0", 1, TRUE) == NULL) return NULL;
    return buffer->buffer;
}

/**
 * Find a node from the current node.  This uses a syntax similar to XPath but
 * (obviously?) not the complete syntax.  The following are recognized:
 *
 * /child - immediate child instance named 'child'
 * //child - match immediate children, then descendants named 'child'
 * /child/@attr - for the immediate node named 'child', return the attribute
 *                'attr' for the 'child' node.  Note that the @attr must be
 *                the end, this is a direct access to the attribute itself
 * #id - matches a node with an 'id' attribute (caseless) with the value
 *
 * @param root The root element to search from.
 * @param path The path of the element to be found.
 * @param descendant If TRUE, recurse through descendants (as if path
 *                   begins with //).  False indicates immediate child, unless
 *                   path begins with //.
 * @return Either an element or attribute reference if found.  Note that the
 *         exact type of data return will depend on the search undertaken.
 */
void *WXML_Find(WXMLElement *root, const char *path, int descendant) {
    WXMLAttribute *attr;
    WXMLElement *elmnt;
    void *retval;
    char *slash;
    int len;

    /* Handle leaders, determine recursion depth */
    if (*path == '/') {
        path++;
        if (*path == '/') {
            descendant = TRUE;
            path++;
        }
    }

    /* Exit if we're there */
    if (*path == '\0') return root;

    /* Delimit next element */
    slash = strchr(path, '/');
    len = (slash == NULL) ? strlen(path) : slash - path;

    /* If we reach an attribute marker, we're at the end */
    if (*path == '@') {
        attr = root->attributes;
        while (attr != NULL) {
            if (strcmp(attr->name, path + 1) == 0) return attr;
            attr = attr->next;
        }
    } else if (*path == '#') {
        attr = root->attributes;
        while (attr != NULL) {
            if (strcasecmp(attr->name, "id") == 0) {
                if ((strncmp(attr->value, path + 1, len - 1) == 0) &&
                                      (strlen(attr->value) == len - 1)) {
                    /* Match is exact or possible descendant */
                    if (slash == NULL) return root;
                    retval = WXML_Find(root, slash, FALSE);
                    if (retval != NULL) return retval;
                }
            }
            attr = attr->next;
        }
    } else {
        elmnt = root->children;
        while (elmnt != NULL) {
            if ((elmnt->name != NULL) &&
                    ((strncmp(elmnt->name, path, len) == 0) &&
                                       (strlen(elmnt->name) == len))) {
                /* Match is exact or possible descendant */
                if (slash == NULL) return elmnt;
                retval = WXML_Find(elmnt, slash, FALSE);
                if (retval != NULL) return retval;
            }
            elmnt = elmnt->next;
        }
    }

    /* If we are in descendant mode, try the children */
    if (descendant) {
        elmnt = root->children;
        while (elmnt != NULL) {
            retval = WXML_Find(elmnt, path, TRUE);
            if (retval != NULL) return retval;
            elmnt = elmnt->next;
        }
    }

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
        } else {
            /* Exit as soon as we leave my list */
            break;
        }
        ns = nextNs;
    }

    /* Finally, discard associated details and the node itself */
    if (root->name != NULL) WXFree(root->name);
    if (root->content != NULL) WXFree(root->content);
    WXFree(root);
}
