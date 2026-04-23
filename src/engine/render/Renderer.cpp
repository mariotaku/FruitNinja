#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include <cstdio>
#include <cmath>

// Fixed-function renderer. Every draw uploads MVP to GL_PROJECTION and
// leaves GL_MODELVIEW as identity — since MatrixManager::GetMVP already
// composes proj * view * world, the result is the same and we save a
// matmul. When we eventually need per-vertex lighting (IsLit=true),
// this shortcut will need unwinding into proper modelview / projection.
//
// Sprite/quad helpers use glColor4f for the tint, GL_MODULATE texenv,
// and client-array vertex/UV streams drawn as GL_TRIANGLE_STRIP.

Renderer* Renderer::s_instance = NULL;

bool Renderer::init() {
    s_instance = this;
    return true;
}

void Renderer::shutdown() {
    // Nothing owned — FF pipeline has no allocated programs.
}

void Renderer::SetupGameOrtho() {
    // Verified from binary: SetupOrtho(160, -160, -240, 240, 2000, -6000)
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
}

// Shared FF setup for a textured 2D draw with a uniform colour tint.
// Uploads MVP, binds texture, sets GL_MODULATE so colour * texel is the
// final fragment colour. Caller handles client-array setup + draw call.
static void SetupFF2D(const float* mvp, GLuint tex,
                      float r, float g, float b, float a) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    if (tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
}

void Renderer::draw_fullscreen_quad(GLuint tex, float alpha) {
    // Clip-space fullscreen quad, identity MVP.
    static const float verts[] = {
        // pos(xyz)       uv
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
    };
    Matrix44 identity;

    SetupFF2D(identity.ptr(), tex, 1.0f, 1.0f, 1.0f, alpha);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 20, verts);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 20, verts + 3);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void Renderer::DrawQuad(const Colour& tint, float u0, float v0, float u1, float v1) {
    // Unit quad (-0.5..0.5) transformed by current matrix stack MVP.
    float verts[] = {
        // pos(xyz)          uv
        -0.5f, -0.5f, 0.0f,  u0, v1,
         0.5f, -0.5f, 0.0f,  u1, v1,
        -0.5f,  0.5f, 0.0f,  u0, v0,
         0.5f,  0.5f, 0.0f,  u1, v0,
    };
    Matrix44 mvp = Mortar::MatrixManager::GetInstance().GetMVP();
    float t[4];
    tint.toFloat(t);

    // Caller already bound the texture via Texture::Set (which glBindTexture's
    // to TEXTURE_2D unit 0). Pass 0 here so SetupFF2D doesn't re-bind —
    // but we still need GL_TEXTURE_2D enabled and the env mode set.
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp.ptr());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_LIGHTING);
    glColor4f(t[0], t[1], t[2], t[3]);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 20, verts);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 20, verts + 3);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void Renderer::draw_sprite(GLuint tex, float x, float y, float w, float h,
                           float angle, float alpha) {
    Mortar::MatrixStack& stack = Mortar::MatrixManager::GetInstance().GetWorldStack();
    stack.Reset();
    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    if (angle != 0.0f) {
        mat.RotZ44(sinf(angle), cosf(angle));
    }
    mat.GlobalTranslate44(Vec3(x + w * 0.5f, y + h * 0.5f, 0.0f));
    stack.SetCurrentMatrix(mat);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    DrawQuad(Colour(255, 255, 255, (uint8_t)(alpha * 255.0f)));
}

// Matches DrawTriList (0x00193f5c) — QUADCUSTOMVERTEX stride 0x24 with
// per-vertex RGBA colour in `verts->colour` (packed BGRA uint32).
void Renderer::DrawTriList(QUADCUSTOMVERTEX* verts, int vertCount) {
    Matrix44 mvp = Mortar::MatrixManager::GetInstance().GetMVP();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp.ptr());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);  // vertex colour wins via COLOR_ARRAY

    const int stride = sizeof(QUADCUSTOMVERTEX);  // 36
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, &verts->x);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, &verts->u);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, &verts->colour);
    glDisableClientState(GL_NORMAL_ARRAY);

    glDrawArrays(GL_TRIANGLES, 0, vertCount);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

void Renderer::DrawTriStrip(QUADCUSTOMVERTEX* verts, int vertCount) {
    Matrix44 mvp = Mortar::MatrixManager::GetInstance().GetMVP();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp.ptr());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    const int stride = sizeof(QUADCUSTOMVERTEX);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, &verts->x);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, &verts->u);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, &verts->colour);
    glDisableClientState(GL_NORMAL_ARRAY);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, vertCount);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}
