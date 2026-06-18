#ifndef FN_TIXML_ELEMENT_H
#define FN_TIXML_ELEMENT_H

#include <tinyxml2.h>

// TiXmlElement — compatibility wrapper matching the binary's TiXml API.
// The binary uses TinyXML-1 (TiXmlElement, TiXmlNode). The port uses
// tinyxml2 (XMLElement, XMLNode). This wrapper provides the TiXml API
// surface over tinyxml2, so function signatures match the binary's
// mangled names.
//
// Binary: Parse(TiXmlElement*)   — mangled with P12TiXmlElement
// Port (old): Parse(tinyxml2::XMLElement*) — different mangling
// Port (new): Parse(TiXmlElement*) — matches binary

class TiXmlElement {
public:
    tinyxml2::XMLElement* m_element;

    TiXmlElement() : m_element(nullptr) {}
    explicit TiXmlElement(tinyxml2::XMLElement* e) : m_element(e) {}

    // --- Read API ---

    const char* Attribute(const char* name) const {
        return m_element ? m_element->Attribute(name) : nullptr;
    }
    const char* Attribute(const char* name, const char* def) const {
        const char* v = m_element ? m_element->Attribute(name) : nullptr;
        return v ? v : def;
    }
    const char* GetText() const {
        return m_element ? m_element->GetText() : nullptr;
    }

    int QueryIntAttribute(const char* name, int* outVal) const {
        return m_element ? m_element->QueryIntAttribute(name, outVal)
                         : tinyxml2::XML_NO_ATTRIBUTE;
    }
    int QueryFloatAttribute(const char* name, float* outVal) const {
        return m_element ? m_element->QueryFloatAttribute(name, outVal)
                         : tinyxml2::XML_NO_ATTRIBUTE;
    }
    int QueryDoubleAttribute(const char* name, double* outVal) const {
        return m_element ? m_element->QueryDoubleAttribute(name, outVal)
                         : tinyxml2::XML_NO_ATTRIBUTE;
    }

    int IntAttribute(const char* name, int def = 0) const {
        return m_element ? m_element->IntAttribute(name, def) : def;
    }
    float FloatAttribute(const char* name, float def = 0.0f) const {
        return m_element ? m_element->FloatAttribute(name, def) : def;
    }

    // --- Child/sibling navigation ---

    TiXmlElement FirstChildElement(const char* name = "") const {
        return TiXmlElement(m_element ? m_element->FirstChildElement(name) : nullptr);
    }
    TiXmlElement NextSiblingElement(const char* name = "") const {
        return TiXmlElement(m_element ? m_element->NextSiblingElement(name) : nullptr);
    }

    bool NoChildren() const {
        return m_element ? m_element->NoChildren() : true;
    }

    tinyxml2::XMLDocument* GetDocument() const {
        return m_element ? m_element->GetDocument() : nullptr;
    }

    // --- Write API ---

    void SetAttribute(const char* name, const char* value) {
        if (m_element) m_element->SetAttribute(name, value);
    }
    void SetAttribute(const char* name, int value) {
        if (m_element) m_element->SetAttribute(name, value);
    }
    void SetAttribute(const char* name, unsigned value) {
        if (m_element) m_element->SetAttribute(name, value);
    }
    void SetAttribute(const char* name, float value) {
        if (m_element) m_element->SetAttribute(name, value);
    }
    void SetAttribute(const char* name, double value) {
        if (m_element) m_element->SetAttribute(name, value);
    }

    tinyxml2::XMLNode* InsertEndChild(tinyxml2::XMLNode* node) {
        return m_element ? m_element->InsertEndChild(node) : nullptr;
    }
    tinyxml2::XMLNode* InsertFirstChild(tinyxml2::XMLNode* node) {
        return m_element ? m_element->InsertFirstChild(node) : nullptr;
    }

    // --- Conversion ---

    operator bool() const { return m_element != nullptr; }
};

// TiXmlNode stub — used by ParseSaveFile. Binary has a separate TiXmlNode
// class; for now a thin wrapper over tinyxml2::XMLNode suffices.
class TiXmlNode {
public:
    tinyxml2::XMLNode* m_node;

    TiXmlNode() : m_node(nullptr) {}
    explicit TiXmlNode(tinyxml2::XMLNode* n) : m_node(n) {}

    operator bool() const { return m_node != nullptr; }
};

#endif // FN_TIXML_ELEMENT_H
