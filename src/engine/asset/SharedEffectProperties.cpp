#include "asset/SharedEffectProperties.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace Mortar {

// EffectPropertyValues ctor.
// Allocates a single contiguous heap buffer partitioned into per-type buckets.
// Each bucket occupies (count * s_TypeSize[t]) bytes, rounded up to 4-byte
// alignment, concatenated in type-index order.  ArrayItem::m_Begin for each
// bucket points into the buffer at the appropriate offset.
EffectPropertyValues::EffectPropertyValues(
        const unsigned long (&bucketSizes)[EffectDataTypes::kNumTypes]) {
    uint32_t total = 0;
    for (int i = 0; i < EffectDataTypes::kNumTypes; ++i) {
        unsigned long bytes = bucketSizes[i] * EffectDataTypes::s_TypeSize[i];
        // Round up to 4-byte boundary.
        bytes = (bytes + 3u) & ~3u;
        m_ValueBuffer.m_Buckets[i].m_Count = bucketSizes[i];
        m_ValueBuffer.m_Buckets[i].m_Begin = NULL;
        total += static_cast<uint32_t>(bytes);
    }
    m_ValueBuffer.m_TotalBytes = total;

    if (total > 0) {
        m_ValueBuffer.m_Buffer = ::operator new(total);
        memset(m_ValueBuffer.m_Buffer, 0, total);
    } else {
        m_ValueBuffer.m_Buffer = NULL;
    }

    // Wire each bucket's begin pointer into the flat buffer.
    uint8_t* cursor = static_cast<uint8_t*>(m_ValueBuffer.m_Buffer);
    for (int i = 0; i < EffectDataTypes::kNumTypes; ++i) {
        if (bucketSizes[i] > 0) {
            m_ValueBuffer.m_Buckets[i].m_Begin = cursor;
            unsigned long bytes = bucketSizes[i] * EffectDataTypes::s_TypeSize[i];
            bytes = (bytes + 3u) & ~3u;
            cursor += bytes;
        }
    }
}

EffectPropertyValues::~EffectPropertyValues() {
    if (m_ValueBuffer.m_Buffer) {
        ::operator delete(m_ValueBuffer.m_Buffer);
        m_ValueBuffer.m_Buffer = NULL;
    }
}

// ASM-spec v1.6.1 EffectPropertyList::~EffectPropertyList @ 0x00264e88:
// purely implicit body — ~vector(m_Props) + ~auto_ptr(m_Values) + SmartPtr::Clear(m_Parent).
// m_Props stores EffectProperty by value so vector dtor handles element cleanup.
EffectPropertyList::~EffectPropertyList() {
    delete m_Values;  // mirrors auto_ptr dtor; see header DIFFERS comment @+0x04
}

// SortProperties — sorts m_Props by name so binary-search in GetProperty works.
void EffectPropertyList::SortProperties() {
    std::sort(m_Props.begin(), m_Props.end(), NameLessThan());
}

// ASM-spec v1.6.1 EffectPropertyList::GetProperty @0x0025d650 (non-const, matches binary).
// Binary-searches m_Props (sorted by name) for `name`, then
// recurses to parent list if not found.
EffectProperty* EffectPropertyList::GetProperty(const char* name) {
    std::vector<EffectProperty>::iterator lo =
        std::lower_bound(m_Props.begin(), m_Props.end(), name, NameLessThan());
    if (lo != m_Props.end() && std::strcmp(lo->m_Def.m_Name.c_str(), name) == 0)
        return &*lo;
    if (m_Parent.IsValid())
        return m_Parent->GetList().GetProperty(name);
    return NULL;
}

// Const overload -- delegates to the non-const primary above (no mutation occurs).
EffectProperty* EffectPropertyList::GetProperty(const char* name) const {
    return const_cast<EffectPropertyList*>(this)->GetProperty(name);
}

// GetProperty(string&) @ 0x001b6820 — forwards to char* overload.
EffectProperty* EffectPropertyList::GetProperty(const std::string& name) const {
    return GetProperty(name.c_str());
}

// Contains(def&) @ 0x001b6828 — pulls name from def and delegates to GetProperty.
bool EffectPropertyList::Contains(const EffectPropertyDefinition& def) const {
    return GetProperty(def.m_Name.c_str()) != NULL;
}

// Contains(ptr) — single-pointer convenience; delegates to ref overload.
// Kept for Mesh.cpp call sites that pass a raw pointer.
bool EffectPropertyList::Contains(const EffectPropertyDefinition* def) const {
    return Contains(*def);
}

// Contains(begin, end) @ 0x002738b8 (v1.6.1) — range overload; true iff all defs present.
bool EffectPropertyList::Contains(const EffectPropertyDefinition* begin,
                                  const EffectPropertyDefinition* end) const {
    for (const EffectPropertyDefinition* p = begin; p != end; ++p) {
        if (!Contains(*p)) return false;
    }
    return true;
}

