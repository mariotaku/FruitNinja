#ifndef RENDERER_H
#define RENDERER_H

#include "gl_funcs.h"
#include "tex_loader.h"

static const int FN_SCREEN_W = 480;
static const int FN_SCREEN_H = 320;

struct Renderer {
    GLuint program;
    GLint u_tex_loc;
    GLint u_alpha_loc;

    bool init();
    void shutdown();

    GLuint upload_texture(const TexImage& img);

    // Draw a textured quad filling the entire screen
    void draw_fullscreen_quad(GLuint tex, float alpha = 1.0f);

    // Draw a textured sprite at game coordinates (0,0 = bottom-left, 320x480)
    // angle in radians, rotation around sprite center
    void draw_sprite(GLuint tex, float x, float y, float w, float h,
                     float angle = 0.0f, float alpha = 1.0f);
};

#endif
