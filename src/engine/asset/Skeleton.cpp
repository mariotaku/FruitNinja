#include "asset/Skeleton.h"
#include "util/AsciiString.h"
#include <cstring>
#include <cstdio>

// Analysed: 2026-04-11T18:30

namespace {

// Build quaternion rotation matrix directly, no Identity() pre-fill.
inline void quatMatrix44(float* m, float qx, float qy, float qz, float qw) {
    m[0]  = 1.0f - 2.0f*(qy*qy + qz*qz);
    m[1]  = 2.0f*(qx*qy + qw*qz);
    m[2]  = 2.0f*(qx*qz - qw*qy);
    m[3]  = 0.0f;
    m[4]  = 2.0f*(qx*qy - qw*qz);
    m[5]  = 1.0f - 2.0f*(qx*qx + qz*qz);
    m[6]  = 2.0f*(qy*qz + qw*qx);
    m[7]  = 0.0f;
    m[8]  = 2.0f*(qx*qz + qw*qy);
    m[9]  = 2.0f*(qy*qz - qw*qx);
    m[10] = 1.0f - 2.0f*(qx*qx + qy*qy);
    m[11] = 0.0f;
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

// Transpose 4x4 matrix via field copy, no Identity() pre-fill.
inline void transpose44(const float* src, float* dst) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            dst[c*4+r] = src[r*4+c];
}

// Build scale matrix from column-major 3x3, no Identity() pre-fill.
inline void scaleMat3To44(float* m, const float* s) {
    m[0] = s[0]; m[1] = s[1]; m[2] = s[2]; m[3] = 0.0f;
    m[4] = s[3]; m[5] = s[4]; m[6] = s[5]; m[7] = 0.0f;
    m[8] = s[6]; m[9] = s[7]; m[10]= s[8]; m[11]= 0.0f;
    m[12]= 0.0f; m[13]= 0.0f; m[14]= 0.0f; m[15]= 1.0f;
}

} // anonymous namespace

namespace Mortar {

// Binary @ 0x0019323c -- _ZNK6Mortar8Skeleton9FindIndexERKNS_11AsciiStringE
// Linear scan; calls AsciiString::Equals (PLT 0x000f7764) per bone.
// Returns first matching index, else 0xFFFFFFFF.
uint32_t Skeleton::FindIndex(const Mortar::AsciiString& name) const {
    uint32_t n = (uint32_t)m_Bones.size();
    for (uint32_t i = 0; i < n; i++) {
        if (m_Bones[i].m_Name == name) return i;
    }
    return 0xFFFFFFFFu;
}

// Port specific: convenience overload, no binary symbol.
uint32_t Skeleton::FindIndex(const char* name) const {
    return FindIndex(Mortar::AsciiString(name));
}

// DIFFERS: original = one `new Matrix44[count*3]` (v1.6.1 Mortar::Skeleton::BuildArrays @0x0023b6f0)
// partitioned into three pointers with no per-element init; using three
// std::vector<Matrix44>::resize because the port owns the arrays via the
// vectors. resize value-constructs each element (identity), so the port runs a
// per-element ctor the binary does not. Result-equivalent: BuildLocalMatrices
// overwrites every entry before any read.
void Skeleton::BuildArrays(unsigned long count) {
    if (count == m_LocalMatrices.size() && !m_LocalMatrices.empty()) return;
    if (count == 0) {
        m_LocalMatrices.clear();
        m_WorldMatrices.clear();
        m_VertMatrices.clear();
        return;
    }
    m_LocalMatrices.resize(count);
    m_WorldMatrices.resize(count);
    m_VertMatrices.resize(count);
}

// ASM-spec v1.6.1 Skeleton::BuildLocalMatrices @ 0x002372fc:
// Binary calls operator* (library function) per multiply -- not inlined.
// Sequence: scale * rot_transpose * translate, using Matrix44::operator*.
void Skeleton::BuildLocalMatrices() {
    int n = (int)m_Bones.size();
    for (int i = 0; i < n; i++) {
        const Bone& bone = m_Bones[i];
        Matrix44& local = m_LocalMatrices[i];

        // Step 1: quaternion (x,y,z,w) -> column-major rotation matrix
        float qx = bone.m_LocalRotation[0];
        float qy = bone.m_LocalRotation[1];
        float qz = bone.m_LocalRotation[2];
        float qw = bone.m_LocalRotation[3];

        float R[16];
        quatMatrix44(R, qx, qy, qz, qw);

        // Step 2: transpose R -> Rt (Matrix44 ctor provides Identity)
        Matrix44 Rt;
        transpose44(R, Rt.m);

        // Step 3: translation matrix T (Matrix44 ctor provides Identity)
        Matrix44 T;
        T.m[12] = bone.m_LocalTranslation[0];
        T.m[13] = bone.m_LocalTranslation[1];
        T.m[14] = bone.m_LocalTranslation[2];

        // Step 4: column-major mat3 -> mat44 S (Matrix44 ctor provides Identity)
        Matrix44 S;
        scaleMat3To44(S.m, bone.m_LocalScale);

        // Steps 5+6: local = S * Rt * T (using Matrix44::operator*)
        local = S * Rt * T;
    }
}

// ASM-spec v1.6.1 Skeleton::BuildFinalMatrices @ 0x00236f68:
//   accumulated = localMatrices[i]
//   walk parent chain: accumulated = localMatrices[j] * accumulated  (parent * child)
//   worldMatrices[i] = accumulated
//   vertMatrices[i]  = accumulated * bindPose  (world * bindPose)
// Binary calls operator* (library function) + ldm/stm float copy (no memcpy, no inline Mul44).
// DIFFERS: binary uses a single new[] block; port uses std::vector.
void Skeleton::BuildFinalMatrices() {
    int n = (int)m_Bones.size();
    for (int i = 0; i < n; i++) {
        Matrix44 accumulated = m_LocalMatrices[i];
        int j = i;
        while (true) {
            j = m_Bones[j].m_ParentIndex;
            if (j < 0) break;
            accumulated = m_LocalMatrices[j] * accumulated;
        }
        m_WorldMatrices[i] = accumulated;

        // reinterpret_cast safe: _Matrix44 is float[16] with no vtable, layout-compatible.
        m_VertMatrices[i] = accumulated * reinterpret_cast<const Matrix44&>(m_Bones[i].m_BindPoseMat);
    }
}

// Matches Skeleton::Swap (0x001aadf4)
// BuildArrays -> vector::swap -> BuildAllMatrices (= BuildLocal + BuildFinal)
void Skeleton::Swap(std::vector<Bone>& bones) {
    int count = (int)bones.size();
    BuildArrays(count);
    m_Bones.swap(bones);
    BuildLocalMatrices();
    BuildFinalMatrices();
}

// Matches Skeleton::Swap (0x001a89c4) -- Skeleton& overload.
// Swaps all four member arrays with the other skeleton. Does NOT rebuild matrices.
// Binary: m_Bones.swap(other.m_Bones) + std::swap for the three matrix arrays.
void Skeleton::Swap(Skeleton& other) {
    m_Bones.swap(other.m_Bones);
    std::swap(m_LocalMatrices, other.m_LocalMatrices);
    std::swap(m_WorldMatrices, other.m_WorldMatrices);
    std::swap(m_VertMatrices,  other.m_VertMatrices);
}

} // namespace Mortar
