#ifndef FN_DEBUG_FLAGS_H
#define FN_DEBUG_FLAGS_H

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

// Render every active Fruit / Bomb / SplatEntity collision sphere as
// a translucent circle. Call from GameDraw after the entity pass.
// No-op when g_DebugHitboxes is false.
void DebugHitbox_Draw();

// Render every active MenuButton click hitbox as a magenta AABB outline.
// Call from GameDraw right after DebugHitbox_Draw().
// No-op when g_DebugHitboxes is false.
void DebugMenuButton_Draw();

} // namespace FN

#endif
