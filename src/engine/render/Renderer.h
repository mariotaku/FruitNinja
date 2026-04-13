#ifndef MORTAR_RENDERER_H
#define MORTAR_RENDERER_H

#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"

struct TexImage;

struct Renderer {
    static Renderer* s_instance;
    static Renderer* GetInstance() { return s_instance; }

    // 2D sprite shader (MVP + uniform tint)
    GLuint program;
    GLint u_mvp;
    GLint u_tex_loc;
    GLint u_tint;

    // 3D mesh shader (MVP + model + lighting + vertex color + material colours)
    GLuint program_3d;
    GLint u3d_mvp;
    GLint u3d_model;
    GLint u3d_light_dir;
    GLint u3d_tex;
    GLint u3d_alpha;
    // Material colour uniforms (from EffectPropertyList: Ambience, Diffuse, SelfIllum, IsLit)
    // Ref: docs/engine/effect-system.md — LoadMesh material properties
    GLint u3d_ambient;
    GLint u3d_diffuse;
    GLint u3d_selfillum;
    GLint u3d_is_lit;

    // 2D vertex-color shader (MVP + per-vertex colour from QUADCUSTOMVERTEX)
    GLuint program_vc;
    GLint uvc_mvp;
    GLint uvc_tex;

    bool init();
    void shutdown();

    GLuint upload_texture(const TexImage& img);

    // Setup ortho projection matching original game
    // Verified constants: top=160, bottom=-160, left=-240, right=240
    void SetupGameOrtho();

    // Draw a textured quad filling the entire screen (legacy, clip space)
    void draw_fullscreen_quad(GLuint tex, float alpha = 1.0f);

    // Matches DrawQuadUnCached — draws unit quad transformed by current world stack MVP
    void DrawQuad(const Colour& tint, float u0 = 0.0f, float v0 = 0.0f,
                  float u1 = 1.0f, float v1 = 1.0f);

    // Draw a textured sprite at game coordinates
    void draw_sprite(GLuint tex, float x, float y, float w, float h,
                     float angle = 0.0f, float alpha = 1.0f);

    // Set up 3D shader with MVP, model matrix, lighting, texture, alpha, and material colours.
    // Vertex color (GL_MODULATE: texture × vertex_color) is bound per-draw from VBO.
    // Ref: EffectPropertyList properties (Ambience, Diffuse, SelfIllum, IsLit) — effect-system.md
    void setup_3d_shader(GLuint tex, const float* mvp, const float* model, float alpha,
                         const float* ambient, const float* diffuse, const float* selfillum,
                         bool isLit);

    // Path B rendering with QUADCUSTOMVERTEX (stride 0x24)
    // Matches original DrawTriList (0x00193f5c) / DrawTriStrip
    void DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount);
    void DrawTriStrip(QUADCUSTOMVERTEX* verts, int vertCount);
};

#endif
