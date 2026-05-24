#ifndef FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H
#define FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H

// SharedEffectProperties and supporting types.
// Phase 1: real field layouts for EffectPropertyDefinition, EffectPropertyValues,
// EffectProperty, EffectPropertyList. SharedEffectProperties ctor body remains
// a shape-stub (Phase 2 will implement the arena allocation + property wiring).
//
// Binary layout refs:
//   SharedEffectProperties ctor (range) @ 0x001b2708
//   SharedEffectProperties ctor (4-def) @ 0x001b2788
//   EffectPropertyList::Contains        @ (scan of m_Props vector)
//   EffectPropertyList::GetProperty     @ (linear search by name)
//   TrySetMatrix_EffectProp free helper @ 0x001b0c28

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/Immutable.h"
#include "asset/EffectDataTypes.h"
#include "math/Matrix44.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include <vector>
#include <cstdint>

namespace Mortar {

// Vec4 — 16-byte float4. No binary symbol exists for this yet in the port;
// declared here as a minimal POD so EffectDataTypeOf<Vec4> can specialise and
// the Type_Vec4 bucket is addressable should Phases 2+ need it.
struct Vec4 {
    float x, y, z, w;
    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// Texture handle types used as value-buffer elements for texture-type buckets.
// Stored as 4-byte GL texture IDs.
struct EffectTexture2D   { uint32_t id; };
struct EffectTexture3D   { uint32_t id; };
struct EffectTextureCube { uint32_t id; };

// EffectPropertyDefinition — 12 bytes.
// +0x00  m_Name  (Immutable<std::string>, 4 bytes)
// +0x04  m_Type  (uint32_t; wire-level storage for EffectDataTypes::Type enum;
//                 stored as uint32_t because -fshort-enums makes the enum 1 byte
//                 but we need the field at offset +0x04 and size 4 for the assert)
// +0x08  m_Count (uint32_t; per-bucket stride — number of elements to reserve)
struct EffectPropertyDefinition {
    Immutable<std::string> m_Name;   // +0x00, 4 bytes
    uint32_t               m_Type;   // +0x04, 4 bytes
    uint32_t               m_Count;  // +0x08, 4 bytes

    EffectDataTypes::Type GetType() const {
        return static_cast<EffectDataTypes::Type>(m_Type);
    }
};

// Forward declaration for circular reference between EffectPropertyList and
// SharedEffectProperties.
class SharedEffectProperties;

// EffectPropertyValues — 0x58 bytes.
// Holds a single contiguous heap allocation partitioned into 10 typed buckets.
// Each bucket's begin pointer and element count are tracked in m_Buckets[].
class EffectPropertyValues {
public:
    struct ArrayItem {
        unsigned long m_Count;  // +0x00, element count in this bucket
        void*         m_Begin;  // +0x04, pointer into m_ValueBuffer.m_Buffer
    };

    struct ValueBuffer {
        ArrayItem     m_Buckets[EffectDataTypes::kNumTypes];  // +0x00, 80 bytes
        uint32_t      m_TotalBytes;                           // +0x50
        void*         m_Buffer;                               // +0x54
    };

    // Ctor: takes per-bucket element counts; computes total bytes, allocates,
    // initialises ArrayItem pointers with correct alignment per s_TypeSize.
    explicit EffectPropertyValues(const unsigned long bucketSizes[EffectDataTypes::kNumTypes]);
    ~EffectPropertyValues();

    template<typename T>
    bool TrySetValue(EffectDataTypes::Type t, unsigned long offset, const T& value);

    template<typename T>
    bool TryGetValue(EffectDataTypes::Type t, unsigned long offset, T& out) const;

