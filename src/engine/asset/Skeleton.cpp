#include "asset/Skeleton.h"
#include "util/AsciiString.h"
#include <cstring>
#include <cstdio>

// Analysed: 2026-04-11T18:30

namespace Mortar {

// Binary @ 0x0019323c — _ZNK6Mortar8Skeleton9FindIndexERKNS_11AsciiStringE
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

// Matches Skeleton::BuildArrays (0x001aa700)
void Skeleton::BuildArrays(int count) {
    m_LocalMatrices.assign(count, Matrix44());
    m_WorldMatrices.assign(count, Matrix44());
    m_VertMatrices.assign(count,  Matrix44());
}

// Matches Skeleton::BuildLocalMatrices (0x002372fc)
// Sequence (from disassembly at 0x002372fc):
//   1. _Quaternion::Matrix44(bone+0x78) → rotation mat R  (binary quat ctor @0x001e5c18)
//   2. Transpose44(R) → Rt  (binary quaternion mat == port's Rt)
//   3. _Matrix44::Translate44(bone+0x6c) → T
//   4. _Matrix33::cast_to_Matrix44(bone+0x88) → S
//   5. tmp = R*S    (binary Mul44(S,R,tmp) @0x0016f5a0 => port-convention tmp = R*S)
//   6. local = T*tmp  (binary Mul44(tmp,T,local) => port-convention local = T*tmp)
void Skeleton::BuildLocalMatrices() {
    int n = (int)m_Bones.size();
    for (int i = 0; i < n; i++) {
        const Bone& bone = m_Bones[i];
        Matrix44& local = m_LocalMatrices[i];

        // Step 1: quaternion (x,y,z,w) → column-major rotation matrix
        float qx = bone.m_LocalRotation[0];
        float qy = bone.m_LocalRotation[1];
        float qz = bone.m_LocalRotation[2];
        float qw = bone.m_LocalRotation[3];

        Matrix44 R;
        R.m[0]  = 1.0f - 2.0f*(qy*qy + qz*qz);
        R.m[1]  = 2.0f*(qx*qy + qw*qz);
        R.m[2]  = 2.0f*(qx*qz - qw*qy);
        R.m[3]  = 0.0f;

        R.m[4]  = 2.0f*(qx*qy - qw*qz);
        R.m[5]  = 1.0f - 2.0f*(qx*qx + qz*qz);
        R.m[6]  = 2.0f*(qy*qz + qw*qx);
        R.m[7]  = 0.0f;

        R.m[8]  = 2.0f*(qx*qz + qw*qy);
        R.m[9]  = 2.0f*(qy*qz - qw*qx);
        R.m[10] = 1.0f - 2.0f*(qx*qx + qy*qy);
        R.m[11] = 0.0f;

        R.m[12] = 0.0f; R.m[13] = 0.0f; R.m[14] = 0.0f; R.m[15] = 1.0f;

        // Step 2: transpose R → Rt (matches Transpose44 call in disasm)
        Matrix44 Rt;
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
                Rt.m[c*4+r] = R.m[r*4+c];

        // Step 3: translation matrix T from bone+0x6c
        Matrix44 T;
        T.m[12] = bone.m_LocalTranslation[0];
        T.m[13] = bone.m_LocalTranslation[1];
        T.m[14] = bone.m_LocalTranslation[2];

        // Step 4: mat3 (column-major float[9]) → mat44 S
        // matches _Matrix33::cast_to_Matrix44(bone+0x88)
        Matrix44 S;
        const float* s = bone.m_LocalScale;
        // Column-major mat3: s[0..2]=col0, s[3..5]=col1, s[6..8]=col2
        S.m[0] = s[0]; S.m[1] = s[1]; S.m[2] = s[2]; S.m[3] = 0.0f;
        S.m[4] = s[3]; S.m[5] = s[4]; S.m[6] = s[5]; S.m[7] = 0.0f;
        S.m[8] = s[6]; S.m[9] = s[7]; S.m[10]= s[8]; S.m[11]= 0.0f;
        S.m[12]= 0.0f; S.m[13]= 0.0f; S.m[14]= 0.0f; S.m[15]= 1.0f;

        // Steps 5+6: local = T * Rt * S  (binary Mul44(S,R)=>R*S then Mul44(tmp,T)=>T*(R*S);
        //   binary R == port Rt, so port: T * Rt * S)
        local = T * Rt * S;
    }
}

// Matches Skeleton::BuildFinalMatrices (0x00236f68)
// For each bone i:
//   accumulated = localMatrices[i]
//   walk parent chain (bones[j].parentIndex) multiplying localMatrices[j] on left
//   worldMatrices[i] = accumulated
//   vertMatrices[i]  = accumulated x bones[i].bindPoseMat
// DIFFERS: binary uses a raw new[] block for m_LocalMatrices accessed via raw pointer;
//   port uses std::vector. Semantics identical (Mul44 operand-reversal proven);
//   remaining asm divergence is std::vector base-pointer access + PIC/GOT +
//   register allocation only -- no logic change needed.
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

        // vertMat = world * bindPoseMat
        Matrix44 bindPose;
        memcpy(bindPose.m, m_Bones[i].m_BindPoseMat, sizeof(float) * 16);
        m_VertMatrices[i] = accumulated * bindPose;
    }
}

// Matches Skeleton::Swap (0x001aadf4)
// BuildArrays → vector::swap → BuildAllMatrices (= BuildLocal + BuildFinal)
void Skeleton::Swap(std::vector<Bone>& bones) {
    int count = (int)bones.size();
    BuildArrays(count);
    m_Bones.swap(bones);
    BuildLocalMatrices();
    BuildFinalMatrices();
}

// Matches Skeleton::Swap (0x001a89c4) — Skeleton& overload.
// Swaps all four member arrays with the other skeleton. Does NOT rebuild matrices.
// Binary: m_Bones.swap(other.m_Bones) + std::swap for the three matrix arrays.
void Skeleton::Swap(Skeleton& other) {
    m_Bones.swap(other.m_Bones);
    std::swap(m_LocalMatrices, other.m_LocalMatrices);
    std::swap(m_WorldMatrices, other.m_WorldMatrices);
    std::swap(m_VertMatrices,  other.m_VertMatrices);
}

} // namespace Mortar
