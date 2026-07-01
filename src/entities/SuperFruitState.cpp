// SuperFruitState — binary @ 0x001b9acc (Parse), 0x001b9a10 (WriteToElement)

#include "SuperFruitState.h"

// Binary @ 0x001b9acc: QueryFloatAttribute("time"), QueryIntAttribute("hits"),
// QueryFloatAttribute("sliceTime"), QueryFloatAttribute("rot").
void SuperFruitState::Parse(TiXmlElement* elem)
{
    if (!elem) return;
    elem->QueryFloatAttribute("time",      &m_Timer);
    elem->QueryIntAttribute  ("hits",      &m_SliceCount);
    elem->QueryFloatAttribute("sliceTime", &m_Lifetime);
    elem->QueryFloatAttribute("rot",       &m_Spin);
}

// Binary @ 0x001b9a10: operator_new(0x50) + TiXmlElement::ctor("superFruitState"),
// SetDoubleAttribute("time", m_Timer), SetAttribute("hits", m_SliceCount),
// SetDoubleAttribute("sliceTime", m_Lifetime), SetDoubleAttribute("rot", m_Spin),
// return elem.
// DIFFERS: binary (TinyXML-1) heap-allocates a standalone TiXmlElement* and returns it;
// port's tinyxml2 shim requires all elements to be owned by a TiXmlDocument — a
// doc-independent node cannot be created here without a dangling pointer. Returns null.
// Save path is deferred (#291); callers must guard on bool(result).
// v1.6.1 SuperFruitState::WriteToElement @0x001b9a10
TiXmlElement SuperFruitState::WriteToElement()
{
    return TiXmlElement();
}
