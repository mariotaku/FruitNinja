#ifndef FN_SUPER_FRUIT_STATE_H
#define FN_SUPER_FRUIT_STATE_H

// SuperFruitState — POD used to serialize/restore a mid-flight super-fruit
// controller across save/load.
//
// Binary struct layout (v1.6.1 SuperFruitState @0x001b9a10/0x001b9acc):
//   +0x00  float  m_Timer       XML attr: "time"
//   +0x04  int    m_SliceCount  XML attr: "hits"
//   +0x08  float  m_Lifetime    XML attr: "sliceTime"
//   +0x0c  float  m_Spin        XML attr: "rot"
//
// Usage:
//   SuperFruitState s;
//   s.Parse(elem);                       // read from <superFruitState> element
//   TiXmlElement* e = s.WriteToElement(); // DIFFERS: returns nullptr in port (see WriteToElement)
//   Real save path: SuperFruitControl::SaveSuperFruitState(Fruit*, TiXmlElement*).

#include <cstdint>

#include "engine/xml/TiXml.h"

struct SuperFruitState {
    float m_Timer;       // +0x00: elapsed controller time,  XML "time"
    int   m_SliceCount;  // +0x04: combo slice count,        XML "hits"
    float m_Lifetime;    // +0x08: explosion threshold,      XML "sliceTime"
    float m_Spin;        // +0x0c: current spin value,       XML "rot"

    SuperFruitState()
        : m_Timer(0.0f), m_SliceCount(0), m_Lifetime(0.0f), m_Spin(0.0f)
    {}

    // Binary @ 0x001b9acc. Reads "time"/"hits"/"sliceTime"/"rot" XML attrs.
    void Parse(TiXmlElement* elem);

    // ASM-verified: v1.6.1 SuperFruitState::WriteToElement @0x001b9a10. Binary heap-allocates
    // a new <superFruitState> element (operator_new(0x50) + TiXmlElement ctor), sets attrs
    // ("time"/"hits"/"sliceTime"/"rot"), and returns the TiXmlElement* -- no parameters,
    // this is not a member element.
    // DIFFERS: original = heap TiXmlElement* (v1.6.1 SuperFruitState::WriteToElement
    // @0x001b9a10); tinyxml2 shim cannot heap-allocate doc-less nodes, returns nullptr;
    // real save path lives in SuperFruitControl::SaveSuperFruitState. Callers must guard
    // on nullptr before use.
    TiXmlElement* WriteToElement();
};

#endif // FN_SUPER_FRUIT_STATE_H
