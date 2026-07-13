#ifndef FN_RENDER_SHADERS_H
#define FN_RENDER_SHADERS_H

// Port specific: GLES2 shader sources, no binary counterpart (the binary
// renders through the ES 1.x fixed-function pipeline).
//
// One 2D program for the Renderer quad/UI path (DrawQuad, DrawTriList,
// DrawTriStrip, DrawColorQuad, draw_fullscreen_quad):
//   fragment = texture2D(u_tex, v_uv) * vertex colour
// which reproduces the fixed-function GL_MODULATE texenv these draws used.
//
// GLSL is ES 1.00 / desktop 1.10 compatible -- no #version line. The
// fragment source carries "precision highp float;" on every ES-flavoured
// backend of the gl_compat.h ladder and omits it on FRUIT_GL_API_GL_COMPAT
// (desktop GLSL 1.10 rejects precision qualifiers).
//
// Attribute locations are fixed by ShaderProgram::Compile via
// glBindAttribLocation before link:
//   0 = a_pos (vec3), 1 = a_uv (vec2), 2 = a_color (vec4, normalized ubyte)
// Uniforms: u_mvp (mat4), u_tex (sampler2D, unit 0).

namespace FnShaders {

extern const char* Quad2D_VS;
extern const char* Quad2D_FS;

}

#endif
