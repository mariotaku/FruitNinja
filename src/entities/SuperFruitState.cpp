// SuperFruitState — binary @ 0x001b9acc (Parse), 0x001b9a10 (WriteToElement)

#include "SuperFruitState.h"
#include <tinyxml2.h>

// Binary @ 0x001b9acc: QueryFloatAttribute("timer"), ("lifetime"), ("spin");
// QueryIntAttribute("slices").
void SuperFruitState::Parse(tinyxml2::XMLElement* elem)
{
    if (!elem) return;
    elem->QueryFloatAttribute("timer",    &m_Timer);
    elem->QueryFloatAttribute("lifetime", &m_Lifetime);
    elem->QueryIntAttribute("slices",     &m_SliceCount);
    elem->QueryFloatAttribute("spin",     &m_Spin);
}

// Binary @ 0x001b9a10: SetAttribute("timer"), ("lifetime"), ("slices"), ("spin").
void SuperFruitState::WriteToElement(tinyxml2::XMLElement* elem) const
{
    if (!elem) return;
    elem->SetAttribute("timer",    m_Timer);
    elem->SetAttribute("lifetime", m_Lifetime);
    elem->SetAttribute("slices",   m_SliceCount);
    elem->SetAttribute("spin",     m_Spin);
}
