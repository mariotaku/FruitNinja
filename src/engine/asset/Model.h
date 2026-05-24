#ifndef FN_ASSET_MODEL_H
#define FN_ASSET_MODEL_H

#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include "util/ReferenceCounter.h"
#include "asset/Skeleton.h"
#include <vector>
#include <string>

namespace Mortar {

class Mesh;        // full def in Mesh.h
class EffectGroup; // full def in Effect.h
struct Bounds3D;   // full def in Mesh.h

// Matches original Mortar::Model (0x58 bytes).
// Inherits ReferenceCounter directly (NOT IModelNode -- that is Mesh).
//   +0x00  ReferenceCounter (vptr + refcount, 0x0C)
//   +0x0C  AsciiString m_Name        (0x28)
//   +0x34  vector<SmartPtr<Mesh>> m_Meshes
//   +0x40  Skeleton m_Skeleton (embedded by value)
//   +0x58  end
// Vtable @ 0x001eb2a8: only ~Model deleting (0x00193bac) / in-place (0x00193b3c).
// No other virtual methods.
class Model : public Mortar::ReferenceCounter {
public:
    std::string m_Name;                             // +0x0C
    std::vector<Mortar::SmartPtr<Mesh> > m_Meshes;  // +0x34
    Skeleton m_Skeleton;                            // +0x40 (by value)

    Model();
    Model(AsciiString const& name);  // binary @ 0x0019326c
    virtual ~Model();                // vtable[0/1]

    // Binary @ 0x001930e0 — single-mesh fast path + multi-mesh depth-sort.
    void Draw(const Matrix44& transform);

    // Binary @ 0x0019346c — push_back + BindSkeleton(&m_Skeleton).
    void AddNode(Mortar::SmartPtr<Mesh> mesh);

    // Binary @ 0x001933f8 — unchecked m_Meshes[index].
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

    // Binary @ 0x00192f10 — per-mesh GenerateBindings forward (Bone slot).
    // Blocked on AnimBindings::Bone::Binding declaration; declare when porting AnimBindings.
    // TODO: 0x00192f10 -- GenerateBindings(AsciiString const&, AsciiString const&, vector<AnimBindings::Bone::Binding>&) const

    // Binary @ 0x00192f5c — per-mesh GenerateBindings forward (Vector slot).
    // Blocked on AnimBindings::Vector::Binding declaration; declare when porting AnimBindings.
    // TODO: 0x00192f5c -- GenerateBindings(AsciiString const&, AsciiString const&, vector<AnimBindings::Vector::Binding>&) const
};

} // namespace Mortar

#endif // FN_ASSET_MODEL_H
