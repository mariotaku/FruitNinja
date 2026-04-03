#ifndef MESH_H
#define MESH_H

#include "gl_funcs.h"
#include <string>
#include <vector>
#include <cstdint>

struct Renderer;

// Interleaved vertex for our GLES2 pipeline
struct MeshVertex {
    float px, py, pz;     // position
    float nx, ny, nz;     // normal
    uint8_t r, g, b, a;   // color
    float u, v;            // texcoord
};

struct Mesh {
    GLuint vbo;
    GLuint ibo;
    int index_count;
    GLenum prim_type;

    Mesh();

    // Load from .mmd file. atlas_tex is the pre-loaded fruit_atlas texture.
    bool load(const std::string& mmd_path);
    void destroy();

    // Draw using the 3D shader. Caller must set up uniforms (MVP, model, etc).
    void draw();
};

#endif
