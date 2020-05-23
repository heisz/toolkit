/*
 * Structures and methods for parsing, representing and generating JSON data.
 *
 * Copyright (C) 2015-2019 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include <ctype.h>
#include "jsonint.h"
#include "mem.h"

/**
 * For debugging/logging, get the textual representation of the parsing error
 * code (not localized).
 *
 * @param errorCode The error code to translate.
 * @return The error string associated to the given error code.
 */
const char *WXJSON_GetErrorStr(WXJSONErrorCode errorCode) {
    static char *errStr[] = {
        "OK, no error encountered",
        "Memory allocation failure",
        "Syntax error: invalid JSON value",
        "Unallowed control character in string value",
        "Invalid Unicode character specification (\\u####)",
        "Invalid escape (\\) character sequence",
        "Unterminated string value (missing closing \")",
        "Syntax error: misplaced JSON value (not in value context)",
        "Syntax error: extraneous/unexpected object terminator '}'",
        "Syntax error: extraneous/unexpected array terminator ']'",
        "Syntax error: misplaced/unexpected colon token",
        "Syntax error: misplaced/unexpected comma token",
        "JSON root content must be a single value",
        "Syntax error: bad array continuation, expecting ',' or ']'",
        "Syntax error: missing object property name",
        "Syntax error: missing object property/value colon separator",
        "Syntax error: bad object continuation, expecting ',' or '}'",
    };

    if ((errorCode < 0) || (errorCode > WXJSONERR_OBJECT_CONTINUE)) {
        return "Invalid/unknown parsing error code";
    }
    return errStr[errorCode];
}

/**
 * Initialize a lexer instance for the provided content.  Just an internal
 * setup wrapper.
 *
 * @param lexer The lexer instance to initialize.
 * @param content The string JSON content to be parsed.
 */
void WXJSONLexerInit(WXJSONLexer *lexer, const char *content) {
    /* Really just a setup for the real work */
    lexer->content = content;
    lexer->offset = 0;
    lexer->lineNumber = 1;
    lexer->lastToken.type = WXJSONTK_START;
}

/* Common macros for common circumstances */
#define RETURN_ERROR(x) \
    lexer->lastToken.type = WXJSONTK_ERROR; \
    lexer->lastToken.value.errorCode = x; \
    return &(lexer->lastToken);

#define IN_VALUE_CONTEXT() \
    ((lexer->lastToken.type == WXJSONTK_START) || \
         (lexer->lastToken.type == WXJSONTK_ARR_START) || \
         (lexer->lastToken.type == WXJSONTK_COLON) || \
         (lexer->lastToken.type == WXJSONTK_COMMA))

#define WAS_VALUE() \
    ((lexer->lastToken.type & WXJSONTK_VALUE) == WXJSONTK_VALUE)

/**
 * Internal method (except for testing) to obtain the next token from the
 * lexer.
 *
 * @param lexer The lexer instance to retrieve the token from.
 * @return Pointer to the next token (reference to the internal lastToken
 *         element).
 */
