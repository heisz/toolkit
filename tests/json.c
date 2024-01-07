/*
 * Test interface for the JSON data processor.
 *
 * Copyright (C) 2015-2024 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "json.h"
#include "jsonint.h"
#include "mem.h"

#define TEST_TOKEN(exp, msg) \
    token = WXJSONLexerNext(&lex); \
    if (token->type != exp) { \
        (void) fprintf(stderr, "Error: %s (%d vs %d)\n", msg, \
                       token->type, exp); \
        exit(1); \
    }

#define TEST_STR_TOKEN(exp) \
    token = WXJSONLexerNext(&lex); \
    if (token->type != WXJSONTK_VALUE_STR) { \
        (void) fprintf(stderr, "Error: not string value (%d vs %d)\n", \
                       token->type, WXJSONTK_VALUE_STR); \
        exit(1); \
    } \
    if (strcmp(token->value.sval, exp) != 0) { \
        (void) fprintf(stderr, "Error: string mismatch ('%s' vs '%s')\n", \
                       token->value.sval, exp); \
        exit(1); \
    } \
    WXFree(token->value.sval);

static char *bigJSON = 
    "{\n"
    "    \"empty_obj\": {},\n"
    "    \"empty\\u005Farr\": [],\n"
    "    \"occ_obj\": {\n"
    "        \"true_key\" : true,\n"
    "        \"false_key\": false,\n"
    "        \"null_key\" : null,\n"
    "        \"int_key\": 12345,\n"
    "        \"nint_key\" : -1234,\n"
    "        \"flt_key\": 12345.45,\n"
    "        \"eflt_key\" : -12345e3,\n"
    "        \"str_key\": \"abcdefg\",\n"
    "        \"allstr_key\" : \"\\\"\\/\\b\\f\\n\\r\\t\",\n"
    "        \"uni_key\" : \"\\u0023 \\u0472 \\u4e1D\"\n"
    "    },"
    "    \"occ_arr\": [\n"
    "        true, false, null, 12345, 1.23, \"abc\"\n"
    "    ]"
    "}";

static struct LexErrorDef {
     char *content;
     WXJSONErrorCode errorCode;
} lexerErrorConds[] = {
     { "true {", WXJSONERR_VALUE_NOT_IN_CONTEXT },
     { "}", WXJSONERR_EXT_OBJECT_TERMINATOR },
     { "false [", WXJSONERR_VALUE_NOT_IN_CONTEXT },
     { "]", WXJSONERR_EXT_ARRAY_TERMINATOR },

     { "true true", WXJSONERR_VALUE_NOT_IN_CONTEXT },
     { "ture", WXJSONERR_INVALID_VALUE },
     { "false false", WXJSONERR_VALUE_NOT_IN_CONTEXT },
     { "fslae", WXJSONERR_INVALID_VALUE },
     { "null null", WXJSONERR_VALUE_NOT_IN_CONTEXT },
     { "nULL", WXJSONERR_INVALID_VALUE },

     { "null \"abc\"", WXJSONERR_VALUE_NOT_IN_CONTEXT },
     { "\"ab\\xde\"", WXJSONERR_INVALID_ESCAPE },
     { "\"ding ding \a\a\"", WXJSONERR_INVALID_CHARACTER },
     { "\"no end in sight", WXJSONERR_UNTERMINATED_STRING },
     { "\"bad \\u1x2y\"", WXJSONERR_INVALID_UNICHAR },
     { "\"trunc \\u123", WXJSONERR_INVALID_UNICHAR },

     { "{ : \"value\"}", WXJSONERR_MISPLACED_COLON },
     { "{ , \"key\" : \"value\"}", WXJSONERR_MISPLACED_COMMA },
     { "[ , true ]", WXJSONERR_MISPLACED_COMMA },
     { "[ null, , ]", WXJSONERR_MISPLACED_COMMA },

     { "xyzzy", WXJSONERR_INVALID_VALUE }
};

#define LEX_ERROR_COUNT \
               (int) (sizeof(lexerErrorConds) / sizeof(struct LexErrorDef))

static struct ParseErrorDef {
     char *content;
     WXJSONErrorCode errorCode;
} parserErrorConds[] = {
     { "{}, \"extra\"", WXJSONERR_NONSINGULAR_ROOT },
     { "[ \"key\" : 13 ]", WXJSONERR_ARRAY_CONTINUE },
     { "{ \"key\" : 13, false }", WXJSONERR_MISSING_PROPERTY },
     { "{ \"key\" : 13, \"keyb\", false }", WXJSONERR_MISSING_COLON },
     { "{ \"key\" : \"a\" : false }", WXJSONERR_OBJECT_CONTINUE }
};

#define PARSE_ERROR_COUNT \
               (int) (sizeof(parserErrorConds) / sizeof(struct ParseErrorDef))

/**
 * Main testing entry point.  Lots of parsing is about to follow...
 */
