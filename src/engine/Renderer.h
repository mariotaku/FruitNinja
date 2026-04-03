#ifndef RENDERER_H
#define RENDERER_H

#include "gl_funcs.h"
#include "tex_loader.h"
#include "MatrixManager.h"
#include "Colour.h"

static const int FN_SCREEN_W = 480;
static const int FN_SCREEN_H = 320;

struct Mesh;

struct Renderer {
    // 2D sprite shader (with MVP projection)
    GLuint program;
    GLint u_mvp;
    GLint u_tex_loc;
    GLint u_tint;

    // 3D mesh shader
    GLuint program_3d;
    GLint u3d_mvp;
    GLint u3d_model;
    GLint u3d_light_dir;
    GLint u3d_tex;
    GLint u3d_alpha;

    // Matrix manager (matches Mortar engine)
    MatrixManager matrix_mgr;

    bool init();
    void shutdown();

    GLuint upload_texture(const TexImage& img);

    // Setup ortho projection matching original game
    // Verified constants: left=160, right=-160, bottom=-240, top=240
    void SetupGameOrtho();

    // Draw a textured quad filling the entire screen (legacy, clip space)
    void draw_fullscreen_quad(GLuint tex, float alpha = 1.0f);

    // Matches DrawQuadUnCached — draws unit quad transformed by current matrix stack
    void DrawQuad(const Colour& tint, float u0 = 0.0f, float v0 = 0.0f,
                  float u1 = 1.0f, float v1 = 1.0f);

    // Draw a textured sprite at game coordinates
    void draw_sprite(GLuint tex, float x, float y, float w, float h,
                     float angle = 0.0f, float alpha = 1.0f);

    // Draw a 3D mesh with MVP and model matrices
    void draw_mesh(Mesh& mesh, GLuint tex, const float* mvp, const float* model,
                   float alpha = 1.0f);
};

#endif