WXJSONToken *WXJSONLexerNext(WXJSONLexer *lexer) {
    char ch, *ptr = (char *) (lexer->content + lexer->offset), *eptr;
    uint8_t strBufferData[1024], utf[4];
    unsigned int idx, uchr, ucnt;
    WXBuffer strBuffer;

    /* Flush prior content */
    (void) memset(&lexer->lastToken.value, 0, sizeof(lexer->lastToken.value));

    /* Read to end of file (or a token occurs) */
    while ((ch = *(ptr++)) != '\0') {
        /* Consume white space (non-Unicode) */
        if ((ch == ' ') || (ch == '\t')) {
            continue;
        }
        if ((ch == '\r') && (*ptr == '\n')) {
            /* Collapse LF-CR into single CR */
            ch = *(ptr++);
        }
        if ((ch == '\r') || (ch == '\n')) {
            lexer->lineNumber++;
            continue;
        }

        /* The grammar isn't that complex, condenses to one switch */
        switch (ch) {
            case '{':
                if (!IN_VALUE_CONTEXT()) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }
                lexer->lastToken.type = WXJSONTK_OBJ_START;
                lexer->offset = ptr - lexer->content;
                return &(lexer->lastToken);

            case '}':
                /* Last item must have been a value (closure) or empty */
                if ((!WAS_VALUE()) &&
                        (lexer->lastToken.type != WXJSONTK_OBJ_START)) {
                    RETURN_ERROR(WXJSONERR_EXT_OBJECT_TERMINATOR);
                }
                lexer->lastToken.type = WXJSONTK_VALUE_OBJ_END;
                lexer->offset = ptr - lexer->content;
                return &(lexer->lastToken);

            case '[':
                if (!IN_VALUE_CONTEXT()) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }
                lexer->lastToken.type = WXJSONTK_ARR_START;
                lexer->offset = ptr - lexer->content;
                return &(lexer->lastToken);

            case ']':
                /* Last item must have been a value (closure) or empty */
                if ((!WAS_VALUE()) &&
                        (lexer->lastToken.type != WXJSONTK_ARR_START)) {
                    RETURN_ERROR(WXJSONERR_EXT_ARRAY_TERMINATOR);
                }
                lexer->lastToken.type = WXJSONTK_VALUE_ARR_END;
                lexer->offset = ptr - lexer->content;
                return &(lexer->lastToken);

            case 't':
                if (!IN_VALUE_CONTEXT()) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }
                if (strncmp(ptr, "rue", 3) == 0) {
                    lexer->lastToken.type = WXJSONTK_VALUE_TRUE;
                    lexer->offset = (ptr - lexer->content) + 3;
                    return &(lexer->lastToken);
                } else {
                    RETURN_ERROR(WXJSONERR_INVALID_VALUE);
                }

            case 'f':
                if (!IN_VALUE_CONTEXT()) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }
                if (strncmp(ptr, "alse", 4) == 0) {
                    lexer->lastToken.type = WXJSONTK_VALUE_FALSE;
                    lexer->offset = (ptr - lexer->content) + 4;
                    return &(lexer->lastToken);
                } else {
                    RETURN_ERROR(WXJSONERR_INVALID_VALUE);
                }

            case 'n':
                if (!IN_VALUE_CONTEXT()) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }
                if (strncmp(ptr, "ull", 3) == 0) {
                    lexer->lastToken.type = WXJSONTK_VALUE_NULL;
                    lexer->offset = (ptr - lexer->content) + 3;
                    return &(lexer->lastToken);
                } else {
                    RETURN_ERROR(WXJSONERR_INVALID_VALUE);
                }

            case '-': case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                if (!IN_VALUE_CONTEXT()) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }
                ptr--;
                for (idx = 1; ch != '\0'; idx++) {
                    ch = *(ptr + idx);
                    if ((ch == '.') || (ch == 'e') || (ch == 'E')) {
                        /* Floating point entry */
                        lexer->lastToken.type = WXJSONTK_VALUE_DBL;
                        lexer->lastToken.value.dval = strtod(ptr, &eptr);
                        lexer->offset = eptr - lexer->content;
                        return &(lexer->lastToken);
                    } else if (!isdigit(ch)) {
                        /* End of number, which is integer at this point */
                        lexer->lastToken.type = WXJSONTK_VALUE_INT;
                        lexer->lastToken.value.ival = strtoll(ptr, &eptr, 10);
                        lexer->offset = eptr - lexer->content;
                        return &(lexer->lastToken);
                    }
                }
                /* Never gets here, null terminator is not a digit */
                break;

            case '"':
                /* Special, standalone value or key leader for object */
                if ((!IN_VALUE_CONTEXT()) &&
                        (lexer->lastToken.type != WXJSONTK_OBJ_START)) {
                    RETURN_ERROR(WXJSONERR_VALUE_NOT_IN_CONTEXT);
                }

                WXBuffer_InitLocal(&strBuffer, strBufferData,
                                   sizeof(strBufferData));
                while ((ch = *(ptr++)) != '"') {
                    if (ch == '\\') {
                        switch (ch = *(ptr++)) {
                            case '"':
                            case '\\':
                            case '/':
                                /* Direct character mapping */
                                break;
                            case 'b':
                                ch = '\b';
                                break;
                            case 'f':
                                ch = '\f';
                                break;
                            case 'n':
                                ch = '\n';
                                break;
                            case 'r':
                                ch = '\r';
                                break;
                            case 't':
                                ch = '\t';
                                break;
                            case 'u':
                                uchr = 0;
                                for (idx = 0; idx < 4; idx++) {
                                    ch = *(ptr + idx);
                                    if (((ch >= 'a') && (ch <= 'f')) ||
                                            ((ch >= 'A') && (ch <= 'F'))) {
                                        uchr = (uchr << 4) | ((ch + 9) & 0x0F);
                                    } else if ((ch >= '0') && (ch <= '9')) {
                                        uchr = (uchr << 4) | (ch & 0x0F);
                                    } else {
                                        WXBuffer_Destroy(&strBuffer);
                                        RETURN_ERROR(WXJSONERR_INVALID_UNICHAR);
                                    }
                                }
                                if (uchr <= 0x7F) {
                                    utf[0] = (uint8_t) (uchr & 0x7F);
                                    ucnt = 1;
                                } else if (uchr <= 0x7FF) {
                                    utf[0] = (uint8_t)
                                                 (0xC0 | ((uchr >> 6) & 0x1F));
                                    utf[1] = (uint8_t) (0x80 | (uchr & 0x3F));
                                    ucnt = 2;
                                } else {
                                    utf[0] = (uint8_t)
                                                 (0xE0 | ((uchr >> 12) & 0x0F));
                                    utf[1] = (uint8_t)
                                                 (0x80 | ((uchr >> 6) & 0x3F));
                                    utf[2] = (uint8_t) (0x80 | (uchr & 0x3F));
                                    ucnt = 3;
                                }
                                if (WXBuffer_Append(&strBuffer, utf,
                                                    ucnt, TRUE) == NULL) {
                                    WXBuffer_Destroy(&strBuffer);
                                    RETURN_ERROR(WXJSONERR_ALLOC_FAILURE);
                                }
                                ptr += 4;
                                ch = '\0';
                                break;
                            default:
                                WXBuffer_Destroy(&strBuffer);
                                RETURN_ERROR(WXJSONERR_INVALID_ESCAPE);
                                break;
                        }
                        if (ch != '\0') {
                            if (WXBuffer_Append(&strBuffer, (uint8_t *) &ch,
                                                1, TRUE) == NULL) {
                                WXBuffer_Destroy(&strBuffer);
                                RETURN_ERROR(WXJSONERR_ALLOC_FAILURE);
                            }
                        }
                    } else if (ch == '\0') {
                        WXBuffer_Destroy(&strBuffer);
                        RETURN_ERROR(WXJSONERR_UNTERMINATED_STRING);
                    } else if (ch < 0x20) {
                        WXBuffer_Destroy(&strBuffer);
                        RETURN_ERROR(WXJSONERR_INVALID_CHARACTER);
                    } else {
                        if (WXBuffer_Append(&strBuffer, (uint8_t *) &ch,
                                            1, TRUE) == NULL) {
                            WXBuffer_Destroy(&strBuffer);
                            RETURN_ERROR(WXJSONERR_ALLOC_FAILURE);
                        }
                    }
                }

                lexer->lastToken.type = WXJSONTK_VALUE_STR;
                if ((lexer->lastToken.value.sval =
                           (char *) WXMalloc(strBuffer.length + 1)) == NULL) {
                    WXBuffer_Destroy(&strBuffer);
                    RETURN_ERROR(WXJSONERR_ALLOC_FAILURE);
                }
                (void) memcpy(lexer->lastToken.value.sval, strBuffer.buffer,
                              strBuffer.length);
                lexer->lastToken.value.sval[strBuffer.length] = '\0';
                WXBuffer_Destroy(&strBuffer);
                lexer->offset = ptr - lexer->content;

                return &(lexer->lastToken);

            case ':':
                /* Must follow a string value (key) */
                if (lexer->lastToken.type != WXJSONTK_VALUE_STR) {
                    RETURN_ERROR(WXJSONERR_MISPLACED_COLON);
                }
                lexer->lastToken.type = WXJSONTK_COLON;
                lexer->offset = ptr - lexer->content;
                return &(lexer->lastToken);

            case ',':
                /* Can follow any value (array or object field) */
                if (!WAS_VALUE()) {
                    RETURN_ERROR(WXJSONERR_MISPLACED_COMMA);
                }
                lexer->lastToken.type = WXJSONTK_COMMA;
                lexer->offset = ptr - lexer->content;
                return &(lexer->lastToken);

            default:
                RETURN_ERROR(WXJSONERR_INVALID_VALUE);
        }
    }

    lexer->lastToken.type = WXJSONTK_EOF;
    lexer->offset = ptr - lexer->content - 1;
    return &(lexer->lastToken);
}

