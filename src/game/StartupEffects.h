#include "game/GameWork.h"
#ifndef FN_GAME_STARTUP_EFFECTS_H
#define FN_GAME_STARTUP_EFFECTS_H

// Free functions drawn at the tail of GameDraw.
// Binary: DrawNews @ 0x0016bbf0, DrawStartFade @ ~0x0016bc12
// Both reside in the game-draw module (no class; called directly from GameDraw).

namespace FN {
// @ 0x0016bbf0 — draw news ticker / MOTD overlay.
void DrawNews();
} // namespace FN

// @ ~0x0016bc12 — draw start-up fade-in overlay.
void DrawStartFade();

// @ 0x00169a9c — prime the first wave when the player picks a game mode.
// Called by GameModeScreen::SetupLevel (vtable[18]) once the camera fade
// crosses -0.9. Resets WaveManager and sets game_work.bM_bPaused = 1 so the
// gameplay loop doesn't tick until STATE_CAMERA_FADE clears the flag.
void PrepareForLevelStart();

#endif // FN_GAME_STARTUP_EFFECTS_H
