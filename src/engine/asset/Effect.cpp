#include "asset/Effect.h"
#include "util/Immutable.h"
#include <algorithm>
#include <cstring>

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

// Binary body:
//   it = std::lower_bound(m_Effects.begin(), m_Effects.end(), effect,
//                         EffectLessThanCompare());
//   if (it != m_Effects.end() && !EffectLessThanCompare()(effect, *it)) {
//       // Equivalent (same m_Name) already in set -> fold this effect's
//       // property defs into the group's m_MergedDefs vector.
//       this->MergeProperties(effect->Properties());
//   } else {
//       m_Effects.insert(it, effect);
//   }
void EffectGroup::AddEffect(SmartPtr<Effect> effect) {
    if (!effect.IsValid()) return;

    EffectLessThanCompare cmp;
    std::vector<SmartPtr<Effect> >::iterator it =
        std::lower_bound(m_Effects.begin(), m_Effects.end(), effect, cmp);

    if (it != m_Effects.end() && !cmp(effect, *it)) {
        // Same name already present -- merge property defs.
        this->MergeProperties(effect->Properties());
    } else {
        m_Effects.insert(it, effect);
    }
}

// EffectGroup::MergeProperties (binary @ 0x001a2030) — fold each
// EffectPropertyDefinition_Bada in `props` into `m_MergedDefs` at its
// lower_bound position keyed on `m_Name`. Returns 1 on success, 0 if
// any incoming def conflicts with an existing same-named entry that
// isn't structurally equal (binary uses
// `EffectPropertyDefinition::operator!=`; port stub treats any same-
// name pair as equal since the rest of the def isn't RE'd yet).
int EffectGroup::MergeProperties(
    const std::vector<EffectPropertyDefinition_Bada>& props)
{
    for (size_t i = 0; i < props.size(); i++) {
        const EffectPropertyDefinition_Bada& incoming = props[i];

        // PropertyDefLessThanCompare: string-compare on m_Name.
        // m_MergedDefs entries are EffectPropertyDefinition (Immutable<string>
        // m_Name); incoming is _Bada (std::string m_Name). Compare via c_str().
        size_t lo = 0, hi = m_MergedDefs.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (std::strcmp(m_MergedDefs[mid].m_Name.c_str(), incoming.m_Name.c_str()) < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if (lo < m_MergedDefs.size() &&
            std::strcmp(m_MergedDefs[lo].m_Name.c_str(), incoming.m_Name.c_str()) == 0) {
            // Same-name entry already present. TODO: re-verify v1.6.1 — RE
            // EffectPropertyDefinition::operator!= to detect structural
            // mismatch and return 0 on conflict. Port treats same-name
            // as equal (no conflict).
        } else {
            // Convert _Bada entry to runtime EffectPropertyDefinition.
            // Only m_Name is read by the binary's comparator; m_Type and
            // m_Count are left zero (no _Bada layout RE for those fields).
            EffectPropertyDefinition def;
            def.m_Name  = Immutable(incoming.m_Name);
            def.m_Type  = 0;
            def.m_Count = 0;
            m_MergedDefs.insert(m_MergedDefs.begin() + lo, def);
        }
    }
    return 1;
}

// ASM-spec v1.6.1 Mortar::operator<(SmartPtr<Effect> const&, SmartPtr<Effect> const&) @0x0025f3d8:
// Binary: string::compare returns int; shift >> 31 extracts sign bit (= < 0).
bool operator<(const SmartPtr<Effect>& a, const SmartPtr<Effect>& b) {
    return a->m_Name.compare(b->m_Name) < 0;
}

// ASM-spec v1.6.1 Mortar::operator<(SmartPtr<Effect> const&, char const*) @0x0025f3f8:
bool operator<(const SmartPtr<Effect>& a, const char* s) {
    return a->m_Name.compare(s) < 0;
}

// ASM-spec v1.6.1 Mortar::operator<(char const*, SmartPtr<Effect> const&) @0x0025f410:
// Binary: return (~compare_result) >> 31, i.e. compare_result > 0.
bool operator<(const char* s, const SmartPtr<Effect>& b) {
    return b->m_Name.compare(s) > 0;
}

}  // namespace Mortar