/* Cleanup method for invalid token discard */
void WXJSONLexerDiscard(WXJSONToken *token) {
    /* Only the string token actually allocated anything */
    if (token->type == WXJSONTK_VALUE_STR) {
        if (token->value.sval != NULL) WXFree(token->value.sval);
    }
}

/**
 * For closure, destruction method for lexer content.  Does not release the
 * object itself and really only resets internal parsing details.
 *
 * @param lexer The lexer instance to be destroyed.
 */
void WXJSONLexerDestroy(WXJSONLexer *lexer) {
    lexer->content = NULL;
    lexer->offset = 0;
}

/* Scanners and forward declarations to release related items */
/* Note that objects contain allocated value, arrays are contained values */
static void _WXJSON_Destroy(WXJSONValue *value, int freeStructure);
static int _objectDestroyScanner(WXHashTable *table, void *key, void *object,
                                 void *userData) {
    WXFree(key);
    _WXJSON_Destroy((WXJSONValue *) object, TRUE);
    return 0;
}
static int _arrayDestroyScanner(WXArray *array, void *object, void *userData) {
    _WXJSON_Destroy((WXJSONValue *) object, FALSE);
    return 0;
}

/* Internal method to capture optional allocated value release */
static void _WXJSON_Destroy(WXJSONValue *value, int freeStructure) {
    switch (value->type) {
        case WXJSONVALUE_ERROR:
        case WXJSONVALUE_NONE:
        case WXJSONVALUE_TRUE:
        case WXJSONVALUE_FALSE:
        case WXJSONVALUE_NULL:
        case WXJSONVALUE_INT:
        case WXJSONVALUE_DOUBLE:
            /* These all have unallocated content */
            break;
        case WXJSONVALUE_STRING:
            WXFree(value->value.sval);
            break;
        case WXJSONVALUE_OBJECT:
            (void) WXHash_Scan(&(value->value.oval), _objectDestroyScanner,
                               NULL);
            WXHash_Destroy(&(value->value.oval));
            break;
        case WXJSONVALUE_ARRAY:
            (void) WXArray_Scan(&(value->value.aval), _arrayDestroyScanner,
                                NULL);
            WXArray_Destroy(&(value->value.aval));
            break;
        default:
            /* Not much we can actually do here... */
            break;
    }

    if (freeStructure) WXFree(value);
}

