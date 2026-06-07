#ifndef MORTAR_DISPLAY_MANAGER_H
#define MORTAR_DISPLAY_MANAGER_H

#include "render/gl_funcs.h"
#include "math/Colour.h"
#include "math/Vec3.h"
#include "math/Matrix44.h"
#include "core/MortarTypes.h"
#include "core/Singleton.h"
#include <cstring>

namespace Mortar {

// Matches original DisplayManager / DisplayManagerBada (0x94 = 148 bytes)
// GL state singleton — manages frame lifecycle, draw colour, depth, filtering
// Layout (with vtable ptr at +0x00 from DisplayManagerBada being polymorphic):
//   +0x00 vtable ptr
//   +0x04 m_ClearColor .. +0x2D m_bSwapPending
//   +0x2E _pad[6]  (binary has 6 bytes of platform state here, unused in port)
//   +0x34 m_TextureOverloadPrefix .. +0x90 end of m_ScreenRotationMatrix
//   sizeof = 0x94
class DisplayManager : public Singleton<DisplayManager> {
    friend class Singleton<DisplayManager>;

public:
    // Virtual destructor makes DisplayManager polymorphic, placing the vtable
    // ptr at +0x00 to match DisplayManagerBada's binary layout.
    virtual ~DisplayManager();

    Colour m_ClearColor;                // +0x04
    Colour m_DrawColor;                 // +0x08
    MortarRectangle m_WindowRect;       // +0x0C (16 bytes)
    Vec3 m_lightDirection;              // +0x1C
    Colour m_GlobalAmbience;            // +0x28
    bool m_bRenderingActive;            // +0x2C
    uint8_t m_bSwapPending;             // +0x2D
    uint8_t _pad[6];                    // +0x2E..+0x33 (binary platform state)
    char m_TextureOverloadPrefix[16];   // +0x34
    int m_MagFilterMode;                // +0x44
    int m_MinFilterMode;                // +0x48
    int m_WrapSMode;                    // +0x4C
    int m_WrapTMode;                    // +0x50
    Matrix44 m_ScreenRotationMatrix;    // +0x54 (64 bytes)

    // Matches DisplayManagerBada::BeginFrame (0x0019dfec)
    void BeginFrame();

    // Matches DisplayManagerBada::EndFrame (0x0019dd1c)
    void EndFrame();

    // Matches DisplayManagerBada::SwapBuffers (0x0019dd2c)
    void SwapBuffers(void* window);   // window = SDL_Window* (opaque to header)

    // Matches DisplayManagerBada::SetDrawColour (0x0019dde4)
    // In GLES2: stores colour for Renderer to read as shader uniform
    void SetDrawColour(const Colour& c);

    // Matches DisplayManagerBada::SetDepthBuffer (0x0019de18)
    void SetDepthBuffer(bool enable);

    // Matches DisplayManagerBada::SetDepthBufferWrite (0x0019de0c)
    void SetDepthBufferWrite(bool enable);

    // Matches DisplayManagerBada::SetGlobalAmbience (0x0019dd40)
    void SetGlobalAmbience(const Colour& c);

    // Matches 0x0019dc94
    MortarRectangle GetWindowSize() const;

    // Matches original SetClearColour
    void SetClearColour(const Colour& c);

    // Matches original SetLightDirection
    void SetLightDirection(const Vec3& dir);

    // Matches 0x0019da3c
    void SetWindowSize(int l, int t, int r, int b);

    // Matches 0x0019da58
    void SetTextureOverloadPrefix(const char* prefix);

    bool IsRenderingAllowed() const { return true; }

    // Platform filter/wrap mode lookups
    GLenum GetPlatformMagFilter();
    GLenum GetPlatformMinFilter();
    GLenum GetPlatformWrapS();
    GLenum GetPlatformWrapT();

private:
    DisplayManager();

public:
    // TODO: 0x0019da38 -- DisplayManager::Destroy singleton teardown;
    // binary body is a near-empty no-op (releases GL/platform display state).
    void Destroy();
};

} // namespace Mortar

#endif
