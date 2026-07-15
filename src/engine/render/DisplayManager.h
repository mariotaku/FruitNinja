#ifndef MORTAR_DISPLAY_MANAGER_H
#define MORTAR_DISPLAY_MANAGER_H

#include "render/gl_funcs.h"
#include "math/Colour.h"
#include "math/_Vector3.h"
#include "math/Matrix44.h"
#include "core/MortarTypes.h"
#include "core/Singleton.h"
#include "util/AsciiString.h"
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
    _Vector3<float> m_lightDirection;              // +0x1C
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

    // v1.6.1 DisplayManager::SetGlobalAmbience @0x002569fc -- stores packed ulong directly
    // into m_GlobalAmbience (4-byte Colour field). Binary: this->m_GlobalAmbience = param_1.
    void SetGlobalAmbience(unsigned long packedRGBA);

    // Matches 0x0019dc94
    MortarRectangle GetWindowSize();

    // Matches original SetClearColour
    void SetClearColour(Colour c);

    // Matches original SetLightDirection
    void SetLightDirection(_Vector3<float> dir);

    // ASM-spec v1.6.1 Mortar::DisplayManager::SetWindowSize @0x002566e8: param order (top,bottom,left,right); stores l->+0x0c,t->+0x10,r->+0x14,b->+0x18.
    void SetWindowSize(long t, long b, long l, long r);
    // Defunct: cross-SKU display hook -- no-op stub; v1.6.1 Mortar::DisplayManager::SetWindowSize(long,long,long,long,bool) @0x00256940 (body bx lr; bool meaning unknown from this SKU; no v1.6.1 callers)
    void SetWindowSize(long t, long b, long l, long r, bool);

    // v1.6.1 DisplayManager::SetTextureOverloadPrefix @0x0011eba4
    // Binary mangled: SetTextureOverloadPrefix(AsciiString const&). Body writes the GLOBAL
    // AlternativeTextureLoader::Prefix (NOT this instance's m_TextureOverloadPrefix field)
    // plus Texture::UseAlternativeTextureLoader = (prefix.Length() != 0). The instance
    // field below is kept for layout only; it is never written by this method.
    void SetTextureOverloadPrefix(const Mortar::AsciiString& prefix);

    bool IsRenderingAllowed() const { return true; }

    // Platform filter/wrap mode lookups
    GLenum GetPlatformMagFilter();
    GLenum GetPlatformMinFilter();
    GLenum GetPlatformWrapS();
    GLenum GetPlatformWrapT();

private:
    DisplayManager();

public:
    // Binary @ 0x0019da38 -- empty no-op (`bx lr`); see DisplayManager.cpp.
    void Destroy();

    // Binary vtable slot 17: Mortar::DisplayManagerBada::GetAspectWvH @0x25695c
    // Returns window width / height from m_WindowRect. On 480x320 target = 1.5.
    // Binary dispatch is virtual; port uses non-virtual call (DisplayManager vtable
    // extension to slot 17/18 deferred to DisplayManager vtable audit).
    float GetAspectWvH();

    // Binary vtable slot 18: Mortar::DisplayManagerBada::GetAspectHvW @0x25698c
    // Returns window height / width from m_WindowRect. On 480x320 target = 0.667.
    float GetAspectHvW();
};

} // namespace Mortar

#endif