/*
 * Parse a single data value at the next lexer point.  Recurses for objects and
 * arrays.  Returns 0 (WXJSONERR_NONE_OK) if successful, error code otherwise.
 */
static WXJSONErrorCode WXJSON_ParseValue(WXJSONLexer *lexer,
                                         WXJSONValue *value,
                                         int allowArrayClosure) {
    WXJSONValue inner, *propVal, *dupVal;
    char *propName = NULL, *dupName;
    WXJSONToken *token;
    int rc;

    /* Grab next token and check for validation from lexer */
    token = WXJSONLexerNext(lexer);
    if (token->type == WXJSONTK_ERROR) {
        return token->value.errorCode;
    }

    /* Translate value tokens appropriately */
    (void) memset(value, 0, sizeof(WXJSONValue));
    switch (token->type) {
        case WXJSONTK_VALUE_TRUE:
            value->type = WXJSONVALUE_TRUE;
            break;

        case WXJSONTK_VALUE_FALSE:
            value->type = WXJSONVALUE_FALSE;
            break;

        case WXJSONTK_VALUE_NULL:
            value->type = WXJSONVALUE_NULL;
            break;

        case WXJSONTK_VALUE_INT:
            value->type = WXJSONVALUE_INT;
            value->value.ival = token->value.ival;
            break;

        case WXJSONTK_VALUE_DBL:
            value->type = WXJSONVALUE_DOUBLE;
            value->value.dval = token->value.dval;
            break;

        case WXJSONTK_VALUE_STR:
            value->type = WXJSONVALUE_STRING;
            value->value.sval = token->value.sval;
            break;

        case WXJSONTK_OBJ_START:
            /* Recursive iteration over object content */
            value->type = WXJSONVALUE_OBJECT;
            (void) WXHash_InitTable(&(value->value.oval), 0);
            while (lexer->lastToken.type != WXJSONTK_EOF) {
                /* Need an object property */
                token = WXJSONLexerNext(lexer);
                if (token->type == WXJSONTK_VALUE_STR) {
                    propName = token->value.sval;
                } else if ((token->type == WXJSONTK_VALUE_OBJ_END) &&
                               (value->value.oval.entryCount == 0)) {
                    /* Empty object... */
                    break;
                } else {
                    WXJSONLexerDiscard(token);
                    _WXJSON_Destroy(value, FALSE);
                    return (token->type == WXJSONTK_ERROR) ?
                        token->value.errorCode : WXJSONERR_MISSING_PROPERTY;
                }

                /* And a colon */
                token = WXJSONLexerNext(lexer);
                if (token->type == WXJSONTK_COLON) {
                    /* Do nothing, just moving along */
                } else {
                    WXFree(propName);
                    WXJSONLexerDiscard(token);
                    _WXJSON_Destroy(value, FALSE);
                    return (token->type == WXJSONTK_ERROR) ?
                        token->value.errorCode : WXJSONERR_MISSING_COLON;
                }

                /* Next a value, once we have that, push to hashtable */
                propVal = (WXJSONValue *) WXMalloc(sizeof(WXJSONValue));
                if (propVal == NULL) {
                    WXFree(propName);
                    _WXJSON_Destroy(value, FALSE);
                    return WXJSONERR_ALLOC_FAILURE;
                }
                rc = WXJSON_ParseValue(lexer, propVal, FALSE);
                if (rc != WXJSONERR_NONE_OK) {
                    WXFree(propName);
                    WXFree(propVal);
                    _WXJSON_Destroy(value, FALSE);
                    return rc;
                }
                if (!WXHash_PutEntry(&(value->value.oval), propName, propVal,
                                     (void **) &dupName, (void **) &dupVal,
                                     WXHash_StrHashFn, WXHash_StrEqualsFn)) {
                    /* Worst case for cleanup */
                    WXFree(propName);
                    _WXJSON_Destroy(propVal, TRUE);
                    _WXJSON_Destroy(value, FALSE);
                    return WXJSONERR_ALLOC_FAILURE;
                }

                /* Handle duplicate/overlaid properties */
                if (dupName != NULL) WXFree(dupName);
                if (dupVal != NULL) _WXJSON_Destroy(dupVal, TRUE);

                /* Finally a comma or end of object */
                token = WXJSONLexerNext(lexer);
                if (token->type == WXJSONTK_COMMA) {
                    /* Do nothing, just moving along */
                } else if (token->type == WXJSONTK_VALUE_OBJ_END) {
                    /* Object is complete, exit loop */
                    break;
                } else {
                    _WXJSON_Destroy(value, FALSE);
                    return (token->type == WXJSONTK_ERROR) ?
                        token->value.errorCode : WXJSONERR_OBJECT_CONTINUE;
                }
            }
            break;

        case WXJSONTK_ARR_START:
            /* Recursive iteration over array content */
            value->type = WXJSONVALUE_ARRAY;
            (void) WXArray_Init(&(value->value.aval), WXJSONValue, 0);
            while (lexer->lastToken.type != WXJSONTK_EOF) {
                /* Value must be first, get it and add to array */
                rc = WXJSON_ParseValue(lexer, &inner, TRUE);
                if (rc != WXJSONERR_NONE_OK) {
                    _WXJSON_Destroy(value, FALSE);
                    return rc;
                }
                if (inner.type == WXJSONVALUE_NONE) {
                    /* Empty array instance */
                    break;
                }
                if (WXArray_Push(&(value->value.aval), &inner) == NULL) {
                    _WXJSON_Destroy(&inner, FALSE);
                    _WXJSON_Destroy(value, FALSE);
                    return WXJSONERR_ALLOC_FAILURE;
                }

                /* Followed by a comma or end of array */
                token = WXJSONLexerNext(lexer);
                if (token->type == WXJSONTK_COMMA) {
                    /* Do nothing, just moving along */
                } else if (token->type == WXJSONTK_VALUE_ARR_END) {
                    /* Array is complete, exit loop */
                    break;
                } else {
                    _WXJSON_Destroy(value, FALSE);
                    return (token->type == WXJSONTK_ERROR) ?
                        token->value.errorCode : WXJSONERR_ARRAY_CONTINUE;
                }
            }
            break;

        case WXJSONTK_VALUE_ARR_END:
            /* Special bypass for empty array */
            if (allowArrayClosure) {
                value->type = WXJSONVALUE_NONE;
                break;
            }

        default:
            /* Lexer should capture these but just in case */
            return WXJSONERR_INVALID_VALUE;
    }

    return WXJSONERR_NONE_OK;
}

