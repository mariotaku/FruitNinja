#ifndef FN_GAME_STARTUP_EFFECTS_H
#define FN_GAME_STARTUP_EFFECTS_H

// Free functions drawn at the tail of GameDraw.
// Binary: DrawNews @ 0x0016bbf0, DrawStartFade @ ~0x0016bc12
// Both reside in the game-draw module (no class; called directly from GameDraw).

namespace FN {

// @ 0x0016bbf0 — draw news ticker / MOTD overlay.
void DrawNews();

// @ ~0x0016bc12 — draw start-up fade-in overlay.
void DrawStartFade();

} // namespace FN

#endif // FN_GAME_STARTUP_EFFECTS_H
