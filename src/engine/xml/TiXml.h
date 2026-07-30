#ifndef FN_ENGINE_XML_TIXML_H
#define FN_ENGINE_XML_TIXML_H

// TiXml shim — binary-faithful TinyXML-1 API over tinyxml2.
//
// The binary (v1.6.1) uses TinyXML-1 (TiXmlNode, TiXmlElement, TiXmlDocument,
// TiXmlAttribute). The port implements tinyxml2 internally but exposes the
// TiXml-1 call surface so function signatures mangle identically to the binary.
//
// DIFFERS: binary TiXmlElement/TiXmlNode are 44/80-byte heap-linked node structs;
// port wraps a void* handle (real: tinyxml2::XMLElement*) passed by value.
// Call graph sequence converges; struct layout intentionally does not — the binary
// never exposes internal TiXml node layout via a public API we'd need to match.
// v1.6.1 TiXmlDocument ctor @0x00110fc4, TiXmlDocument::LoadFile @0x001157a4,
// TiXmlNode::FirstChildElement @0x0021ffa0, TiXmlNode::NextSiblingElement @0x00220044,
// TiXmlElement::Attribute @0x00221ca0, TiXmlElement::QueryIntAttribute @0x00221c74,
// TiXmlAttribute::Parse @0x00102d20.
//
// This header is tinyxml2-FREE — it must compile in the asm-verify cross-build
// without requiring tinyxml2 headers. All tinyxml2 code lives in TiXml.cpp only.

// TiXml status constants (TinyXML-1 API).
// TIXML_SUCCESS == 0, matching tinyxml2::XML_SUCCESS.
static const int TIXML_SUCCESS      = 0;
static const int TIXML_NO_ATTRIBUTE = 1;
static const int TIXML_WRONG_TYPE   = 2;

// Forward declarations (methods return these by value).
class TiXmlNode;
class TiXmlElement;
class TiXmlDocument;

// TiXmlAttribute — wraps tinyxml2::XMLAttribute* (read-only attribute cursor).
// v1.6.1 TiXmlAttribute::Parse @0x0022311c
// DIFFERS: binary TiXmlAttribute is a 32-byte node; port wraps void* handle.
class TiXmlAttribute {
public:
    void* m_attr;  // real type: tinyxml2::XMLAttribute*

    TiXmlAttribute();
    explicit TiXmlAttribute(void* attr);

    const char*    Name()  const;
    const char*    Value() const;
    // Next(): returns the next attribute, or an invalid (null) TiXmlAttribute when done.
    // Returns by value (not pointer) for safe use in range loops.
    TiXmlAttribute Next() const;

    operator bool() const;
};

// TiXmlNode — base class for TiXmlElement and TiXmlDocument.
// v1.6.1 TiXmlNode::FirstChildElement @0x0021ffa0
// v1.6.1 TiXmlNode::NextSiblingElement @0x00220044
// DIFFERS: binary TiXmlNode is an 80-byte heap-linked node struct; port wraps void*.
class TiXmlNode {
public:
    void* m_node;  // real type: tinyxml2::XMLNode*

    TiXmlNode();
    explicit TiXmlNode(void* node);

    // Navigation (name=0 means "any name" — matches tinyxml2 nullptr behavior).
    // ASM-spec v1.6.1 TiXmlNode::FirstChildElement @0x0021ffa0
    TiXmlElement FirstChildElement(const char* name = 0) const;
    // ASM-spec v1.6.1 TiXmlNode::NextSiblingElement @0x00220044
    TiXmlElement NextSiblingElement(const char* name = 0) const;

    const char* Value() const;

    TiXmlDocument GetDocument() const;

    // Write path: insert a child node (accepts TiXmlElement by void* token).
    // Returns the inserted element.
    TiXmlElement InsertEndChild(TiXmlElement child);
    TiXmlElement InsertFirstChild(TiXmlElement child);
    // LinkEndChild: TinyXML-1 alias for InsertEndChild (same semantics in tinyxml2).
    TiXmlElement LinkEndChild(TiXmlElement child);

