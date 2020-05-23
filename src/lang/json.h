/*
 * Structures and methods for parsing, representing and generating JSON data.
 *
 * Copyright (C) 2015-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_JSON_H
#define WX_JSON_H 1

#include "array.h"
#include "buffer.h"
#include "hash.h"

/**
 * Error codes for failures in the lexer and parser.
 */
typedef enum {
    /* No error in the parsing, all is well! */
    WXJSONERR_NONE_OK = 0,

    /* Memory allocation failure during parsing */
    WXJSONERR_ALLOC_FAILURE = 1,

    /* Invalid value encountered, either bad content or misplaced */
    WXJSONERR_INVALID_VALUE = 2,

    /* Invalid control character in string */
    WXJSONERR_INVALID_CHARACTER = 3,

    /* Bad Unicode character escape sequence */
    WXJSONERR_INVALID_UNICHAR = 4,

    /* Incorrect character escape sequence */
    WXJSONERR_INVALID_ESCAPE = 5,

    /* Missing closing quote */
    WXJSONERR_UNTERMINATED_STRING = 6,

    /* Misplaced JSON value (syntax error) */
    WXJSONERR_VALUE_NOT_IN_CONTEXT = 7,

    /* Extraneous object terminator (closing brace) */
    WXJSONERR_EXT_OBJECT_TERMINATOR = 8,

    /* Extraneous array terminator (closing bracket) */
    WXJSONERR_EXT_ARRAY_TERMINATOR = 9,

    /* Misplaced colon (syntax error) */
    WXJSONERR_MISPLACED_COLON = 10,

    /* Misplaced comma (syntax error) */
    WXJSONERR_MISPLACED_COMMA = 11,

    /* Extra content after root value */
    WXJSONERR_NONSINGULAR_ROOT = 12,

    /* Invalid array continuation (comma or end of array) */
    WXJSONERR_ARRAY_CONTINUE = 13,

    /* Expected property name */
    WXJSONERR_MISSING_PROPERTY = 14,

    /* And then a colon separator */
    WXJSONERR_MISSING_COLON = 15,

    /* Invalid object continuation (comma or end of object) */
    WXJSONERR_OBJECT_CONTINUE = 16
} WXJSONErrorCode;

/**
 * For debugging/logging, get the textual representation of the parsing error
 * code (not localized).
 *
 * @param errorCode The error code to translate.
 * @return The error string associated to the given error code.
 */
const char *WXJSON_GetErrorStr(WXJSONErrorCode errorCode);

/**
 * Enumeration of JSON value types returned by the parser, including
 * error return.  Hopefully values are self-explanatory.
 */
typedef enum {
    WXJSONVALUE_ERROR = -1,
    WXJSONVALUE_NONE = 0,
    WXJSONVALUE_TRUE = 1,
    WXJSONVALUE_FALSE = 2,
    WXJSONVALUE_NULL = 3,
    WXJSONVALUE_INT = 4,
    WXJSONVALUE_DOUBLE = 5,
    WXJSONVALUE_STRING = 6,
    WXJSONVALUE_OBJECT = 7,
    WXJSONVALUE_ARRAY = 8
} WXJSONValueType;

/**
 * Based on the enumerated type (see above), storage for the associated
 * data value content.
 */
typedef struct {
    /* The type of value being stored */
    WXJSONValueType type;

    /* Overlay of storage elements tied to the associated value type */
    union {
        long long int ival;
        double dval;
        char *sval;
        WXHashTable oval;
        WXArray aval;

        struct {
            WXJSONErrorCode errorCode;
            unsigned int lineNumber;
        } error;
    } value;
} WXJSONValue;

/**
 * Parse/decode a JSON document, returning a corresponding data representation.
 *
 * @param content The JSON document/content to be parsed.
 * @return The root value of the JSON data, note that this contains an error
 *         item for any parsing error.  NULL for memory failure.
 */
WXJSONValue *WXJSON_Decode(const char *content);

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
char *WXJSON_Encode(WXBuffer *buffer, WXJSONValue *value, int prettyPrint);

/**
 * Destroy/release the contents of the provided data value (and all nested
 * values).  This method will also free the value itself (consistent with
 * the allocated return from the parse method).
 *
 * @param value The value to be destroyed/freed.
 */
void WXJSON_Destroy(WXJSONValue *value);

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
WXJSONValue *WXJSON_Find(WXJSONValue *root, const char *childName);

/* Enumeration of data types for physical JSON binding (below) */
typedef enum {
    /* Data type is an allocated string pointer, recognizes string values */
    WXJSONBIND_STR,

    /* Data type is a native int, recognizes true/false data values */
    WXJSONBIND_BOOLEAN,

    /* Data type is a native int, recognizes int data values */
    WXJSONBIND_INT,

    /* Data type is size_t, recognizes int data values */
    WXJSONBIND_SIZE,

    /* Data type is native long long int, recognizes int data values */
    WXJSONBIND_LONG,

    /* Data type is a native double, recognizes double values */
    WXJSONBIND_DOUBLE,

    /* Data type is a pointer (JSON value reference), recognizes object/array */
    WXJSONBIND_REF
} WXJSONBindType;

/* Definition information for binding JSON data to physical elements */
typedef struct {
    const char *name;
    WXJSONBindType type;
    uint32_t offset;
    int required;
} WXJSONBindDefn;

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
                int defnCount, char *errorMsg, int errorMsgLen);

#endif
