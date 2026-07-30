#ifndef MORTAR_MESH_MANAGER_H
#define MORTAR_MESH_MANAGER_H

#include "asset/Mesh.h"
#include "asset/Model.h"
#include "asset/ResourceLoader.h"
#include "util/SmartPtr.h"
#include "util/List.h"

namespace Mortar {

// MeshManager -- v1.6.1 ctor @0x002368cc, dtor @0x002368b8
// 20-byte body: single List<SmartPtr<Model>> m_Models field (List = 20 bytes).
// Singleton accessed via GetInstance().
class MeshManager {
public:
    MeshManager();
    ~MeshManager();

    static MeshManager* GetInstance() { return s_instance; }
    static MeshManager* s_instance;

    // v1.6.1 Load @0x00236874
    // Binary mangled: _ZN6Mortar11MeshManager4LoadERKNS_11AsciiStringE -- takes
    // AsciiString const&. AsciiString(const char*) is non-explicit so callers
    // passing string literals still compile unchanged.
    Mortar::SmartPtr<Model> Load(const AsciiString& path);
    // v1.6.1 ReleaseAll @0x0023689c -- GOT-thunk tail-call to List<SmartPtr<Model>>::Clear.
    void ReleaseAll();
    void Initialise(int capacity = 32);

private:
    List<Mortar::SmartPtr<Model>> m_Models;

    // v1.6.1 MeshManager::LoadMeshInternal @0x00238644 -- thin dispatcher that registers
    // IVertexStream/IIndexStream/Model/Mesh loaders + calls ResourceLoader::Load<Model>(path).
    // Does NOT touch m_Models directly.
    // DIFFERS: port caches in m_Models manually; binary caches in ResourceLoader.
    Mortar::SmartPtr<Model> LoadMeshInternal(const AsciiString& path);

public:
    // v1.6.1 Mortar::MeshManager::~MeshManager @0x002368a4 (D1/D2 aliased) -- calls Destroy (-> ReleaseAll -> List::Clear) then List::Destroy.
    void Destroy();
    // v1.6.1 Find(AsciiString const&) @0x0023695c -- linear scan; returns the cached Model
    // whose m_name matches, else an empty SmartPtr.
    Mortar::SmartPtr<Model> Find(AsciiString const& name) const;
    // v1.6.1 Find(SmartPtr<Model> const&) @0x002369c0 -- linear scan by pointer identity;
    // returns the matching cached entry, else an empty SmartPtr.
    Mortar::SmartPtr<Model> Find(SmartPtr<Model> const& model) const;
    // v1.6.1 MeshManager::InitialiseInternal @0x00238198 -- empty in the binary (bare 'bx lr').
    void InitialiseInternal();
    // v1.6.1 Release(SmartPtr<Model>) @0x00236908 -- calls List::Remove to unlink
    // and drop refcount on the matching node.
    void Release(SmartPtr<Model> model);
};

} // namespace Mortar

#endif
