#ifndef FN_DEBUG_OSD_H
#define FN_DEBUG_OSD_H

// OSD -- on-screen message overlay (dev toasts).
//
// Binary: OSD_Init @0x1ca2b4 / OSD_AddMessage @0x1ca2b8 are dead bx-lr stubs
// with zero call sites -- the debug OSD is compiled out of v1.6.1.
//
// Port specific: the port revives the API as a dev toast system so debug
// confirmations (F-key toggles, screenshot saves) are visible on the screen
// itself when there is no console (TV / LAN device testing). Everything
// beyond the two binary stub signatures is port-invented.
//
// Behaviour:
// - Fixed store of 16 messages, no heap churn (text is copied into a fixed
//   char[64]).
// - OSD_AddMessage posts a toast; when all slots are full the oldest is
//   evicted. Default lifetime 2.5 s.
// - Messages stack top-left, newest on top, BELOW the FPS counter line
//   (DebugFps_Draw). Rendered with the bundled bitmap font
//   "fonts/verdana.fnt" (size 8, yellow at 60% opacity) via the same
//   Font::DrawString path as DebugHUDBounds_Draw -- a game asset, so it
//   loads on the web/emscripten build too (whole Data dir is preloaded).
// - Call OSD_Update(dt) + OSD_Draw() once per display frame in the debug
//   overlay layer (Game::renderFrame, next to DebugFps_Draw). Toasts are
//   user-triggered confirmations, so they render regardless of any debug
//   flag level. Never part of the gameplay draw.
//
// On the __bada__ cross-build the two binary symbols keep their faithful
// stub bodies and the port-only entry points compile out to inline no-ops.

// Binary-stub-compatible signatures (symbols exist in the binary).
// Host build: OSD_Init clears the store; OSD_AddMessage posts a toast with
// the default lifetime and returns its argument unchanged.
void OSD_Init();
const char* OSD_AddMessage(const char* s);

#ifndef __bada__

// Port specific: post a toast with an explicit lifetime in seconds.
// Text is copied (truncated to 63 chars). Null/empty text is ignored.
void OSD_AddMessage(const char* s, float ttl);

// Port specific: append text to the NEWEST toast in place (no new slot, ttl
// unchanged); ignored when the store is empty or the line is already full.
// For diagnostics whose last field only becomes known after the toast was
// posted -- e.g. SoundManager::SFXPlay toasts a play, then
// SoundManager::SFXSetVolume appends the volume byte that arrives one call
// later. Only meaningful when nothing else posted a toast in between (the
// caller owns that ordering).
void OSD_AppendToLast(const char* s);

// Port specific: age active messages by dt seconds; expired ones are freed.
void OSD_Update(float dt);

// Port specific: draw the active messages (no-op when none). Restores the
// game ortho itself via Renderer::SetupGameOrtho, same as DebugFps_Draw.
void OSD_Draw();

#else // __bada__

// Port specific: the toast system does not exist on the cross-build target.
inline void OSD_AddMessage(const char*, float) {}
inline void OSD_AppendToLast(const char*) {}
inline void OSD_Update(float) {}
inline void OSD_Draw() {}

#endif // !__bada__

#endif // FN_DEBUG_OSD_H
