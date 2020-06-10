/*
 * Test interface for the XML data processor.
 *
 * Copyright (C) 2007-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "xml.h"
#include "xmlint.h"
#include "mem.h"

#define TEST_TOKEN(exp, msg) \
    type = WXMLLexerNext(&lex, errorMsg, sizeof(errorMsg)); \
    if (type != exp) { \
        (void) fprintf(stderr, "Error: %s (%d vs %d)\n", msg, \
                       type, exp); \
        exit(1); \
    }

#define TEST_STR_TOKEN(exp, msg, sval) \
    type = WXMLLexerNext(&lex, errorMsg, sizeof(errorMsg)); \
    if (type != exp) { \
        (void) fprintf(stderr, "Error: %s (%d vs %d)\n", msg, \
                       type, exp); \
        exit(1); \
    } \
    if (strcmp(lex.lastToken.val, sval) != 0) { \
        (void) fprintf(stderr, "Error: string mismatch ('%s' vs '%s')\n", \
                       lex.lastToken.val, sval); \
        exit(1); \
    } \
    WXFree(lex.lastToken.val);

static char *bigXML = 
    "<?xml version=\"1.0\"?>\n"
    "<!-- This is a pretty big bit of XML to test the lexer -->\n"
    "<!DOCTYPE test SYSTEM \"test.dtd\">\n"
    "<ns:root xmlns:ns='test:xml' xmlns='dflt'>mixed text\n"
    "    <empty attr \t />\n"
    "    <notsoempty sqattr = '&lt;&amp;yo&gt;' "
                    "ns:dqattr=\"\">"
            "&apos;&#36;content&#x0025;&quot;&lt;"
        "</notsoempty>\n"
    "</ns:root>\n";

static struct LexErrorDef {
     char *content, *exp;
} lexerErrorConds[] = {
     { "\n<!-- there is no end....", "unterminated comment" },
     { "<! there still is no end....", "unterminated DTD" },
     { "<?xml?>\n<a>\n<![CDATA[ again its unending", "unterminated CDATA " },
     { "<a attr='sensing a theme here", "unterminated attr 'value'" },
     { "<a attr=\"yup, definitely...", "unterminated attr \"value\"" },
     { "<a\n   attr='oh wait'\n001234>", "invalid text" },
     { "<a\n   attr='oh wait'\n>&notavalue;", "Invalid character entity" },
     { "<a\n   attr='oh wait'\n>&#12a;", "Invalid numeric character entity" }
};

#define LEX_ERROR_COUNT \
               (int) (sizeof(lexerErrorConds) / sizeof(struct LexErrorDef))

static struct ParseErrorDef {
     char *content, *exp;
} parserErrorConds[] = {
     { "", "no root element" },
     { "<a>", "unclosed element 'a'" },
     { "<a/><b></b>", "Multiple root elements" },
     { "<?xml 'private' version=\"1.0\"", "unterminated processing inst" },
     { "<?xml?>\n<'value'>", "Missing name in opening" },
     { "<?xml?>\n<a key='val'", "unterminated element tag" },
     { "<?xml?>\n<a xmlns:bad/>", "require URI" },
     { "<?xml?>\n<a='oops'/>", "missing identifier for attribute" },
     { "<!DOCTYPE>\n<a><b empty missing=/>", "requires value" },
     { "<a><b 'xxx'/>", "invalid text in element tag" },
     { "</b>", "Unexpected end tag" },
     { "<a></>", "Missing name in closing tag" },
     { "<a></b>", "Unmatched closing tag" },
     { "<a></a", "Missing end of closing tag" },
     { "<a></a dummy=''>", "Extraneous content in closing tag" }
};

#define PARSE_ERROR_COUNT \
               (int) (sizeof(parserErrorConds) / sizeof(struct ParseErrorDef))

/**
 * Main testing entry point.  Lots of parsing is about to follow...
 */
