#include "render/Utf8StringProxy.h"

namespace Mortar {

// Binary @ 0x001018bc -- single-arg ctor is a PLT/GOT trampoline
// (adr r12,0x1018c4; add #0xef000; ldr pc,[r12,#0x420]!) jumping through the
// dynamic relocation slot PTR_Utf8StringProxy_001f0ce4 to the real ctor, which
// lives outside this module's statically-resolvable code. The real proxy
// single-arg ctor only installs the (GOT-indirect) vtable; it does NOT consume
// `str` -- the caller (Utf8StringIterator::Utf8StringIterator @ 0x0012fe00) sets
// field1_0x4 = str on a stack proxy itself and routes field init through
// Utf8StringIterator::_Init. Vtable install has no SDL/GLES2 counterpart: the
// port relies on the C++ compiler's native virtual dispatch, so this ctor only
// needs to seed the data fields the port-side standalone proxy starts from.
// Port specific: GOT-indirect vtable install replaced by native C++ vptr.
Utf8StringProxy::Utf8StringProxy(const char* str)
    : m_Begin(str)
    , m_NumChars(0)
    , m_End(str)
    , m_PrevBegin(str)
    , m_NextScan(str)
    , m_CurrentCodepoint(0)
{
}

// Binary @ 0x001840c0 (Mortar::Utf8StringProxy copy-ctor). Copies ONLY the three
// scalar fields field1_0x4 / field_0x8 / field6_0xc and installs the vtable
// (this->vtable = GOT-resolved table + 8). It deliberately does NOT touch
// +0x10 / +0x14 / +0x18 -- those are copied by the derived
// Utf8StringIterator copy-ctor (@ 0x00160cbc) after it chains to this base ctor.
// Faithful: leave m_PrevBegin / m_NextScan / m_CurrentCodepoint default-uninitialised here.
// Port specific: vtable install handled by native C++ vptr.
Utf8StringProxy::Utf8StringProxy(const Utf8StringProxy& other)
    : m_Begin(other.m_Begin)
    , m_NumChars(other.m_NumChars)
    , m_End(other.m_End)
    , m_PrevBegin(0)
    , m_NextScan(0)
    , m_CurrentCodepoint(0)
{
}

Utf8StringProxy::~Utf8StringProxy() {}

} // namespace Mortar
