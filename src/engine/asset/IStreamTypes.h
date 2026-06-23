#ifndef MORTAR_ASSET_ISTREAMTYPES_H
#define MORTAR_ASSET_ISTREAMTYPES_H

// IVertexStream / IIndexStream -- binary types that represent loaded geometry
// stream data (vertex buffer and index buffer respectively).
//
// Binary: IVertexStream and IIndexStream are ReferenceCounter subclasses held in
// GeometryBinding_Bada::m_VertexStreams / m_IndexStream. Their vtables contain the
// stream-source interface (bind / stride / count / etc.) that PassBinding::Apply
// dispatches through. The PassBinding subsystem is structurally bypassed in the port
// (Geometry::Render draws from load-cached m_Vbo/m_Ibo/m_Layout, same fixed-function GLES1.x
// calls as PassBinding::Apply -- NOT a GLES2 shader path); layout-shape and ownership wiring matter here.
//
// Port: IVertexStream and IIndexStream are minimal ReferenceCounter subclasses that
// carry the GL buffer handles and layout metadata produced by the PSP stream parsers.
// They exist so LoaderHelper<IVertexStream> / LoaderHelper<IIndexStream> can be
// instantiated with the correct types (matching the binary's RegisterLoader calls in
// MeshManager::LoadMeshInternal @ 0x00238644) while keeping meshes loading correctly.
//
// LoadMesh (@ 0x0023890c) reconstructs a Geometry from the two stream SmartPtrs by
// copying the GL handles and layout out of them. The Geometry then owns the GPU data
// for rendering.
//
// Binary: IVertexStream @ GeometryBinding_Bada+0x10 (vector<SmartPtr<IVertexStream>>)
//         IIndexStream  @ GeometryBinding_Bada+0x1C (SmartPtr<IIndexStream>)

#include "util/ReferenceCounter.h"
#include "render/gl_funcs.h"
#include "asset/Geometry.h"

namespace Mortar {

// IVertexStream: carries a VBO handle and vertex layout metadata parsed from
// the PSP vertex declaration stream by LoadVertexStreamPSP.
// Binary: virtual base (vtable for stream interface). Port: minimal concrete subclass.
class IVertexStream : public ReferenceCounter {
public:
    GLuint      m_Vbo;
    int         m_VertCount;
    VertexLayout m_Layout;

    IVertexStream() : m_Vbo(0), m_VertCount(0) {
        memset(&m_Layout, 0, sizeof(m_Layout));
    }
    virtual ~IVertexStream() {}
};

// IIndexStream: carries an IBO handle, index count, and primitive type parsed from
// the PSP index stream by LoadIndexStreamPSP.
// Binary: virtual base (vtable for index stream interface). Port: minimal concrete subclass.
class IIndexStream : public ReferenceCounter {
public:
    GLuint m_Ibo;
    int    m_IndexCount;
    GLenum m_PrimType;

    IIndexStream() : m_Ibo(0), m_IndexCount(0), m_PrimType(GL_TRIANGLES) {}
    virtual ~IIndexStream() {}
};

}  // namespace Mortar

#endif  // MORTAR_ASSET_ISTREAMTYPES_H
