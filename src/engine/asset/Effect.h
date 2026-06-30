#ifndef MORTAR_ASSET_EFFECT_H
#define MORTAR_ASSET_EFFECT_H

// Mortar::Effect -- fixed-pipeline effect descriptor.
//
// Class hierarchy (binary): ReferenceCounter <- Effect_Bada <- Effect_GLES1 <- Effect.
// DIFFERS: port omits Effect_GLES1 intermediate; layout is identical (no new data fields).
// Total Effect size = 0x38 bytes per binary RE (v1.6.1 @0x0025f590 operator new(0x38)).
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
//   +0x34  uint32_t                _tail_pad     (compiler tail padding; binary: uninitialized, never destructed)
//   Total: 0x38
//
// Defunct in port: the Effect/Pass/EffectGroup multi-pass render
// machinery is fully replaced by the port's Geometry::Render walk in
// Mesh::Draw (Phase 5 ported Geometry as a real class). The classes
// are ported for ABI/call-graph parity (so EffectGroup::AddEffect can
// do its real lower_bound + merge dance) but no live render-time call
// site reaches them -- see Geometry::EffectGroupSet (v1.6.1 Geometry::EffectGroupSet @0x0025eee0)
// (binary stub).

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/Immutable.h"
#include "asset/SharedEffectProperties.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Mortar {

class DataStreamReader;  // forward declaration for LoadPlatformData + Read overloads

// EffectPropertyDefinition_Bada -- element type of Effect's m_PropertyDefs
// vector and EffectGroup's m_MergedDefs vector. Per binary RE on
// EffectGroup::MergeProperties, only the +0x0C `m_Name` string is read
// by PropertyDefLessThanCompare; rest of the layout is unused by the
// port's reachable code paths. Forward-decl-with-name suffices for the
// vector types to instantiate (the merge body can compare via accessor).
struct EffectPropertyDefinition_Bada {
    // Minimal layout to keep vector<EffectPropertyDefinition_Bada>
    // instantiable. The real binary struct is larger; only m_Name is
    // load-bearing for the merge comparator. TODO: 0x???? -- RE the
    // full EffectPropertyDefinition_Bada layout if any port code other
    // than EffectGroup::MergeProperties ever reads it.
    std::string m_Name;  // +0x0C in real binary (with leading vector data)
};

// VertexElementBase -- 8-byte wrapper around an immutable vertex-attribute
// name and binding/semantic index. Binary ctor @ 0x001a1ba0; 5 contiguous
// sub-objects make up an Effect_Bada::Pass (8-byte stride, total 40 bytes).
// Binary Immutable<basic_string, ImmutableTraitsDefault> is 8 bytes;
// port Immutable is 4 bytes (single Node* ptr), so uint32_t m_Index
// brings VertexElementBase to the binary-matching 8 bytes.
struct VertexElementBase {
    Immutable m_Name;   // +0x00 (4 bytes in port; 8-byte Immutable variant in binary)
    uint32_t  m_Index;  // +0x04 -- read from stream via ReadBasicType<unsigned long>
                        // TODO: v1.6.1 VertexElementBase @0x0025f4b4 -- confirm semantic
                        //   (attribute binding index? semantic index?); debug-only field.

    VertexElementBase() : m_Index(0) {}
    VertexElementBase(const VertexElementBase& o) : m_Name(o.m_Name), m_Index(o.m_Index) {}
};

// Effect_Bada -- platform-portable base, holds the Pass + property-def
// vectors.
class Effect_Bada : public ReferenceCounter {
public:
    // Effect_Bada::Pass -- 40 bytes, non-polymorphic aggregate of 5 VertexElementBase
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
    // Effect::DebugInfo (size 0x1c = 28 bytes). Shader/attribute debug metadata.
    // ASM-spec v1.6.1 operator>>(DataStreamReader&, Effect::DebugInfo&) @0x00261674.
    struct DebugInfo {
        std::string                             m_Name;           // +0x00 (4 bytes on ARM32)
        std::vector<EffectPropertyDefinition>   m_PropDefs;       // +0x04 (12 bytes)
        std::vector<VertexElementBase>          m_VertexElements; // +0x10 (12 bytes)
    };  // total = 0x1c = 28 bytes

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
    uint32_t               _tail_pad;    // +0x34 compiler tail padding; binary: uninitialized, never destructed

    // ASM-spec v1.6.1 Effect::LoadPlatformData @0x00263360:
    //   if (tag != 0x31534569u) return; else Read<Effect_GLES1::Pass,alloc>(reader, m_Passes).
    //   Port is a no-op stub: GLES1 pass data is defunct in the SDL2 port; main reader
    //   cursor is advanced externally by dataSize (see Read(SmartPtr<Effect>&) caller).
    void LoadPlatformData(unsigned long tag, DataStreamReader& reader);
};