/**
 * Parse/decode a JSON document, returning a corresponding data representation.
 *
 * @param content The JSON document/content to be parsed.
 * @return The root value of the JSON data, note that this contains an error
 *         item for any parsing error.  NULL for memory failure.
 */
WXJSONValue *WXJSON_Decode(const char *content) {
    WXJSONValue *retval = NULL;
    WXJSONToken *token;
    WXJSONLexer lexer;
    int rc;

    /* Prepare for parsing and return */
    retval = (WXJSONValue *) WXMalloc(sizeof(WXJSONValue));
    if (retval == NULL) return NULL;
    WXJSONLexerInit(&lexer, content);

    /* And away we go! */
    rc = WXJSON_ParseValue(&lexer, retval, FALSE);
    if (rc != WXJSONERR_NONE_OK) {
        retval->type = WXJSONVALUE_ERROR;
        retval->value.error.errorCode = rc;
        retval->value.error.lineNumber = lexer.lineNumber;
        WXJSONLexerDestroy(&lexer);
        return retval;
    }

    /* There can be only one */
    token = WXJSONLexerNext(&lexer);
    if (token->type != WXJSONTK_EOF) {
        _WXJSON_Destroy(retval, FALSE);
        retval->type = WXJSONVALUE_ERROR;
        retval->value.error.errorCode = (token->type == WXJSONTK_ERROR) ?
                           token->value.errorCode : WXJSONERR_NONSINGULAR_ROOT;
        retval->value.error.lineNumber = lexer.lineNumber;
        WXJSONLexerDiscard(token);
    }

    WXJSONLexerDestroy(&lexer);
    return retval;
}

/* Tracking element for separator handling */
typedef struct {
    WXBuffer *buffer;
    int isFirstElement, prettyPrint, indent;
} WXJSONListEncodeTracker;

