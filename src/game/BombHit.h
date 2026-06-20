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
#include "game/GameWork.h"

namespace FN {

// CriticalFlash (binary 0x0016a9a4) — kicks a full-screen colour tint
// overlay that fades out over ~0.3s. Binary stamps two Colour fields on
// the FruitGame singleton (+0xe4, +0xf0) and resets a ScreenTint time
// scale. Port collapses this into a single static state + fade timer
// rendered by DrawCriticalFlash during GameDraw. Used by Fruit slice
// for critical-hit and rare (special fruit) feedback.
void CriticalFlash(const Vec3& pos, const Colour& colour);

// Per-frame advance of the CriticalFlash fade timer. Called from
// GameUpdate after Mortar::ActorManager::Update.
void UpdateCriticalFlash(float dt);

// Draws the current CriticalFlash tint as a full-screen quad. Called
// from GameDraw after the slice-line pass and before the HUD overlays,
// so the flash sits under the logo/buttons. No-op when the timer is 0.
void DrawCriticalFlash();

// Writes g_BombHitPos (world position of last bomb hit). Called by
// Bomb::HitBomb / Bomb::HitMenuBomb and legacy FN:: call sites.
void SetBombHitPos(const Vec3& pos);

// Matches ResetGameEntities (binary address pending RE). Walks every
// live entity in Mortar::ActorManager and deactivates fruit + bombs. Called
// from UpdateBombHit at the 1.5s threshold to wipe the screen before
// the game-over UI appears. The bool gates a "killAll vs partial"
// mode in the binary — port currently treats both modes identically.
void ResetGameEntities(bool killAll);

// Matches EndRetryLevel @ 0x0016a208. Resets game state after the retry
// shrink animation completes (retryTimer -> 0). Clears retryFlag, resets
// score/save fields, calls ResetGameEntities(false), resets WaveManager,
// clears bM_bPaused, and sets MainScreen to CAMERA_FADE state.
void EndRetryLevel();

// Matches RetryLevel @ 0x0016b008. Called from PauseScreen RETRY_EXIT.
// Sets retryFlag=1, retryTimer=0.1f, arms level-transition gate, sets
// per-fruit timed-fade params, mutes ambient SFX, plays retry SFX.
// GameUpdate's retry dispatch tail then calls RetryUpdate each frame until
// retryTimer reaches 0, then hands off to EndRetryLevel.
void RetryLevel();

// Matches RetryUpdate @ 0x00169cd4. Called each frame from GameUpdate
// while retryFlag != 0 and retryTimer > 0. Scales fruits/bombs toward
// negative (visually shrinks them to zero) over the retryTimer window.
void RetryUpdate(float dt);

} // namespace FN

#endif
