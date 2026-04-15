#ifndef FN_DEBUG_HITBOX_H
#define FN_DEBUG_HITBOX_H

//
// Debug overlay — draws fruit + bomb collision spheres as translucent
// circles. Toggle with F1 in the SDL event loop.
//

namespace FN {

extern bool g_DebugHitboxes;

// Render every active Fruit / Bomb / SplatEntity collision sphere as
// a translucent circle. Call from GameDraw after the entity pass.
// No-op when g_DebugHitboxes is false.
void DebugHitbox_Draw();

} // namespace FN

#endif
