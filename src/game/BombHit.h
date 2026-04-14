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
#include "math/Colour.h"

namespace FN {

// CriticalFlash (binary 0x0016a9a4) — kicks a full-screen colour tint
// overlay that fades out over ~0.3s. Binary stamps two Colour fields on
// the FruitGame singleton (+0xe4, +0xf0) and resets a ScreenTint time
// scale. Port collapses this into a single static state + fade timer
// rendered by DrawCriticalFlash during GameDraw. Used by Fruit slice
// for critical-hit and rare (special fruit) feedback.
void CriticalFlash(const Vec3& pos, const Colour& colour);

// Per-frame advance of the CriticalFlash fade timer. Called from
// GameUpdate after ActorManager::Update.
void UpdateCriticalFlash(float dt);

// Draws the current CriticalFlash tint as a full-screen quad. Called
// from GameDraw after the slice-line pass and before the HUD overlays,
// so the flash sits under the logo/buttons. No-op when the timer is 0.
void DrawCriticalFlash();

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
