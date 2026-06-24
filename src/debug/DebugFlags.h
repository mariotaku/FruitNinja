#ifndef FN_DEBUG_FLAGS_H
#define FN_DEBUG_FLAGS_H

#ifndef __bada__

//
// Port specific: debug-only overlay flags. No binary equivalent.
// g_DebugHitboxes:  draw entity collision spheres + HUD bounds. Toggle F1.
// g_DebugWireframe: glPolygonMode(GL_LINE) around 3D entity pass. Toggle F2.
//                   Desktop GL only (no-op under GLES).
// g_ShowFps:        "FPS NNN" counter in top-left corner. Toggle F3,
//                   or launch with --fps / --show-fps, or web ?fps=1.
// g_DebugTimeScale: multiplies fixed dt=1/60. 1.0 = normal, 0.1 = slow-mo.
//                   Toggle F7.
//

namespace FN {

extern bool  g_DebugHitboxes;
extern bool  g_DebugWireframe; // Port specific: desktop GL only
extern float g_DebugTimeScale; // Port specific: debug-only, no binary equivalent
extern bool  g_ShowFps;        // Port specific: FPS counter overlay (toggle F3, --fps, ?fps=1)

// Render every active Fruit / Bomb / SplatEntity collision sphere as
// a translucent circle. Call from GameDraw after the entity pass.
// No-op when g_DebugHitboxes is false.
void DebugHitbox_Draw();

// Render every active HUDControl bounding box as a magenta AABB outline.
// Covers all HUDControl subclasses (MenuButton, BonusScreen controls, etc.).
// Call from GameDraw right after DebugHitbox_Draw().
// No-op when g_DebugHitboxes is false.
void DebugHUDBounds_Draw();

// Render "FPS NNN" in the top-left corner of the screen.
// Call from renderFrame() after GameTaskDraw so the overlay is additive.
// No-op when g_ShowFps is false or fps <= 0.
void DebugFps_Draw(float fps);

// Draw a thick line from m_TailPos to m_HeadPos for every active blade
// (IsBladeActive() == true). Yellow line + small end-cap markers.
// Call from GameDraw after DebugHitbox_Draw / DebugHUDBounds_Draw.
// No-op when g_DebugHitboxes is false.
void DebugBladeTrails_Draw();

} // namespace FN

#else // __bada__

// On the Bada / cross-build target there is no debug time scaling.
// Provide g_DebugTimeScale as a compile-time constant so call sites
// that multiply by it compile and reduce to no-op arithmetic.
namespace FN {
static const float g_DebugTimeScale = 1.0f;
static const bool  g_DebugHitboxes  = false;
static const bool  g_DebugWireframe = false;
static const bool  g_ShowFps        = false;
inline void DebugHitbox_Draw()  {}
inline void DebugHUDBounds_Draw() {}
inline void DebugFps_Draw(float) {}
inline void DebugBladeTrails_Draw() {}
} // namespace FN

#endif // !__bada__

#endif
