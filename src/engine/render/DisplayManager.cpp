#include "render/DisplayManager.h"
#include <cstring>
#include <cstddef>
// SDL-bound bits live in DisplayManagerSDL.cpp.

#ifdef __bada__
static_assert(offsetof(Mortar::DisplayManager, m_TextureOverloadPrefix) == 0x34,
    "DisplayManager::m_TextureOverloadPrefix must be at +0x34");
static_assert(sizeof(Mortar::DisplayManager) == 0x94,
    "DisplayManager must be 0x94 bytes");
#endif

namespace Mortar {

DisplayManager::~DisplayManager() {}

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

// v1.6.1 Mortar::DisplayManagerBada::BeginFrame @ 0x00256b64
// Binary calls glDepthMask(1) + glClear(0x4100). No glClearColor — default
// clear is transparent black. Port diverged by setting white clear color,
// causing a white flash during/after the splash phase.
void DisplayManager::BeginFrame() {
    // TODO: v1.6.1 GlClientStates::Reset @0x00258000 disables GL_BLEND at frame top;
    // port BeginFrame enables it (anti-faithful) -- make faithful after auditing
    // font/particle 2D paths for the same per-draw-enable need.
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Port specific: re-enable depth-buffer write before glClear. The
    // previous frame ends with `dm.SetDepthBufferWrite(false)` for the HUD
    // pass (depth-test-on, depth-write-off, matches binary). glClear
    // honours glDepthMask per the GL spec, so without this the
    // GL_DEPTH_BUFFER_BIT clear silently no-ops and the depth buffer
    // carries stale fruit-depth values across frames -- causing the
    // "fruit punches holes through HUD" + "fruit looks half-rendered"
    // artefacts (depth-test rejects current-frame fruit fragments
    // against last-frame fruit depth at the same pixel). The Bada binary
    // doesn't need this because its GL driver clears depth regardless of
    // mask state (DisplayManagerBada::BeginFrame does not toggle mask
    // around the clear either; this is a desktop-GL spec divergence).
    glDepthMask(GL_TRUE);

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

// SwapBuffers — see DisplayManagerSDL.cpp (SDL-bound).

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

void DisplayManager::SetGlobalAmbience(unsigned long packedRGBA) {
    // Binary @ 0x002569fc: raw little-endian 4-byte store of packed ulong into m_GlobalAmbience.
    // Colour layout is b,g,r,a at offsets +0,+1,+2,+3 so byte 0 of the ulong goes to .b.
    m_GlobalAmbience.b = (uint8_t)(packedRGBA & 0xff);
    m_GlobalAmbience.g = (uint8_t)((packedRGBA >> 8)  & 0xff);
    m_GlobalAmbience.r = (uint8_t)((packedRGBA >> 16) & 0xff);
    m_GlobalAmbience.a = (uint8_t)((packedRGBA >> 24) & 0xff);
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
GLenum DisplayManager::GetPlatformMagFilter() {
    return (m_MagFilterMode == 0) ? GL_NEAREST : GL_LINEAR;
}

GLenum DisplayManager::GetPlatformMinFilter() {
    return (m_MinFilterMode == 0) ? GL_NEAREST : GL_LINEAR;
}

GLenum DisplayManager::GetPlatformWrapS() {
    return (m_WrapSMode == 0) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

GLenum DisplayManager::GetPlatformWrapT() {
    return (m_WrapTMode == 0) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

// Binary @ 0x0019da38 -- DisplayManager::Destroy is a single `bx lr` (empty
// no-op). The Bada build had no GL/platform display state to release here, so
// the faithful port body is empty.
// ASM-verified: 2026-06-07T00:00Z v1.6.1 binary @ 0x0019da38 (asm-inspector)
void DisplayManager::Destroy() {}

// ASM-spec v1.6.1 Mortar::DisplayManagerBada::GetAspectWvH @0x25695c (vtable slot 17)
// Returns (right - left) / (bottom - top) from m_WindowRect. On 480x320 = 1.5.
float DisplayManager::GetAspectWvH() const {
    return static_cast<float>(m_WindowRect.right - m_WindowRect.left) /
           static_cast<float>(m_WindowRect.bottom - m_WindowRect.top);
}

// ASM-spec v1.6.1 Mortar::DisplayManagerBada::GetAspectHvW @0x25698c (vtable slot 18)
// Returns (bottom - top) / (right - left) from m_WindowRect. On 480x320 = 0.667.
float DisplayManager::GetAspectHvW() const {
    return static_cast<float>(m_WindowRect.bottom - m_WindowRect.top) /
           static_cast<float>(m_WindowRect.right - m_WindowRect.left);
}

} // namespace Mortar