int main(int argc, char **argv) {
    WXJSONValue *value, *sub, *subsub, *arrPtr;
    WXJSONToken *token;
    WXJSONLexer lex;
    WXBuffer buffer;
    int idx;

    /* At some point, put the MTraq testcase identifiers in here */

    /* Lexer test cases first, lots of manual lexing tokenization checks */
    WXJSONLexerInit(&lex, bigJSON);

    TEST_TOKEN(WXJSONTK_OBJ_START, "Global object start")

    TEST_STR_TOKEN("empty_obj")
    TEST_TOKEN(WXJSONTK_COLON, "Empty obj colon")
    TEST_TOKEN(WXJSONTK_OBJ_START, "Empty obj start")
    TEST_TOKEN(WXJSONTK_VALUE_OBJ_END, "Empty obj closure")
    TEST_TOKEN(WXJSONTK_COMMA, "Empty obj comma")

    TEST_STR_TOKEN("empty_arr")
    TEST_TOKEN(WXJSONTK_COLON, "Empty array colon")
    TEST_TOKEN(WXJSONTK_ARR_START, "Empty array start")
    TEST_TOKEN(WXJSONTK_VALUE_ARR_END, "Empty array end")
    TEST_TOKEN(WXJSONTK_COMMA, "Empty array comma")

    TEST_STR_TOKEN("occ_obj")
    TEST_TOKEN(WXJSONTK_COLON, "Occ object colon")
    TEST_TOKEN(WXJSONTK_OBJ_START, "Occ object start")

    TEST_STR_TOKEN("true_key")
    TEST_TOKEN(WXJSONTK_COLON, "True value colon")
    TEST_TOKEN(WXJSONTK_VALUE_TRUE, "True value")
    TEST_TOKEN(WXJSONTK_COMMA, "True value comma")

    TEST_STR_TOKEN("false_key")
    TEST_TOKEN(WXJSONTK_COLON, "False value colon")
    TEST_TOKEN(WXJSONTK_VALUE_FALSE, "False value")
    TEST_TOKEN(WXJSONTK_COMMA, "False value comma")

    TEST_STR_TOKEN("null_key")
    TEST_TOKEN(WXJSONTK_COLON, "Null value colon")
    TEST_TOKEN(WXJSONTK_VALUE_NULL, "Null value")
    TEST_TOKEN(WXJSONTK_COMMA, "Null value comma")

    TEST_STR_TOKEN("int_key")
    TEST_TOKEN(WXJSONTK_COLON, "Int value colon")
    TEST_TOKEN(WXJSONTK_VALUE_INT, "Int value")
    if (lex.lastToken.value.ival != 12345) {
        (void) fprintf(stderr, "Incorrect parsed integer value\n");
        exit(1);
    }
    TEST_TOKEN(WXJSONTK_COMMA, "Int value comma")

    TEST_STR_TOKEN("nint_key")
    TEST_TOKEN(WXJSONTK_COLON, "Negative int value colon")
    TEST_TOKEN(WXJSONTK_VALUE_INT, "Negative int value")
    if (lex.lastToken.value.ival != -1234) {
        (void) fprintf(stderr, "Incorrect parsed negative integer value\n");
        exit(1);
    }
    TEST_TOKEN(WXJSONTK_COMMA, "Negative int value comma")

    TEST_STR_TOKEN("flt_key")
    TEST_TOKEN(WXJSONTK_COLON, "Float value colon")
    TEST_TOKEN(WXJSONTK_VALUE_DBL, "Float value")
    if (lex.lastToken.value.dval != 12345.45) {
        (void) fprintf(stderr, "Incorrect parsed float value\n");
        exit(1);
    }
    TEST_TOKEN(WXJSONTK_COMMA, "Float value comma")

    TEST_STR_TOKEN("eflt_key")
    TEST_TOKEN(WXJSONTK_COLON, "Exp loat value colon")
    TEST_TOKEN(WXJSONTK_VALUE_DBL, "Exp float value")
    if (lex.lastToken.value.dval != -12345e3) {
        (void) fprintf(stderr, "Incorrect parsed exp float value\n");
        exit(1);
    }
    TEST_TOKEN(WXJSONTK_COMMA, "Exp float value comma")

    TEST_STR_TOKEN("str_key")
    TEST_TOKEN(WXJSONTK_COLON, "Str value colon")
    TEST_STR_TOKEN("abcdefg")
    TEST_TOKEN(WXJSONTK_COMMA, "Str value comma")

    TEST_STR_TOKEN("allstr_key")
    TEST_TOKEN(WXJSONTK_COLON, "All str value colon")
    TEST_STR_TOKEN("\"/\b\f\n\r\t")
    TEST_TOKEN(WXJSONTK_COMMA, "All str value comma")

    TEST_STR_TOKEN("uni_key")
    TEST_TOKEN(WXJSONTK_COLON, "Uni str value colon")
    TEST_STR_TOKEN("# \xD1\xB2 \xE4\xB8\x9D")

    TEST_TOKEN(WXJSONTK_VALUE_OBJ_END, "Occ objectect end")
    TEST_TOKEN(WXJSONTK_COMMA, "Occ object comma")

    TEST_STR_TOKEN("occ_arr")
    TEST_TOKEN(WXJSONTK_COLON, "Occ array colon")
    TEST_TOKEN(WXJSONTK_ARR_START, "Occ array start")

    TEST_TOKEN(WXJSONTK_VALUE_TRUE, "Array true value")
    TEST_TOKEN(WXJSONTK_COMMA, "Array true value comma")

    TEST_TOKEN(WXJSONTK_VALUE_FALSE, "Array false value")
    TEST_TOKEN(WXJSONTK_COMMA, "Array false value comma")

    TEST_TOKEN(WXJSONTK_VALUE_NULL, "Array null value")
    TEST_TOKEN(WXJSONTK_COMMA, "Array null value comma")

    TEST_TOKEN(WXJSONTK_VALUE_INT, "Array int value")
    if (lex.lastToken.value.ival != 12345) {
        (void) fprintf(stderr, "Incorrect parsed array integer value\n");
        exit(1);
    }
    TEST_TOKEN(WXJSONTK_COMMA, "Array int value comma")

    TEST_TOKEN(WXJSONTK_VALUE_DBL, "Array flt value")
    if (lex.lastToken.value.dval != 1.23) {
        (void) fprintf(stderr, "Incorrect parsed array flt value\n");
        exit(1);
    }
    TEST_TOKEN(WXJSONTK_COMMA, "Array flt value comma")

    TEST_STR_TOKEN("abc")

    TEST_TOKEN(WXJSONTK_VALUE_ARR_END, "Occ array closure")

    TEST_TOKEN(WXJSONTK_VALUE_OBJ_END, "Final object closure")
    TEST_TOKEN(WXJSONTK_EOF, "End of file")
    TEST_TOKEN(WXJSONTK_EOF, "End of file (two)")

    WXJSONLexerDestroy(&lex);

    /* And a bunch of error cases, driven by table... */
    for (idx = 0; idx < LEX_ERROR_COUNT; idx++) {
        WXJSONLexerInit(&lex, lexerErrorConds[idx].content);
        while ((lex.lastToken.type != WXJSONTK_EOF) &&
                   (lex.lastToken.type != WXJSONTK_ERROR)) {
            token = WXJSONLexerNext(&lex); \
            if (token->type == WXJSONTK_VALUE_STR) WXFree(token->value.sval);
        }
        if ((token->type != WXJSONTK_ERROR) ||
                (token->value.errorCode != lexerErrorConds[idx].errorCode)) {
            (void) fprintf(stderr, "%d: Incorrect lex error: %d (%d not %d)\n",
                           idx, token->type, token->value.errorCode,
                           lexerErrorConds[idx].errorCode);
            exit(1);
        }
        (void) fprintf(stderr, "Expected error: %s\n",
                       WXJSON_GetErrorStr(token->value.errorCode));
        WXJSONLexerDestroy(&lex);

        /* While in this loop, test the parser error passthrough */
        value = WXJSON_Decode(lexerErrorConds[idx].content);
        if ((value->type != WXJSONVALUE_ERROR) ||
                (value->value.error.errorCode !=
                                 lexerErrorConds[idx].errorCode)) {
            (void) fprintf(stderr, "%d: Incorrect lprs error: %d (%d not %d)\n",
                           idx, value->type, value->value.error.errorCode,
                           lexerErrorConds[idx].errorCode);
            exit(1);
        }
        (void) fprintf(stderr, "Expected error: %s\n",
                       WXJSON_GetErrorStr(value->value.error.errorCode));
        WXJSON_Destroy(value);
    }

    (void) fprintf(stderr, "Lex error tests complete\n");

    /* Second set of errors are only detected by the parser */
    for (idx = 0; idx < PARSE_ERROR_COUNT; idx++) {
        value = WXJSON_Decode(parserErrorConds[idx].content);
        if ((value->type != WXJSONVALUE_ERROR) ||
                (value->value.error.errorCode !=
                                 parserErrorConds[idx].errorCode)) {
            (void) fprintf(stderr, "%d: Incorrect prs error: %d (%d not %d)\n",
                           idx, value->type, value->value.error.errorCode,
                           parserErrorConds[idx].errorCode);
            exit(1);
        }
        (void) fprintf(stderr, "Expected error: %s\n",
                       WXJSON_GetErrorStr(value->value.error.errorCode));
        WXJSON_Destroy(value);
    }

    (void) fprintf(stderr, "Parse error tests complete\n");


    /* And now to full parsing */
    value = WXJSON_Decode(bigJSON);
    if (value->type == WXJSONVALUE_ERROR) {
        (void) fprintf(stderr, "Failed to parse bigval [%d: %s]?\n",
                       value->value.error.lineNumber,
                       WXJSON_GetErrorStr(value->value.error.errorCode));
        exit(1);
    }
    if (value->type != WXJSONVALUE_OBJECT) {
        (void) fprintf(stderr, "Root value not an object\n");
        exit(1);
    }

    /* Check results */
    sub = WXHash_GetEntry(&(value->value.oval), "empty_obj",
                          WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((sub == NULL) || (sub->type != WXJSONVALUE_OBJECT)) {
        (void) fprintf(stderr, "Incorrect empty_obj content\n");
        exit(1);
    }
    if (sub->value.oval.entryCount != 0) {
        (void) fprintf(stderr, "Empty object does not appear to be empty\n");
        exit(1);
    }

    sub = WXHash_GetEntry(&(value->value.oval), "empty_arr",
                          WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((sub == NULL) || (sub->type != WXJSONVALUE_ARRAY)) {
        (void) fprintf(stderr, "Incorrect empty_arr content\n");
        exit(1);
    }
    if (sub->value.aval.length != 0) {
        (void) fprintf(stderr, "Empty array does not appear to be empty\n");
        exit(1);
    }

    sub = WXHash_GetEntry(&(value->value.oval), "occ_obj",
                          WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((sub == NULL) || (sub->type != WXJSONVALUE_OBJECT)) {
        (void) fprintf(stderr, "Incorrect empty_obj content\n");
        exit(1);
    }
    if (sub->value.oval.entryCount != 10) {
        (void) fprintf(stderr, "Occupied object has wrong count\n");
        exit(1);
    }

    subsub = WXHash_GetEntry(&(sub->value.oval), "true_key",
                             WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((subsub == NULL) || (subsub->type != WXJSONVALUE_TRUE)) {
        (void) fprintf(stderr, "Incorrect true keyed value\n");
        exit(1);
    }
    subsub = WXHash_GetEntry(&(sub->value.oval), "false_key",
                             WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((subsub == NULL) || (subsub->type != WXJSONVALUE_FALSE)) {
        (void) fprintf(stderr, "Incorrect false keyed value\n");
        exit(1);
    }
    subsub = WXHash_GetEntry(&(sub->value.oval), "null_key",
                             WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((subsub == NULL) || (subsub->type != WXJSONVALUE_NULL)) {
        (void) fprintf(stderr, "Incorrect null keyed value\n");
        exit(1);
    }
    subsub = WXHash_GetEntry(&(sub->value.oval), "nint_key",
                             WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((subsub == NULL) || (subsub->type != WXJSONVALUE_INT) ||
            (subsub->value.ival != -1234)) {
        (void) fprintf(stderr, "Incorrect negint keyed value\n");
        exit(1);
    }
    subsub = WXHash_GetEntry(&(sub->value.oval), "str_key",
                             WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((subsub == NULL) || (subsub->type != WXJSONVALUE_STRING) ||
            (strcmp(subsub->value.sval, "abcdefg") != 0)) {
        (void) fprintf(stderr, "Incorrect str keyed value\n");
        exit(1);
    }

    sub = WXHash_GetEntry(&(value->value.oval), "occ_arr",
                          WXHash_StrHashFn, WXHash_StrEqualsFn);
    if ((sub == NULL) || (sub->type != WXJSONVALUE_ARRAY)) {
        (void) fprintf(stderr, "Incorrect empty_arr content\n");
        exit(1);
    }
    if (sub->value.aval.length != 6) {
        (void) fprintf(stderr, "Occupied array has wrong count\n");
        exit(1);
    }
    arrPtr = (WXJSONValue *) sub->value.aval.array;
    if (arrPtr[0].type != WXJSONVALUE_TRUE) {
        (void) fprintf(stderr, "Incorrect array value 0\n");
        exit(1);
    }
    if (arrPtr[1].type != WXJSONVALUE_FALSE) {
        (void) fprintf(stderr, "Incorrect array value 1\n");
        exit(1);
    }
    if (arrPtr[2].type != WXJSONVALUE_NULL) {
        (void) fprintf(stderr, "Incorrect array value 2\n");
        exit(1);
    }
    if ((arrPtr[3].type != WXJSONVALUE_INT) ||
                     (arrPtr[3].value.ival != 12345)) {
        (void) fprintf(stderr, "Incorrect array value 3\n");
        exit(1);
    }
    if ((arrPtr[4].type != WXJSONVALUE_DOUBLE) ||
                     (arrPtr[4].value.dval != 1.23)) {
        (void) fprintf(stderr, "Incorrect array value 4\n");
        exit(1);
    }
    if ((arrPtr[5].type != WXJSONVALUE_STRING) ||
                     (strcmp(arrPtr[5].value.sval, "abc") != 0)) {
        (void) fprintf(stderr, "Incorrect array value 5\n");
        exit(1);
    }

    /* Play with the finder */
    sub = WXJSON_Find(value, "empty_obj");
    if ((sub == NULL) || (sub->type != WXJSONVALUE_OBJECT) ||
            (sub->value.oval.entryCount != 0)) {
        (void) fprintf(stderr, "Find found incorrect object (empty)\n");
        exit(1);
    }
    sub = WXJSON_Find(value, "occ_obj.int_key");
    if ((sub == NULL) || (sub->type != WXJSONVALUE_INT) ||
            (sub->value.ival != 12345)) {
        (void) fprintf(stderr, "Find found incorrect object (int)\n");
        exit(1);
    }
    sub = WXJSON_Find(value, "occ_obj.int_key.nope");
    if (sub != NULL) {
        (void) fprintf(stderr, "Eh?  Found child for non-object\n");
        exit(1);
    }
    sub = WXJSON_Find(value, "empty_obj.nope");
    if (sub != NULL) {
        (void) fprintf(stderr, "Eh?  Found child for missing entry\n");
        exit(1);
    }

    /* Cleanup */
    WXJSON_Destroy(value);

    /* And encode/decode sequences */
    WXBuffer_Init(&buffer, 0);
    value = WXJSON_Decode("true");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer, "true") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of true value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    value = WXJSON_Decode("false");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer, "false") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of false value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    value = WXJSON_Decode("null");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer, "null") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of null value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    value = WXJSON_Decode("123");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer, "123") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of int value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    /* Strings are a big one... */
    value = WXJSON_Decode("\"-\\\"-\\\\-\\/-\\b-\\f-\\n-\\r-\\t-\"");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer,
                    "\"-\\\"-\\\\-\\/-\\b-\\f-\\n-\\r-\\t-\"") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of str value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    value = WXJSON_Decode("\"-\\u0007-\\u0154-\\u7562\"");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer,
                    "\"-\\u0007-\\u0154-\\u7562\"") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of str value 2");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    /* Of course, so are objects and arrays */
    value = WXJSON_Decode("[12, null, \"abc\", true]");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer,
                    "[12,null,\"abc\",true]") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of array value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    value = WXJSON_Decode("{\"abc\": 1234}");
    if ((WXJSON_Encode(&buffer, value, FALSE) == NULL) ||
            (strcmp((char *) buffer.buffer,
                    "{\"abc\":1234}") != 0)) {
        (void) fprintf(stderr, "Incorrect dec/enc of object value\n");
        exit(1);
    }
    WXJSON_Destroy(value);
    WXBuffer_Empty(&buffer);

    /* One last output for checking prettiness */
    value = WXJSON_Decode(bigJSON);
    WXJSON_Encode(&buffer, value, TRUE);
    (void) fprintf(stdout, "\n%s\n", buffer.buffer);
    WXJSON_Destroy(value);

    WXBuffer_Destroy(&buffer);
}
