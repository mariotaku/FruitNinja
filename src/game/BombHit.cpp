//
// BombHit.cpp — CriticalFlash + DrawCritHit + bomb-hit state helpers.
// v1.6.1 symbols: CriticalFlash @ 0x001cca50, DrawCritHit @ 0x001ccfa0.
//

#include "BombHit.h"
#include "debug/Logger.h"
#include "engine/network/P2PMessageHandling.h"
#include "game/GameMode.h"
#include "Game.h"
#include "entities/ActorManager.h"
#include "entities/BombBlast.h"
#include "entities/Bomb.h"
#include "entities/Fruit.h"
#include "entities/SplatEntity.h"
#include "entities/SlashEntity.h"
#include "particle/PSPParticleManager.h"
#include "render/MatrixManager.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "audio/GameSound.h"
#include "game/WaveManager.h"
#include "game/GameTaskState.h"
#include "game/FruitSaveData.h"
#include "game/GameOver.h"
#include "screens/MainScreen.h"
#include <cstdio>
#include "game/GameWork.h"
#include "render/Layout.h"

// CriticalFlash state — matches binary CriticalFlash (v1.6.1) @ 0x001cca50 +
// DrawCritHit (v1.6.1) @ 0x001ccfa0.
//
// Storage: the binary keeps the colour in a static block at GOT+0x452d4
// (BSS @ 0x00231A04), with the active copy at +0xf0. The timer is on the
// Game singleton at +0x2c (Game::m_CritTimer). On trigger:
//   m_CritTimer = 0.5         (CRITICAL_FLASH_TIME)
//   stored_colour = passed colour
// DrawCritHit reads both, computes a fade scale + alpha, and draws a
// full-screen TEXTURED quad ("flash.tex"). Rendering window: t in [0,0.5).
// norm > 1 for t < 0.4 (clamped to 480/320 screen caps), 1.0 at t=0.4,
// 0 at t=0.5 (not drawn). Effectively flash is full-screen for t in (0, 0.4)
// and fades to zero in (0.4, 0.5).
static Colour s_CritFlashColour(255, 255, 255, 255);

static const float CRITICAL_FLASH_TIME       = 0.5f;   // Fruit::CRITICAL_FLASH_TIME @ 0x001f3e3c
static const float CRITICAL_FLASH_FULL       = 0.4f;   // Fruit::CRITICAL_FLASH_FULL @ 0x001f3e40
static const float CRITICAL_FLASH_START_FADE = 0.3f;   // Fruit::CRITICAL_FLASH_START_FADE @ 0x001f3e44
static const float CRITICAL_FLASH_SCALE_MUL  = 15000.0f; // v1.6.1 DrawCritHit @0x001ccfa0 (was 15002.0f: stale DAT_0016b714)
static const float CRITICAL_FLASH_MAX_X      = 480.0f;   // v1.6.1 DrawCritHit @0x001ccfa0
static const float CRITICAL_FLASH_MAX_Y      = 320.0f;   // v1.6.1 DrawCritHit @0x001ccfa0

namespace FN {
// Writes the bomb-hit world position used by Bomb::DrawBombHit.
// Bomb::HitBomb and Bomb::HitMenuBomb now write g_BombHitPos directly;
// this wrapper remains for call sites that use the FN:: form.
void SetBombHitPos(const _Vector3<float>& pos) {
    g_BombHitPos = pos;
}
} // namespace FN

// ASM-spec v1.6.1 CriticalFlash @ 0x001cca50. Stores the colour and resets
// Game::m_CritTimer to the full duration. The pos arg exists in the
// binary signature but isn't used by DrawCritHit — the flash is
// always full-screen at the origin.
void CriticalFlash(_Vector3<float> pos, Colour colour) {
    (void)pos;
    s_CritFlashColour = colour;
    // Binary tail is `ldr r3,[r4,r3]; ldr r2,[r4,r2]; vldr.32 s15,[r2]; vstr.32
    // s15,[r3,#0x2c]` -- game_work from the GOT, unconditional store. No null test.
    game_work.m_CritTimer = CRITICAL_FLASH_TIME;
}

