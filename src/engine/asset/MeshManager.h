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
    // Binary @ 0x001929BC -- tail-calls ReleaseAll().
    void Destroy();
    // Binary @ 0x00192BA8 -- linear scan; returns the cached Model whose m_name
    // matches, else an empty SmartPtr.
    Mortar::SmartPtr<Model> Find(AsciiString const& name) const;
    // Binary @ 0x00192B54 -- linear scan by handle (SmartPtr ==); returns the
    // matching cached entry, else an empty SmartPtr.
    Mortar::SmartPtr<Model> Find(SmartPtr<Model> const& model) const;
    // Binary @ 0x001A74B8 -- empty in the binary (one-time hook, no body).
    void InitialiseInternal();
    // Binary @ 0x00192B1C -- if the handle is valid, remove it from the cache
    // (List::Remove), dropping its refcount.
    void Release(SmartPtr<Model> const& model);
};

} // namespace Mortar

#endif