int main(int argc, char **argv) {
    WXMLElement *doc, *child;
    WXMLAttribute *attr;
    char errorMsg[1024];
    WXMLTokenType type;
    WXBuffer buffer;
    WXMLLexer lex;
    int idx;

    /* At some point, put the MTraq testcase identifiers in here */

    /* Big run of lexing test cases */
    WXMLLexerInit(&lex, bigXML);
    TEST_TOKEN(WXMLTK_PI_START, "Processing instruction start");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "PI attribute", "xml");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "PI version", "version");
    TEST_TOKEN(WXMLTK_ATTR_EQ, "PI version assign");
    TEST_STR_TOKEN(WXMLTK_ATTR_VALUE, "PI version value", "1.0");
    TEST_TOKEN(WXMLTK_PI_END, "Processing instruction end");

    /* Force the reset handled in the grammar (PI exit without element) */
    lex.ignoreWhitespace = TRUE;

    TEST_TOKEN(WXMLTK_ELMNT_TAG_START, "Root tag start");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Root tag name", "ns:root");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Root namespace id", "xmlns:ns");
    TEST_TOKEN(WXMLTK_ATTR_EQ, "Root namespace eq");
    TEST_STR_TOKEN(WXMLTK_ATTR_VALUE, "Root namespace val", "test:xml");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Root namespace dflt id", "xmlns");
    TEST_TOKEN(WXMLTK_ATTR_EQ, "Root namespace dflt eq");
    TEST_STR_TOKEN(WXMLTK_ATTR_VALUE, "Root namespace dflt val", "dflt");
    TEST_TOKEN(WXMLTK_ELMNT_TAG_END, "Root tag end");
    TEST_STR_TOKEN(WXMLTK_CONTENT, "Root first content", "mixed text\n    ");

    TEST_TOKEN(WXMLTK_ELMNT_TAG_START, "Empty tag start");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Empty tag name", "empty");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Empty attr", "attr");
    TEST_TOKEN(WXMLTK_EMPTY_ELMNT_TAG_END, "Empty tag end");
    TEST_STR_TOKEN(WXMLTK_CONTENT, "Root second content", "\n    ");

    TEST_TOKEN(WXMLTK_ELMNT_TAG_START, "Full tag start");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Full tag name", "notsoempty");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Single quote attr nm", "sqattr");
    TEST_TOKEN(WXMLTK_ATTR_EQ, "Single quote attr eq");
    TEST_STR_TOKEN(WXMLTK_ATTR_VALUE, "Single quote attr val", "<&yo>");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Double quote attr nm", "ns:dqattr");
    TEST_TOKEN(WXMLTK_ATTR_EQ, "Double quote attr eq");
    TEST_STR_TOKEN(WXMLTK_ATTR_VALUE, "Double quote attr val", "");
    TEST_TOKEN(WXMLTK_ELMNT_TAG_END, "Full tag start close");
    TEST_STR_TOKEN(WXMLTK_CONTENT, "Full tag content", "'$content%\"<");
    TEST_TOKEN(WXMLTK_CLOSE_ELMNT_TAG_START, "Full tag end");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Full tag name", "notsoempty");
    TEST_TOKEN(WXMLTK_ELMNT_TAG_END, "Full tag end close");
    TEST_STR_TOKEN(WXMLTK_CONTENT, "Root third content", "\n");

    TEST_TOKEN(WXMLTK_CLOSE_ELMNT_TAG_START, "Root tag end");
    TEST_STR_TOKEN(WXMLTK_IDENTIFIER, "Root tag name", "ns:root");
    TEST_TOKEN(WXMLTK_ELMNT_TAG_END, "Root tag end close");

    /* Force the reset handled in the grammar (closure of element) */
    lex.ignoreWhitespace = TRUE;

    TEST_TOKEN(WXMLTK_EOF, "End of document");

    /* A bunch of error cases, driven by tables... */
    for (idx = 0; idx < LEX_ERROR_COUNT; idx++) {
        WXMLLexerInit(&lex, lexerErrorConds[idx].content);
        while ((lex.lastToken.type != WXMLTK_EOF) &&
                   (lex.lastToken.type != WXMLTK_ERROR)) {
            type = WXMLLexerNext(&lex, errorMsg, sizeof(errorMsg)); \
            if (lex.lastToken.val != NULL) WXFree(lex.lastToken.val);
        }
        if ((type != WXMLTK_ERROR) ||
                (strstr(errorMsg, lexerErrorConds[idx].exp) == NULL)) {
            (void) fprintf(stderr, "%d: Incorrect lex error: %s (exp %s)\n",
                           idx, errorMsg, lexerErrorConds[idx].exp);
            exit(1);
        }
        (void) fprintf(stderr, "Expected lex error: %s\n", errorMsg);
        WXMLLexerDestroy(&lex);

        /* While in this loop, test the parser error passthrough */
        doc = WXML_Decode(lexerErrorConds[idx].content, errorMsg,
                          sizeof(errorMsg));
        if ((doc != NULL) ||
                (strstr(errorMsg, lexerErrorConds[idx].exp) == NULL)) {
            (void) fprintf(stderr, "%d: Incorrect lprs error: %s (exp %s)\n",
                           idx, errorMsg, lexerErrorConds[idx].exp);
            exit(1);
        }
        (void) fprintf(stderr, "Expected lparse error: %s\n", errorMsg);
    }

    (void) fprintf(stderr, "Lex error tests complete\n");

    /* Second set of errors are only detected by the parser */
    for (idx = 0; idx < PARSE_ERROR_COUNT; idx++) {
        doc = WXML_Decode(parserErrorConds[idx].content, errorMsg,
                          sizeof(errorMsg));
        if ((doc != NULL) ||
                (strstr(errorMsg, parserErrorConds[idx].exp) == NULL)) {
            (void) fprintf(stderr, "%d: Incorrect parse error: %s (exp %s)\n",
                           idx, errorMsg, parserErrorConds[idx].exp);
            exit(1);
        }
        (void) fprintf(stderr, "Expected parse error: %s\n", errorMsg);
    }

    (void) fprintf(stderr, "Parse error tests complete\n");

    /* And now to full parsing */
    doc = WXML_Decode(bigXML, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse main document: %s\n", errorMsg);
        exit(1);
    }

    /* Lots of validations */
    if ((doc->namespace == NULL) ||
            (strcmp(doc->namespace->href, "test:xml") != 0) ||
            (strcmp(doc->name, "root") != 0)) {
        (void) fprintf(stderr, "Invalid name(space) for root element\n");
        exit(1);
    }
    if (doc->attributes != NULL) {
        (void) fprintf(stderr, "Not expecting root attributes\n");
        exit(1);
    }
    if (strcmp(doc->content, "mixed text\n    \n    \n") != 0) {
        (void) fprintf(stderr, "Incorrect aggregated content\n");
        exit(1);
    }

    /* Nice that XML is ordered... */
    child = doc->children;
    if ((child == NULL) || (child->namespace == NULL) ||
            (strcmp(child->namespace->href, "dflt") != 0) ||
            (strcmp(child->name, "empty") != 0)) {
        (void) fprintf(stderr, "Invalid name(space) for empty element\n");
        exit(1);
    }
    attr = child->attributes;
    if ((attr == NULL) || (attr->namespace == NULL) ||
            (strcmp(attr->namespace->href, "dflt") != 0) ||
            (strcmp(attr->name, "attr") != 0)) {
        (void) fprintf(stderr, "Invalid name(space) for empty attr\n");
        exit(1);
    }
    if (attr->value != NULL) {
        (void) fprintf(stderr, "Not expecting content for empty attribute\n");
        exit(1);
    }

    child = child->next;
    if ((child == NULL) || (child->namespace == NULL) ||
            (strcmp(child->namespace->href, "dflt") != 0) ||
            (strcmp(child->name, "notsoempty") != 0)) {
        (void) fprintf(stderr, "Invalid name(space) for notempty element\n");
        exit(1);
    }
    attr = child->attributes;
    if ((attr == NULL) || (attr->namespace == NULL) ||
            (strcmp(attr->namespace->href, "dflt") != 0) ||
            (strcmp(attr->name, "sqattr") != 0)) {
        (void) fprintf(stderr, "Invalid name(space) for notempty sq attr\n");
        exit(1);
    }
    if (strcmp(attr->value, "<&yo>") != 0) {
        (void) fprintf(stderr, "Incorrect value for notempty sq attr\n");
        exit(1);
    }

    attr = attr->next;
    if ((attr == NULL) || (attr->namespace == NULL) ||
            (strcmp(attr->namespace->href, "test:xml") != 0) ||
            (strcmp(attr->name, "dqattr") != 0)) {
        (void) fprintf(stderr, "Invalid name(space) for notempty dq attr\n");
        exit(1);
    }
    if (strcmp(attr->value, "") != 0) {
        (void) fprintf(stderr, "Incorrect value for notempty dq attr\n");
        exit(1);
    }

    if (strcmp(child->content, "'$content%\"<") != 0) {
        (void) fprintf(stderr, "Incorrect content for non-empty element\n");
        exit(1);
    }

    /* Check some cases */
    WXBuffer_Init(&buffer, 0);
    WXML_Encode(&buffer, doc->children, FALSE);
    if (strcmp((char *) buffer.buffer, "<empty attr/>") != 0) {
        (void) fprintf(stderr, "Incorrect encoding of empty element/attr\n");
        exit(1);
    }

    WXBuffer_Empty(&buffer);
    WXML_Encode(&buffer, doc->children->next, FALSE);
    if (strstr((char *) buffer.buffer, "sqattr=\"&lt;&amp;yo&gt;\"") == NULL) {
        (void) fprintf(stderr, "Incorrect encoding of attr characters\n");
        exit(1);
    }
    if (strstr((char *) buffer.buffer, "ns:dqattr=\"\"") == NULL) {
        (void) fprintf(stderr, "Incorrect encoding of ns attr\n");
        exit(1);
    }
    if (strstr((char *) buffer.buffer, ">'$content%\"&lt;</") == NULL) {
        (void) fprintf(stderr, "Incorrect encoding of content\n");
        exit(1);
    }

    /* Big pretty and compact for visual compare */
    WXML_Encode(&buffer, doc, TRUE);
    (void) fprintf(stdout, "\n%s\n", buffer.buffer);
    WXBuffer_Empty(&buffer);
    WXML_Encode(&buffer, doc, FALSE);
    (void) fprintf(stdout, "\n%s\n", buffer.buffer);
    WXML_Destroy(doc);

    /* Visual check for deeply nested layout */
    WXBuffer_Empty(&buffer);
    doc = WXML_Decode("<one><two id=\"id2\"><three attr='yo'>a</three>"
                                "<four>b</four></two></one>",
                      errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse main document: %s\n", errorMsg);
        exit(1);
    }
    WXML_Encode(&buffer, doc, TRUE);
    (void) fprintf(stdout, "\n%s\n", buffer.buffer);

    /*
     * <one>
     *     <two Id="id2">
     *         <three attr="yo">a</three>
     *         <four>b</four>
     *     </two>
     * </one>
     */ 

    /* Try some find bits and pieces */
    child = (WXMLElement *) WXML_Find(doc, "dummy", FALSE);
    if (child != NULL) {
        (void) fprintf(stderr, "Found an undefined root element?\n");
        exit(1);
    }
    child = (WXMLElement *) WXML_Find(doc, "/two", FALSE);
    if ((child == NULL) || (strcmp(child->name, "two") != 0)) {
        (void) fprintf(stderr, "Did not find the child\n");
        exit(1);
    }
    child = (WXMLElement *) WXML_Find(doc, "/two//four", FALSE);
    if ((child == NULL) || (strcmp(child->name, "four") != 0)) {
        (void) fprintf(stderr, "Did not find the lowest child\n");
        exit(1);
    }
    child = (WXMLElement *) WXML_Find(doc, "//four", FALSE);
    if ((child == NULL) || (strcmp(child->name, "four") != 0)) {
        (void) fprintf(stderr, "Did not find the lowest child descendant\n");
        exit(1);
    }
    attr = (WXMLAttribute *) WXML_Find(doc, "//two/three/@attr", FALSE);
    if ((attr == NULL) || (strcmp(attr->name, "attr") != 0)) {
        (void) fprintf(stderr, "Did not find the embedded attribute\n");
        exit(1);
    }
    attr = (WXMLAttribute *) WXML_Find(doc, "//three/@attr", FALSE);
    if ((attr == NULL) || (strcmp(attr->name, "attr") != 0)) {
        (void) fprintf(stderr, "Did not find the embedded attribute desc\n");
        exit(1);
    }
    attr = (WXMLAttribute *) WXML_Find(doc, "//@attr", FALSE);
    if ((attr == NULL) || (strcmp(attr->name, "attr") != 0)) {
        (void) fprintf(stderr, "Did not find the embedded attribute desc!\n");
        exit(1);
    }
    child = (WXMLElement *) WXML_Find(doc, "#id", TRUE);
    if (child != NULL) {
        (void) fprintf(stderr, "Did not find node by partial id\n");
        exit(1);
    }
    attr = (WXMLAttribute *) WXML_Find(doc, "//#id2/three/@attr", FALSE);
    if ((attr == NULL) || (strcmp(attr->name, "attr") != 0)) {
        (void) fprintf(stderr, "Did not find the embedded attribute from id\n");
        exit(1);
    }
    attr = (WXMLAttribute *) WXML_Find(doc, "//#id2//@attr", FALSE);
    if ((attr == NULL) || (strcmp(attr->name, "attr") != 0)) {
        (void) fprintf(stderr, "Did not find the descend attribute from id\n");
        exit(1);
    }

    /* Clean up on aisle 3! */
    WXML_Destroy(doc);
    WXBuffer_Destroy(&buffer);

    (void) fprintf(stderr, "All tests passed\n");
}
