// TiXml.cpp — sole TU that #includes tinyxml2.
// All other TUs include TiXml.h only (tinyxml2-free).
//
// Excluded from the asm-verify cross-build source list (verify-sources.cmake)
// because it has no binary counterpart in this form; the binary's real TiXml*.o
// are separate symbols not yet matched by the asm-verify pipeline.

#include "TiXml.h"
#include "asset/FileManager.h"
#include "debug/Logger.h"
#include <tinyxml2.h>

// Convenience casts from opaque void* to real tinyxml2 pointers.
static inline tinyxml2::XMLAttribute* AsAttr(void* p) {
    return static_cast<tinyxml2::XMLAttribute*>(p);
}
static inline const tinyxml2::XMLAttribute* AsAttrC(void* p) {
    return static_cast<const tinyxml2::XMLAttribute*>(p);
}
static inline tinyxml2::XMLNode* AsNode(void* p) {
    return static_cast<tinyxml2::XMLNode*>(p);
}
static inline const tinyxml2::XMLNode* AsNodeC(void* p) {
    return static_cast<const tinyxml2::XMLNode*>(p);
}
static inline tinyxml2::XMLElement* AsElem(void* p) {
    return static_cast<tinyxml2::XMLElement*>(p);
}
static inline const tinyxml2::XMLElement* AsElemC(void* p) {
    return static_cast<const tinyxml2::XMLElement*>(p);
}
static inline tinyxml2::XMLDocument* AsDoc(void* p) {
    return static_cast<tinyxml2::XMLDocument*>(p);
}
static inline const tinyxml2::XMLDocument* AsDocC(void* p) {
    return static_cast<const tinyxml2::XMLDocument*>(p);
}

// ---------------------------------------------------------------------------
// TiXmlAttribute
// ---------------------------------------------------------------------------

TiXmlAttribute::TiXmlAttribute() : m_attr(0) {}
TiXmlAttribute::TiXmlAttribute(void* attr) : m_attr(attr) {}

const char* TiXmlAttribute::Name() const {
    return m_attr ? AsAttrC(m_attr)->Name() : 0;
}
const char* TiXmlAttribute::Value() const {
    return m_attr ? AsAttrC(m_attr)->Value() : 0;
}
TiXmlAttribute TiXmlAttribute::Next() const {
    if (!m_attr) return TiXmlAttribute();
    return TiXmlAttribute(const_cast<tinyxml2::XMLAttribute*>(AsAttrC(m_attr)->Next()));
}
TiXmlAttribute::operator bool() const { return m_attr != 0; }

// ---------------------------------------------------------------------------
// TiXmlNode
// ---------------------------------------------------------------------------

TiXmlNode::TiXmlNode() : m_node(0) {}
TiXmlNode::TiXmlNode(void* node) : m_node(node) {}

// ASM-spec v1.6.1 TiXmlNode::FirstChildElement @0x0021ffa0
TiXmlElement TiXmlNode::FirstChildElement(const char* name) const {
    if (!m_node) return TiXmlElement();
    // tinyxml2 FirstChildElement(nullptr) returns all children regardless of name.
    // DIFFERS: TinyXML-1 FirstChildElement(nullptr) also returns first child regardless
    // of name, matching this behavior. (original TiXmlElement.h DIFFERS note preserved.)
    // const_cast: TiXmlElement(void*) requires non-const void*; the pointer is only
    // used for future mutation calls so const-cast is safe.
    return TiXmlElement(const_cast<tinyxml2::XMLElement*>(AsNodeC(m_node)->FirstChildElement(name)));
}
// ASM-spec v1.6.1 TiXmlNode::NextSiblingElement @0x00220044
TiXmlElement TiXmlNode::NextSiblingElement(const char* name) const {
    if (!m_node) return TiXmlElement();
    const tinyxml2::XMLElement* e = AsNodeC(m_node)->ToElement();
    if (!e) {
        // m_node might be an XMLDocument or other node type — walk from there.
        return TiXmlElement(const_cast<tinyxml2::XMLElement*>(AsNodeC(m_node)->NextSiblingElement(name)));
    }
    return TiXmlElement(const_cast<tinyxml2::XMLElement*>(e->NextSiblingElement(name)));
}
const char* TiXmlNode::Value() const {
    return m_node ? AsNodeC(m_node)->Value() : 0;
}
TiXmlDocument TiXmlNode::GetDocument() const {
    // Returns a non-owning TiXmlDocument view of the owning document.
    // The returned object has m_doc=null so its dtor will not delete the document.
    // Caller must not let the returned view outlive the owning TiXmlDocument.
    if (!m_node) return TiXmlDocument(TiXmlDocumentView(), 0);
    tinyxml2::XMLDocument* doc = AsNode(m_node)->GetDocument();
    return TiXmlDocument(TiXmlDocumentView(), doc);
}
TiXmlElement TiXmlNode::InsertEndChild(TiXmlElement child) {
    if (!m_node || !child.m_node) return TiXmlElement();
    tinyxml2::XMLNode* inserted = AsNode(m_node)->InsertEndChild(AsNode(child.m_node));
    return TiXmlElement(inserted ? inserted->ToElement() : 0);
}
TiXmlElement TiXmlNode::InsertFirstChild(TiXmlElement child) {
    if (!m_node || !child.m_node) return TiXmlElement();
    tinyxml2::XMLNode* inserted = AsNode(m_node)->InsertFirstChild(AsNode(child.m_node));
    return TiXmlElement(inserted ? inserted->ToElement() : 0);
}
TiXmlElement TiXmlNode::LinkEndChild(TiXmlElement child) {
    return InsertEndChild(child);
}
TiXmlNode::operator bool() const { return m_node != 0; }