namespace FN {
// Matches the Game::m_CritTimer decrement in the binary's main update
// loop — runs every frame, clamps at 0.
void UpdateCriticalFlash(float dt) {
    // Port helper: the binary inlines this decrement in GameUpdate; every peer that
    // touches game_work.m_CritTimer (CriticalFlash @0x001cca50, DrawCritHit
    // @0x001ccfa0) loads game_work straight from the GOT with no null test.
    if (game_work.m_CritTimer > 0.0f) {
        game_work.m_CritTimer -= dt;
        if (game_work.m_CritTimer < 0.0f) game_work.m_CritTimer = 0.0f;
    }
}
} // namespace FN

// ASM-spec v1.6.1 DrawCritHit @0x001ccfa0: lazy-loads shared "flash.tex" but draws
// UNTEXTURED -- Texture vtable slot 4 UnSet(true) before matrix setup + again after
// Mesh::DrawQuadUnCached(tint, NULL). Slot map: vptr+0xc=Set, vptr+0x10=UnSet(bool).
// flash.tex is loaded here (lazily) but only ever BOUND by DrawBombHit / PauseScreen
// (shared static) -- DrawCritHit always draws it unbound, i.e. a flat-colour quad.
//
// Fix summary (vs. prior FN::DrawCriticalFlash port):
//   Bug 1 — fade formula: binary uses denom=(FLASH_TIME-FLASH_FULL)=0.1,
//            num=(t-FLASH_FULL); renders for t in [0,0.5) with norm>1 for
//            t<0.4 (clamped to screen dims). Port was using START_FADE-based
//            formula which only rendered the narrow (0.3,0.4) window.
//   Bug 2 — texture: binary calls UnSet(true) both times (untextured flat-colour
//            quad); a prior RE mis-read vtable slot +0x10 as "Set(1)" and the port
//            drew a textured quad. Fixed below (task #316 / drain #39).
//   Bug 3 — scale constant: 15002.0f -> 15000.0f (stale DAT_0016b714 comment).
void DrawCritHit() {
    // ASM-spec v1.6.1 DrawCritHit @0x001ccfa0: after the lazy flash.tex load the
    // binary reads `ldr r3,[r4,r3]; vldr.32 s13,[r3,#0x2c]` -- game_work from the
    // GOT. No Game::GetInstance, no null test.
    const float t = game_work.m_CritTimer;

    if (t <= 0.0f || t >= CRITICAL_FLASH_TIME) return;

    // Bug 1 fix: binary formula — norm = 1 - (t - FULL) / (TIME - FULL).
    // norm > 1 for t < FULL (clamped by 480/320 caps), 1.0 at t=FULL,
    // 0 at t=TIME. Old port checked norm<1.0 which blocked the full-screen phase.
    const float norm = 1.0f - (t - CRITICAL_FLASH_FULL) / (CRITICAL_FLASH_TIME - CRITICAL_FLASH_FULL);
    // Binary clamps norm to [0,1] before the *15000 multiply.
    float normClamped = norm;
    if (normClamped < 0.0f) normClamped = 0.0f;
    if (normClamped > 1.0f) normClamped = 1.0f;
    float scale = normClamped * CRITICAL_FLASH_SCALE_MUL;
    if (scale <= 0.0f) return;

    // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 480 under __bada__ --
    // widen the crit-flash X cap by HalfWidth()/240 so the fullscreen flash covers
    // the widened field sides, matching the game background / pause-dim quads
    // (GameInit.cpp:758/967). Identity at HalfWidth()==240.
#ifdef __bada__
    const float maxX = CRITICAL_FLASH_MAX_X;                            // 480.0f (faithful, no widescreen on Bada)
#else
    const float maxX = CRITICAL_FLASH_MAX_X * (Layout::HalfWidth() / 240.0f);
#endif
    float sx = scale; if (sx > maxX) sx = maxX;
    float sy = scale; if (sy > CRITICAL_FLASH_MAX_Y) sy = CRITICAL_FLASH_MAX_Y;

    // Alpha = clamp(stored.a * t, 0, stored.a)
    int alpha = (int)((float)s_CritFlashColour.a * t);
    if (alpha < 0)                   alpha = 0;
    if (alpha > s_CritFlashColour.a) alpha = s_CritFlashColour.a;

    const Colour tint(s_CritFlashColour.r,
                      s_CritFlashColour.g,
                      s_CritFlashColour.b,
                      (uint8_t)alpha);

    // Lazy-load "flash.tex" (shared static; only ever bound elsewhere).
    if (!g_FlashTexture.Get()) {
        g_FlashTexture = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
    }

    // Bug 2 fix: vtable slot 4 = UnSet(bool), called BOTH times -- texturing off.
    if (g_FlashTexture.Get()) {
        g_FlashTexture.Get()->UnSet(true);
    }

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().Scale(_Vector3<float>(sx, sy, 1.0f));
    mm.GetWorldStack().Translate(_Vector3<float>(0.0f, 0.0f, 0.0f));
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawQuadUnCached(tint, NULL);

    if (g_FlashTexture.Get()) {
        g_FlashTexture.Get()->UnSet(true);
    }
}

