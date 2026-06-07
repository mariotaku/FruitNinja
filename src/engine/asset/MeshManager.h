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
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: MeshManager::Destroy -- auto stub from binary missing-symbol set
    void Destroy();
    // STUB: MeshManager::Find -- auto stub from binary missing-symbol set
    void Find(AsciiString const&) const;
    // STUB: MeshManager::Find -- auto stub from binary missing-symbol set
    void Find(SmartPtr<Model> const&) const;
    // STUB: MeshManager::InitialiseInternal -- auto stub from binary missing-symbol set
    void InitialiseInternal();
    // STUB: MeshManager::Release -- auto stub from binary missing-symbol set
    void Release(SmartPtr<Model> const&);
    // ---- end AUTO-STUB MERGE ----
};

} // namespace Mortar

#endif
