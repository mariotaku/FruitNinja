#ifndef FN_SUPER_FRUIT_STATE_H
#define FN_SUPER_FRUIT_STATE_H

// SuperFruitState — POD used to serialize/restore a mid-flight super-fruit
// controller across save/load. Binary @ 0x001b9acc (Parse), 0x001b9a10 (WriteToElement).
//
// Fields (binary offsets within the serialized XML buffer):
//   float timer      (+0x00)
//   float lifetime   (+0x08 in serialized form)
//   int   sliceCount
//   float spin

#include <cstdint>

#include "engine/xml/TiXmlElement.h"

struct SuperFruitState {
    float m_Timer;       // +0x00: elapsed controller time
    float m_Lifetime;    // +0x04: explosion threshold baseline
    int   m_SliceCount;  // +0x08: combo slice count at save time
    float m_Spin;        // +0x0c: current spin value

    SuperFruitState()
        : m_Timer(0.0f), m_Lifetime(0.0f), m_SliceCount(0), m_Spin(0.0f)
    {}

    // Binary @ 0x001b9acc. Reads "timer"/"lifetime"/"slices"/"spin" XML attrs.
    void Parse(TiXmlElement* elem);

    // Binary @ 0x001b9a10. Writes the fields as XML attributes on elem.
    void WriteToElement(TiXmlElement* elem) const;
};

#endif // FN_SUPER_FRUIT_STATE_H