// Matches ResetGameEntities (binary 0x0016a058, 40 lines).
// See docs/engine/reset-game-entities.md for the full pseudocode.
//
// Pattern: instead of synchronously killing entities, fling them
// off-screen with a downward drift so the regular Update loop's
// off-screen check cleans them up naturally. Gives a brief visible
// "everything flies away" animation before the game-over screen.
//
// Sequence:
//   1. SlashEntity::Reset x16 — blade trail flush (binary entry loop)
//   2. Bombs: pos.y = -480, vel.y = -1.5, Chuck(0)
//   3. Fruits: Chuck(0). If Zen mode or killAll → force m_bSliced.
//      Otherwise compute impulse from camera origin and call
//      OnSliced before flinging. Set both pos.y AND m_HalfB_pos.y
//      to -480, both vel.y AND m_HalfB_vel.y to -1.5.
//   4. (Skipped — multiplayer-only splat removal)
//
// Off-screen Y = -480 from DAT_0016a190 (well below the -160 bottom
// of the centred ortho). Downward vel = -1.5 carries them out cleanly.
//
// Callers in binary:
//   UpdateBombHit (false)        — bomb hit timer crosses 1.5s
//   EndRetryLevel (false)        — player retries after game over
//   InstantLevelDestroy (true)   — power-up nuke
static const float OFFSCREEN_Y    = -480.0f;  // DAT_0016a190
static const float DRIFT_Y        =   -1.5f;
static const float DIST_SQ_THRESH =  400.0f;  // DAT_0016a198
static const float IMPULSE_LEN    =   20.0f;