// SharedEffectProperties range ctor @ 0x001b2708.
SharedEffectProperties::SharedEffectProperties(
        const EffectPropertyDefinition* begin,
        const EffectPropertyDefinition* end,
        SmartPtr<SharedEffectProperties> parent) {
    m_List.InitPropertyList(begin, end, parent);
}

// Explicit instantiation of InitPropertyList for the const EffectPropertyDefinition*
// iterator so the symbol emits from this TU.
template void EffectPropertyList::InitPropertyList<const EffectPropertyDefinition*>(
    const EffectPropertyDefinition*, const EffectPropertyDefinition*,
    SmartPtr<SharedEffectProperties>);

// Explicit template instantiations so the out-of-line specialisations emit
// symbols. GCC 4.4.1 (cross-build) requires explicit instantiation for
// templates defined in a header when the .cpp uses them out-of-line.

template bool EffectPropertyValues::TrySetValue<int>(EffectDataTypes::Type, unsigned long, const int&);
template bool EffectPropertyValues::TrySetValue<float>(EffectDataTypes::Type, unsigned long, const float&);
template bool EffectPropertyValues::TrySetValue<bool>(EffectDataTypes::Type, unsigned long, const bool&);
template bool EffectPropertyValues::TrySetValue<Matrix44>(EffectDataTypes::Type, unsigned long, const Matrix44&);
template bool EffectPropertyValues::TrySetValue<_Vector2<float>>(EffectDataTypes::Type, unsigned long, const _Vector2<float>&);
template bool EffectPropertyValues::TrySetValue<_Vector3<float>>(EffectDataTypes::Type, unsigned long, const _Vector3<float>&);
template bool EffectPropertyValues::TrySetValue<Vec4>(EffectDataTypes::Type, unsigned long, const Vec4&);
template bool EffectPropertyValues::TrySetValue<EffectTexture2D>(EffectDataTypes::Type, unsigned long, const EffectTexture2D&);
template bool EffectPropertyValues::TrySetValue<EffectTexture3D>(EffectDataTypes::Type, unsigned long, const EffectTexture3D&);
template bool EffectPropertyValues::TrySetValue<EffectTextureCube>(EffectDataTypes::Type, unsigned long, const EffectTextureCube&);

template bool EffectPropertyValues::TryGetValue<int>(EffectDataTypes::Type, unsigned long, int&) const;
template bool EffectPropertyValues::TryGetValue<float>(EffectDataTypes::Type, unsigned long, float&) const;
template bool EffectPropertyValues::TryGetValue<bool>(EffectDataTypes::Type, unsigned long, bool&) const;
template bool EffectPropertyValues::TryGetValue<Matrix44>(EffectDataTypes::Type, unsigned long, Matrix44&) const;
template bool EffectPropertyValues::TryGetValue<_Vector2<float>>(EffectDataTypes::Type, unsigned long, _Vector2<float>&) const;
template bool EffectPropertyValues::TryGetValue<_Vector3<float>>(EffectDataTypes::Type, unsigned long, _Vector3<float>&) const;
template bool EffectPropertyValues::TryGetValue<Vec4>(EffectDataTypes::Type, unsigned long, Vec4&) const;
template bool EffectPropertyValues::TryGetValue<EffectTexture2D>(EffectDataTypes::Type, unsigned long, EffectTexture2D&) const;
template bool EffectPropertyValues::TryGetValue<EffectTexture3D>(EffectDataTypes::Type, unsigned long, EffectTexture3D&) const;
template bool EffectPropertyValues::TryGetValue<EffectTextureCube>(EffectDataTypes::Type, unsigned long, EffectTextureCube&) const;

template const int* EffectPropertyValues::GetValueRef<int>(EffectDataTypes::Type, unsigned long) const;
template const float* EffectPropertyValues::GetValueRef<float>(EffectDataTypes::Type, unsigned long) const;
template const bool* EffectPropertyValues::GetValueRef<bool>(EffectDataTypes::Type, unsigned long) const;
template const Matrix44* EffectPropertyValues::GetValueRef<Matrix44>(EffectDataTypes::Type, unsigned long) const;
template const _Vector2<float>* EffectPropertyValues::GetValueRef<_Vector2<float>>(
    EffectDataTypes::Type, unsigned long) const;
template const _Vector3<float>* EffectPropertyValues::GetValueRef<_Vector3<float>>(
    EffectDataTypes::Type, unsigned long) const;
template const Vec4* EffectPropertyValues::GetValueRef<Vec4>(EffectDataTypes::Type, unsigned long) const;
template const EffectTexture2D* EffectPropertyValues::GetValueRef<EffectTexture2D>(EffectDataTypes::Type, unsigned long) const;
template const EffectTexture3D* EffectPropertyValues::GetValueRef<EffectTexture3D>(EffectDataTypes::Type, unsigned long) const;
template const EffectTextureCube* EffectPropertyValues::GetValueRef<EffectTextureCube>(EffectDataTypes::Type, unsigned long) const;

}  // namespace Mortar
