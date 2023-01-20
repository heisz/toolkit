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
        doc = WXML_Decode(lexerErrorConds[idx].content, TRUE, errorMsg,
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
        doc = WXML_Decode(parserErrorConds[idx].content, FALSE, errorMsg,
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
    doc = WXML_Decode(bigXML, FALSE, errorMsg, sizeof(errorMsg));
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
    if ((attr == NULL) || (attr->namespace != NULL) ||
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
    if ((attr == NULL) || (attr->namespace != NULL) ||
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
    /* Note that this can support retaining since there's no extra space */
    WXBuffer_Empty(&buffer);
    doc = WXML_Decode("<one><two id=\"id2\"><three attr='yo'>a</three>"
                                "<four>b</four></two></one>",
                      TRUE, errorMsg, sizeof(errorMsg));
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
    WXML_Destroy(doc);

    /* Try a more complex case with retained text content */
    doc = WXML_Decode(" <one><two id=\"id2&apos;&quot;&lt;&gt;\">"
                                           "<![CDATA[ ab]]>"
                                "<three attr='yo'>a&apos;&gt;&lt;\"</three>"
                                " cd <four>b</four><empty/>fg</two></one>  ",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse main document: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Encode(&buffer, doc, TRUE);
    (void) fprintf(stdout, "PRETTY:\n%s\n", buffer.buffer);
    WXBuffer_Empty(&buffer);
    WXML_Encode(&buffer, doc, FALSE);
    (void) fprintf(stdout, "STANDARD:\n%s\n", buffer.buffer);
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, doc, NULL, TRUE);
    (void) fprintf(stdout, "CANONICAL:\n%s\n", buffer.buffer);
    WXML_Destroy(doc);

    /* Why not use the test cases from the canonical specification? */
    /* Section 3.1 (note, not quite correct due to PI) */
    doc = WXML_Decode("<?xml version=\"1.0\"?>\n"
                      "\n"
                      "<?xml-stylesheet   href=\"doc.xsl\"\n"
                      "   type=\"text/xsl\"   ?>\n"
                      "\n"
                      "<!DOCTYPE doc SYSTEM \"doc.dtd\">\n"
                      "\n"
                      "<doc>Hello, world!<!-- Comment 1 --></doc>\n"
                      "\n"
                      "<?pi-without-data     ?>\n"
                      "\n"
                      "<!-- Comment 2 -->\n"
                      "\n"
                      "<!-- Comment 3 -->",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse document 3.1: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, doc, NULL, TRUE);
    if (strcmp(buffer.buffer, "<doc>Hello, world!</doc>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for Section 3.1\n");
        exit(1);
    }
    WXML_Destroy(doc);

    /* Section 3.2 */
    doc = WXML_Decode("<doc>\n"
                      "   <clean>   </clean>\n"
                      "   <dirty>   A   B   </dirty>\n"
                      "   <mixed>\n"
                      "      A\n"
                      "      <clean>   </clean>\n"
                      "      B\n"
                      "      <dirty>   A   B   </dirty>\n"
                      "      C\n"
                      "   </mixed>\n"
                      "</doc>",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse document 3.2: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, doc, NULL, TRUE);
    if (strcmp(buffer.buffer,
               "<doc>\n"
               "   <clean>   </clean>\n"
               "   <dirty>   A   B   </dirty>\n"
               "   <mixed>\n"
               "      A\n"
               "      <clean>   </clean>\n"
               "      B\n"
               "      <dirty>   A   B   </dirty>\n"
               "      C\n"
               "   </mixed>\n"
               "</doc>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for Section 3.2\n");
        exit(1);
    }
    WXML_Destroy(doc);

    /* Section 3.3 */
    /* Note: no DOCTYPE support for default attributes */
    doc = WXML_Decode("<doc>\n"
                      "   <e1   />\n"
                      "   <e2   ></e2>\n"
                      "   <e3   name = \"elem3\"   id=\"elem3\"   />\n"
                      "   <e4   name=\"elem4\"   id=\"elem4\"   ></e4>\n"
                      "   <e5 a:attr=\"out\" b:attr=\"sorted\" "
                                                 "attr2=\"all\" attr=\"I'm\"\n"
                      "      xmlns:b=\"http://www.ietf.org\"\n"
                      "      xmlns:a=\"http://www.w3.org\"\n"
                      "      xmlns=\"http://example.org\"/>\n"
                      "   <e6 xmlns=\"\" xmlns:a=\"http://www.w3.org\">\n"
                      "      <e7 xmlns=\"http://www.ietf.org\">\n"
                      "         <e8 xmlns=\"\" xmlns:a=\"http://www.w3.org\">\n"
                      "            <e9 attr=\"default\" xmlns=\"\" xmlns:a=\""
                                           "http://www.ietf.org\"/>\n"
                      "         </e8>\n"
                      "      </e7>\n"
                      "   </e6>\n"
                      "</doc>\n",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse document 3.3: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, doc, NULL, TRUE);
    if (strcmp(buffer.buffer,
               "<doc>\n"
               "   <e1></e1>\n"
               "   <e2></e2>\n"
               "   <e3 id=\"elem3\" name=\"elem3\"></e3>\n"
               "   <e4 id=\"elem4\" name=\"elem4\"></e4>\n"
               "   <e5 xmlns=\"http://example.org\" "
                      "xmlns:a=\"http://www.w3.org\" "
                      "xmlns:b=\"http://www.ietf.org\" "
                      "attr=\"I'm\" attr2=\"all\" "
                      "b:attr=\"sorted\" a:attr=\"out\"></e5>\n"
               "   <e6 xmlns:a=\"http://www.w3.org\">\n"
               "      <e7 xmlns=\"http://www.ietf.org\">\n"
               "         <e8 xmlns=\"\">\n"
               "            <e9 xmlns:a=\"http://www.ietf.org\" "
                               "attr=\"default\"></e9>\n"
               "         </e8>\n"
               "      </e7>\n"
               "   </e6>\n"
               "</doc>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for Section 3.3 '%s'\n", buffer.buffer);
        exit(1);
    }
    WXML_Destroy(doc);

    /* Section 3.4 */
    /* This has slight mutation because the DOCTYPE controls are not parsed */
    doc = WXML_Decode("<doc>\n"
                      "   <text>First line&#x0d;&#10;Second line</text>\n"
                      "   <value>&#x32;</value>\n"
                      "   <compute><![CDATA[value>\"0\" && value<\"10\""
                                " ?\"valid\":\"error\"]]></compute>\n"
                      "   <compute expr='value>\"0\" &amp;&amp; value&lt;"
                                "\"10\" ?\"valid\":\"error\"'>valid</compute>\n"
                      "   <norm attr=' &apos;   &#x20;&#13;&#xa;&#9;"
                                "   &apos; '/>\n"
                      "   <normNames attr='A&#x20;&#13;&#xa;&#9; B'/>\n"
                      "   <normId id='&apos;&#x20;&#13;&#xa;&#9; &apos;'/>\n"
                      "</doc>",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse document 3.4: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, doc, NULL, TRUE);
    if (strcmp(buffer.buffer,
               "<doc>\n"
               "   <text>First line&#xD;\n"
               "Second line</text>\n"
               "   <value>2</value>\n"
               "   <compute>value&gt;\"0\" &amp;&amp; value&lt;\"10\""
                            " ?\"valid\":\"error\"</compute>\n"
               "   <compute expr=\"value>&quot;0&quot; &amp;&amp;"
                            " value&lt;&quot;10&quot; ?&quot;valid&quot;:"
                            "&quot;error&quot;\">valid</compute>\n"
               "   <norm attr=\" '    &#xD;&#xA;&#x9;   ' \"></norm>\n"
               "   <normNames attr=\"A &#xD;&#xA;&#x9; B\"></normNames>\n"
               "   <normId id=\"' &#xD;&#xA;&#x9; '\"></normId>\n"
               "</doc>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for Section 3.4\n");
        exit(1);
    }
    WXML_Destroy(doc);

    /* Section 3.5 ignored as it doesn't handle entity references */
    /* Section 3.6 ignored as it doesn't handle different charset encoding */

    /* Section 3.7 */
    doc = WXML_Decode("<doc xmlns=\"http://www.ietf.org\" "
                                           "xmlns:w3c=\"http://www.w3.org\">\n"
                      "   <e1>\n"
                      "      <e2 xmlns=\"\">\n"
                      "         <e3 id=\"E3\"/>\n"
                      "      </e2>\n"
                      "   </e1>\n"
                      "</doc>",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse document 3.7: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, WXML_Find(doc, "e1", FALSE), NULL, TRUE);
    if (strcmp(buffer.buffer,
               "<e1 xmlns=\"http://www.ietf.org\" "
                                         "xmlns:w3c=\"http://www.w3.org\">\n"
               "      <e2 xmlns=\"\">\n"
               "         <e3 id=\"E3\"></e3>\n"
               "      </e2>\n"
               "   </e1>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for Section 3.7\n");
        exit(1);
    }
    WXML_Destroy(doc);

    /* Section 3.8 ignored as we don't get deeply into attribute propagation */

    /* This was a bug when used in SAML signing (NOT VALID) */
    doc = WXML_Decode("<samlp:Response xmlns:samlp=\"urn:SAML:2.0:protocol\" "
                             "ID=\"dkjfhdgfuwefwfuwefsdfsfosfsf\" "
                             "InResponseTo=\"xyzzy\">"
                        "<Issuer xmlns=\"urn:oasis:SAML:2.0:assertion\">"
                                       "https://sts.windows.net/706/</Issuer>"
                        "<Assertion xmlns=\"urn:oasis:SAML:2.0:assertion\" "
                                    "ID=\"dfgdljfgddfjdsfhslfdjhdsflsjdf\">"
                          "<Issuer>https://sts.windows.net/706/</Issuer>"
                          "<Signature xmlns=\"http://www.w3.org/xmldsig#\">"
                            "<SignedInfo>"
                              "<CanonicalizationMethod Algorithm=\"exc-c14\"/>"
                              "<SignatureMethod Algorithm=\"xmldsig\"/>"
                            "</SignedInfo>"
                          "</Signature>"
                        "</Assertion>"
                      "</samlp:Response>",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse SAML: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, WXML_Find(doc, "Signature", TRUE), NULL, TRUE);
    if (strcmp(buffer.buffer,
               "<Signature xmlns=\"http://www.w3.org/xmldsig#\" "
                          "xmlns:samlp=\"urn:SAML:2.0:protocol\">"
                 "<SignedInfo>"
                   "<CanonicalizationMethod Algorithm=\"exc-c14\">"
                                             "</CanonicalizationMethod>"
                   "<SignatureMethod Algorithm=\"xmldsig\"></SignatureMethod>"
                 "</SignedInfo>"
               "</Signature>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for SAML\n");
        exit(1);
    }

    /* Same source for test case of exclusive canonicalization */
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, WXML_Find(doc, "Signature", TRUE), NULL, FALSE);
    if (strcmp(buffer.buffer,
               "<Signature xmlns=\"http://www.w3.org/xmldsig#\">"
                 "<SignedInfo>"
                   "<CanonicalizationMethod Algorithm=\"exc-c14\">"
                                             "</CanonicalizationMethod>"
                   "<SignatureMethod Algorithm=\"xmldsig\"></SignatureMethod>"
                 "</SignedInfo>"
               "</Signature>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for SAML excl\n");
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, WXML_Find(doc, "SignedInfo", TRUE), NULL, FALSE);
    if (strcmp(buffer.buffer,
               "<SignedInfo xmlns=\"http://www.w3.org/xmldsig#\">"
                 "<CanonicalizationMethod Algorithm=\"exc-c14\">"
                                           "</CanonicalizationMethod>"
                 "<SignatureMethod Algorithm=\"xmldsig\"></SignatureMethod>"
               "</SignedInfo>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for xmlns excl\n");
        exit(1);
    }
    WXML_Destroy(doc);

    /* Super complicated case to test the namespace rendering rules (3.1) */
    doc = WXML_Decode("<rt xmlns=\"main\" "
                              "xmlns:a=\"main-eh\" xmlns:b=\"main-bee\">\n"
                        "<nest>\n"
                          "<a:right el=\"xxx\"/>\n"
                          "<rightoh a:el=\"xxx\"/>\n"
                          "<left>\n"
                              "<a:left el=\"xyyz\"/>\n"
                              "<leftoh b:el=\"xyzzy\"/>\n"
                          "</left>\n"
                        "</nest>\n"
                      "</rt>",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse complex: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, WXML_Find(doc, "nest", TRUE), NULL, FALSE);
    if (strcmp(buffer.buffer,
               "<nest xmlns=\"main\">\n"
                  "<a:right xmlns:a=\"main-eh\" el=\"xxx\"></a:right>\n"
                  "<rightoh xmlns:a=\"main-eh\" a:el=\"xxx\"></rightoh>\n"
                  "<left>\n"
                      "<a:left xmlns:a=\"main-eh\" el=\"xyyz\"></a:left>\n"
                      "<leftoh xmlns:b=\"main-bee\" b:el=\"xyzzy\"></leftoh>\n"
                  "</left>\n"
                "</nest>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for deep render\n");
        exit(1);
    }
    WXML_Destroy(doc);

    /* Another challenge from the SAML canonicalization failures */
    doc = WXML_Decode("<samlp:Response xmlns:samlp=\"urn:SAML:2.0:protocol\" "
                             "ID=\"dkjfhdgfuwefwfuwefsdfsfosfsf\" "
                             "InResponseTo=\"xyzzy\">"
                        "<Issuer xmlns=\"urn:oasis:SAML:2.0:assertion\">"
                                       "https://sts.windows.net/706/</Issuer>"
                        "<Assertion xmlns=\"urn:oasis:SAML:2.0:assertion\" "
                                    "ID=\"dfgdljfgddfjdsfhslfdjhdsflsjdf\">"
                          "<Issuer>https://sts.windows.net/706/</Issuer>"
                          "<ds:Signature xmlns:ds=\"xmldsig#\">"
                            "<ds:SignedInfo>"
                              "<ds:CanonicalizationMethod "
                                         "Algorithm=\"xml-exc-c14n#\"/>"
                              "<ds:SignatureMethod "
                                         "Algorithm=\"#rsa-sha256\"/>"
                              "<ds:Reference URI=\"#_aaa\">"
                                "<ds:Transforms>"
                                  "<ds:Transform "
                                         "Algorithm=\"xmldsig#enveloped\"/>"
                                  "<ds:Transform "
                                         "Algorithm=\"xml-exc-c14n#\"/>"
                                "</ds:Transforms>"
                                "<ds:DigestMethod "
                                         "Algorithm=\"xmlenc#sha256\"/>"
                                "<ds:DigestValue>abcdefg</ds:DigestValue>"
                              "</ds:Reference>"
                            "</ds:SignedInfo>"
                          "</ds:Signature>"
                        "</Assertion>"
                      "</samlp:Response>",
                      TRUE, errorMsg, sizeof(errorMsg));
    if (doc == NULL) {
        (void) fprintf(stderr, "Failed to parse SAML: %s\n", errorMsg);
        exit(1);
    }
    WXBuffer_Empty(&buffer);
    WXML_Canonicalize(&buffer, WXML_Find(doc, "SignedInfo", TRUE), NULL, FALSE);
    if (strcmp(buffer.buffer,
               "<ds:SignedInfo xmlns:ds=\"xmldsig#\">"
                 "<ds:CanonicalizationMethod Algorithm=\"xml-exc-c14n#\">"
                     "</ds:CanonicalizationMethod>"
                 "<ds:SignatureMethod Algorithm=\"#rsa-sha256\">"
                     "</ds:SignatureMethod>"
                 "<ds:Reference URI=\"#_aaa\">"
                   "<ds:Transforms>"
                     "<ds:Transform Algorithm=\"xmldsig#enveloped\">"
                         "</ds:Transform>"
                     "<ds:Transform Algorithm=\"xml-exc-c14n#\">"
                         "</ds:Transform>"
                   "</ds:Transforms>"
                   "<ds:DigestMethod Algorithm=\"xmlenc#sha256\">"
                       "</ds:DigestMethod>"
                   "<ds:DigestValue>abcdefg</ds:DigestValue>"
                 "</ds:Reference>"
               "</ds:SignedInfo>") != 0) {
        (void) fprintf(stderr, "Incorrect canonical result for ADFS SAML\n");
        exit(1);
    }

    /* Clean up on aisle 3! */
    WXBuffer_Destroy(&buffer);

    (void) fprintf(stderr, "All tests passed\n");
}