void ResetGameEntities(bool killAll) {
    // ASM-spec v1.6.1 ResetGameEntities @ 0x001cb9c0 (thunk @ 0x001083b4).
    // (The old ASM-verified stamp cited the v1.5.1 address 0x0016a058.)
    // Binary entry loop: iVar4 = 0; do { iVar4 += 4;
    // g_pSlashEntities[iVar4>>2 - 1]->Reset(); } while (iVar4 != 0x40);
    // 16 iterations. Drops every live blade trail (m_NumPoints = 0) so
    // the collision loop in SlashEntity::Update cannot fire
    // CollisionResponse on freshly-spawned menu fruit for the next
    // several frames. Without this flush, the Quit-gesture slash
    // survives across the PauseScreen->MainScreen transition and slices
    // the just-created Play / Dojo / About menu fruits in trajectory order.
    int count_non_null = 0;
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
#if !defined(__bada__)
            if (g_pSlashEntities[i]->IsBladeActive()) count_non_null++;
#endif
            g_pSlashEntities[i]->Reset();
        }
    }
    LOG_INFO("BOMBHIT", "ResetGameEntities(killAll=%d) flushed %d slash slots", (int)killAll, count_non_null);

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // ASM-spec v1.6.1 ResetGameEntities @ 0x001cb9c0 reads game_work+0x6 (retryFlag).
    // DIFFERS: prior port code read gameMode as 'zen'; actual binary reads retryFlag.
    const bool forceSliceAll = (game_work.retryFlag != 0) || killAll;

    // Iterate Fruit (0) + Bomb (1) type lists.
    for (int t = 0; t <= 1; t++) {
    const std::list<Mortar::Entity*>& list = am->GetTypeList(t);
    for (std::list<Mortar::Entity*>::const_iterator it = list.begin(); it != list.end(); ++it) {
        Mortar::Entity* e = *it;
        if (!e || !e->IsActive()) continue;

        if (e->entityType == 1) {
            // Bomb: reset chuck, fling off-screen.
            Bomb* bomb = static_cast<Bomb*>(e);
            // DIFFERS (better-than-binary): explicit ClearEmitter for the
            // fuse trail. Binary Bomb::Init @ 0x00172504 sets m_pEmitter
            // = NULL without ClearEmitter -- the emitter then survives in
            // PSPParticleManager's active list because bomb_smoke is
            // naturallyInfinite+active (TimeStop=0, PerSec=50, so
            // PSPEmitterTemplate::Ends() returns false). Binary only
            // reaps it later at level/screen teardown via ClearEmitters().
            // PSPParticleManager::Update @ 0x00115ed8 DOES support a
            // back-ref reap mechanism (m_pRefPtr nulls the owner pointer
            // on reap), but Bomb::Update @ 0x00172e?? deliberately passes
            // ppRef=NULL to AddEmitter so the hook is unused. Result in
            // binary: visible smoke trail leaks across the game-over
            // re-chuck animation until teardown. Port explicitly clears
            // here to avoid that visible leak; binary-faithful would be a
            // bug, not a feature.
            if (bomb->m_pEmitter) {
                PSPParticleManager::GetInstance().ClearEmitter(bomb->m_pEmitter);
                bomb->m_pEmitter = nullptr;
            }
            bomb->Chuck(0.0f);
            bomb->pos.y = OFFSCREEN_Y;
            bomb->vel.y = DRIFT_Y;
            bomb->Update(0.0f);
        } else if (e->entityType == 0) {
            // Fruit: chuck reset, optional force-slice, off-screen.
            Fruit* fruit = static_cast<Fruit*>(e);
            fruit->vel = _Vector3<float>(0, 0, 0);
            fruit->Chuck(0.0f);

            if (forceSliceAll) {
                LOG_INFO("FRUIT", "m_bSliced=1 set on entity=%p pos=(%.1f,%.1f) type=%d (in BombHit forceSliceAll killAll=%d retryFlag=%d)",
                         static_cast<void*>(fruit), fruit->pos.x, fruit->pos.y, (int)fruit->m_FruitType, (int)killAll, (int)game_work.retryFlag);
                fruit->m_bSliced = true;
            }

            // If still not sliced, fire the slice path so score/effects
            // run. Binary computes a normalized impulse from the camera
            // origin; port simplifies to a unit downward impulse since
            // we don't have a stable camera-origin global yet.
            if (!fruit->m_bSliced) {
                _Vector3<float> impulse(0.0f, -IMPULSE_LEN, 0.0f);
                // Scale up if the fruit is far from origin (binary's
                // dist² > 400 normalize-then-multiply-by-20 path).
                const float distSq = fruit->pos.x * fruit->pos.x +
                                     fruit->pos.y * fruit->pos.y;
                if (distSq > DIST_SQ_THRESH) {
                    _Vector3<float> dir(fruit->pos.x, fruit->pos.y, 0.0f);
                    const float len = sqrtf(distSq);
                    if (len > 0.0001f) {
                        impulse = dir * (IMPULSE_LEN / len);
                    }
                }
                // Binary @ BombHit: calls CollisionResponse via vtable slot 9.
                // Pass impulse as bladeVelocity; other args are runtime-0.
                fruit->CollisionResponse(nullptr, 0, 0, &impulse);
                fruit->Slice();
            }

            // Fling both halves off-screen — binary writes both
            // pos.y AND m_HalfB_pos.y AND both vel.y / m_HalfB_vel.y.
            fruit->pos.y         = OFFSCREEN_Y;
            fruit->m_SecondPos.y = OFFSCREEN_Y;
            fruit->vel.y         = DRIFT_Y;
            fruit->m_SecondVel.y = DRIFT_Y;
            // Zero-dt update propagates new pos/vel through entity physics
            // before the next frame can observe stale state.
            fruit->Update(0.0f);
        }
    }
    }  // end type loop

    // Splats: binary only purges in same-screen multiplayer
    // (SplatEntity::RemoveAllSplats @ 0x0017eea4). Port has no
    // multiplayer so we skip — splats fade naturally.
}