/* Strings need a bit more support for encoding */
static int _escapeJSONString(WXBuffer *buffer, char *str) {
    char  escBuff[16], *block = str;
    int l, len = strlen(str);
    uint32_t uniChar;
    unsigned char ch;

    escBuff[0] = '\\';
    if (WXBuffer_Append(buffer, "\"", 1, TRUE) == NULL) return -1;
    while (len > 0) {
        ch = (unsigned char) *(str++);
        len--;

        if ((ch & 0x80) != 0) {
            if ((l = (str - block) - 1) > 0) {
                if (WXBuffer_Append(buffer, block, l, TRUE) == NULL) return -1;
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
                            /* Three-byte block */
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
                    /* Two-byte block */
                    uniChar = (((uint32_t) ch) & 0x1F) << 6;
                    uniChar |= *(str++) & 0x3F;
                    (void) sprintf(escBuff, "\\u%04X", uniChar);
                    len--;
                }
            }
            if (WXBuffer_Append(buffer, escBuff, 6, TRUE) == NULL) return -1;
            block = str;
        } else if ((ch == '"') || (ch == '\\') || (ch == '/')) {
            if ((l = (str - block) - 1) > 0) {
                if (WXBuffer_Append(buffer, block, l, TRUE) == NULL) return -1;
            }
            escBuff[1] = ch;
            if (WXBuffer_Append(buffer, escBuff, 2, TRUE) == NULL) return -1;
            block = str;
        } else if (ch < 0x20) {
            if ((l = (str - block) - 1) > 0) {
                if (WXBuffer_Append(buffer, block, l, TRUE) == NULL) return -1;
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
                                        TRUE) == NULL) return -1;
                    break;
                default:
                    (void) sprintf(escBuff, "\\u00%02X", ch);
                    if (WXBuffer_Append(buffer, escBuff, 6,
                                        TRUE) == NULL) return -1;
                    break;
            }
            block = str;
        } else {
            /* Just a regular character, track as a block */
        }
    }
    if ((l = (str - block)) > 0) {
        if (WXBuffer_Append(buffer, block, l, TRUE) == NULL) return -1;
    }
    if (WXBuffer_Append(buffer, "\"", 1, TRUE) == NULL) return -1;

    return 0;
}

/* Convenience method to generate spaced indent for pretty printing */
static int _WXJSONIndent(WXBuffer *buffer, int indent) {
    static char *spaces = "                                        ";
    int len;

    indent *= 4;
    while (indent > 0) {
        len = (indent < 20) ? indent : 20;
        if (WXBuffer_Append(buffer, spaces, len, TRUE) == NULL) return -1;
        indent -= len;
    }
    return 0;
}

/* Forward declaration for nested looping */
static int WXJSON_EncodeValue(WXBuffer *buffer, WXJSONValue *value,
                              int prettyPrint, int indent);

/* Scanners for encoding complex values */
static int _objectEncodeScanner(WXHashTable *table, void *key, void *object,
                                void *userData) {
    WXJSONListEncodeTracker *trk = (WXJSONListEncodeTracker *) userData;

    if (!trk->isFirstElement) {
        if (WXBuffer_Append(trk->buffer, ",", 1, TRUE) == NULL) return -1;
        if (trk->prettyPrint) {
            if (WXBuffer_Append(trk->buffer, "\n", 1, TRUE) == NULL) return -1;
        }
    }
    trk->isFirstElement = FALSE;

    if (trk->prettyPrint) {
        if (_WXJSONIndent(trk->buffer, trk->indent) < 0) return -1;
    }
    if (_escapeJSONString(trk->buffer, (char *) key) < 0) return -1;
    if (trk->prettyPrint) {
        if (WXBuffer_Append(trk->buffer, ": ", 2, TRUE) == NULL) return -1;
    } else {
        if (WXBuffer_Append(trk->buffer, ":", 1, TRUE) == NULL) return -1;
    }

    if (WXJSON_EncodeValue(trk->buffer, (WXJSONValue *) object,
                           trk->prettyPrint, trk->indent) < 0) return -1;

    return 0;
}
static int _arrayEncodeScanner(WXArray *array, void *object, void *userData) {
    WXJSONListEncodeTracker *trk = (WXJSONListEncodeTracker *) userData;

    if (!trk->isFirstElement) {
        if (trk->prettyPrint) {
            if (WXBuffer_Append(trk->buffer, ", ", 2, TRUE) == NULL) return -1;
        } else {
            if (WXBuffer_Append(trk->buffer, ",", 1, TRUE) == NULL) return -1;
        }
    }
    trk->isFirstElement = FALSE;

    if (WXJSON_EncodeValue(trk->buffer, (WXJSONValue *) object,
                           trk->prettyPrint, trk->indent) < 0) return -1;

    return 0;
}

