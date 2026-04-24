//
// BombHit.cpp — DrawBombHit white flash overlay.
// See BombHit.h for binary references.
//
// Analysed: 2026-04-13T22:00
//

#include "BombHit.h"
#include "Game.h"
#include "entities/ActorManager.h"
#include "entities/BombBlast.h"
#include "entities/Bomb.h"
#include "entities/Fruit.h"
#include "entities/SplatEntity.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "util/SmartPtr.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "audio/GameSound.h"
#include <cstdio>

namespace FN {

// Binary DAT constants for DrawBombHit (docs/entities/bomb.md)
static const float FLASH_START     = 1.55f;   // DAT_0016b864
static const float FLASH_DUR_RECIP = -0.45f;  // DAT_0016b868 (divisor)
static const float FLASH_MAX_SCALE = 20000.0f;// DAT_0016b870
static const float FLASH_ALPHA_MUL = 255.0f;  // DAT_0016b874
static const float FLASH_THRESHOLD = 2.0f;    // only draws when timer < 2.0
static const float BLAST_PURGE_THR = 1.55f;   // DAT_0016a1fc
static const float BLAST_RESET_THR = 1.5f;    // ResetGameEntities trigger

static Vec3 s_BombHitPos(0, 0, 0);
static SmartPtr<Mortar::Texture> s_FlashTex;

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

void SetBombHitPos(const Vec3& pos) {
    s_BombHitPos = pos;
}

// Matches HitMenuBomb (0x0016b234). Binary also writes a task-state
// flag at +0xf8 and gates on a field_0x10c check -- the port's
// equivalent state (BombFlash pool / task-state guard) isn't ported,
// so we drop the guard and just trigger the timer + SFX + pos.
void HitMenuBomb(const Vec3& pos) {
    Game* game = Game::GetInstance();
    if (!game) return;
    SetBombHitPos(pos);
    game->bombHitTimer = 2.0f;                       // binary: 0x40000000 = 2.0f
    if (game->pGameSound) {
        // Binary pre-loads the SFX via SoundManager::PreLoadSound; the
        // port's GameSound::SFXPlay loads on demand, so preload is a
        // no-op deferred.
        game->pGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }
}

// Matches BombFlashFull (0x00168f24). Returns true once the bomb-hit
// flash has wound below 1.0s of remaining time (i.e. past the main
// flash peak). The binary also checks a redundant 1.55 upper bound --
// since 1.0 is tighter, the effective test is just `timer < 1.0`.
// In the idle state (timer == 0) this returns true, so callers must
// only poll it AFTER kicking the timer via HitMenuBomb / HitBomb.
bool BombFlashFull() {
    const Game* game = Game::GetInstance();
    if (!game) return true;
    return game->bombHitTimer < 1.0f;
}

// Matches CriticalFlash @ 0x0016a9a4. Stores the colour and resets
// Game::m_CritTimer to the full duration. The pos arg exists in the
// binary signature but isn't used by DrawCritHit — the flash is
// always full-screen at the origin.
void CriticalFlash(const Vec3& pos, const Colour& colour) {
    (void)pos;
    s_CritFlashColour = colour;
    if (Game* game = Game::GetInstance()) {
        game->m_CritTimer = CRITICAL_FLASH_TIME;
    }
}

// Matches the Game::m_CritTimer decrement in the binary's main update
// loop — runs every frame, clamps at 0.
void UpdateCriticalFlash(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;
    if (game->m_CritTimer > 0.0f) {
        game->m_CritTimer -= dt;
        if (game->m_CritTimer < 0.0f) game->m_CritTimer = 0.0f;
    }
}

// Matches DrawCritHit @ 0x0016b5b4. Called from GameDraw between
// HUD::Draw(0x08) and HUD::Draw(0x100). Draws an untextured full-
// screen colour quad whose size + alpha track Game::m_CritTimer.
void DrawCriticalFlash() {
    Game* game = Game::GetInstance();
    if (!game) return;
    const float t = game->m_CritTimer;

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

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(sx, sy, 1.0f);
    mat.GlobalTranslate44(Vec3(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawQuad(tint);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DrawBombHit() {
    Game* game = Game::GetInstance();
    if (!game) return;
    const float timer = game->bombHitTimer;
    if (timer <= 0.0f || timer >= FLASH_THRESHOLD) return;

    if (!s_FlashTex.IsValid()) {
        // Binary DrawBombHit (0x16b73c) lazy-loads `flash.tex` into
        // `g_bombHitData + 0x10c` on first call. String resolved at
        // 0x001BC7E9 via re-analyst RE pass 2026-04-13.
        s_FlashTex = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
        if (!s_FlashTex.IsValid()) return;
    }

    // Scale animation: starts at 0 when timer == 1.55, grows to 20000 as
    // timer drops to 1.1, then stays at 20000 until FLASH_THRESHOLD passes.
    const float t = (timer - FLASH_START) / FLASH_DUR_RECIP + 1.0f;
    float scale;
    if (t <= 0.0f)      scale = 0.0f;
    else if (t < 1.0f)  scale = t * FLASH_MAX_SCALE;
    else                scale = FLASH_MAX_SCALE;

    if (scale <= 0.0f) return;

    // Alpha = clamp(255 * timer, 0, 255)
    int a = (int)(FLASH_ALPHA_MUL * timer);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    const Colour tint(255, 255, 255, (uint8_t)a);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
    mat.GlobalTranslate44(s_BombHitPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    s_FlashTex->Set();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawQuad(tint);
    }
    s_FlashTex->UnSet();
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
//   1. (Skipped — port has 1 SlashEntity not 16; SlashEntity::Reset
//       isn't a thing right now)
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
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    // Use the gamemode flag as the "Zen" gate — binary reads
    // *(game + 6) which is the gameMode byte at +0x04..+0x0c block.
    Game* game = Game::GetInstance();
    const bool zenMode = game && (game->gameMode == 2);

    // Iterate Fruit (0) + Bomb (1) type lists.
    for (int t = 0; t <= 1; t++) {
    const std::list<Entity*>& list = am->GetTypeList(t);
    for (auto it = list.begin(); it != list.end(); ++it) {
        Entity* e = *it;
        if (!e || !e->IsActive()) continue;

        if (e->entityType == 1) {
            // Bomb: reset chuck, fling off-screen.
            Bomb* bomb = static_cast<Bomb*>(e);
            bomb->Chuck(0.0f);
            bomb->pos.y = OFFSCREEN_Y;
            bomb->vel.y = DRIFT_Y;
        } else if (e->entityType == 0) {
            // Fruit: chuck reset, optional force-slice, off-screen.
            Fruit* fruit = static_cast<Fruit*>(e);
            fruit->Chuck(Vec3(0, 0, 0), 0.0f);

            const bool forceSliced = zenMode || killAll;
            if (forceSliced) {
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
                fruit->OnSliced(impulse);
            }

            // Fling both halves off-screen — binary writes both
            // pos.y AND m_HalfB_pos.y AND both vel.y / m_HalfB_vel.y.
            fruit->pos.y         = OFFSCREEN_Y;
            fruit->m_SecondPos.y = OFFSCREEN_Y;
            fruit->vel.y         = DRIFT_Y;
            fruit->m_SecondVel.y = DRIFT_Y;
        }
    }
    }  // end type loop

    // Splats: binary only purges in same-screen multiplayer. Port
    // has no multiplayer so we skip — splats fade naturally.
    (void)SplatEntity::RemoveAll;  // intentional unreference
}

void UpdateBombHit(float prevTimer) {
    Game* game = Game::GetInstance();
    if (!game) return;
    const float currentTimer = game->bombHitTimer;

    // Binary UpdateBombHit (0x16a1a8): at the 1.5s downward edge
    // (i.e. timer just dropped below 1.5), call ResetGameEntities
    // to wipe the gameplay area before the game-over screen
    // renders over it.
    if (prevTimer > BLAST_RESET_THR && currentTimer <= BLAST_RESET_THR) {
        ResetGameEntities(false);
    }

    // Below 1.55s, bulk-remove every live BombBlast (type 4) — matches
    // RemoveFlashEntities (0x169ca0).
    if (currentTimer > 0.0f && currentTimer < BLAST_PURGE_THR) {
        BombBlast::RemoveAll();
    }
}

} // namespace FN
