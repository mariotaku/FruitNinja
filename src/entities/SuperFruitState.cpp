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

// ASM-verified: v1.6.1 SuperFruitState::WriteToElement @0x001b9a10: operator_new(0x50) +
// TiXmlElement::ctor("superFruitState"), SetDoubleAttribute("time", m_Timer),
// SetAttribute("hits", m_SliceCount), SetDoubleAttribute("sliceTime", m_Lifetime),
// SetDoubleAttribute("rot", m_Spin), return elem (TiXmlElement*, heap-allocated, no param).
// DIFFERS: original = heap TiXmlElement* (v1.6.1 SuperFruitState::WriteToElement
// @0x001b9a10); tinyxml2 shim cannot heap-allocate doc-less nodes, returns nullptr;
// real save path lives in SuperFruitControl::SaveSuperFruitState.
TiXmlElement* SuperFruitState::WriteToElement()
{
    return nullptr;
}