// ---------------------------------------------------------------------------
// TiXmlElement
// ---------------------------------------------------------------------------

TiXmlElement::TiXmlElement() : TiXmlNode() {}
TiXmlElement::TiXmlElement(void* elem) : TiXmlNode(elem) {}

const char* TiXmlElement::Name() const {
    return m_node ? AsElemC(m_node)->Name() : 0;
}
// ASM-spec v1.6.1 TiXmlElement::Attribute @0x00221ca0
const char* TiXmlElement::Attribute(const char* name) const {
    return m_node ? AsElemC(m_node)->Attribute(name) : 0;
}
const char* TiXmlElement::Attribute(const char* name, const char* def) const {
    const char* v = m_node ? AsElemC(m_node)->Attribute(name) : 0;
    return v ? v : def;
}
const char* TiXmlElement::GetText() const {
    return m_node ? AsElemC(m_node)->GetText() : 0;
}
// ASM-spec v1.6.1 TiXmlElement::QueryIntAttribute @0x00221c74
int TiXmlElement::QueryIntAttribute(const char* name, int* outVal) const {
    if (!m_node) return TIXML_NO_ATTRIBUTE;
    tinyxml2::XMLError err = AsElemC(m_node)->QueryIntAttribute(name, outVal);
    return (err == tinyxml2::XML_SUCCESS) ? TIXML_SUCCESS : TIXML_NO_ATTRIBUTE;
}
int TiXmlElement::QueryFloatAttribute(const char* name, float* outVal) const {
    if (!m_node) return TIXML_NO_ATTRIBUTE;
    tinyxml2::XMLError err = AsElemC(m_node)->QueryFloatAttribute(name, outVal);
    return (err == tinyxml2::XML_SUCCESS) ? TIXML_SUCCESS : TIXML_NO_ATTRIBUTE;
}
int TiXmlElement::QueryDoubleAttribute(const char* name, double* outVal) const {
    if (!m_node) return TIXML_NO_ATTRIBUTE;
    tinyxml2::XMLError err = AsElemC(m_node)->QueryDoubleAttribute(name, outVal);
    return (err == tinyxml2::XML_SUCCESS) ? TIXML_SUCCESS : TIXML_NO_ATTRIBUTE;
}
int TiXmlElement::QueryUnsignedAttribute(const char* name, unsigned* outVal) const {
    if (!m_node) return TIXML_NO_ATTRIBUTE;
    tinyxml2::XMLError err = AsElemC(m_node)->QueryUnsignedAttribute(name, outVal);
    return (err == tinyxml2::XML_SUCCESS) ? TIXML_SUCCESS : TIXML_NO_ATTRIBUTE;
}
int TiXmlElement::IntAttribute(const char* name, int def) const {
    return m_node ? AsElemC(m_node)->IntAttribute(name, def) : def;
}
float TiXmlElement::FloatAttribute(const char* name, float def) const {
    return m_node ? AsElemC(m_node)->FloatAttribute(name, def) : def;
}
TiXmlAttribute TiXmlElement::FirstAttribute() const {
    if (!m_node) return TiXmlAttribute();
    return TiXmlAttribute(const_cast<tinyxml2::XMLAttribute*>(AsElemC(m_node)->FirstAttribute()));
}
bool TiXmlElement::NoChildren() const {
    return m_node ? AsElemC(m_node)->NoChildren() : true;
}
void TiXmlElement::SetAttribute(const char* name, const char* value) {
    if (m_node) AsElem(m_node)->SetAttribute(name, value);
}
void TiXmlElement::SetAttribute(const char* name, int value) {
    if (m_node) AsElem(m_node)->SetAttribute(name, value);
}
void TiXmlElement::SetAttribute(const char* name, unsigned value) {
    if (m_node) AsElem(m_node)->SetAttribute(name, value);
}
void TiXmlElement::SetAttribute(const char* name, float value) {
    if (m_node) AsElem(m_node)->SetAttribute(name, value);
}
void TiXmlElement::SetAttribute(const char* name, double value) {
    if (m_node) AsElem(m_node)->SetAttribute(name, value);
}
void TiXmlElement::SetDoubleAttribute(const char* name, double value) {
    if (m_node) AsElem(m_node)->SetAttribute(name, value);
}

