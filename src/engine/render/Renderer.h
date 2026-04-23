#ifndef MORTAR_RENDERER_H
#define MORTAR_RENDERER_H

#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/MatrixManager.h"
#include "math/Colour.h"
#include "core/MortarTypes.h"

// Fixed-function GL renderer — targets the same pipeline as the binary
// (ES 1.x / desktop-GL compatibility profile). No shaders, no attrib
// locations, no programs. Every draw call goes through
// glMatrixMode / glLoadMatrixf / glEnableClientState / glVertexPointer
// and friends.
struct Renderer {
    static Renderer* s_instance;
    static Renderer* GetInstance() { return s_instance; }

    bool init();
    void shutdown();

    // Setup ortho projection matching original game.
    // Verified constants: top=160, bottom=-160, left=-240, right=240.
    void SetupGameOrtho();

    // Draw a textured quad filling the entire screen (clip space).
    void draw_fullscreen_quad(GLuint tex, float alpha = 1.0f);

    // Matches DrawQuadUnCached — draws unit quad transformed by current MVP.
    void DrawQuad(const Colour& tint, float u0 = 0.0f, float v0 = 0.0f,
                  float u1 = 1.0f, float v1 = 1.0f);

    // Draw a textured sprite at game coordinates.
    void draw_sprite(GLuint tex, float x, float y, float w, float h,
                     float angle = 0.0f, float alpha = 1.0f);

    // Path B rendering with QUADCUSTOMVERTEX (stride 0x24).
    // Matches original DrawTriList (0x00193f5c) / DrawTriStrip.
    void DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount);
    void DrawTriStrip(QUADCUSTOMVERTEX* verts, int vertCount);
};

#endif
