//
// BombHit.cpp — DrawBombHit white flash overlay.
// See BombHit.h for binary references.
//
// Analysed: 2026-04-13T22:00
//

#include "BombHit.h"
#include "debug/Logger.h"
#include "game/GameMode.h"
#include "Game.h"
#include "entities/ActorManager.h"
#include "entities/BombBlast.h"
#include "entities/Bomb.h"
#include "entities/Fruit.h"
#include "entities/SplatEntity.h"
#include "entities/SlashEntity.h"
#include "particle/PSPParticleManager.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "util/SmartPtr.h"
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

namespace FN {

// CriticalFlash state — matches binary CriticalFlash @ 0x0016a9a4 +
// DrawCritHit @ 0x0016b5b4 (verified 2026-04-15).
//
// Storage: the binary keeps the colour in a static block at GOT+0x452d4
// (BSS @ 0x00231A04), with the active copy at +0xf0. The timer is on the
// Game singleton at +0x2c (Game::m_CritTimer). On trigger:
//   m_CritTimer = 0.5         (CRITICAL_FLASH_TIME)
//   stored_colour = passed colour
// DrawCritHit reads both, computes a fade scale + alpha, and draws a
// full-screen UNTEXTURED quad. The visible window is narrow:
// m_CritTimer drops 0.5 → 0; only (0.3, 0.4) renders a non-zero quad
// (≈0.1s flash). Outside that window the quad is degenerate (size 0).
static Colour s_CritFlashColour(255, 255, 255, 255);

static const float CRITICAL_FLASH_TIME       = 0.5f;   // Fruit::CRITICAL_FLASH_TIME @ 0x001f3e3c
static const float CRITICAL_FLASH_FULL       = 0.4f;   // Fruit::CRITICAL_FLASH_FULL @ 0x001f3e40
static const float CRITICAL_FLASH_START_FADE = 0.3f;   // Fruit::CRITICAL_FLASH_START_FADE @ 0x001f3e44
static const float CRITICAL_FLASH_SCALE_MUL  = 15002.0f; // DAT_0016b714
static const float CRITICAL_FLASH_MAX_X      = 480.0f;   // DAT_0016b718
static const float CRITICAL_FLASH_MAX_Y      = 320.0f;   // DAT_0016b71c

// Lazy 1×1 white texture for the untextured quad path. The Renderer's
// quad shader samples u_tex × u_tint; without a bound texture the
// sample is undefined, so bind a solid-white pixel and let the tint
// drive the colour.
static GLuint s_WhitePx = 0;
static void EnsureWhitePx() {
    if (s_WhitePx) return;
    glGenTextures(1, &s_WhitePx);
    glBindTexture(GL_TEXTURE_2D, s_WhitePx);
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Writes the bomb-hit world position used by Bomb::DrawBombHit.
// Bomb::HitBomb and Bomb::HitMenuBomb now write g_BombHitPos directly;
// this wrapper remains for call sites that use the FN:: form.
void SetBombHitPos(const Vec3& pos) {
    g_BombHitPos = pos;
}

// Matches CriticalFlash @ 0x0016a9a4. Stores the colour and resets
// Game::m_CritTimer to the full duration. The pos arg exists in the
// binary signature but isn't used by DrawCritHit — the flash is
// always full-screen at the origin.
void CriticalFlash(const Vec3& pos, const Colour& colour) {
    (void)pos;
    s_CritFlashColour = colour;
    if (Game* game = Game::GetInstance()) {
        game_work.m_CritTimer = CRITICAL_FLASH_TIME;
    }
}

// Matches the Game::m_CritTimer decrement in the binary's main update
// loop — runs every frame, clamps at 0.
void UpdateCriticalFlash(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;
    if (game_work.m_CritTimer > 0.0f) {
        game_work.m_CritTimer -= dt;
        if (game_work.m_CritTimer < 0.0f) game_work.m_CritTimer = 0.0f;
    }
}

// Matches DrawCritHit @ 0x0016b5b4. Called from GameDraw between
// HUD::Draw(0x08) and HUD::Draw(0x100). Draws an untextured full-
// screen colour quad whose size + alpha track Game::m_CritTimer.
void DrawCriticalFlash() {
    Game* game = Game::GetInstance();
    if (!game) return;
    const float t = game_work.m_CritTimer;

    // Binary guard at the head of DrawCritHit: early-exit if the timer
    // has already exceeded the full duration. The very first frame
    // after CriticalFlash() this is exactly CRITICAL_FLASH_TIME, so
    // nothing draws — drawing starts from the next frame as the timer
    // decrements below the threshold.
    if (t <= 0.0f || t >= CRITICAL_FLASH_TIME) return;

    // Quad size: a normalized fade `norm` is computed so it ramps up
    // through the (0.3, 0.4) window then falls off. Outside that
    // window the size is 0 (degenerate quad → not drawn).
    //   norm = 1 - (t - START_FADE) / (FULL - START_FADE)
    // The * 15002 multiplier saturates against the 480/320 caps for
    // any nonzero norm, so the visible quad is always full-screen.
    const float denom = CRITICAL_FLASH_FULL - CRITICAL_FLASH_START_FADE;
    const float norm  = 1.0f - (t - CRITICAL_FLASH_START_FADE) / denom;
    float scale = 0.0f;
    if (norm > 0.0f && norm < 1.0f) {
        scale = norm * CRITICAL_FLASH_SCALE_MUL;
    }
    if (scale <= 0.0f) return;

    float sx = scale; if (sx > CRITICAL_FLASH_MAX_X) sx = CRITICAL_FLASH_MAX_X;
    float sy = scale; if (sy > CRITICAL_FLASH_MAX_Y) sy = CRITICAL_FLASH_MAX_Y;

    // Alpha = clamp(stored.a * t, 0, stored.a)
    int alpha = (int)((float)s_CritFlashColour.a * t);
    if (alpha < 0)                       alpha = 0;
    if (alpha > s_CritFlashColour.a)     alpha = s_CritFlashColour.a;

    const Colour tint(s_CritFlashColour.r,
                      s_CritFlashColour.g,
                      s_CritFlashColour.b,
                      (uint8_t)alpha);

    EnsureWhitePx();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_WhitePx);
    // Sync s_LastBoundTexId since the white-pixel is procedural (no
    // Mortar::Texture wrapper), and Renderer::DrawQuad would otherwise
    // skip the draw thinking nothing is bound.
    Mortar::Texture::s_LastBoundTexId = s_WhitePx;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(sx, sy, 1.0f);
    mat.GlobalTranslate44(Vec3(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawQuadUnCached(tint, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    Mortar::Texture::s_LastBoundTexId = 0;
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
    // ASM-verified: 2026-05-20 binary @ 0x0016a058 (asm-inspector)
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
            if (g_pSlashEntities[i]->IsBladeActive()) count_non_null++;
            g_pSlashEntities[i]->Reset();
        }
    }
    LOG_INFO("BOMBHIT", "ResetGameEntities(killAll=%d) flushed %d slash slots", (int)killAll, count_non_null);

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // ASM-verified: 2026-05-20 binary @ 0x0016a0e4 reads game+0x6 (retryFlag).
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
            fruit->vel = Vec3(0, 0, 0);
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
                Vec3 impulse(0.0f, -IMPULSE_LEN, 0.0f);
                // Scale up if the fruit is far from origin (binary's
                // dist² > 400 normalize-then-multiply-by-20 path).
                const float distSq = fruit->pos.x * fruit->pos.x +
                                     fruit->pos.y * fruit->pos.y;
                if (distSq > DIST_SQ_THRESH) {
                    Vec3 dir(fruit->pos.x, fruit->pos.y, 0.0f);
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

// ASM-verified: 2026-05-20T00:00:00Z binary @ 0x0016a208 (asm-inspector)
// (previously implemented as a file-static in PauseScreen.cpp; moved here
//  so GameUpdate can call it from the retry dispatch tail)
void EndRetryLevel() {
    LOG_INFO("BOMBHIT", "%s (%s)", "EndRetryLevel enter", "binary @ 0x0016a208");
    Game* game = Game::GetInstance();
    if (!game) return;

    // Binary @ 0x0016a220 / 0x0016a226: writes to GameTaskState+0x110 (0.5f)
    // and GameTaskState+0x10c (0). NOT MainScreen -- decompiler misdirected
    // these to mainScreen method calls in the prior port.
    GameTaskState* ts = GetTaskState();
    if (ts) {
        ts->m_ScoreStateField_0x110 = 0.5f;            // 0x16a220 [GTS+0x110]
        ts->m_TimedModeAccumulator  = 0;               // 0x16a226 [GTS+0x10c]
    }

    FN::SetScore(0, -1);                               // 0x16a22a

    if (game_work.m_SaveData) {
        FruitSaveData* sd = game_work.m_SaveData;
        sd->m_GameOverField2 = -1;                     // 0x16a23a [+0x120]
        sd->m_GameOverField4 = -1;                     // 0x16a23e [+0x128]
        sd->m_GameOverField3 = -1;                     // 0x16a242 [+0x124]
        sd->m_GameOverField1 = -1;                     // 0x16a246 [+0x11c]
    }

    // Binary @ 0x0016a24a: m_CoinsAtGameStart re-snapshot so the retried
    // run's "YOU JUST EARNT %i COINS" delta starts from zero.
    // (game+0x28) = (game+0x20).
    game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;

    FN::ResetGameEntities(false);                      // 0x16a24e
    BombBlast::RemoveAll();                            // 0x16a252 (RemoveFlashEntities)
    WaveManager::GetInstance()->Reset(true);           // 0x16a25c

    game_work.retryFlag            = 0;                // 0x16a26e [+0x06]
    game_work.m_GameDt             = 0.0f;             // 0x16a270 [+0x0c] DAT_0016a284=0.0f
    game_work.bM_bPaused = 0;               // 0x16a274 [+0x05]

    // ASM-spec: GameInit binary @ 0x0016ca7c steps 11/13 creates a fresh
    // MainScreen (m_State=0) and sets m_GameDt = -1.0f. Port collapses
    // Frontend/Game task split into one task so MainScreen drifts past
    // STATE_CAMERA_ZOOM into STATE_GAME_START during gameplay entry,
    // leaving m_GameDt = 0. This breaks the Pause->Retry recovery path
    // (|m_GameDt| > 0.998969 threshold in GameUpdate !active branch never
    // fires). Reach the binary's same end-state by resetting here.
    // re-analyst RE: 2026-05-20.
    game_work.m_GameDt = -1.0f;
    if (game_work.mMainScreen) {
        game_work.mMainScreen->ResetTimers();  // GameInit step 11: fresh ctor values
    }

    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE); // 0x16a276 -- 0x11
    }

    // Defunct: RetryOnlineMultiplayerGame (binary 0x001053e4) -- no-op stub; binary @ 0x0016a27e
}

// ASM-verified: 2026-05-20 binary @ 0x0016b008 (re-analyst)
void RetryLevel() {
    // game+0x08 = retryTimer: 0.1f initial countdown window.
    game_work.retryTimer = 0.1f;
    // game+0x06 = retryFlag: arms the retry-update dispatch in GameUpdate.
    game_work.retryFlag = 1;
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);
    // game+0x05 = bM_bPaused: suppresses GameOver cross-check + fuse SFX.
    game_work.bM_bPaused = 1;

    // ASM-verified: 2026-05-20 binary @ 0x0016b040 (re-analyst follow-up)
    // Iterates WaveManager's wave-list (std::vector<WAVE_INFO*>, stride 0x78),
    // NOT ActorManager fruits as a prior RE incorrectly claimed.
    // Sets each wave's +0x6c = 0.25f, clamps +0x68 <= 0.15f.
    // Port-side WAVE_INFO has +0x68 = m_WaveIndex (int) and +0x6c = m_pCoinChance (void*),
    // but the binary writes floats to these slots; access via reinterpret cast.
    {
        WaveManager* wm = WaveManager::GetInstance();
        if (wm) {
            std::vector<WAVE_INFO*>& waves = wm->m_WaveInfo[game_work.gameMode];
            for (std::vector<WAVE_INFO*>::iterator it = waves.begin(); it != waves.end(); ++it) {
                WAVE_INFO* wave = *it;
                if (!wave) continue;
                *reinterpret_cast<float*>(&wave->m_pCoinChance) = 0.25f;  // +0x6c
                float& fade68 = *reinterpret_cast<float*>(&wave->m_WaveIndex);  // +0x68
                if (fade68 > 0.15f) fade68 = 0.15f;
            }
        }
    }

    // ASM-verified: 2026-05-20 binary @ 0x0016b0c4 (re-analyst)
    // Mute the persistent looping Bomb-Fuse handle for the 0.1s retry-shrink window.
    // Binary path: *(GameTaskState*)(GOT+0x452d4) +0xD8 = m_pBombFuseSound; SetVolume(0).
    // NOT ambient music -- the prior TODO label was wrong.
    if (GameTaskState* ts = GetTaskState()) {
        if (ts->m_pBombFuseSound) {
            ts->m_pBombFuseSound->SetVolume(0.0f);
        }
    }

    // ASM-verified: 2026-05-20 binary @ 0x0016b0f8 (re-analyst)
    // Play the retry whoosh -- string at rodata 0x001B96AF resolves to "Game-start"
    // (same SFX as level-start; already used by GameOverScreen / GameModeScreen).
    // Binary calls MakeSFXDelegate_GT to build a stock complete-handler delegate;
    // port uses a default-constructed Delegate1 (functionally identical -- the
    // stock handler is a no-op release-on-done path that the port's GameSound
    // implements internally).
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("Game-start", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
}

// ASM-verified: 2026-05-20 binary @ 0x00169cd4 (re-analyst)
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
            bomb->scale = Vec3(-t, -t, -t);
            bomb->vel   = Vec3(0.0f, 0.0f, 0.0f);
        }
    }

    // Fruits (type 0): scale = -original_scale; vel = (0,0,0); pos.* = 0 for second half.
    {
        const std::list<Mortar::Entity*>& list = am->GetTypeList(0);
        for (std::list<Mortar::Entity*>::const_iterator it = list.begin(); it != list.end(); ++it) {
            Mortar::Entity* e = *it;
            if (!e || !e->IsActive()) continue;
            Fruit* fruit = static_cast<Fruit*>(e);
            fruit->scale      = Vec3(-t, -t, -t);
            fruit->vel        = Vec3(0.0f, 0.0f, 0.0f);
            fruit->m_SecondPos = Vec3(0.0f, 0.0f, 0.0f);
        }
    }
    (void)dt;
}

} // namespace FN
