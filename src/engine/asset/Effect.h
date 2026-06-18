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
// machinery is fully replaced by the port's Geometry::Render walk in
// Mesh::Draw (Phase 5 ported Geometry as a real class). The classes
// are ported for ABI/call-graph parity (so EffectGroup::AddEffect can
// do its real lower_bound + merge dance) but no live render-time call
// site reaches them — see Geometry::EffectGroupSet @ 0x001a00f8
// (binary stub).

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/Immutable.h"
#include "asset/SharedEffectProperties.h"
#include <vector>
#include <string>
#include <cstdint>

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

// VertexElementBase — 8-byte wrapper around an immutable vertex-attribute
// name/binding slot. Binary ctor @ 0x001a1ba0; 5 contiguous sub-objects
// make up an Effect_Bada::Pass (8-byte stride, total 40 bytes).
// Binary Immutable<basic_string, ImmutableTraitsDefault> is 8 bytes;
// port Immutable is 4 bytes (single Node* ptr), so 4 bytes of explicit
// padding brings VertexElementBase to the binary-matching 8 bytes.
struct VertexElementBase {
    Immutable m_Name;       // +0x00 (4 bytes in port; 8-byte variant in binary)
    int       _pad;         // +0x04 padding to match binary 8-byte stride

    VertexElementBase() : _pad(0) {}
    VertexElementBase(const VertexElementBase& o) : m_Name(o.m_Name), _pad(0) {}
};

// Effect_Bada — platform-portable base, holds the Pass + property-def
// vectors.
class Effect_Bada : public ReferenceCounter {
public:
    // Effect_Bada::Pass — 40 bytes, non-polymorphic aggregate of 5 VertexElementBase
    // sub-objects. Binary ctors: default @ 0x001a33e4, copy @ 0x001a3410.
    // 8-byte stride: m_elem0@+0x00, m_elem1@+0x08, m_elem2@+0x10, m_elem3@+0x18, m_elem4@+0x20.
    struct Pass {
        VertexElementBase m_elem0;  // +0x00
        VertexElementBase m_elem1;  // +0x08
        VertexElementBase m_elem2;  // +0x10
        VertexElementBase m_elem3;  // +0x18
        VertexElementBase m_elem4;  // +0x20
    };

    Effect_Bada();
    virtual ~Effect_Bada();

    std::vector<Pass>                           m_Passes;        // +0x0C
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
    bool operator()(SmartPtr<Effect> a, SmartPtr<Effect> b) const {
        return a->m_Name.compare(b->m_Name) < 0;
    }
};

// EffectGroup — 0x38 bytes.
// Binary ctor @ 0x001a2c10 (templated_ctor<Iter>), D2 @ 0x001a1a70.
// Layout:
//   +0x00  ReferenceCounter base (12 bytes)
//   +0x0C  std::vector<EffectPropertyDefinition>  m_MergedDefs
//   +0x18  std::vector<SmartPtr<Effect> >         m_Effects
//   +0x24..+0x37  4-byte align pad + 2x Event1<EffectGroup&>
//                 (not modelled; uint8_t pad keeps binary-shape sizeof
//                 if asm-verify ever cares).
//
// AddEffect (binary @ 0x001a0274): sorted-vector-set insert into m_Effects
// via std::lower_bound + EffectLessThanCompare; on duplicate name, calls
// `this->MergeProperties(effect->Properties())` which folds the new
// effect's property defs into m_MergedDefs.
//
// Live render path is fully bypassed in the port (Renderer uses GLES2
// shaders directly, not Effect/EffectGroup multi-pass dispatch). Both
// AddEffect and MergeProperties are reachable in code shape but never
// fire at runtime — every binary caller chains through stubs.
class EffectGroup : public ReferenceCounter {
public:
    std::vector<EffectPropertyDefinition>  m_MergedDefs;   // +0x0C
    std::vector<SmartPtr<Effect> >              m_Effects;      // +0x18
    // +0x24..+0x37: 4-byte align pad + two Event1<EffectGroup&> slots.
    // Not modelled; gap preserved for binary-shape sizeof.
    uint8_t _events_gap[20];

    EffectGroup() {}

    // Binary @ 0x001a0274 — sorted-set insert by name; merge on dup.
    void AddEffect(SmartPtr<Effect> effect);

    // Binary @ 0x001a2030 — fold each property def in `props` into
    // m_MergedDefs at its lower_bound position. Incoming `props` is the
    // per-Effect on-disk _Bada vector; m_MergedDefs is the runtime type.
    // Returns 1 on success, 0 if any def conflicts.
    int MergeProperties(const std::vector<EffectPropertyDefinition_Bada>& props);
};

}  // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::VertexElementBase) == 8,
              "VertexElementBase must be 8 bytes (binary: 8-byte stride in Pass ctor @0x001a33e4)");
static_assert(sizeof(Mortar::Effect_Bada::Pass) == 40,
              "Effect_Bada::Pass must be 40 bytes (binary: 5x VertexElementBase @0x001a33e4)");
#endif

#endif  // MORTAR_ASSET_EFFECT_H
