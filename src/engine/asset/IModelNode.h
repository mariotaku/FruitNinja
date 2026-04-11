#ifndef MORTAR_ASSET_IMODELNODE_H
#define MORTAR_ASSET_IMODELNODE_H

// Analysed: 2026-04-11T18:30

#include "util/ReferenceCounter.h"
#include "asset/Skeleton.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"
#include <string>

namespace Mortar {

// Matches Mortar::IModelNode (virtual, 0x0C = same size as ReferenceCounter)
// Ref: Mesh vtable @ 0x001ebdd8 (11 entries), IModelNode ctor @ 0x001b1fd8
//
// Original inheritance: ReferenceCounter → IModelNode → Mesh
// IModelNode adds no data fields — only virtual methods.
//
// Vtable layout:
//   [0] ~Mesh deleting   [1] ~Mesh complete   [2] GetRefCounter (ReferenceCounter)
//   [3] GetName          [4] Draw             [5] GetBounds
//   [6] GenerateBindings [7] GenerateBindings (stub)
//   [8] BindSkeleton     [9] GetGeometryCount [10] GetGeometry
class IModelNode : public ReferenceCounter {
public:
    virtual ~IModelNode() {}

    // vtable[3]: const AsciiString& GetName() const
    // Port: std::string instead of AsciiString
    virtual const std::string& GetName() const = 0;

    // vtable[4]: void Draw(const Matrix44& worldMatrix)
    virtual void Draw(const Matrix44& worldMatrix) = 0;

    // vtable[5]: Bounds3D GetBounds() const
    // Port: split into outMin/outMax ref params (no Bounds3D struct)
    virtual void GetBounds(Vec3& outMin, Vec3& outMax) const = 0;

    // vtable[6,7]: void GenerateBindings(...)
    // Port: stub — AnimBindings not yet implemented
    virtual void GenerateBindings() {}

    // vtable[8]: void BindSkeleton(const Skeleton& skel)
    // Port: takes Skeleton* (pointer; skeleton owned by Model)
    virtual void BindSkeleton(Skeleton* skeleton) = 0;

    // vtable[9]: uint GetGeometryCount() const
    virtual int GetGeometryCount() const = 0;

    // vtable[10]: SmartPtr<Geometry> GetGeometry(ulong idx) const
    // Port: omitted — Geometry class replaced by GeometryEntry; nothing uses this polymorphically.
    // Mesh provides GetGeometryEntry(idx) directly.
};

} // namespace Mortar

#endif
