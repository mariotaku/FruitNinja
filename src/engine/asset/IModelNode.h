#ifndef MORTAR_ASSET_IMODELNODE_H
#define MORTAR_ASSET_IMODELNODE_H

// Analysed: 2026-04-11T18:30

#include "util/ReferenceCounter.h"
#include "asset/Skeleton.h"
#include "asset/AnimationState.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"
#include <vector>

namespace Mortar {

// Matches Mortar::IModelNode (virtual, 0x0C = same size as Mortar::ReferenceCounter)
// Ref: Mesh vtable @ 0x002d0f30 (11 entries), IModelNode ctor @ 0x001b1fd8
//
// Original inheritance: Mortar::ReferenceCounter -> IModelNode -> Mesh
// IModelNode adds no data fields -- only virtual methods.
//
// Vtable layout:
//   [0] ~Mesh deleting   [1] ~Mesh complete   [2] GetRefCounter (Mortar::ReferenceCounter)
//   [3] GetName          [4] Draw             [5] GetBounds
//   [6] GenerateBindings(Vector) @ slot +0x18 -- Mesh::GenerateBindings(Vector) @ 0x0027350c
//   [7] GenerateBindings(Bone)   @ slot +0x1c -- Mesh::GenerateBindings(Bone)   @ 0x0027385c
//   [8] BindSkeleton     [9] GetGeometryCount [10] GetGeometry

// Forward-declare; concrete struct lives in Mesh.h.
struct Bounds3D;

class IModelNode : public Mortar::ReferenceCounter {
public:
    virtual ~IModelNode() {}

    // vtable[3]: const AsciiString& GetName() const
    virtual const AsciiString& GetName() const = 0;

    // vtable[4]: void Draw(const Matrix44& worldMatrix)
    virtual void Draw(const Matrix44& worldMatrix) = 0;

    // vtable[5]: Bounds3D GetBounds() const
    // Binary signature is value-return Bounds3D (struct-return via hidden r0 retval ptr).
    virtual Bounds3D GetBounds() const = 0;

    // vtable[6] (+0x18): GenerateBindings(Vector) -- Mesh override @ 0x0027350c
    // Walks mesh channel groups and fills out with channel->EffectProperty bindings.
    // EffectProperty path is defunct-stubbed so Mesh produces zero bindings (correct).
    virtual void GenerateBindings(AsciiString const& channelName,
                                  AsciiString const& targetName,
                                  std::vector<AnimBindings::Vector::Binding>& out) {}

    // vtable[7] (+0x1c): GenerateBindings(Bone) -- Mesh override @ 0x0027385c (empty BX LR)
    // Bone path never produces bindings; Mesh body is an empty stub.
    virtual void GenerateBindings(AsciiString const& channelName,
                                  AsciiString const& targetName,
                                  std::vector<AnimBindings::Bone::Binding>& out) {}

    // vtable[8]: void BindSkeleton(Skeleton* skeleton)
    virtual void BindSkeleton(Skeleton* skeleton) = 0;

    // vtable[9]: int GetGeometryCount() const
    virtual int GetGeometryCount() const = 0;

    // vtable[10]: Mortar::SmartPtr<Geometry> GetGeometry(ulong idx) const
    // Port: omitted from the virtual interface -- nothing dispatches through IModelNode
    // polymorphically for this. Mesh::GetGeometryEntry(idx) is the direct accessor.
};

} // namespace Mortar

#endif