static int WXJSON_EncodeValue(WXBuffer *buffer, WXJSONValue *value,
                              int prettyPrint, int indent) {
    WXJSONListEncodeTracker trk;
    char prtBuff[128];

    /* Common preparation for nested iteration processing */
    trk.buffer = buffer;
    trk.prettyPrint = prettyPrint;
    trk.isFirstElement = TRUE;
    trk.indent = indent + 1;

    /* Output based on type */
    switch (value->type) {
        case WXJSONVALUE_TRUE:
            if (WXBuffer_Append(buffer, "true", 4, TRUE) == NULL) return -1;
            break;
        case WXJSONVALUE_FALSE:
            if (WXBuffer_Append(buffer, "false", 5, TRUE) == NULL) return -1;
            break;
        case WXJSONVALUE_NULL:
            if (WXBuffer_Append(buffer, "null", 4, TRUE) == NULL) return -1;
            break;
        case WXJSONVALUE_INT:
            (void) sprintf(prtBuff, "%lld", value->value.ival);
            if (WXBuffer_Append(buffer, prtBuff, strlen(prtBuff),
                                TRUE) == NULL) return -1;
            break;
        case WXJSONVALUE_DOUBLE:
            /* There is no consistent model for exact reproduction */
            (void) sprintf(prtBuff, "%g", value->value.dval);
            if (WXBuffer_Append(buffer, prtBuff, strlen(prtBuff),
                                TRUE) == NULL) return -1;
            break;
        case WXJSONVALUE_STRING:
            if (_escapeJSONString(buffer, value->value.sval) < 0) return -1;
            break;
        case WXJSONVALUE_OBJECT:
            if (WXBuffer_Append(buffer, "{", 1, TRUE) == NULL) return -1;
            if (prettyPrint && (value->value.oval.entryCount != 0)) {
                if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return -1;
            }
            (void) WXHash_Scan(&(value->value.oval), _objectEncodeScanner,
                               &trk);
            if (prettyPrint && (value->value.oval.entryCount != 0)) {
                if (WXBuffer_Append(buffer, "\n", 1, TRUE) == NULL) return -1;
                if (_WXJSONIndent(buffer, indent) < 0) return -1;
            }
            if (WXBuffer_Append(buffer, "}", 1, TRUE) == NULL) return -1;
            break;
        case WXJSONVALUE_ARRAY:
            if (WXBuffer_Append(buffer, "[", 1, TRUE) == NULL) return -1;
            (void) WXArray_Scan(&(value->value.aval), _arrayEncodeScanner,
                                &trk);
            if (WXBuffer_Append(buffer, "]", 1, TRUE) == NULL) return -1;
            break;
        default:
            /* Not much we can actually do here, no encoding none/error */
            break;
    }

    return 0;
}

/**
 * Converse to the above, translate the JSON data content to a document.
 *
 * @param buffer Buffer into which the JSON data should be encoded.
 * @param value The root JSON value to be encoded.
 * @param prettyPrint If TRUE (non-zero) , pretty-print the JSON output,
 *                    otherwise (false/zero) output in compact format.
 * @return The buffer area containing the output document (null terminated)
 *         or NULL if memory allocation failure occurred.
 */
char *WXJSON_Encode(WXBuffer *buffer, WXJSONValue *value, int prettyPrint) {
    if (WXJSON_EncodeValue(buffer, value, prettyPrint, 0) < 0) return NULL;
    if (WXBuffer_Append(buffer, "\0", 1, TRUE) == NULL) return NULL;
    return buffer->buffer;
}

/**
 * Destroy/release the contents of the provided data value (and all nested
 * values).  This method will also free the value itself (consistent with
 * the allocated return from the parse method).
 *
 * @param value The value to be destroyed/freed.
 */
void WXJSON_Destroy(WXJSONValue *value) {
    _WXJSON_Destroy(value, TRUE);
}

/**
 * Quick utility method to locate a JSON value entry based on a fully qualified
 * child node name.  Cannot (currently) cross array boundaries...
 *
 * @param root The parsed JSON node value to search from.
 * @param childName Fully qualified (period-delimited) name of the child node to
 *             retrieve.  An internal copy is made in local stack space, names
 *             over the internal alloc limit will not be found (truncation).
 * @return The child node, if located, or NULL if any entry in the name is not
 *         found.
 */
WXJSONValue *WXJSON_Find(WXJSONValue *root, const char *childName) {
    char *ptr, *eptr, name[4096];
    WXJSONValue *child;

    /* Copy for manipulation */
    (void) strncpy(name, childName, sizeof(name));
    (void) strcpy(name + sizeof(name) - 5, "---");
    ptr = name;

    /* Away we go... */
    while (*ptr != '\0') {
        /* At any point, looking for the child of a non-object is a fail... */
        if (root->type != WXJSONVALUE_OBJECT) return NULL;

        /* Find the next step in the chain, which could be the end */
        eptr = strchr(ptr, '.');
        if (eptr != NULL) *eptr = '\0';
        child = WXHash_GetEntry(&(root->value.oval), ptr,
                                WXHash_StrHashFn, WXHash_StrEqualsFn);
        if ((child == NULL) || (eptr == NULL)) return child;

        /* Next please... */
        ptr = eptr + 1;
        root = child;
    }

    return NULL;
}

