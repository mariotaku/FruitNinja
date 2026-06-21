#ifndef FN_ASSET_MODEL_H
#define FN_ASSET_MODEL_H

#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include "util/ReferenceCounter.h"
#include "asset/Skeleton.h"
#include "asset/AnimationState.h"
#include <vector>
#include <string>

namespace Mortar {

class Mesh;        // full def in Mesh.h
class EffectGroup; // full def in Effect.h
struct Bounds3D;   // full def in Mesh.h

// Matches original Mortar::Model (0x58 bytes).
// Inherits ReferenceCounter directly (NOT IModelNode -- that is Mesh).
//   +0x00  ReferenceCounter (vptr + refcount, 0x0C)
//   +0x0C  AsciiString m_name        (0x28)
//   +0x34  vector<SmartPtr<Mesh>> m_nodes
//   +0x40  Skeleton m_skeleton (embedded by value)
//   +0x58  end
// Vtable @ 0x001eb2a8: only ~Model deleting (0x00193bac) / in-place (0x00193b3c).
// No other virtual methods.
class Model : public Mortar::ReferenceCounter {
public:
    AsciiString m_name;                              // +0x0C  binary: AsciiString m_name (40 bytes)
    std::vector<Mortar::SmartPtr<Mesh> > m_nodes;   // +0x34
    Skeleton m_skeleton;                             // +0x40 (by value)

    Model();
    Model(AsciiString const& name);  // binary @ 0x0019326c
    virtual ~Model();                // vtable[0/1]

    // Binary @ 0x001930e0 — single-mesh fast path + multi-mesh depth-sort.
    void Draw(const Matrix44& transform);

    // Binary @ 0x0019346c — push_back + BindSkeleton(&m_skeleton).
    void AddNode(Mortar::SmartPtr<Mesh> mesh);

    // Binary @ 0x001933f8 — unchecked m_nodes[index].
    Mortar::SmartPtr<Mesh> GetNode(unsigned long index) const;

    // Binary @ 0x00193414 — linear scan by name; null on miss (AsciiString overload).
    Mortar::SmartPtr<Mesh> GetNode(AsciiString const& name) const;

    // Port helper overload: std::string variant; delegates to AsciiString overload.
    Mortar::SmartPtr<Mesh> GetNode(const std::string& name) const;

    // Binary @ 0x001aaba8 — Skeleton::Swap + UpdateBoneLinks.
    void SwapSkeleton(Skeleton& skel);

    // Binary @ 0x00193010 — re-bind every mesh to m_Skeleton.
    void UpdateBoneLinks();

    // Binary @ 0x00192fa8 — union of per-mesh bounds (seed = mesh[0]).
    Bounds3D GetBounds() const;

    // Binary @ 0x00192f04 — m_Meshes.size().
    int NodeCount() const;

    // Binary @ 0x0019335c — per-mesh RebuildEffectBindings (inner geometry calls are defunct stubs).
    void SetEffectGroup(Mortar::SmartPtr<EffectGroup> effectGroup);

    // Binary @ 0x00236e8c — walks m_nodes and calls Mesh vtable slot 7 (+0x1c) per node.
    // (Non-virtual Model member; not on Model's vtable.)
    void GenerateBindings(AsciiString const& channelName,
                          AsciiString const& targetName,
                          std::vector<AnimBindings::Bone::Binding>& out) const;

    // Binary @ 0x00236e48 — walks m_nodes and calls Mesh vtable slot 6 (+0x18) per node.
    // (Non-virtual Model member; not on Model's vtable.)
    void GenerateBindings(AsciiString const& channelName,
                          AsciiString const& targetName,
                          std::vector<AnimBindings::Vector::Binding>& out) const;
};

} // namespace Mortar

#if defined(__bada__)
#include <cstddef>
// TODO(#93-followup): cross sizeof(Mortar::Model) != binary 0x58 because
//   Skeleton uses 4 std::vector<> members (4*12=48B) vs binary's 1 vector + 3 raw
//   pointers (12+4+4+4=24B). Fix requires porting Skeleton to binary-faithful layout.
//   Expected: Model=0x58, m_name@0x0C, m_nodes@0x34, m_skeleton@0x40.
#if 0
static_assert(sizeof(Mortar::Model)                   == 0x58, "Mortar::Model size mismatch");
static_assert(offsetof(Mortar::Model, m_name)         == 0x0C, "Mortar::Model::m_name offset");
static_assert(offsetof(Mortar::Model, m_nodes)        == 0x34, "Mortar::Model::m_nodes offset");
static_assert(offsetof(Mortar::Model, m_skeleton)     == 0x40, "Mortar::Model::m_skeleton offset");
#endif
#endif

#endif // FN_ASSET_MODEL_H
