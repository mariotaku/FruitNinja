#include "render/DisplayManager.h"
#include <SDL.h>
#include <cstring>

namespace Mortar {

DisplayManager::DisplayManager()
    : m_ClearColor(0, 0, 0, 255)
    , m_DrawColor(255, 255, 255, 255)
    , m_GlobalAmbience(0, 0, 0, 255)
    , m_bRenderingActive(false)
    , m_bSwapPending(0)
    , m_MagFilterMode(1)
    , m_MinFilterMode(1)
    , m_WrapSMode(1)
    , m_WrapTMode(1)
{
    m_WindowRect.left = 0;
    m_WindowRect.top = 0;
    m_WindowRect.right = FN_SCREEN_W;
    m_WindowRect.bottom = FN_SCREEN_H;
    m_lightDirection = Vec3(0.4f, 0.7f, 0.6f);
    memset(m_TextureOverloadPrefix, 0, sizeof(m_TextureOverloadPrefix));
    m_ScreenRotationMatrix.Identity();
}

// Matches DisplayManagerBada::BeginFrame (0x0019dfec) — GL call order
// is a strict 1:1 port of the binary, including the redundant second
// glEnable(GL_BLEND) / glDisable(GL_CULL_FACE) and the dual clearColor.
void DisplayManager::BeginFrame() {
    glEnable(GL_BLEND);                                   // 0xb57
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);                 // DAT_0019e124 = 0
    glDisable(GL_CULL_FACE);                              // 0xb44
    glDisable(GL_DEPTH_TEST);                             // 0xb71
    glDepthFunc(GL_LESS);                                 // 0x201
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);                 // DAT_0019e128 = 255 (clamps to 1)
    glClearDepthf(1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);    // 0x302, 0x303
    glDisable(GL_LIGHTING);                               // 0xb50
    glDisable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);                          // 0x1701
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);                           // 0x1700
    glLoadIdentity();

    m_bRenderingActive = true;
}

void DisplayManager::EndFrame() {
    m_bRenderingActive = false;
}

void DisplayManager::SwapBuffers(void* window) {
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window));
    m_bSwapPending = m_bSwapPending ? 0 : 1;
}

void DisplayManager::SetDrawColour(const Colour& c) {
    m_DrawColor = c;
}

void DisplayManager::SetDepthBuffer(bool enable) {
    if (enable) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void DisplayManager::SetDepthBufferWrite(bool enable) {
    glDepthMask(enable ? GL_TRUE : GL_FALSE);
}

void DisplayManager::SetGlobalAmbience(const Colour& c) {
    m_GlobalAmbience = c;
}

MortarRectangle DisplayManager::GetWindowSize() const {
    return m_WindowRect;
}

void DisplayManager::SetWindowSize(int l, int t, int r, int b) {
    m_WindowRect.left = l;
    m_WindowRect.top = t;
    m_WindowRect.right = r;
    m_WindowRect.bottom = b;
}

void DisplayManager::SetClearColour(const Colour& c) {
    m_ClearColor = c;
}

void DisplayManager::SetLightDirection(const Vec3& dir) {
    m_lightDirection = dir;
}

void DisplayManager::SetTextureOverloadPrefix(const char* prefix) {
    strncpy(m_TextureOverloadPrefix, prefix, sizeof(m_TextureOverloadPrefix) - 1);
    m_TextureOverloadPrefix[sizeof(m_TextureOverloadPrefix) - 1] = '\0';
}

// Platform filter/wrap mode lookups (matching DisplayManagerBada tables)
GLenum DisplayManager::GetPlatformMagFilter() const {
    return (m_MagFilterMode == 0) ? GL_NEAREST : GL_LINEAR;
}

GLenum DisplayManager::GetPlatformMinFilter() const {
    return (m_MinFilterMode == 0) ? GL_NEAREST : GL_LINEAR;
}

GLenum DisplayManager::GetPlatformWrapS() const {
    return (m_WrapSMode == 0) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

GLenum DisplayManager::GetPlatformWrapT() const {
    return (m_WrapTMode == 0) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

} // namespace Mortar
