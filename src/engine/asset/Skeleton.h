#ifndef MORTAR_ASSET_SKELETON_H
#define MORTAR_ASSET_SKELETON_H

// Analysed: 2026-04-11T18:30

#include "math/Matrix44.h"
#include <vector>
#include <string>
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
        std::string m_Name;          // +0x00: bone name
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

    // Matches Skeleton::FindIndex (0x0019323c)
    // Linear scan by name. Returns 0xFFFFFFFF if not found.
    uint32_t FindIndex(const char* name) const;

    // Matches Skeleton::GetVertex (0x001b15d0)
    const Matrix44* GetVertex(int index) const {
        if (index >= 0 && index < (int)m_VertMatrices.size())
            return &m_VertMatrices[index];
        return nullptr;
    }

    // Matches Skeleton::GetWorld (0x001b15c8)
    const Matrix44* GetWorld(int index) const {
        if (index >= 0 && index < (int)m_WorldMatrices.size())
            return &m_WorldMatrices[index];
        return nullptr;
    }

    // Matches Skeleton::GetLocal (0x001b15c0)
    const Matrix44* GetLocal(int index) const {
        if (index >= 0 && index < (int)m_LocalMatrices.size())
            return &m_LocalMatrices[index];
        return nullptr;
    }

    bool IsValid() const { return !m_Bones.empty(); }

private:
    // Matches Skeleton::BuildArrays (0x001aa700)
    // Allocates matrix arrays for N bones.
    void BuildArrays(int count);

    // Matches Skeleton::BuildLocalMatrices (0x00193064)
    // Converts per-bone TRS (quaternion + vec3 + mat3) → local Matrix44.
    // Sequence from disassembly: Mul44(scale_mat44, rotation_transpose, tmp); Mul44(tmp, translate, local)
    void BuildLocalMatrices();

    // Matches Skeleton::BuildFinalMatrices (0x00192e0c)
    // Computes world (parent-chain accumulation) and vert (world × bindPose) matrices.
    void BuildFinalMatrices();
};

} // namespace Mortar

#endif
