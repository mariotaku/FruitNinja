#ifndef RENDERER_H
#define RENDERER_H

#include "gl_funcs.h"
#include "tex_loader.h"

static const int FN_SCREEN_W = 480;
static const int FN_SCREEN_H = 320;

struct Mesh;

struct Renderer {
    // 2D sprite shader
    GLuint program;
    GLint u_tex_loc;
    GLint u_alpha_loc;

    // 3D mesh shader
    GLuint program_3d;
    GLint u3d_mvp;
    GLint u3d_model;
    GLint u3d_light_dir;
    GLint u3d_tex;
    GLint u3d_alpha;

    bool init();
    void shutdown();

    GLuint upload_texture(const TexImage& img);

    // Draw a textured quad filling the entire screen
    void draw_fullscreen_quad(GLuint tex, float alpha = 1.0f);

    // Draw a textured sprite at game coordinates (0,0 = bottom-left)
    void draw_sprite(GLuint tex, float x, float y, float w, float h,
                     float angle = 0.0f, float alpha = 1.0f);

    // Draw a 3D mesh with MVP and model matrices
    void draw_mesh(Mesh& mesh, GLuint tex, const float* mvp, const float* model,
                   float alpha = 1.0f);
};

#endif