// Free comparator. Binary @ 0x001a0480 -- strcmp on Effect::m_Name.
// Used by EffectGroup::AddEffect's std::lower_bound call to keep
// m_Effects sorted by name.
struct EffectLessThanCompare {
    bool operator()(SmartPtr<Effect> a, SmartPtr<Effect> b) const {
        return a->m_Name.compare(b->m_Name) < 0;
    }
};

// Heterogeneous free comparators for sorted SmartPtr<Effect> containers.
// Compare by Effect::m_Name (std::string at +0x30).
// Binary v1.6.1 Mortar::operator< @0x0025f3d8, @0x0025f3f8, @0x0025f410.
bool operator<(const SmartPtr<Effect>& a, const SmartPtr<Effect>& b);
bool operator<(const SmartPtr<Effect>& a, const char* s);
bool operator<(const char* s, const SmartPtr<Effect>& b);

// EffectGroup -- 0x38 bytes.
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
// Live render path is fully bypassed in the port (Geometry::Render draws from load-cached
// m_Vbo/m_Ibo/m_Layout rather than walking Effect/EffectGroup multi-pass dispatch;
// both paths are fixed-function GLES1.x). Both AddEffect and MergeProperties are reachable
// in code shape but never fire at runtime -- every binary caller chains through stubs.
class EffectGroup : public ReferenceCounter {
public:
    std::vector<EffectPropertyDefinition>  m_MergedDefs;   // +0x0C
    std::vector<SmartPtr<Effect> >         m_Effects;       // +0x18
    // +0x24..+0x37: 4-byte align pad + two Event1<EffectGroup&> slots.
    // Not modelled; gap preserved for binary-shape sizeof.
    uint8_t _events_gap[20];

    EffectGroup() {}

    // Binary @ 0x001a0274 -- sorted-set insert by name; merge on dup.
    void AddEffect(SmartPtr<Effect> effect);

    // Binary @ 0x001a2030 -- fold each property def in `props` into
    // m_MergedDefs at its lower_bound position. Incoming `props` is the
    // per-Effect on-disk _Bada vector; m_MergedDefs is the runtime type.
    // Returns 1 on success, 0 if any def conflicts.
    int MergeProperties(const std::vector<EffectPropertyDefinition_Bada>& props);
};

// Free Read overloads for streaming Effect types -- defined in Effect.cpp.
// Single-item overloads:
//   v1.6.1 Mortar::Read(DataStreamReader&, EffectPropertyDefinition&) @0x0025f4d4
//   v1.6.1 Mortar::Read(DataStreamReader&, VertexElementBase&)        @0x0025f4b4
//   v1.6.1 Mortar::Read(DataStreamReader&, SmartPtr<Effect>&)         @0x0025f590
//   v1.6.1 Mortar::operator>>(DataStreamReader&, Effect::DebugInfo&)  @0x00261674
// Vector (template instantiation) overloads:
//   v1.6.1 Read<EffectPropertyDefinition,allocator>  @0x00261210  stride 0xc
//   v1.6.1 Read<VertexElementBase,allocator>         @0x002615b0  stride 8
//   v1.6.1 Read<SmartPtr<Effect>,allocator>          @0x00261e48  stride 4
//   v1.6.1 Read<Effect::DebugInfo,allocator>         @0x00261aa8  stride 0x1c
void Read(DataStreamReader& reader, EffectPropertyDefinition& def);
void Read(DataStreamReader& reader, VertexElementBase& ve);
void Read(DataStreamReader& reader, SmartPtr<Effect>& sp);
DataStreamReader& operator>>(DataStreamReader& reader, Effect::DebugInfo& di);

void Read(DataStreamReader& reader,
          std::vector<EffectPropertyDefinition,
                      std::allocator<EffectPropertyDefinition> >& vec);
void Read(DataStreamReader& reader,
          std::vector<VertexElementBase,
                      std::allocator<VertexElementBase> >& vec);
void Read(DataStreamReader& reader,
          std::vector<SmartPtr<Effect>,
                      std::allocator<SmartPtr<Effect> > >& vec);
void Read(DataStreamReader& reader,
          std::vector<Effect::DebugInfo,
                      std::allocator<Effect::DebugInfo> >& vec);

}  // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::VertexElementBase) == 8,
              "VertexElementBase must be 8 bytes (binary: 8-byte stride in Pass ctor @0x001a33e4)");
static_assert(sizeof(Mortar::Effect_Bada::Pass) == 40,
              "Effect_Bada::Pass must be 40 bytes (binary: 5x VertexElementBase @0x001a33e4)");
static_assert(sizeof(Mortar::Effect) == 0x38,
              "Effect must be 0x38 bytes (binary: operator new(0x38) in Read(SmartPtr<Effect>&) @0x0025f590)");
static_assert(sizeof(Mortar::Effect::DebugInfo) == 0x1c,
              "Effect::DebugInfo must be 0x1c bytes (binary: stride 0x1c in Read<DebugInfo,alloc> @0x00261aa8)");
#endif

#endif  // MORTAR_ASSET_EFFECT_H
