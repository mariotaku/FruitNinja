#ifndef MORTAR_MESH_MANAGER_H
#define MORTAR_MESH_MANAGER_H

#include "asset/Mesh.h"
#include "asset/Model.h"
#include "asset/ResourceLoader.h"
#include "util/SmartPtr.h"
#include "util/List.h"
#include <string>

namespace Mortar {

// Matches original MeshManager (20 bytes)
// List-based cache of loaded Models, accessed via GetInstance()
// Ref: docs/engine/texture-mesh-manager.md
class MeshManager {
public:
    MeshManager();
    ~MeshManager();

    static MeshManager* GetInstance() { return s_instance; }
    static MeshManager* s_instance;

    Mortar::SmartPtr<Model> Load(const char* path);
    void ReleaseAll();
    void Initialise(int capacity = 32);

private:
    List<Mortar::SmartPtr<Model>> m_Models;

    // Matches LoadMeshInternal (0x001a8518) + LoadModel (0x001a8468) + LoadMesh (0x001a7c90)
    Mortar::SmartPtr<Model> LoadMeshInternal(const char* path);

public:
    // TODO: 0x001929BC -- tear down the model cache (mirror of ReleaseAll + free instance state)
    void Destroy();
    // TODO: 0x00192BA8 -- look up a cached Model by name, return cached SmartPtr or null
    void Find(AsciiString const&) const;
    // TODO: 0x00192B54 -- locate a cached Model entry by handle
    void Find(SmartPtr<Model> const&) const;
    // TODO: 0x001A74B8 -- one-time internal cache/capacity setup invoked by Initialise
    void InitialiseInternal();
    // TODO: 0x00192B1C -- drop one Model's refcount and evict it from the cache when unreferenced
    void Release(SmartPtr<Model> const&);
};

} // namespace Mortar

#endif