/* Internal messaging method to delineate types */
static char *descJSONType(WXJSONValueType type) {
    switch (type) {
        case WXJSONVALUE_TRUE:
        case WXJSONVALUE_FALSE:
            return "boolean (t/f)";
        case WXJSONVALUE_NULL:
            return "null";
        case WXJSONVALUE_INT:
            return "integer";
        case WXJSONVALUE_DOUBLE:
            return "double/float";
        case WXJSONVALUE_STRING:
            return "string";
        case WXJSONVALUE_OBJECT:
            return "object";
        case WXJSONVALUE_ARRAY:
            return "array";
    }

    return "unknown";
}

/**
 * Utility method to bind a JSON data object (hierarchy) into a physical
 * data structure.  The binding method is reasonably strict, will not convert
 * between JSON and native data types outside of direct allocation/casting.
 *
 * @param root Parsed JSON data node to bind information from.
 * @param data Pointer to physical data structure to bind into.
 * @param defn Binding information for translating JSON to physical elements.
 * @param defnCount Number of elements in the binding information array.
 * @param errorMsg Externally provided buffer for returning binding error
 *                 information.
 * @param errorMsgLen Length of provided buffer.
 * @return TRUE if bind processing was successful, FALSE on error (message
 *         in provided buffer).
 */
int WXJSON_Bind(WXJSONValue *root, void *data, WXJSONBindDefn *defn,
                int defnCount, char *errorMsg, int errorMsgLen) {
    WXJSONValue *val;
    char *ptr;
    int idx;

    /* Zoom through the provided binding definitions */
    for (idx = 0; idx < defnCount; idx++, defn++) {
        /* Find the corresponding data point and dereference the structure */
        val = WXJSON_Find(root, defn->name);
        if (val == NULL) {
            if (defn->required != 0) {
                (void) snprintf(errorMsg, errorMsgLen,
                                "Missing JSON value for '%s'", defn->name);
                return FALSE;
            }
            continue;
        }
        ptr = ((char *) data) + defn->offset;

        /* Bind away! */
        switch (defn->type) {
            case WXJSONBIND_STR:
                if ((val->type != WXJSONVALUE_STRING) &&
                        (val->type != WXJSONVALUE_NULL)) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Expecting string/null value for '%s', "
                                    "found %s instead", defn->name,
                                    descJSONType(val->type));
                    return FALSE;
                }
                if (val->type == WXJSONVALUE_NULL) val = NULL;

                /* Lots of jiggerypokery to manage string (re)allocation */
                if (val != NULL) {
                    if (*((char **) ptr) != NULL) WXFree(*((char **) ptr));
                    *((char **) ptr) = WXMalloc(strlen(val->value.sval) + 1);
                    if (*((char **) ptr) != NULL) {
                        (void) strcpy(*((char **) ptr), val->value.sval);
                    }
                } else {
                    *((char **) ptr) = NULL;
                }
                break;
            case WXJSONBIND_BOOLEAN:
                if ((val->type != WXJSONVALUE_TRUE) &&
                        (val->type != WXJSONVALUE_FALSE)) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Expecting true/false value for '%s', "
                                    "found %s instead", defn->name,
                                    descJSONType(val->type));
                    return FALSE;
                }
                *((int *) ptr) = (val->type == WXJSONVALUE_TRUE) ? TRUE : FALSE;
                break;
            case WXJSONBIND_INT:
            case WXJSONBIND_SIZE:
            case WXJSONBIND_LONG:
                if (val->type != WXJSONVALUE_INT) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Expecting integer value for '%s', "
                                    "found %s instead", defn->name,
                                    descJSONType(val->type));
                    return FALSE;
                }
                if (defn->type == WXJSONBIND_INT) {
                    *((int *) ptr) = (int) val->value.ival;
                } else if (defn->type == WXJSONBIND_SIZE) {
                    *((size_t *) ptr) = (size_t) val->value.ival;
                } else {
                    *((long long int *) ptr) = val->value.ival;
                }
                break;
            case WXJSONBIND_DOUBLE:
                /* Only convenience conversion, as int is a double as well */
                if ((val->type != WXJSONVALUE_INT) &&
                        (val->type != WXJSONVALUE_DOUBLE)) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Expecting numeric value for '%s', "
                                    "found %s instead", defn->name,
                                    descJSONType(val->type));
                    return FALSE;
                }
                if (defn->type == WXJSONBIND_INT) {
                    *((double *) ptr) = (double) val->value.ival;
                } else {
                    *((double *) ptr) = val->value.dval;
                }
                break;
            case WXJSONBIND_REF:
                if ((val->type != WXJSONVALUE_OBJECT) &&
                        (val->type != WXJSONVALUE_ARRAY)) {
                    (void) snprintf(errorMsg, errorMsgLen,
                                    "Expecting object/array value for '%s', "
                                    "found %s instead", defn->name,
                                    descJSONType(val->type));
                    return FALSE;
                }
                *((WXJSONValue **) ptr) = val;
                break;
            default:
                (void) snprintf(errorMsg, errorMsgLen,
                                "Internal error, unrecognized bind type");
                return FALSE;
        }
    }

    return TRUE;
}
