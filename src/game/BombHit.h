#ifndef FN_BOMB_HIT_H
#define FN_BOMB_HIT_H

//
// BombHit — white flash overlay drawn after a bomb is slashed.
// Matches binary DrawBombHit, UpdateBombHit, and HitBomb helpers.
// Global state lives on the Game singleton (Game::bombHitTimer),
// backing position stored here.
//
// v1.6.1 binary symbols:
//   CriticalFlash @ 0x001cca50
//   DrawCritHit   @ 0x001ccfa0
//

#include "math/_Vector3.h"
#include "math/Colour.h"
#include "game/GameWork.h"

// CriticalFlash (v1.6.1) @ 0x001cca50 — kicks a full-screen colour tint
// overlay that fades out over ~0.5s. Binary stamps two Colour fields on
// the FruitGame singleton (+0xe4, +0xf0) and resets a ScreenTint time
// scale. Port collapses this into a single static state + fade timer
// rendered by DrawCritHit during GameDraw. Used by Fruit slice
// for critical-hit and rare (special fruit) feedback.
void CriticalFlash(_Vector3<float> pos, Colour colour);

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

// Matches v1.6.1 RetryUpdate @ 0x001cb4fc. Called each frame from GameUpdate
// while retryFlag != 0 and retryTimer > 0. Scales fruits/bombs toward
// negative (visually shrinks them to zero) over the retryTimer window.
void RetryUpdate(float dt);

// ASM-spec v1.6.1 DrawCritHit @0x001ccfa0: lazy-loads shared "flash.tex" but draws
// UNTEXTURED -- Texture vtable slot 4 UnSet(true) before matrix setup + again after
// Mesh::DrawQuadUnCached(tint, NULL). Slot map: vptr+0xc=Set, vptr+0x10=UnSet(bool).
// Draws the current CriticalFlash tint as a full-screen flat-colour quad. Called
// from GameDraw after the slice-line pass and before the HUD overlays, so the
// flash sits under the logo/buttons. No-op when the timer is 0 or >= CRITICAL_FLASH_TIME.
void DrawCritHit();

// RemoveFlashEntities v1.6.1 @ 0x001cb4b0 — iterate type-4 (BombBlast) entities and
// OR ENT_SKIP_MASK (0x11 = ENT_INACTIVE | ENT_KILLED) into each entity's flags.
// Called by UpdateBombHit, EndRetryLevel, and InstantLevelDestroy.
void RemoveFlashEntities();

// InstantLevelDestroy v1.6.1 @ 0x001cbcd8 — mute bomb fuse, zero retry/pause/dt state,
// clear all splats, unpause screen, reset all entities and flash blasts.
// Called via function pointer (power-up nuke dispatch); no direct call-graph xrefs in binary.
// DIFFERS: binary reads s_bombSound static @ 0x00316770; port uses ts->m_pBombFuseSound
// (established convention, same source of truth).
void InstantLevelDestroy();

// UnpauseSlices v1.6.1 @ 0x001da910 — clear m_pFruit on every active SliceEffect
// so they keep animating after the host fruit is removed.
void UnpauseSlices();

namespace FN {
// Per-frame advance of the CriticalFlash fade timer. Called from
// GameUpdate after Mortar::ActorManager::Update.
void UpdateCriticalFlash(float dt);

// Writes g_BombHitPos (world position of last bomb hit). Called by
// Bomb::HitBomb / Bomb::HitMenuBomb and legacy FN:: call sites.
void SetBombHitPos(const _Vector3<float>& pos);
} // namespace FN

#endif
