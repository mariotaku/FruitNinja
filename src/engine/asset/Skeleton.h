#ifndef MORTAR_ASSET_SKELETON_H
#define MORTAR_ASSET_SKELETON_H

// Analysed: 2026-04-11T18:30

#include "math/Matrix44.h"
#include "util/AsciiString.h"
#include <vector>
#include <cstdint>
#include <cstring>

namespace Mortar {

// Matches Mortar::Skeleton (0x18 = 24 bytes)
// Ref: docs/engine/mesh.md § "Skeleton Class"
//
// Original memory layout:
//   +0x00 vector<Bone>    m_Bones
//   +0x0C Matrix44*       m_LocalMatrices  (base of single alloc: N × 3 × 64 bytes)
//   +0x10 Matrix44*       m_WorldMatrices  (= m_LocalMatrices + N)
//   +0x14 Matrix44*       m_VertMatrices   (= m_LocalMatrices + 2N)
// Port: uses std::vector<Matrix44> for each to avoid manual allocation.
class Skeleton {
public:
    // Matches Mortar::Skeleton::Bone (0xAC = 172 bytes in memory)
    // Serialised by ReadType<Skeleton::Bone> (0x001a7600):
    //   ReadString → Read<long> → Read<float[16]> → Read<float[3]> → Read<float[4]> → Read<float[9]>
    struct Bone {
        Mortar::AsciiString m_Name;  // +0x00: bone name (40 bytes)
        int         m_ParentIndex;   // +0x28: parent index; -1 = root
        float       m_BindPoseMat[16]; // +0x2C: bind-pose Matrix44 (float[16])
        float       m_LocalTranslation[3]; // +0x6C
        float       m_LocalRotation[4];    // +0x78: quaternion (x,y,z,w)
        float       m_LocalScale[9];       // +0x88: 3x3 scale/rotation matrix (column-major)

        Bone() : m_ParentIndex(-1) {
            memset(m_BindPoseMat, 0, sizeof(m_BindPoseMat));
            m_BindPoseMat[0] = m_BindPoseMat[5] = m_BindPoseMat[10] = m_BindPoseMat[15] = 1.0f;
            memset(m_LocalTranslation, 0, sizeof(m_LocalTranslation));
            memset(m_LocalRotation, 0, sizeof(m_LocalRotation));
            m_LocalRotation[3] = 1.0f; // w = 1 (identity quaternion)
            memset(m_LocalScale, 0, sizeof(m_LocalScale));
            m_LocalScale[0] = m_LocalScale[4] = m_LocalScale[8] = 1.0f; // identity mat3
        }
    };

    std::vector<Bone>     m_Bones;
    // Port specific: using vectors instead of single raw allocation
    std::vector<Matrix44> m_LocalMatrices;
    std::vector<Matrix44> m_WorldMatrices;
    std::vector<Matrix44> m_VertMatrices;

    Skeleton() {}
    ~Skeleton() {}

    // Matches Skeleton::Swap (0x001aadf4)
    // Takes the loaded bone list, swaps into m_Bones, then builds matrices.
    void Swap(std::vector<Bone>& bones);

    // Matches Skeleton::Swap (0x001a89c4) — Skeleton& overload.
    // Swaps all four member arrays (m_Bones, m_LocalMatrices, m_WorldMatrices,
    // m_VertMatrices) with the other skeleton. Does NOT rebuild matrices.
    // Binary sequence: m_Bones.swap, swap(local), swap(world), swap(vert).
    void Swap(Skeleton& other);

    // Binary @ 0x0019323c — _ZNK6Mortar8Skeleton9FindIndexERKNS_11AsciiStringE
    // Linear scan by AsciiString name. Returns 0xFFFFFFFF if not found.
    // ONLY overload in binary; no const char* version exists in binary.
    uint32_t FindIndex(const Mortar::AsciiString& name) const;

    // Port specific: convenience overload, no binary symbol.
    uint32_t FindIndex(const char* name) const;

    // Binary @ 0x001b15d0 — raw pointer arithmetic, no bounds check.
    // ldr r0,[r0,#0x14]; add.w r0,r0,r1,lsl #0x6; bx lr
    // All callers gate on m_SkeletonIndex >= 0 upstream (Mesh::GetBone*Transform).
    const Matrix44* GetVertex(uint32_t index) const {
        return &m_VertMatrices[index];
    }

    // Binary @ 0x001b15c8 — raw pointer arithmetic, no bounds check.
    // ldr r0,[r0,#0x10]; add.w r0,r0,r1,lsl #0x6; bx lr
    // All callers gate on m_SkeletonIndex >= 0 upstream (Mesh::GetBone*Transform).
    const Matrix44* GetWorld(uint32_t index) const {
        return &m_WorldMatrices[index];
    }

    // Binary @ 0x001b15c0 — raw pointer arithmetic, no bounds check.
    // ldr r0,[r0,#0x0c]; add.w r0,r0,r1,lsl #0x6; bx lr
    // All callers gate on m_SkeletonIndex >= 0 upstream (Mesh::GetBone*Transform).
    const Matrix44* GetLocal(uint32_t index) const {
        return &m_LocalMatrices[index];
    }

    bool IsValid() const { return !m_Bones.empty(); }

private:
    // Matches Skeleton::BuildArrays (0x001aa700)
    // Allocates matrix arrays for N bones.
    void BuildArrays(int count);

    // Matches Skeleton::BuildLocalMatrices @ 0x002372fc
    // Converts per-bone TRS (quaternion + vec3 + mat3) -> local Matrix44.
    // Sequence: scale * rot_transpose * translate (Matrix44::operator*).
    void BuildLocalMatrices();

    // Matches Skeleton::BuildFinalMatrices (0x00192e0c)
    // Computes world (parent-chain accumulation) and vert (world × bindPose) matrices.
    void BuildFinalMatrices();
};

} // namespace Mortar

#endif
