#ifndef MORTAR_ASSET_EFFECT_H
#define MORTAR_ASSET_EFFECT_H

// Mortar::Effect — fixed-pipeline effect descriptor.
//
// Class hierarchy: ReferenceCounter <- Effect_Bada <- Effect.
// Total Effect size = 0x34 bytes per binary RE.
//
// Effect_Bada layout (size 0x30):
//   +0x00  vptr
//   +0x04  ReferenceCounter base data (8 bytes; total RC size = 12)
//   +0x0C  std::vector<Pass>                          m_Passes
//   +0x18  std::vector<EffectPropertyDefinition_Bada> m_PropertyDefs
//
// Effect (extends Effect_Bada with):
//   +0x24  std::vector<DebugInfo>  m_DebugInfo
//   +0x30  std::string             m_Name        (used by EffectLessThanCompare)
//
// Defunct in port: the Effect/Pass/EffectGroup multi-pass render
// machinery is fully replaced by the port's flat GeometryEntry walk in
// Mesh::Draw. The classes are ported for ABI/call-graph parity (so
// EffectGroup::AddEffect can do its real lower_bound + merge dance) but
// no live render-time call site reaches them — see
// Geometry::EffectGroupSet @ 0x001a00f8 (binary stub).

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include <vector>
#include <string>

namespace Mortar {

// EffectPropertyDefinition_Bada — element type of Effect's m_PropertyDefs
// vector and EffectGroup's m_MergedDefs vector. Per binary RE on
// EffectGroup::MergeProperties, only the +0x0C `m_Name` string is read
// by PropertyDefLessThanCompare; rest of the layout is unused by the
// port's reachable code paths. Forward-decl-with-name suffices for the
// vector types to instantiate (the merge body can compare via accessor).
struct EffectPropertyDefinition_Bada {
    // Minimal layout to keep vector<EffectPropertyDefinition_Bada>
    // instantiable. The real binary struct is larger; only m_Name is
    // load-bearing for the merge comparator. TODO: 0x???? — RE the
    // full EffectPropertyDefinition_Bada layout if any port code other
    // than EffectGroup::MergeProperties ever reads it.
    std::string m_Name;  // +0x0C in real binary (with leading vector data)
};

// Effect_Bada — platform-portable base, holds the Pass + property-def
// vectors. Pass struct itself is opaque to the port (live render path
// doesn't use it).
class Effect_Bada : public ReferenceCounter {
public:
    // Effect_Bada::Pass — opaque to the port. TODO: 0x001a3194 — RE
    // Effect_Bada::Pass layout if any port code ever needs to populate
    // m_Passes. Currently nothing in the port writes it; the vector
    // stays empty.
    struct Pass;

    Effect_Bada();
    virtual ~Effect_Bada();

    std::vector<Pass*>                          m_Passes;        // +0x0C
    std::vector<EffectPropertyDefinition_Bada>  m_PropertyDefs;  // +0x18
};

// Effect adds DebugInfo + name. Properties() / m_Name make this class
// the load-bearing keystone for EffectGroup::AddEffect's lower_bound
// chain (binary @ 0x001a0274).
class Effect : public Effect_Bada {
public:
    // Effect::DebugInfo (size 0x1C). Only m_Name @ +0x00 has a known
    // role; +0x04 / +0x10 are vectors of unknown element type that
    // hold shader / attribute debug data. TODO: 0x001a30dc — RE
    // DebugInfo's two inner vectors' element types.
    struct DebugInfo {
        std::string         m_Name;     // +0x00
        std::vector<int>    m_Field04;  // +0x04 — TODO: real element type
        std::vector<int>    m_Field10;  // +0x10 — TODO: real element type
    };

    Effect();
    ~Effect();

    // Binary @ 0x001a3214: `adds r0,#0x18; bx lr`. Two-instruction leaf,
    // NOT virtual. Returns &m_PropertyDefs (the +0x18 field on the
    // Effect_Bada base).
    const std::vector<EffectPropertyDefinition_Bada>& Properties() const {
        return m_PropertyDefs;
    }

    std::vector<DebugInfo> m_DebugInfo;  // +0x24
    std::string            m_Name;       // +0x30
};

// Free comparator. Binary @ 0x001a0480 — strcmp on Effect::m_Name.
// Used by EffectGroup::AddEffect's std::lower_bound call to keep
// m_Effects sorted by name.
struct EffectLessThanCompare {
    bool operator()(const SmartPtr<Effect>& a, const SmartPtr<Effect>& b) const {
        return a->m_Name.compare(b->m_Name) < 0;
    }
};

}  // namespace Mortar

#endif  // MORTAR_ASSET_EFFECT_H
