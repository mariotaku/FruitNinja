#ifndef FN_PLATFORM_WII_GX_RENDERER_H
#define FN_PLATFORM_WII_GX_RENDERER_H

// GX render backend placeholder -- see gx/README.md for the full
// file-by-file GL -> GX mapping this will implement.
//
// Not wired into src/engine/render/Renderer.cpp yet. This header exists so
// the shape of the seam is visible; a future pass either:
//   (a) adds FRUIT_PLATFORM_WII branches inside Renderer.cpp/DisplayManager.cpp
//       that call into functions declared here, or
//   (b) splits Renderer.cpp into a shared front-end + *Gx.cpp / *SDL.cpp
//       backend pair, mirroring the DisplayManager.cpp / DisplayManagerSDL.cpp
///      split that already exists for GL.
//
// Only compiled when FRUIT_PLATFORM_WII is defined (see
// src/platform/wii/CMakeLists.txt) -- host/web builds never see this file.
#ifdef FRUIT_PLATFORM_WII

namespace fn {
namespace wii {

// TODO(wii): GX video/rendering-state init (GX_Init, GX_SetCopyClear,
// GX_SetViewport, GX_SetBlendMode, TEV stage setup for GX_MODULATE --
// see gx/README.md's ShaderProgram.cpp row). Called once from mainWii.cpp
// after VIDEO_Init/fifo setup, before the first frame.
void GxInit(void* xfb, void* rmode);

// TODO(wii): per-frame begin -- GX_InvalidateTexAll + any per-frame state
// reset equivalent to what DisplayManager::BeginFrame does on the GL path.
void GxBeginFrame();

// TODO(wii): swap -- GX_DrawDone, GX_CopyDisp(xfb, GX_TRUE), VIDEO_Flush,
// VIDEO_WaitVSync. Equivalent to SDL_GL_SwapWindow on the SDL backend.
void GxSwapBuffers();

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_GX_RENDERER_H
