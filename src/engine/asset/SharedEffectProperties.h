#ifndef FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H
#define FN_ENGINE_ASSET_SHARED_EFFECT_PROPERTIES_H

// SharedEffectProperties and supporting types.
// Phase 1: real field layouts for EffectPropertyDefinition, EffectPropertyValues,
// EffectProperty, EffectPropertyList.
// Phase 2: real bodies for EffectPropertyList::Contains / GetProperty /
// InitPropertyList, and SharedEffectProperties ctors.
//
// Binary layout refs (v1.6.1):
//   EffectProperty ctor                   @ 0x0025d6e0
//   EffectPropertyList ctor               @ 0x00274c54
//   EffectPropertyList::~EffectPropertyList@ 0x00264e88
//   EffectPropertyList::InitPropertyList  @ 0x00274ac8
//   EffectPropertyList::GetProperty(char*)@ 0x0025d650
//   EffectPropertyList::Contains(def&)    @ TODO: verify addr
//   EffectPropertyList::Contains(range)   @ 0x002738b8
//   SharedEffectProperties ctor (range)   @ TODO: verify addr
//   SharedEffectProperties ctor (4-def)   @ TODO: verify addr
//   TrySetMatrix_EffectProp free helper   @ TODO: verify addr

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/Immutable.h"
#include "asset/EffectDataTypes.h"
#include "math/Matrix44.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include <vector>
#include <algorithm>
#include <cstring>
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
// +0x00  m_Name  (Immutable, 4 bytes)
// +0x04  m_Type  (uint32_t; wire-level storage for EffectDataTypes::Type enum;
//                 stored as uint32_t because -fshort-enums makes the enum 1 byte
//                 but we need the field at offset +0x04 and size 4 for the assert)
// +0x08  m_Count (uint32_t; per-bucket stride -- number of elements to reserve)
struct EffectPropertyDefinition {
    Immutable m_Name;   // +0x00, 4 bytes
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
    // ASM-spec v1.6.1 Mortar::EffectPropertyValues::EffectPropertyValues @0x0025d774 -> ValueBuffer @0x0025d730 (param is ref-to-array const ulong(&)[kNumTypes])
    explicit EffectPropertyValues(const unsigned long (&bucketSizes)[EffectDataTypes::kNumTypes]);
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

    // Generic SetValue<T> — writes value at bucket slot m_Offset.
    // T must match the declared type (m_Def.m_Type) or TrySetValue returns false.
    template<typename T>
    bool SetValue(const T& value) {
        if (!m_Owner) return false;
        return m_Owner->TrySetValue<T>(
            static_cast<EffectDataTypes::Type>(m_Def.m_Type), m_Offset, value);
    }

    // Binary @ 0x0023fc9c: SetValue<SmartPtr<Texture2D>> specialization.
    // Original stores a SmartPtr<Texture2D> (4-byte pointer) via
    // TrySetValue<SmartPtr<Texture2D>>. Port stores EffectTexture2D (uint32_t GL ID)
    // in the bucket; this wrapper converts to the bucket-compatible type.
    // Index is added to m_Offset for the per-element position within the bucket.
    void SetValue(const EffectTexture2D& value, unsigned long index) {
        if (!m_Owner) return;
        m_Owner->TrySetValue<EffectTexture2D>(
            static_cast<EffectDataTypes::Type>(m_Def.m_Type), index + m_Offset, value);
    }
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
    ~EffectPropertyList();

    // Contains(def&) @ 0x001b6828 — pulls name out of def and forwards to GetProperty.
    bool Contains(const EffectPropertyDefinition& def) const;

    // Contains(ptr) — single-pointer convenience used by Mesh.cpp call sites;
    // forwards to the ref overload by dereferencing.
    bool Contains(const EffectPropertyDefinition* def) const;

    // Contains(begin, end) @ 0x001b1938 — range overload; true iff every def in
    // [begin, end) is contained.
    bool Contains(const EffectPropertyDefinition* begin,
                  const EffectPropertyDefinition* end) const;

    // GetProperty(char*) @ 0x001b67b8 — binary-search m_Props by name; recurse to
    // parent list if not found.
    EffectProperty* GetProperty(const char* name) const;

    // GetProperty(string&) @ 0x001b6820 — forwards to char* overload.
    EffectProperty* GetProperty(const std::string& name) const;

    void SetParent(SmartPtr<SharedEffectProperties> parent) {
        m_Parent = parent;
    }

    // InitPropertyList @ 0x001b25b4 — two-pass sizing + slot-building algorithm.
    // Template body lives in header so it can be instantiated for any iterator.
    template <typename Iter>
    void InitPropertyList(Iter begin, Iter end,
                          SmartPtr<SharedEffectProperties> parent);

    // SetValue<T>(key, value) — looks up property by name and writes value.
    // Returns false if property not found or type mismatch.
    template<typename T>
    bool SetValue(const char* key, const T& value) {
        EffectProperty* prop = GetProperty(key);
        if (!prop) return false;
        return prop->SetValue<T>(value);
    }

