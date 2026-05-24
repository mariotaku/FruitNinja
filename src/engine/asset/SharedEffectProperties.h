#ifndef FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H
#define FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H

// SharedEffectProperties and supporting types — shape-preserved defunct stubs.
// The real subsystem manages per-mesh named groups of typed EffectProperty slots
// (World, View, Projection, WVP matrices). The port's Mesh::Draw bypasses this
// entirely via MatrixManager, so no live code path reaches these impls.
//
// Binary layout refs:
//   SharedEffectProperties ctor (range) @ 0x001b2708
//   SharedEffectProperties ctor (4-def) @ 0x001b2788
//   EffectPropertyList::Contains        @ (scan of m_Props vector)
//   EffectPropertyList::GetProperty     @ (linear search by name)

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include <vector>
#include <cstdint>

namespace Mortar {

// EffectPropertyDefinition — 12 bytes; parameter to non-const GetPropertiesGroup.
// Binary: m_Name is Immutable<std::string>* (ported as void* since Immutable<T>
// is not yet ported); m_Type is a type-bucket index; m_Count is per-bucket stride.
struct EffectPropertyDefinition {
    void*    m_Name;   // +0x00 — Immutable<std::string>* in binary; nullptr in stubs
    uint32_t m_Type;   // +0x04 — type-bucket index (1, 3 seen)
    uint32_t m_Count;  // +0x08 — per-bucket stride (1, 3 seen)
};

// Forward declaration for circular reference between EffectPropertyList and
// SharedEffectProperties.
class SharedEffectProperties;

// EffectPropertyList — 0x14 bytes (20). Lives inside SharedEffectProperties at +0x0c.
// Defunct: real impl maintains a typed arena of property slots. Port stubs always
// report properties as already-present so no arena is ever allocated.
class EffectPropertyList {
public:
    EffectPropertyList() : m_Values(NULL) {}

    // Defunct: SharedEffectProperties subsystem -- shape preserved.
    // Real impl: linear scan of m_Props checking name+type match.
    // Stub returns true so the "already-present" fast path wins and no new
    // SharedEffectProperties is ever materialized from GetPropertiesGroup.
    bool Contains(const EffectPropertyDefinition* /*def*/) const {
        // Defunct: SharedEffectProperties subsystem -- no-op stub
        return true;
    }

    // Defunct: SharedEffectProperties subsystem -- shape preserved.
    // Real impl: linear search of m_Props by name, returns EffectProperty* into arena.
    // Stub returns nullptr; callers store the handle but never dereference it in the port.
    void* GetProperty(const char* /*name*/) const {
        // Defunct: SharedEffectProperties subsystem -- no-op stub
        return NULL;
    }

    void SetParent(const SmartPtr<SharedEffectProperties>& parent) {
        m_Parent = parent;
    }

private:
    SmartPtr<SharedEffectProperties> m_Parent;   // +0x00, 4 bytes
    void*                            m_Values;   // +0x04, std::auto_ptr<EffectPropertyValues>*
    std::vector<void*>               m_Props;    // +0x08, 12 bytes (EffectProperty*)
    // total: 4 + 4 + 12 = 20 = 0x14
};

// SharedEffectProperties — 0x20 bytes; ReferenceCounter-derived, managed by SmartPtr.
// Binary: holds an EffectPropertyList at +0x0c (after 12-byte ReferenceCounter base).
// Defunct: no live render-time path reaches any method body.
class SharedEffectProperties : public ReferenceCounter {
public:
    // Range ctor — matches binary @ 0x001b2708.
    // Real impl: allocates an EffectPropertyValues arena of size proportional to
    // (end - begin), then inserts EffectProperty objects for each def.
    // Stub: default-initializes m_List; accepts parent for SmartPtr shape parity.
    // Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b2708
    SharedEffectProperties(const EffectPropertyDefinition* /*begin*/,
                           const EffectPropertyDefinition* /*end*/,
                           const SmartPtr<SharedEffectProperties>& parent) {
        // Defunct: SharedEffectProperties subsystem -- no-op stub; binary @ 0x001b2708
        m_List.SetParent(parent);
    }

    EffectPropertyList& GetList() { return m_List; }
    const EffectPropertyList& GetList() const { return m_List; }

private:
    EffectPropertyList m_List;  // +0x0c (ReferenceCounter base is 12 bytes)
    // total: 12 + 20 = 32 = 0x20
};

}  // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::EffectPropertyDefinition) == 12,
              "EffectPropertyDefinition must be 12 bytes");
static_assert(sizeof(Mortar::EffectPropertyList) == 0x14,
              "EffectPropertyList must be 0x14 (20) bytes");
static_assert(sizeof(Mortar::SharedEffectProperties) == 0x20,
              "SharedEffectProperties must be 0x20 (32) bytes");
#endif

#endif  // FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H