    operator bool() const;
};

// TiXmlElement — wraps tinyxml2::XMLElement*.
// v1.6.1 TiXmlElement::Attribute @0x00221ca0
// v1.6.1 TiXmlElement::QueryIntAttribute @0x00221c74
// DIFFERS: binary TiXmlElement is a 44-byte node; port wraps void* handle.
class TiXmlElement : public TiXmlNode {
public:
    TiXmlElement();
    explicit TiXmlElement(void* elem);

    // Name of this element (same as Value() for elements).
    const char* Name() const;

    // --- Read API ---
    // ASM-spec v1.6.1 TiXmlElement::Attribute @0x00221ca0
    const char* Attribute(const char* name)                        const;
    const char* Attribute(const char* name, const char* def)       const;
    const char* GetText()                                          const;
    // ASM-spec v1.6.1 TiXmlElement::QueryIntAttribute @0x00221c74
    int QueryIntAttribute(const char* name, int* outVal)           const;
    int QueryFloatAttribute(const char* name, float* outVal)       const;
    int QueryDoubleAttribute(const char* name, double* outVal)     const;
    int QueryUnsignedAttribute(const char* name, unsigned* outVal) const;

    // Convenience attribute readers (return default on missing).
    int   IntAttribute(const char* name, int def = 0)        const;
    float FloatAttribute(const char* name, float def = 0.0f) const;

    // Attribute iteration (for Bonus.cpp's "walk all attrs" pattern).
    TiXmlAttribute FirstAttribute() const;

    bool NoChildren() const;

    // --- Write API ---
    void SetAttribute(const char* name, const char* value);
    void SetAttribute(const char* name, int value);
    void SetAttribute(const char* name, unsigned value);
    void SetAttribute(const char* name, float value);
    void SetAttribute(const char* name, double value);
    void SetDoubleAttribute(const char* name, double value);
};

// Tag type for constructing a non-owning TiXmlDocument view.
struct TiXmlDocumentView {};

// TiXmlDocument — owns the underlying tinyxml2::XMLDocument.
// v1.6.1 TiXmlDocument ctor @0x00110fc4
// v1.6.1 TiXmlDocument::LoadFile @0x00220730
// DIFFERS: binary TiXmlDocument is an 80-byte linked node; port owns a
// heap-allocated tinyxml2::XMLDocument stored as void* (new in ctor, delete in dtor).
//
// Ownership: m_doc != null means this instance owns the underlying XMLDocument.
// The TiXmlDocumentView constructor creates a non-owning view so that
// TiXmlNode::GetDocument() can return one without allocating a second document.
// Callers must not let a non-owning view outlive the owning TiXmlDocument.
class TiXmlDocument : public TiXmlNode {
public:
    // ASM-spec v1.6.1 TiXmlDocument ctor @0x00110fc4
    TiXmlDocument();
    ~TiXmlDocument();

    // Non-owning view constructor: m_doc = null, dtor will not delete.
    TiXmlDocument(TiXmlDocumentView, void* docPtr);

    // Copy: produces a non-owning view of the same underlying document.
    TiXmlDocument(const TiXmlDocument& other);
    TiXmlDocument& operator=(const TiXmlDocument& other);

    // ASM-spec v1.6.1 TiXmlDocument::LoadFile @0x002206a4
    // Bare relative paths route through FileManager (data-root prepended centrally;
    // CI resolution in FileSystem_Direct layer). Absolute paths go to tinyxml2 directly
    // (port-specific save-file callers only).
    // Returns true on success (TinyXML-1 API; binary returns bool).
    bool LoadFile(const char* path);
    bool SaveFile(const char* path) const;

    // Create a new unattached element owned by this document.
    TiXmlElement NewElement(const char* name);

    // Returns the document root element (first child element).
    TiXmlElement RootElement() const;

private:
    void* m_doc;  // real type: tinyxml2::XMLDocument* (owned when non-null)
};

#endif // FN_ENGINE_XML_TIXML_H
