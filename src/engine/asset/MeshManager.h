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

    // Load a model from .mmd file
    // Returns existing if already loaded, otherwise parses HBR0 container
    SmartPtr<Model> Load(const char* path);

    // Release all cached models
    void ReleaseAll();

    // Initialize (reserve capacity)
    void Initialise(int capacity = 32);

private:
    List<SmartPtr<Model>> m_Models;

    // Internal: parse HBR0 container and build Model
    SmartPtr<Model> LoadMeshInternal(const char* path);

    // Parse vertex stream from raw data buffer
    // Matches LoadVertexStreamPSP (0x001a7b0c)
    bool TryParseVertexStream(const uint8_t* data, size_t dataSize, Mesh& mesh);

    // Parse index stream from raw data buffer
    // Matches LoadIndexStreamPSP (0x001a799c)
    // On success, `consumed` is set to the number of bytes used
    bool TryParseIndexStream(const uint8_t* data, size_t dataSize,
                              Mesh& mesh, size_t& consumed);
};

} // namespace Mortar

#endif
