#ifndef FN_BOMB_HIT_H
#define FN_BOMB_HIT_H

//
// BombHit — white flash overlay drawn after a bomb is slashed.
// Matches binary DrawBombHit (0x0016b73c), UpdateBombHit (0x0016a1a8),
// and HitBomb (0x0016b0fc) helpers. Global state lives on the Game
// singleton (Game::bombHitTimer), backing position stored here.
//
// Analysed: 2026-04-13T22:00
//

#include "math/Vec3.h"

namespace FN {

// Stores the explosion position for DrawBombHit to centre the flash on.
// Matches binary g_bombHitData->pos (Vec3 at +0xcc). Set by Bomb::OnSliced.
void SetBombHitPos(const Vec3& pos);

// Matches DrawBombHit (0x16b73c) — draws expanding white quad scaled
// with (bombHitTimer - 1.55) / -0.45. Called from GameDraw.
void DrawBombHit();

// Matches UpdateBombHit (0x16a1a8) — called each frame with the previous
// frame's timer; clears BombBlasts once the timer drops below 1.55s.
void UpdateBombHit(float prevTimer);

// Matches ResetGameEntities (binary address pending RE). Walks every
// live entity in ActorManager and deactivates fruit + bombs. Called
// from UpdateBombHit at the 1.5s threshold to wipe the screen before
// the game-over UI appears. The bool gates a "killAll vs partial"
// mode in the binary — port currently treats both modes identically.
void ResetGameEntities(bool killAll);

} // namespace FN

#endif
