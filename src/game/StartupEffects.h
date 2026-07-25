#include "game/GameWork.h"
#ifndef FN_GAME_STARTUP_EFFECTS_H
#define FN_GAME_STARTUP_EFFECTS_H

// Free functions drawn at the tail of GameDraw.
// Binary: DrawNews @ 0x0016bbf0, DrawStartFade @ ~0x0016bc12
// Both reside in the game-draw module (no class; called directly from GameDraw).

namespace FN {
// @ 0x0016bbf0 — draw news ticker / MOTD overlay.
void DrawNews();

// Port specific: web audio-consent splash freeze. No binary equivalent --
// the binary never gates its own splash on a browser-gesture requirement.
// Set true by mainEmscripten.cpp's BootWait right after g_game.init()
// succeeds (before the game loop's first GameUpdate tick), so the splash's
// FIRST rendered frame is already the frozen one -- no partial fade-out
// flashes through before the overlay appears. GameUpdate (GameInit.cpp)
// gates the splashFadeTimer decrement on !g_AudioConsentPending, so the
// timer (and therefore DrawStartFade's visible frame) holds steady at
// whatever value it had when this flag went true (typically 1.5, its
// GameTaskState ctor default -- the fully-opaque starting frame, since
// BootWait sets this before any GameUpdate tick has run) while the
// audio-consent overlay is shown on top. Cleared by the overlay tap
// (fn_set_audio_enabled, mainEmscripten.cpp), which resumes the normal
// splashFadeTimer drain on the next tick. This flag is a plain bool compiled
// on every target (desktop, web, bada cross-build) so GameInit.cpp's read of
// it needs no #ifdef -- only mainEmscripten.cpp (Emscripten-only) ever
// writes true, so it stays false for the lifetime of desktop/bada builds and
// their splash behaviour is completely unaffected.
extern bool g_AudioConsentPending;

// Port specific: web audio-consent overlay decision. True iff a save file
// (FruitySave.xml) already existed at boot -- i.e. FruitSaveData::LoadGame
// (GameInitialise.cpp) returned true. Set once, at boot, by GameInitialise.cpp
// under __EMSCRIPTEN__; read by mainEmscripten.cpp's BootWait to publish
// window.FNAudioPrefs.hasSave for shell.html's 4-way overlay branch (returning
// user with a saved preference gets a single-tap "unlock only" overlay instead
// of the two-button first-run PLAY WITH SOUND / PLAY MUTED choice). Compiled
// on every target like g_AudioConsentPending; stays false off-web.
extern bool g_SaveFileExisted;
} // namespace FN

// @ ~0x0016bc12 — draw start-up fade-in overlay.
void DrawStartFade();

// @ 0x00169a9c — prime the first wave when the player picks a game mode.
// Called by GameModeScreen::SetupLevel (vtable[18]) once the camera fade
// crosses -0.9. Resets WaveManager and sets game_work.bM_bPaused = 1 so the
// gameplay loop doesn't tick until STATE_CAMERA_FADE clears the flag.
void PrepareForLevelStart();

#endif // FN_GAME_STARTUP_EFFECTS_H