    template<typename T>
    bool SetValue(const std::string& key, const T& value) {
        return SetValue<T>(key.c_str(), value);
    }

private:
    // Comparator for lower_bound / sort: compares EffectProperty by name vs char*.
    struct NameLessThan {
        bool operator()(const EffectProperty& p, const char* n) const {
            return std::strcmp(p.m_Def.m_Name.c_str(), n) < 0;
        }
        // Overload for std::sort (both sides are EffectProperty by value).
        bool operator()(const EffectProperty& a, const EffectProperty& b) const {
            return std::strcmp(a.m_Def.m_Name.c_str(), b.m_Def.m_Name.c_str()) < 0;
        }
    };

    void SortProperties();

    SmartPtr<SharedEffectProperties> m_Parent;  // +0x00, 4 bytes
    // DIFFERS: binary uses std::auto_ptr<EffectPropertyValues> (v1.6.1 EffectPropertyList @0x00264e88),
    // using raw ptr because MSVC 2022 STL removes auto_ptr via _HAS_AUTO_PTR_ETC in practice;
    // layout is identical (4 bytes on ARM32).
    EffectPropertyValues*            m_Values;  // +0x04, 4 bytes
    std::vector<EffectProperty>      m_Props;   // +0x08, 12 bytes (by value; matches binary EStack_34 push pattern)
    // total: 4 + 4 + 12 = 20 = 0x14
};

// GetColourRGB @ 0x00237f44 — unpacks a packed ABGR uint32 colour into a Vec3 RGB.
// x = (c & 0xff) / 255.0f   (R, low byte)
// y = ((c >> 8) & 0xff) / 255.0f  (G)
// z = ((c >> 16) & 0xff) / 255.0f (B)
// Alpha (high byte) is ignored.
inline Vec3 GetColourRGB(uint32_t c) {
    return Vec3(
        static_cast<float>(c & 0xffu) / 255.0f,
        static_cast<float>((c >> 8) & 0xffu) / 255.0f,
        static_cast<float>((c >> 16) & 0xffu) / 255.0f
    );
}

// TextureProps — value type in SharedPropsInfo::m_TexMaps.
// Binary @ 0x001b15d8 ctor zeroes the handle; 0x001b1394 AddTextureMap writes
// the GetProperty return into *this. Single 4-byte EffectProperty*.
struct TextureProps {
    EffectProperty* m_Handle;
    TextureProps() : m_Handle(NULL) {}
};

// SharedEffectProperties — 0x20 bytes; ReferenceCounter-derived, managed by SmartPtr.
class SharedEffectProperties : public ReferenceCounter {
public:
    // Range ctor @ 0x001b2708.
    SharedEffectProperties(const EffectPropertyDefinition* begin,
                           const EffectPropertyDefinition* end,
                           SmartPtr<SharedEffectProperties> parent);

    EffectPropertyList& GetList() { return m_List; }
    const EffectPropertyList& GetList() const { return m_List; }

private:
    EffectPropertyList m_List;  // +0x0c (ReferenceCounter base is 12 bytes)
    // total: 12 + 20 = 32 = 0x20
};

// InitPropertyList template body — must live in the header for implicit instantiation,
// and must appear after SharedEffectProperties is complete so GCC 4.4.1 two-phase
// lookup can resolve m_Parent->GetList() at template-definition time.
// Binary v1.6.1 @ 0x00274ac8.
template <typename Iter>
void EffectPropertyList::InitPropertyList(Iter begin, Iter end,
                                          SmartPtr<SharedEffectProperties> parent) {
    m_Parent = parent;

    // Pass 1: tally per-bucket bytes needed for new defs only (those not already
    // present in parent).
    unsigned long bucketCounts[EffectDataTypes::kNumTypes];
    for (int i = 0; i < EffectDataTypes::kNumTypes; ++i) bucketCounts[i] = 0;
    unsigned long newCount = 0;
    for (Iter p = begin; p != end; ++p) {
        if (m_Parent.IsValid() && m_Parent->GetList().Contains(*p)) continue;
        ++newCount;
        // Pass 1 uses m_Count-or-1 fallback so zero-count defs still allocate 1 slot.
        unsigned long c = (p->m_Count != 0) ? p->m_Count : 1;
        bucketCounts[p->m_Type] += c;
    }

    m_Values = new EffectPropertyValues(bucketCounts);

    // Pass 2: build EffectProperty entries; NO m_Count-or-1 fallback in offsets.
    unsigned long bucketOffsets[EffectDataTypes::kNumTypes];
    for (int i = 0; i < EffectDataTypes::kNumTypes; ++i) bucketOffsets[i] = 0;
    m_Props.reserve(newCount);
    for (Iter p = begin; p != end; ++p) {
        if (m_Parent.IsValid() && m_Parent->GetList().Contains(*p)) continue;
        EffectProperty prop;
        prop.m_Def    = *p;
        prop.m_Owner  = m_Values;
        prop.m_Offset = bucketOffsets[p->m_Type];
        m_Props.push_back(prop);
        // Pass 2 advances by m_Count without the ?:1 fallback — per binary v1.6.1 @ 0x00274ac8.
        bucketOffsets[p->m_Type] += p->m_Count;
    }

    SortProperties();
}

}  // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::TextureProps) == 4, "TextureProps is 4 bytes");
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
