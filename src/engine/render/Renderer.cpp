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

Renderer* Renderer::s_instance = nullptr;

bool Renderer::init() {
    s_instance = this;
    return true;
}

void Renderer::shutdown() {
    // Nothing owned — FF pipeline has no allocated programs.
}

// Matches SetPerspective (binary 0x00181e00, anonymous namespace).
// Binary uses a 91-entry LUT of precomputed tan(fov/2) values keyed by
// the rounded integer fov (clamped to [0, 90]); we call tanf directly —
// same result modulo float precision.
//   fov    — vertical field of view in degrees, clamped to [0, 90]
//   aspect — width/height
//   near, far — depth range (frustum planes)
//
// halfH = near * tan(fov/2);  halfW = halfH * aspect
// glFrustumf(-halfW, halfW, -halfH, halfH, near, far)
static void SetPerspective(float fov, float aspect, float nearPlane, float farPlane) {
    int ifov = (int)(fov + 0.5f);
    if (ifov < 0) ifov = 0;
    if (ifov > 90) ifov = 90;
    const float halfH = nearPlane * tanf((float)ifov * (3.14159265358979f / 360.0f));
    const float halfW = halfH * aspect;
    glFrustumf(-halfW, halfW, -halfH, halfH, nearPlane, farPlane);
}

// Matches FruitNinja::InitGL (0x00181e54). Runs once at startup after
// the GL context exists, establishes baseline state. The binary's EGL
// Pbuffer path is omitted — SDL handles the surface.
//
// The enables here (DEPTH_TEST, CULL_FACE) and the SetPerspective seed
// all get flipped/overwritten by BeginFrame's glDisable + glLoadIdentity
// on the first frame — so they're effectively one-time hints to the
// driver. Mirrored anyway for strict 1:1 fidelity with the binary.
void Renderer::InitGL(int width, int height) {
    glShadeModel(GL_SMOOTH);
    glViewport(0, 0, width, height);
    glEnable(GL_CULL_FACE);
    // glCullFace(GL_BACK) — GL default, binary sets it explicitly but we
    // don't load glCullFace (binary never changes the face away from
    // GL_BACK anywhere else), so we rely on the default.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // DAT_00181f40 = 60.0f (fov), DAT_00181f44 = 400.0f (far).
    // near = 1.0f literal; aspect = width / height.
    const float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    SetPerspective(60.0f, aspect, 1.0f, 400.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::SetupGameOrtho() {
    // Verified from binary: SetupOrtho(160, -160, -240, 240, 2000, -6000)
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);
    mm.GetViewStack().Reset();
    mm.GetWorldStack().Reset();
}

// Shared FF setup for a textured 2D draw with a uniform byte-colour tint.
// Uploads MVP, binds texture, sets GL_MODULATE so colour * texel is the
// final fragment colour. Caller handles client-array setup + draw call.
static void SetupFF2D(const float* mvp, GLuint tex, const Colour& tint) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    if (tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    glDisable(GL_LIGHTING);
    glColor4ub(tint.r, tint.g, tint.b, tint.a);
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

    SetupFF2D(identity.ptr(), tex,
              Colour(255, 255, 255, (uint8_t)(alpha * 255.0f)));

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

    // Caller already bound the texture via Texture::Set (which
    // glBindTexture's to TEXTURE_2D unit 0); re-enable TEXTURE_2D and
    // set the texenv mode / tint without re-binding.
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(mvp.ptr());
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
    glDisable(GL_LIGHTING);
    glColor4ub(tint.r, tint.g, tint.b, tint.a);

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
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
    glDisable(GL_LIGHTING);
    glColor4ub(255, 255, 255, 255);  // vertex colour wins via COLOR_ARRAY

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
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
    glDisable(GL_LIGHTING);
    glColor4ub(255, 255, 255, 255);

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
