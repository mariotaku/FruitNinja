#include "render/DisplayManager.h"
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

// Matches DisplayManagerBada::BeginFrame (0x0019dfec)
// GLES2 port: no glMatrixMode/glLoadIdentity (those are ES 1.x)
void DisplayManager::BeginFrame() {
    glClearColor(m_ClearColor.r / 255.0f, m_ClearColor.g / 255.0f,
                 m_ClearColor.b / 255.0f, m_ClearColor.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_bRenderingActive = true;
}

void DisplayManager::EndFrame() {
    m_bRenderingActive = false;
}

void DisplayManager::SwapBuffers(SDL_Window* window) {
    SDL_GL_SwapWindow(window);
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