// ---------------------------------------------------------------------------
// TiXmlDocument
// ---------------------------------------------------------------------------

// ASM-spec v1.6.1 TiXmlDocument ctor @0x00110fc4
TiXmlDocument::TiXmlDocument() : TiXmlNode(), m_doc(0) {
    tinyxml2::XMLDocument* doc = new tinyxml2::XMLDocument();
    m_doc  = doc;
    m_node = doc;
}
TiXmlDocument::~TiXmlDocument() {
    if (m_doc) {
        delete AsDoc(m_doc);
        m_doc  = 0;
        m_node = 0;
    }
    // Non-owning view (m_doc == null, m_node != null): don't delete.
}
// Non-owning view constructor: wraps an existing XMLDocument* without taking ownership.
TiXmlDocument::TiXmlDocument(TiXmlDocumentView, void* docPtr)
    : TiXmlNode(docPtr), m_doc(0) {
    // m_doc = 0: dtor will not delete.
}
// Copy construction: produce a non-owning view of the same underlying document.
TiXmlDocument::TiXmlDocument(const TiXmlDocument& other)
    : TiXmlNode(other.m_node), m_doc(0) {
    // m_doc = 0: copy is always a view — will NOT delete the document on destruction.
}
TiXmlDocument& TiXmlDocument::operator=(const TiXmlDocument& other) {
    if (this != &other) {
        // Release any owned document before overwriting.
        if (m_doc) { delete AsDoc(m_doc); m_doc = 0; }
        m_node = other.m_node;
        // m_doc stays null: assignment always produces a non-owning view.
    }
    return *this;
}

// ASM-spec v1.6.1 TiXmlDocument::LoadFile @0x002206a4: wraps Mortar::File ->
// FileManager::GetFileData (data-root prepended in file layer) -> tinyxml2 Parse.
// Callers pass bare relative paths.
// Port specific: absolute paths (save files) bypass FileManager and go to tinyxml2 directly.
// CI resolution lives in FileSystem_Direct::GetFileData (applies after TranslateFileName).
bool TiXmlDocument::LoadFile(const char* path) {
    if (!m_node || !path) return false;
    tinyxml2::XMLDocument* doc = AsDoc(m_node);

    // Detect absolute paths: save-file callers use OS-absolute paths not routed via FileManager.
    // Port specific: also recognise devkitPPC/libogc device-prefixed paths
    // (sd:/..., usb:/...) as absolute -- general "letters+':'+'/'" check
    // rather than a Wii-only #ifdef, since no relative asset path used by
    // FileManager ever contains ':' (host paths use '/'; web IDBFS paths use
    // a leading '/', already covered above).
    bool isAbsolute = (path[0] == '/'
#ifdef _WIN32
        || (path[0] != '\0' && path[1] == ':')
#endif
    );
    if (!isAbsolute) {
        const char* p = path;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) ++p;
        if (p != path && p[0] == ':' && p[1] == '/') isAbsolute = true;
    }

    if (isAbsolute) {
        tinyxml2::XMLError err = doc->LoadFile(path);
        if (err != tinyxml2::XML_SUCCESS)
            LOG_ERROR("TiXml", "failed to load '%s' (err=%d)", path, (int)err);
        return err == tinyxml2::XML_SUCCESS;
    }

    void* buf = 0;
    unsigned long size = 0;
    bool owned = false;
    if (!FileManager::GetInstance().GetFileData(path, &buf, &size, 0, owned)) {
        LOG_ERROR("TiXml", "failed to load '%s'", path);
        return false;
    }
    tinyxml2::XMLError err = doc->Parse(static_cast<const char*>(buf), (size_t)size);
    if (owned)
        delete[] static_cast<unsigned char*>(buf);
    if (err != tinyxml2::XML_SUCCESS)
        LOG_ERROR("TiXml", "failed to parse '%s' (err=%d)", path, (int)err);
    return err == tinyxml2::XML_SUCCESS;
}
bool TiXmlDocument::SaveFile(const char* path) const {
    if (!m_node) return false;
    // tinyxml2::XMLDocument::SaveFile is non-const; cast to non-const for the call.
    tinyxml2::XMLError err = AsDoc(const_cast<void*>(m_node))->SaveFile(path);
    return err == tinyxml2::XML_SUCCESS;
}
TiXmlElement TiXmlDocument::NewElement(const char* name) {
    if (!m_node) return TiXmlElement();
    return TiXmlElement(AsDoc(m_node)->NewElement(name));
}
TiXmlElement TiXmlDocument::RootElement() const {
    if (!m_node) return TiXmlElement();
    // RootElement() returns const XMLElement*; cast to void* (non-const void* needed by ctor).
    return TiXmlElement(const_cast<tinyxml2::XMLElement*>(AsDocC(m_node)->RootElement()));
}
