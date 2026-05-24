#include "asset/SharedEffectProperties.h"

#include <cstring>
#include <cstdlib>

namespace Mortar {

// EffectPropertyValues ctor.
// Allocates a single contiguous heap buffer partitioned into per-type buckets.
// Each bucket occupies (count * s_TypeSize[t]) bytes, rounded up to 4-byte
// alignment, concatenated in type-index order.  ArrayItem::m_Begin for each
// bucket points into the buffer at the appropriate offset.
EffectPropertyValues::EffectPropertyValues(
        const unsigned long bucketSizes[EffectDataTypes::kNumTypes]) {
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

// Explicit template instantiations so the out-of-line specialisations emit
// symbols. GCC 4.4.1 (cross-build) requires explicit instantiation for
// templates defined in a header when the .cpp uses them out-of-line.

template bool EffectPropertyValues::TrySetValue<int>(EffectDataTypes::Type, unsigned long, const int&);
template bool EffectPropertyValues::TrySetValue<float>(EffectDataTypes::Type, unsigned long, const float&);
template bool EffectPropertyValues::TrySetValue<bool>(EffectDataTypes::Type, unsigned long, const bool&);
template bool EffectPropertyValues::TrySetValue<Matrix44>(EffectDataTypes::Type, unsigned long, const Matrix44&);
template bool EffectPropertyValues::TrySetValue<Vec2>(EffectDataTypes::Type, unsigned long, const Vec2&);
template bool EffectPropertyValues::TrySetValue<Vec3>(EffectDataTypes::Type, unsigned long, const Vec3&);
template bool EffectPropertyValues::TrySetValue<Vec4>(EffectDataTypes::Type, unsigned long, const Vec4&);
template bool EffectPropertyValues::TrySetValue<EffectTexture2D>(EffectDataTypes::Type, unsigned long, const EffectTexture2D&);
template bool EffectPropertyValues::TrySetValue<EffectTexture3D>(EffectDataTypes::Type, unsigned long, const EffectTexture3D&);
template bool EffectPropertyValues::TrySetValue<EffectTextureCube>(EffectDataTypes::Type, unsigned long, const EffectTextureCube&);

template bool EffectPropertyValues::TryGetValue<int>(EffectDataTypes::Type, unsigned long, int&) const;
template bool EffectPropertyValues::TryGetValue<float>(EffectDataTypes::Type, unsigned long, float&) const;
template bool EffectPropertyValues::TryGetValue<bool>(EffectDataTypes::Type, unsigned long, bool&) const;
template bool EffectPropertyValues::TryGetValue<Matrix44>(EffectDataTypes::Type, unsigned long, Matrix44&) const;
template bool EffectPropertyValues::TryGetValue<Vec2>(EffectDataTypes::Type, unsigned long, Vec2&) const;
template bool EffectPropertyValues::TryGetValue<Vec3>(EffectDataTypes::Type, unsigned long, Vec3&) const;
template bool EffectPropertyValues::TryGetValue<Vec4>(EffectDataTypes::Type, unsigned long, Vec4&) const;
template bool EffectPropertyValues::TryGetValue<EffectTexture2D>(EffectDataTypes::Type, unsigned long, EffectTexture2D&) const;
template bool EffectPropertyValues::TryGetValue<EffectTexture3D>(EffectDataTypes::Type, unsigned long, EffectTexture3D&) const;
template bool EffectPropertyValues::TryGetValue<EffectTextureCube>(EffectDataTypes::Type, unsigned long, EffectTextureCube&) const;

template const int* EffectPropertyValues::GetValueRef<int>(EffectDataTypes::Type, unsigned long) const;
template const float* EffectPropertyValues::GetValueRef<float>(EffectDataTypes::Type, unsigned long) const;
template const bool* EffectPropertyValues::GetValueRef<bool>(EffectDataTypes::Type, unsigned long) const;
template const Matrix44* EffectPropertyValues::GetValueRef<Matrix44>(EffectDataTypes::Type, unsigned long) const;
template const Vec2* EffectPropertyValues::GetValueRef<Vec2>(EffectDataTypes::Type, unsigned long) const;
template const Vec3* EffectPropertyValues::GetValueRef<Vec3>(EffectDataTypes::Type, unsigned long) const;
template const Vec4* EffectPropertyValues::GetValueRef<Vec4>(EffectDataTypes::Type, unsigned long) const;
template const EffectTexture2D* EffectPropertyValues::GetValueRef<EffectTexture2D>(EffectDataTypes::Type, unsigned long) const;
template const EffectTexture3D* EffectPropertyValues::GetValueRef<EffectTexture3D>(EffectDataTypes::Type, unsigned long) const;
template const EffectTextureCube* EffectPropertyValues::GetValueRef<EffectTextureCube>(EffectDataTypes::Type, unsigned long) const;

}  // namespace Mortar