// ASM-spec v1.6.1 EndRetryLevel @ 0x001cbc24 (thunk @ 0x00103dc0).
// (previously implemented as a file-static in PauseScreen.cpp; moved here
//  so GameUpdate can call it from the retry dispatch tail)
// Binary body, in order: mainScreen(+0x11c) = 0.5f and mainScreen(+0x118) = 0;
// SetScore(0,-1); m_SaveData +0x128/+0x124/+0x11c/+0x120 = -1; coin re-snapshot
// (game_work+0x28 = +0x20); ResetGameEntities(0); RemoveFlashEntities();
// WaveManager::GetInstance()->Reset(true); game_work+0x5 = 0; game_work+0xc = 0.0f;
// game_work(+0x164)->+0x118 = 0x11; game_work+0x6 = 0; tail-call
// RetryOnlineMultiplayerGame when game_work+0x174 != 0.
// game_work comes straight off the GOT and m_SaveData / mMainScreen are
// dereferenced unguarded -- there is no Game::GetInstance call and no null test.
// (Downgraded from ASM-verified: the old stamp cited the v1.5.1 address 0x0016a208
//  and survived a port-added `if (!game) return;` guard, so it described neither
//  this body nor v1.6.1.)
void EndRetryLevel() {
    LOG_INFO("BOMBHIT", "%s (%s)", "EndRetryLevel enter", "v1.6.1 @ 0x001cbc24");

    // 0x001cbc44 loads the MainScreen pointer from 0x0031671c -- the same pointer
    // GameInit @0x001ce82c stores into game_work+0x164 (str r5,[r6,#0x1c] then
    // ldr r3,[r6,#0x1c] / str r3,[r2,#0x164]), and the one SaveCurrentData
    // @0x001cdf00 reads as [block+0x1c]->+0x118 to compare against 0x11.
    game_work.mMainScreen->SetIntroHoldTimer(0.5f);          // 0x001cbc4c [+0x11c]
    game_work.mMainScreen->SetState(STATE_CAMERA_ZOOM);      // 0x001cbc50 [+0x118] = 0

    SetScore(0, -1);                               // 0x001cbc54

    // v1.6.1 EndRetryLevel @0x001cbc24: `ldr r3,[r4,#0x50]` at 0x001cbc68 feeds the
    // four stores directly -- no cmp/cbz between the load and 0x001cbc6c. There is
    // no gate here at all.
    {
        FruitSaveData* sd = game_work.m_SaveData;
        sd->m_GameOverField2 = -1;                     // 0x001cbc78 [+0x120]
        sd->m_GameOverField4 = -1;                     // 0x001cbc6c [+0x128]
        sd->m_GameOverField3 = -1;                     // 0x001cbc70 [+0x124]
        sd->m_GameOverField1 = -1;                     // 0x001cbc74 [+0x11c]
    }

    // Binary @ 0x001cbc7c: m_CoinsAtGameStart re-snapshot so the retried
    // run's "YOU JUST EARNT %i COINS" delta starts from zero.
    // (game+0x28) = (game+0x20).
    game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;

    ResetGameEntities(false);                      // 0x001cbc84
    RemoveFlashEntities();                             // 0x001cbc88
    WaveManager::GetInstance()->Reset(true);           // 0x001cbc8c / 0x001cbc94

    game_work.retryFlag            = 0;                // 0x001cbcb4 [+0x06]
    game_work.m_PauseAmount             = 0.0f;             // 0x001cbca8 [+0x0c] const @0x001cbcc8 = 0.0f
    game_work.bM_bPaused = 0;               // 0x001cbca4 [+0x05]

    // ASM-spec: GameInit binary @ 0x0016ca7c steps 11/13 creates a fresh
    // MainScreen (m_State=0) and sets m_PauseAmount = -1.0f. Port collapses
    // Frontend/Game task split into one task so MainScreen drifts past
    // STATE_CAMERA_ZOOM into STATE_GAME_START during gameplay entry,
    // leaving m_PauseAmount = 0. This breaks the Pause->Retry recovery path
    // (|m_PauseAmount| > 0.998969 threshold in GameUpdate !active branch never
    // fires). Reach the binary's same end-state by resetting here.
    // re-analyst RE: 2026-05-20.
    game_work.m_PauseAmount = -1.0f;
    game_work.mMainScreen->ResetTimers();  // GameInit step 11: fresh ctor values

    game_work.mMainScreen->SetState(STATE_CAMERA_FADE); // 0x001cbcac -- 0x11

    // Defunct: P2P multiplayer -- no-op stub; v1.6.1 EndRetryLevel @ 0x001cbc24
    // (gate on game_work+0x174 @0x001cbcb0, tail-call @0x001cbcc4)
    RetryOnlineMultiplayerGame();
}

static void RetryShrinkSplat(SplatEntity* s, void* /*ctx*/) {
    if (s->m_Life > 0.15f) s->m_Life = 0.15f;
    s->m_DecayRate = 0.25f;
}

