#include "render/Utf8StringProxy.h"

namespace Mortar {

// Binary @ 0x001018bc (GOT thunk) -> real ctor @ 0x001840c0
// TODO: 0x001840c0 — full proxy ctor body: installs vtable (GOT-indirect), sets
//   field1_0x4 = str, zeroes remaining fields, then calls some initialization path.
Utf8StringProxy::Utf8StringProxy(const char* str)
    : m_PrevBegin(str)
    , m_CurrentCodepoint(0)
    , m_NumChars(0)
    , m_NextScan(str)
    , m_End(str)
    , m_field9_0x18(0)
{
}

// Binary @ 0x00160cbc (copy path) copies fields 0x10/0x14/0x18 from source
Utf8StringProxy::Utf8StringProxy(const Utf8StringProxy& other)
    : m_PrevBegin(other.m_PrevBegin)
    , m_CurrentCodepoint(other.m_CurrentCodepoint)
    , m_NumChars(other.m_NumChars)
    , m_NextScan(other.m_NextScan)
    , m_End(other.m_End)
    , m_field9_0x18(other.m_field9_0x18)
{
}

Utf8StringProxy::~Utf8StringProxy() {}

} // namespace Mortar