    template<typename T>
    const T* GetValueRef(EffectDataTypes::Type t, unsigned long offset) const;

private:
    ValueBuffer m_ValueBuffer;  // +0x00, 0x58 bytes
};

// EffectProperty — 0x14 bytes.
// POD handle: embedded definition + pointer to arena + offset within bucket.
struct EffectProperty {
    EffectPropertyDefinition  m_Def;    // +0x00, 12 bytes
    EffectPropertyValues*     m_Owner;  // +0x0c, 4 bytes
    unsigned long             m_Offset; // +0x10, 4 bytes
};

// Free helper @ binary 0x001b0c28.
// Used by Mesh::Draw to upload the 4 standard matrices each frame.
inline bool TrySetMatrix_EffectProp(EffectProperty* p, const Matrix44* m) {
    if (!p || !p->m_Owner || !m) return false;
    return p->m_Owner->TrySetValue<Matrix44>(
        static_cast<EffectDataTypes::Type>(p->m_Def.m_Type), p->m_Offset, *m);
}

// EffectDataTypeOf — maps C++ type to EffectDataTypes::Type value.
// Used by TrySetValue/TryGetValue/GetValueRef template bodies to validate
// that the caller's T matches the bucket's declared type.
template<typename T> struct EffectDataTypeOf;
template<> struct EffectDataTypeOf<int>              { static const EffectDataTypes::Type value = EffectDataTypes::Type_Int; };
template<> struct EffectDataTypeOf<float>            { static const EffectDataTypes::Type value = EffectDataTypes::Type_Float; };
template<> struct EffectDataTypeOf<bool>             { static const EffectDataTypes::Type value = EffectDataTypes::Type_Bool; };
template<> struct EffectDataTypeOf<Matrix44>         { static const EffectDataTypes::Type value = EffectDataTypes::Type_Matrix44; };
template<> struct EffectDataTypeOf<Vec2>             { static const EffectDataTypes::Type value = EffectDataTypes::Type_Vec2; };
template<> struct EffectDataTypeOf<Vec3>             { static const EffectDataTypes::Type value = EffectDataTypes::Type_Vec3; };
template<> struct EffectDataTypeOf<Vec4>             { static const EffectDataTypes::Type value = EffectDataTypes::Type_Vec4; };
template<> struct EffectDataTypeOf<EffectTexture2D>  { static const EffectDataTypes::Type value = EffectDataTypes::Type_Texture2D; };
template<> struct EffectDataTypeOf<EffectTexture3D>  { static const EffectDataTypes::Type value = EffectDataTypes::Type_Texture3D; };
template<> struct EffectDataTypeOf<EffectTextureCube>{ static const EffectDataTypes::Type value = EffectDataTypes::Type_TextureCube; };

// Template bodies inline here; explicit instantiations live in SharedEffectProperties.cpp.
template<typename T>
bool EffectPropertyValues::TrySetValue(EffectDataTypes::Type t, unsigned long offset, const T& value) {
    if (t != EffectDataTypeOf<T>::value) return false;
    ArrayItem& bucket = m_ValueBuffer.m_Buckets[static_cast<int>(t)];
    if (offset >= bucket.m_Count) return false;
    T* slot = static_cast<T*>(bucket.m_Begin);
    slot[offset] = value;
    return true;
}

template<typename T>
bool EffectPropertyValues::TryGetValue(EffectDataTypes::Type t, unsigned long offset, T& out) const {
    if (t != EffectDataTypeOf<T>::value) return false;
    const ArrayItem& bucket = m_ValueBuffer.m_Buckets[static_cast<int>(t)];
    if (offset >= bucket.m_Count) return false;
    const T* slot = static_cast<const T*>(bucket.m_Begin);
    out = slot[offset];
    return true;
}

template<typename T>
const T* EffectPropertyValues::GetValueRef(EffectDataTypes::Type t, unsigned long offset) const {
    if (t != EffectDataTypeOf<T>::value) return NULL;
    const ArrayItem& bucket = m_ValueBuffer.m_Buckets[static_cast<int>(t)];
    if (offset >= bucket.m_Count) return NULL;
    const T* slot = static_cast<const T*>(bucket.m_Begin);
    return &slot[offset];
}

// EffectPropertyList — 0x14 bytes (20). Lives inside SharedEffectProperties at +0x0c.
class EffectPropertyList {
public:
    EffectPropertyList() : m_Values(NULL) {}

    // TODO: EffectPropertyList::Contains (binary address unknown) — real impl:
    // linear scan of m_Props checking name+type match. Phase 2 implements.
    // Stub returns true so the "already-present" fast path wins and no new
    // SharedEffectProperties is ever materialized from GetPropertiesGroup.
    bool Contains(const EffectPropertyDefinition* /*def*/) const {
        return true;
    }

    // TODO: EffectPropertyList::GetProperty (binary address unknown) — real impl:
    // linear search of m_Props by name, returns EffectProperty* into arena.
    // Phase 2 implements. Stub returns NULL.
    EffectProperty* GetProperty(const char* /*name*/) const {
        return NULL;
    }

    void SetParent(const SmartPtr<SharedEffectProperties>& parent) {
        m_Parent = parent;
    }

private:
    SmartPtr<SharedEffectProperties> m_Parent;    // +0x00, 4 bytes
    EffectPropertyValues*            m_Values;    // +0x04, 4 bytes (owned raw ptr)
    std::vector<EffectProperty*>     m_Props;     // +0x08, 12 bytes
    // total: 4 + 4 + 12 = 20 = 0x14
};

// SharedEffectProperties — 0x20 bytes; ReferenceCounter-derived, managed by SmartPtr.
class SharedEffectProperties : public ReferenceCounter {
public:
    // Range ctor — shape stub; binary @ 0x001b2708.
    // TODO: 0x001b2708 — allocate EffectPropertyValues arena, insert EffectProperty
    // objects for each def in [begin, end), wire m_List.m_Values + m_List.m_Props
    // (Phase 2).
    SharedEffectProperties(const EffectPropertyDefinition* /*begin*/,
                           const EffectPropertyDefinition* /*end*/,
                           const SmartPtr<SharedEffectProperties>& parent) {
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
static_assert(sizeof(Mortar::EffectPropertyValues::ArrayItem) == 8,
              "EffectPropertyValues::ArrayItem must be 8 bytes");
static_assert(sizeof(Mortar::EffectPropertyValues::ValueBuffer) == 0x58,
              "EffectPropertyValues::ValueBuffer must be 0x58 bytes");
static_assert(sizeof(Mortar::EffectPropertyValues) == 0x58,
              "EffectPropertyValues must be 0x58 bytes");
static_assert(sizeof(Mortar::EffectProperty) == 0x14,
              "EffectProperty must be 0x14 bytes");
static_assert(sizeof(Mortar::EffectPropertyList) == 0x14,
              "EffectPropertyList must be 0x14 (20) bytes");
static_assert(sizeof(Mortar::SharedEffectProperties) == 0x20,
              "SharedEffectProperties must be 0x20 (32) bytes");
#endif

#endif  // FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H
