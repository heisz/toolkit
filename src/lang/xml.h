/*
 * Structures and methods for parsing, representing and generating XML data.
 *
 * Copyright (C) 1997-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#ifndef WX_XML_H
#define WX_XML_H 1

#include "buffer.h"

/**
 * Some notes on the migration of this code, originally based on the matching
 * MiniXML.pm implementation that was used in the really early MTraq tools.
 *
 * - it is not a full XML implementation, there are more complete libraries
 *   available for managing DTD, XSL, etc. etc. etc.
 * - it is intended to be a fast, lightweight parser for use with 'compliant'
 *   services (basic messaging, configuration, etc.) and memory images
 * - is a full DOM parser using coding models consistent with other elements
 *   of the toolkit, does not support a SAX callback model
 * - it does not provide access to individual fragments of XML, it aims to
 *   provide straightforward access to content only.  The decode/encode
 *   operations are not completely reversible and all significant whitespace
 *   is preserved
 */

/* Structure representing namespace information */
typedef struct WXMLNamespace {
    /* Attribute and element namespace prefix */
    char *prefix;

    /* Where provided/defined, the URI associated to the namespace */
    char *href;

    /* Element from which this namespace originates (for cleanup) */
    struct WXMLElement *origin;

    /* Next namespace in the element linked list, can point up tree */
    struct WXMLNamespace *next;
} WXMLNamespace;

/* Structure representing attributes of an element instance */
typedef struct WXMLAttribute {
    /* The name of the attribute */
    char *name;

    /* Namespace reference, if applicable */
    struct XMLNamespace *namespace;

    /* Next attribute in the element linked list */
    struct WXMLAttribute *next;

    /* Associated value of the attribute, when provided */
    char *value;
} WXMLAttribute;

/* Structure representing an XML element/node instance */
typedef struct WXMLElement {
    /* The name of the element */
    char *name;

    /* List of namespaces defined for this element and explicit namespace ref */
    WXMLNamespace *namespace, *namespaceSet;

    /* Linked list of element attributes with last/append element */
    WXMLAttribute *attributes, *lastAttribute;

    /* Linked list of child (nested) nodes and the last/append node */
    struct WXMLElement *children, *lastChild;

    /* For child nodes, reference to the parent and next child instance */
    struct WXMLElement *parent, *next;

    /* Amalgamation of all content fragments found within the element */
    char *content;
} WXMLElement;

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
                                  WXMLNamespace *namespace, const char *content,
                                  int duplicate);

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
                                      const char *href, int duplicate);

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
                                      int duplicate);

/**
 * Parse/decode XML text, returning a corresponding document representation.
 *
 * @param content The XML document/content to be parsed.
 * @param errorMsg External buffer for returning parsing error details.
 * @param errorMsgLen Length of provided buffer.
 * @return The document root instance, or NULL on parsing or memory failure.
 */
WXMLElement *WXML_Decode(const char *content, char *errorMsg, int errorMsgLen);

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
char *WXML_Encode(WXBuffer *buffer, WXMLElement *root, int prettyPrint);

/**
 * Destroy/release the contents of the provided node/document (and all nested
 * content).  This method will also free the value itself (consistent with
 * the allocated return from the parse method).
 *
 * @param root The XML node/tree to be destroyed/freed.
 */
void WXML_Destroy(WXMLElement *root);

#endif
