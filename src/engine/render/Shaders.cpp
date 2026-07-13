// Port specific: GLES2 shader sources for the Renderer 2D path.
// See Shaders.h for the contract (attribute slots, uniforms, precision gate).

#include "render/Shaders.h"

namespace FnShaders {

const char* Quad2D_VS =
    "uniform mat4 u_mvp;\n"
    "attribute vec3 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec4 a_color;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    "void main(){\n"
    "    gl_Position = u_mvp * vec4(a_pos,1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

const char* Quad2D_FS =
    // FRUIT_GL_API_GL_COMPAT is the only desktop branch of the gl_compat.h
    // ladder; every other backend (EMSCRIPTEN / ES1 / default GLES2 / bada
    // stub) is ES-flavoured and wants an explicit default precision.
#if !defined(FRUIT_GL_API_GL_COMPAT)
    "precision highp float;\n"
#endif
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "varying vec4 v_color;\n"
    // Colour::PlatformColour() packs bytes as [r,g,b,a] (little-endian:
    // r in the low byte, a in the high byte -- see engine/math/Colour.h).
    // GL_UNSIGNED_BYTE vertex attribs read those 4 bytes in memory order,
    // so a_color already arrives as RGBA -- no swizzle needed to match
    // the old glColorPointer(GL_UNSIGNED_BYTE) fixed-function behaviour.
    "void main(){\n"
    "    gl_FragColor = texture2D(u_tex, v_uv) * v_color;\n"
    "}\n";

// 3D mesh program (Geometry::Render). Structurally identical to the 2D
// program -- lighting is off for every mesh (IsLit=false), so the unlit
// texture2D * v_color modulate IS the fixed-function output. Aliased to
// the 2D sources; split into real separate strings only if a lit path
// ever lands.
const char* Mesh3D_VS = Quad2D_VS;
const char* Mesh3D_FS = Quad2D_FS;

}
