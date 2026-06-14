#ifndef FN_DEBUG_FLAGS_H
#define FN_DEBUG_FLAGS_H

#ifndef __bada__

//
// Debug overlay — draws fruit + bomb collision spheres as translucent
// circles. Toggle with F1 in the SDL event loop.
//
// Port specific: debug-only, no binary equivalent.
// g_DebugTimeScale: multiplies the fixed dt=1/60 before it reaches game
// update paths. 1.0 = normal speed, 0.1 = 10x slow-motion. Toggle with F7.
// g_DebugWireframe: forces glPolygonMode(GL_LINE) around the 3D entity
// draw pass. Desktop GL only (no-op under GLES). Toggle with F2.
//

namespace FN {

extern bool  g_DebugHitboxes;
extern bool  g_DebugWireframe; // Port specific: desktop GL only
extern float g_DebugTimeScale; // Port specific: debug-only, no binary equivalent
// DEBUG: auto-slash for screenshot capture -- TEMPORARY
extern bool  g_DebugAutoSlash;

// Render every active Fruit / Bomb / SplatEntity collision sphere as
// a translucent circle. Call from GameDraw after the entity pass.
// No-op when g_DebugHitboxes is false.
void DebugHitbox_Draw();

// Render every active HUDControl bounding box as a magenta AABB outline.
// Covers all HUDControl subclasses (MenuButton, BonusScreen controls, etc.).
// Call from GameDraw right after DebugHitbox_Draw().
// No-op when g_DebugHitboxes is false.
void DebugHUDBounds_Draw();

} // namespace FN

#else // __bada__

// On the Bada / cross-build target there is no debug time scaling.
// Provide g_DebugTimeScale as a compile-time constant so call sites
// that multiply by it compile and reduce to no-op arithmetic.
namespace FN {
static const float g_DebugTimeScale = 1.0f;
static const bool  g_DebugHitboxes  = false;
static const bool  g_DebugWireframe = false;
static const bool  g_DebugAutoSlash = false;
inline void DebugHitbox_Draw()  {}
inline void DebugHUDBounds_Draw() {}
} // namespace FN

#endif // !__bada__

#endif
