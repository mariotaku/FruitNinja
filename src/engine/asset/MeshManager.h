#ifndef MORTAR_MESH_MANAGER_H
#define MORTAR_MESH_MANAGER_H

#include "asset/Mesh.h"
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

    SmartPtr<Model> Load(const char* path);
    void ReleaseAll();
    void Initialise(int capacity = 32);

private:
    List<SmartPtr<Model>> m_Models;

    // Matches LoadMeshInternal (0x001a8518) + LoadModel (0x001a8468) + LoadMesh (0x001a7c90)
    SmartPtr<Model> LoadMeshInternal(const char* path);
};

} // namespace Mortar

#endif