// ASM-spec v1.6.1 RetryLevel @ 0x001cf124 (thunk @ 0x00115930).
// (The old ASM-verified stamp cited the v1.5.1 address 0x0016b008.)
void RetryLevel() {
    // game+0x08 = retryTimer: 0.1f initial countdown window.
    game_work.retryTimer = 0.1f;
    // game+0x06 = retryFlag: arms the retry-update dispatch in GameUpdate.
    game_work.retryFlag = 1;
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);
    // game+0x05 = bM_bPaused: suppresses GameOver cross-check + fuse SFX.
    game_work.bM_bPaused = 1;

    // v1.6.1 RetryLevel @0x001cf124: clamp m_Life<=0.15 + m_DecayRate=0.25 on all pooled splats
    SplatEntity::ForEachInPool(&RetryShrinkSplat, NULL);

    // ASM-spec v1.6.1 RetryLevel @ 0x001cf124.
    // Mute the persistent looping Bomb-Fuse handle for the 0.1s retry-shrink window.
    // Binary path: *(GameTaskState*)(GOT+0x452d4) +0xD8 = m_pBombFuseSound; SetVolume(0).
    // NOT ambient music -- the prior TODO label was wrong.
    // NOTE: the m_pBombFuseSound test is GENUINE -- v1.6.1 RetryLevel @0x001cf124
    // does `ldr r0,[r3,#0x70]; cmp r0,#0x0; beq 0x001cf1cc` at 0x001cf1b8 before
    // MortarSound::SetVolume. The outer GetTaskState() test was a port addition:
    // GetTaskState returns &s_taskState, a file-static that can never be null, and
    // the binary reads that block straight off the GOT.
    {
        GameTaskState* ts = GetTaskState();
        if (ts->m_pBombFuseSound) {
            ts->m_pBombFuseSound->SetVolume(0.0f);
        }
    }

    // ASM-spec v1.6.1 RetryLevel @ 0x001cf124.
    // Play the retry whoosh -- string at rodata 0x001B96AF resolves to "Game-start"
    // (same SFX as level-start; already used by GameOverScreen / GameModeScreen).
    // Binary calls MakeSFXDelegate_GT to build a stock complete-handler delegate;
    // port uses a default-constructed Delegate1 (functionally identical -- the
    // stock handler is a no-op release-on-done path that the port's GameSound
    // implements internally).
    // v1.6.1 RetryLevel @0x001cf124: `ldr r6,[r3,#0x18c]` at 0x001cf1dc loads
    // mGameSound and 0x001cf210 calls SFXPlay -- no test. (Contrast the
    // m_pBombFuseSound test above, which the binary DOES have; the omission here
    // is deliberate.)
    game_work.mGameSound->SFXPlay("Game-start", 1.0f, 1.0f,
        Mortar::Delegate1<bool, Mortar::MortarSound*>());
}

// ASM-verified: 2026-05-20 v1.6.1 RetryUpdate @ 0x001cb4fc (re-analyst)
void RetryUpdate(float dt) {
    static const float TARGET_TIME = 0.1f;  // matches retryTimer initial value
    const float t_raw = (TARGET_TIME - game_work.retryTimer) / TARGET_TIME;
    const float t = t_raw * t_raw;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Bombs (type 1): scale = -original_scale; vel = (0,0,0).
    {
        const std::list<Mortar::Entity*>& list = am->GetTypeList(1);
        for (std::list<Mortar::Entity*>::const_iterator it = list.begin(); it != list.end(); ++it) {
            Mortar::Entity* e = *it;
            if (!e || !e->IsActive()) continue;
            Bomb* bomb = static_cast<Bomb*>(e);
            bomb->scale = _Vector3<float>(-t, -t, -t);
            bomb->vel   = _Vector3<float>(0.0f, 0.0f, 0.0f);
        }
    }

    // Fruits (type 0): scale = -original_scale; vel = (0,0,0); pos.* = 0 for second half.
    {
        const std::list<Mortar::Entity*>& list = am->GetTypeList(0);
        for (std::list<Mortar::Entity*>::const_iterator it = list.begin(); it != list.end(); ++it) {
            Mortar::Entity* e = *it;
            if (!e || !e->IsActive()) continue;
            Fruit* fruit = static_cast<Fruit*>(e);
            fruit->scale      = _Vector3<float>(-t, -t, -t);
            fruit->vel        = _Vector3<float>(0.0f, 0.0f, 0.0f);
            fruit->m_SecondPos = _Vector3<float>(0.0f, 0.0f, 0.0f);
        }
    }
    (void)dt;
}
