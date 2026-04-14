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

// CriticalFlash state — one global tint drawn full-screen, fades over
// ~0.3s. Matches binary CriticalFlash (0x0016a9a4) which stamps two
// Colour fields on the FruitGame singleton and lets a ScreenTint
// renderer consume them. Port collapses into a timer + colour pair.
static Colour s_CritFlashColour(0, 0, 0, 0);
static float  s_CritFlashTimer = 0.0f;
static const float CRIT_FLASH_DURATION = 0.3f;

void SetBombHitPos(const Vec3& pos) {
    s_BombHitPos = pos;
}

void CriticalFlash(const Vec3& pos, const Colour& colour) {
    (void)pos;  // position stored on binary but the overlay is full-screen
    s_CritFlashColour = colour;
    s_CritFlashTimer  = CRIT_FLASH_DURATION;
}

void UpdateCriticalFlash(float dt) {
    if (s_CritFlashTimer > 0.0f) {
        s_CritFlashTimer -= dt;
        if (s_CritFlashTimer < 0.0f) s_CritFlashTimer = 0.0f;
    }
}

void DrawCriticalFlash() {
    if (s_CritFlashTimer <= 0.0f) return;

    // Lazy-share the bomb flash.tex — the binary's ScreenTint uses a
    // radial texture too. Load on first use.
    if (!s_FlashTex.IsValid()) {
        s_FlashTex = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
        if (!s_FlashTex.IsValid()) return;
    }

    // Linear alpha fade from initial → 0 over CRIT_FLASH_DURATION.
    const float t = s_CritFlashTimer / CRIT_FLASH_DURATION;
    int a = (int)((float)s_CritFlashColour.a * t);
    if (a < 0)   a = 0;
    if (a > 255) a = 255;

    Colour tint(s_CritFlashColour.r, s_CritFlashColour.g,
                s_CritFlashColour.b, (uint8_t)a);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    // Centred tint disc — large enough to cover the whole screen even
    // at the edges (flash.tex is radial and fades to transparent).
    Matrix44 mat = Matrix44::MakeScale(800.0f, 800.0f, 1.0f);
    mat.GlobalTranslate44(Vec3(0.0f, 0.0f, -10.0f));
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

    for (auto it = am->entities.begin(); it != am->entities.end(); ++it) {
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
