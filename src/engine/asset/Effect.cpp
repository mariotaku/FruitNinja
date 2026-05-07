#include "asset/Effect.h"

namespace Mortar {

// Effect_Bada::Effect_Bada — binary @ 0x001a15a4.
// Calls ReferenceCounter ctor, writes vptr (`vtable+8`), default-constructs
// m_Passes and m_PropertyDefs.
Effect_Bada::Effect_Bada() {
}

// Effect_Bada::~Effect_Bada — destroys m_PropertyDefs then m_Passes.
Effect_Bada::~Effect_Bada() {
}

// Effect::Effect — separately default-ctors m_DebugInfo (+0x24) and
// m_Name (+0x30). Binary equivalent is inlined into Effect_Bada's ctor
// in some build configurations; the explicit body here keeps the port
// readable.
Effect::Effect() {
}

// Effect::~Effect — binary @ 0x001a182c (D1) / 0x001a1884 (D2).
// Writes vtable+8, destroys m_Name, m_DebugInfo, then chains to
// ~Effect_Bada -> ~ReferenceCounter.
Effect::~Effect() {
}

}  // namespace Mortar
