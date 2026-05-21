// Analysed: 2026-05-03T00:00
// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// Binary: ctor 0x00132048, dtor 0x00131F9C, Update 0x00132930,
//         Draw 0x0013325C, AddAward 0x00133664, AwardScores 0x0013260C

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "game/FruitCamera.h"
#include "game/GameOver.h"
#include "entities/Coin.h"
#include "engine/audio/MortarSound.h"
#include "engine/audio/GameSound.h"
#include "engine/asset/TextureManager.h"
#include "engine/math/MathUtil.h"
#include "engine/particle/PSPParticleManager.h"
#include "engine/util/StringHash.h"
#include "util/Delegate.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "render/Utf8StringIterator.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include "game/GameWork.h"

using Mortar::TextureManager;

// Phase-timer rodata — binary @ 0x001f3d48, seven floats.
// ASM-verified: 2026-05-18 binary @ 0x001f3d48 (re-analyst)
static const float kRevealStart     = 0.66597f;  // pfVar14[0]
static const float kPerAward        = 0.6f;       // pfVar14[1]
static const float kRevealHalfBeat  = 0.33298f;   // pfVar14[2]
static const float kFinaleHoldExtra = 0.25f;      // pfVar14[3]
static const float kDismissBuffer   = 7.0f;       // pfVar14[4]
static const float kAwardYStep      = -42.0f;     // pfVar14[5] timerArr+0x14 @ 0x001f3d5c
static const float kTextOffsetScalar = 250.0f;    // pfVar14[6] timerArr+0x18 @ 0x001f3d60

// ---------------------------------------------------------------------------
// Per-coin arrival callback — binary @ 0x0013243c
// Fires on each coin that reaches the HUD score target.
// Milestones at 3, 6, >= 9: camera shake + firework SFX + particles.
// Always credits coin.scoreValue via AddToCurrentScore.
// ---------------------------------------------------------------------------

static int s_BonusCoinCounter = 0;

static void AddToScoreOnArrival(Coin* coin) {
    if (!coin) return;
    s_BonusCoinCounter++;

    bool milestone = (s_BonusCoinCounter == 3 || s_BonusCoinCounter == 6
                      || s_BonusCoinCounter >= 9);

    if (milestone) {
        Game* game = Game::GetInstance();
        if (game && game_work.m_FruitCamera) {
            Vec3 coinPos = coin->pos;
            game_work.m_FruitCamera->CreateCameraShake(coinPos, 0.3f, 0.75f);
        }
        if (game && game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Bonus-Firework-Explode", 1.0f, 1.0f);
        }
        {
            PSPParticleManager& pm = PSPParticleManager::GetInstance();
            PSPParticleEmitter* e1 = pm.AddEmitter(StringHash("bonus_mode_fx_red"));
            if (e1) e1->m_Pos = coin->pos;
            // sprintf'd particle: "Bonus-Explosion-%i" with i in {1, 3, 5} cycling per milestone
            char pname[32];
            static const int kExplosionIds[3] = {1, 3, 5};
            int slot = (s_BonusCoinCounter == 3) ? 0 : (s_BonusCoinCounter == 6) ? 1 : 2;
            snprintf(pname, sizeof(pname), "Bonus-Explosion-%i", kExplosionIds[slot]);
            PSPParticleEmitter* e2 = pm.AddEmitter(StringHash(pname));
            if (e2) {
                e2->m_Pos = coin->pos;
                // random offset: x in [-12..0], y in [+3..15]
                e2->m_Pos.x += (float)(rand() % 13) * -1.0f;
                e2->m_Pos.y += 3.0f + (float)(rand() % 13);
            }
        }
        if (s_BonusCoinCounter >= 9) {
            s_BonusCoinCounter = 0;
        }
    }

    FN::AddToCurrentScore(coin->m_CoinValue, 0, false, false);
}

// ---------------------------------------------------------------------------
// BonusScreen ctor (binary @ 0x00132048)
// ---------------------------------------------------------------------------

BonusScreen::BonusScreen()
    : HUDControl3d(),
      m_DisplayedScore(0),
      m_TotalScore(0),
      m_PulseField15(0.0f),
      m_PulseTimer(0.0f),
      m_PulseField17(1.0f),
      m_PulseAngle(0),
      _padPulse(0),
      m_PulseTarget(0.0f, 0.0f, 0.0f),
      m_NameScale(1.0f),
      m_LeaderboardSubmitted(0),
      _pad1(0),
      _pad2(0),
      _pad3(0),
      m_RushSFX(nullptr),
      // ASM-verified: 2026-05-22 binary @ 0x00132048 ctor reads
      // -kRevealHalfBeat (0.33298f) from a global timer array.
      // Negative phase = slide-in animation; transitions to 0 then positive
      // for the reveal beats.
      m_PhaseTimer(-kRevealHalfBeat),
      m_PosOffset(0.0f, 0.0f, 0.0f),
      _padfield23(0),
      _padfield24(0)
{
    m_Awards.reserve(3);

    // Binary @ 0x0013211c: m_LayerFlags = 0x80 (HUD_LAYER_POST_ACTOR).
    // ASM-verified: 2026-05-18 binary @ 0x0013211c (re-analyst)
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // ASM-verified: 2026-05-22 binary @ 0x0013207e..0x00132088 (re-analyst).
    // DAT_00132204 -> rodata "arcade_diolog_box.tex" (literal typo "diolog"
    // matches the asset on disk at Data/textures/arcade_diolog_box.tex).
    // Prior port string "dialog-box-big.tex" was a guess and didn't resolve --
    // m_Texture invalid -> size stayed at (0,0,0) -> HUDControl3d::Draw produced
    // a degenerate 0x0 quad and the dialog box was invisible.
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        TextureManager::LoadLocalisedTexture("arcade_diolog_box.tex");
    m_Texture = bgTex;

    // ASM-verified: 2026-05-22 binary @ 0x001320cc..0x001320f4 (re-analyst).
    // Without this write, HUDControl3d::Draw renders a degenerate 0x0 quad.
    if (m_Texture.IsValid()) {
        float w = (float)m_Texture->m_Width;
        float h = (float)m_Texture->m_Height;
        size = Vec3(w, h, 0.0f);
    }

    // ASM-verified: 2026-05-22 binary @ 0x0013218a..0x001321c8 (re-analyst).
    // SFX preloads in order; clip names resolved from literal pool
    // 0x00132210..0x00132224. PreLoadSoundEx variant for the drum-roll
    // (vtbl[1]) -- looping/streamed flavour vs the one-shot fireworks/explosions.
    {
        Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();
        sm.PreLoadSound  ("Bonus-Firework-Explode");
        sm.PreLoadSoundEx("Bonus-drum-roll", true);
        sm.PreLoadSound  ("equip-unlock");
        sm.PreLoadSound  ("Bonus-Explosion-1");
        sm.PreLoadSound  ("Bonus-Explosion-3");
        sm.PreLoadSound  ("Bonus-Explosion-5");
    }
}

// ---------------------------------------------------------------------------
// BonusScreen dtor (binary @ 0x00131F9C)
// ---------------------------------------------------------------------------

BonusScreen::~BonusScreen() {
    // m_RushSFX: not owned (pointer to a manager-owned sound), no delete.
    // m_Awards vector destructs its BonusAwardHud entries (SmartPtr released).
}

// ---------------------------------------------------------------------------
// AddAward (binary @ 0x00133664)
// ---------------------------------------------------------------------------

// Binary @ 0x00133664
void BonusScreen::AddAward(uint32_t colour, Mortar::SmartPtr<Mortar::Texture> tex,
                           const char* name, int tier) {
    BonusAwardHud entry;
    if (name) {
        std::strncpy(entry.m_Name, name, sizeof(entry.m_Name) - 1);
        entry.m_Name[sizeof(entry.m_Name) - 1] = '\0';
    }
    entry.m_StarTex        = tex;
    entry.m_TierBase       = tier;
    entry.m_Multiplier     = 0;
    entry.m_DisplayedScore = 0;
    // Pack uint32_t BGRA into Colour (stored as b,g,r,a fields).
    entry.m_Colour.b = (uint8_t)((colour >>  0) & 0xFF);
    entry.m_Colour.g = (uint8_t)((colour >>  8) & 0xFF);
    entry.m_Colour.r = (uint8_t)((colour >> 16) & 0xFF);
    entry.m_Colour.a = (uint8_t)((colour >> 24) & 0xFF);
    entry.m_Scale    = 1.0f;
    m_TotalScore    += tier;
    m_Awards.push_back(entry);
}

// ---------------------------------------------------------------------------
// STUBS (binary methods not yet ported)
// ---------------------------------------------------------------------------

// STUB: BonusScreen::AddAward -- binary @ 0x???? (TODO RE)
void BonusScreen::AddAward(Colour /*colour*/, Mortar::SmartPtr<Mortar::Texture> /*tex*/,
                           const char* /*name*/, int /*tier*/) {}

// STUB: BonusScreen::Draw -- binary @ 0x???? (TODO RE)
void BonusScreen::Draw(float* /*mtx*/) {}

// STUB: BonusScreen::GetTimeFirstAward -- binary @ 0x???? (TODO RE)
float BonusScreen::GetTimeFirstAward() { return 0.0f; }

// STUB: BonusScreen::GetTimePerAward -- binary @ 0x???? (TODO RE)
float BonusScreen::GetTimePerAward() { return 0.0f; }

// STUB: BonusScreen::LoadContent -- binary @ 0x???? (TODO RE)
void BonusScreen::LoadContent() {}

// STUB: BonusScreen::Shake -- binary @ 0x???? (TODO RE)
void BonusScreen::Shake(float /*amplitude*/, float /*duration*/) {}

// STUB: BonusScreen::UnLoadContent -- binary @ 0x???? (TODO RE)
void BonusScreen::UnLoadContent() {}

// ---------------------------------------------------------------------------
// AwardScores (binary @ 0x0013260C)
// One-shot finale: coin spawn, camera shake, big particle, finish SFX.
// ---------------------------------------------------------------------------

// Binary @ 0x0013260C
void BonusScreen::AwardScores() {
    // Spawn position: base pos + m_PosOffset chain (binary: pos + member field chain).
    // TODO: resolve exact member offsets from _Stack_9c/_Stack_a8/_Stack_b4 in binary frame.
    Vec3 spawnPos = pos;
    spawnPos.x += m_PosOffset.x;
    spawnPos.y += m_PosOffset.y;
    spawnPos.z += m_PosOffset.z;

    // Reset per-coin milestone counter for this batch.
    s_BonusCoinCounter = 0;

    Mortar::Delegate1<void, Coin*> onArrived =
        Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival);

    int total = m_TotalScore;
    if (total <= 5) {
        // Single batch, all coins, delay step -0.05 / cap -0.3.
        Coin::MakeCoins(total, 6,
                        Vec3(-0.05f, -0.3f, 0.0f),
                        0xff3a, 0, spawnPos,
                        "bonus_star_trail", "bonus_star_impact",
                        onArrived, false);
    } else {
        // First batch: 6 coins, tighter delay cap -0.5.
        Coin::MakeCoins(6, 6,
                        Vec3(-0.05f, -0.5f, 0.0f),
                        0xff3a, 0, spawnPos,
                        "bonus_star_trail", "bonus_star_impact",
                        onArrived, false);
        // Second batch: leftover, no arrival callback (silent score credit).
        Coin::MakeCoins(total - 6, 6,
                        Vec3(-0.05f, -0.3f, 0.0f),
                        0xff3a, 0, spawnPos,
                        "bonus_star_trail", "bonus_star_impact",
                        Mortar::Delegate1<void, Coin*>(), false);
    }

    // ASM-verified: 2026-05-20 binary @ 0x0013260C -- game_work.field_0x34 = 3 (BonusFinalePhase).
    // Field is uint8 in the port -- binary uses 4-byte `str.w` instructions at +0x34 because
    // the source value (small enum) is known-zero in upper bytes, not because the field is
    // int32. The adjacent +0x35 (m_bSlowMotion) is a true byte field (`strb.w`), confirmed
    // by independent writes at 0x0016c178/0x0016c1b6/0x0016c324/0x00141bec/0x00141e52/
    // 0x00142002. Layout stays as {uint8 field_0x34; uint8 m_bSlowMotion;}.
    game_work.field_0x34 = 3;

    Game* game = Game::GetInstance();
    if (game && game_work.m_FruitCamera) {
        game_work.m_FruitCamera->CreateCameraShake(spawnPos, 0.3f, 1.0f);
    }

    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        PSPParticleEmitter* e = pm.AddEmitter(StringHash("impact_fx"));
        if (e) { e->m_Pos = spawnPos; }
    }

    // Return value discarded in binary (cache-warming call).
    if (game) {
        (void)game_work.currentScore;
    }

    // Vol = 0.0f is the literal binary value (DAT_00132910 = 0x00000000).
    // Per-coin fireworks in AddToScoreOnArrival handle audibility.
    if (game && game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("equip-unlock", 0.0f, 1.0f,
                                  Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
}

// ---------------------------------------------------------------------------
// Update (binary @ 0x00132930) — three-phase state machine driven by m_PhaseTimer
// ---------------------------------------------------------------------------

// Binary @ 0x00132930
void BonusScreen::Update(float dt) {
    // ASM-verified: 2026-05-18 binary @ 0x00132930 (re-analyst). Timer is owned by GameOverScreen, not BonusScreen.
    // m_PhaseTimer is written by GameOverScreen::Update case-1 postlude: m_pBonusScreen->m_PhaseTimer = m_Timer;
    // BonusScreen::Update only READS m_PhaseTimer; it must NOT advance it here.
#ifndef __bada__
    {
        static int s_LogFrame = 0;
        static int s_LogThrottle = 0;
        if (s_LogThrottle == 0) {
            LOG_VERBOSE("BONUS_SCREEN", "frame=%d phaseTimer=%.3f awards=%d",
                        s_LogFrame,
                        m_PhaseTimer,
                        (int)m_Awards.size());
        }
        s_LogFrame++;
        s_LogThrottle = (s_LogThrottle + 1) % 60;
    }
#endif

    // -----------------------------------------------------------------------
    // Dismiss check (binary: early-out before phase B/C logic)
    // pfVar14[4] + pfVar14[3] = 7.0 + 0.25 = 7.25
    // -----------------------------------------------------------------------
    if (m_PhaseTimer > kDismissBuffer + kFinaleHoldExtra) {
        m_bPendingRemoval = 1;
    }

    // -----------------------------------------------------------------------
    // Phase A: pre-show slide-in (timer < 0)
    // -----------------------------------------------------------------------
    if (m_PhaseTimer < 0.0f) {
        // Slide-in from off-screen. m_PosOffset.y interpolates toward 0.
        // TODO: resolve exact slide-in math from binary @ 0x00132930
        m_PosOffset.y = m_PhaseTimer * kRevealStart;

        // Start rush SFX once.
        // TODO: SoundManager::PreLoadSound / play "BonusRush" into m_RushSFX
        return;
    }

    // Compute finaleStart: revealStart + perAward * (numAwards + 0.25)
    float finaleStart = kRevealStart + kPerAward * ((float)m_Awards.size() + 0.25f);

    // -----------------------------------------------------------------------
    // Phase B: per-award reveal (0 <= timer < finaleStart)
    // -----------------------------------------------------------------------
    if (m_PhaseTimer < finaleStart) {
        // ASM-verified: 2026-05-22 binary @ 0x00132930 (re-analyst).
        m_DisplayedScore = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            float normT = (m_PhaseTimer - (float)i * kPerAward) / kPerAward;

            if (normT < 0.0f || normT > 1.0f) {
                // Outside this award's reveal window.
                entry.m_Colour.a = 0xff;
                entry.m_DisplayedScore = entry.m_TierBase * entry.m_Multiplier;
                entry.m_Scale = 1.0f;
            } else {
                // Within reveal beat.
                float beat = fmodf(m_PhaseTimer, kPerAward);
                float alpha = beat / 0.2f;
                if (alpha > 1.0f) alpha = 1.0f;
                if (alpha < 0.0f) alpha = 0.0f;
                entry.m_Colour.a = (uint8_t)(alpha * 255.0f);
                if (beat <= 0.0f) {
                    entry.m_DisplayedScore = 0;
                } else {
                    entry.m_DisplayedScore = (int)((float)(entry.m_TierBase * entry.m_Multiplier) * (beat * 0.5f + 0.5f));
                }
                // Scale wobble: SinIdx(beat*115*182) / SinIdx(0x5550).
                // SinIdx is BAM-style (65536 = 2*pi). sinf approximation pending BAM-table port.
                float angle = beat * 115.0f * 182.0f * (2.0f * 3.14159265f / 65536.0f);
                float denom = 0.882f;  // sinf(0x5550 * 2*pi/65536) ~= 0.882
                entry.m_Scale = sinf(angle) / denom;
            }
            m_DisplayedScore += entry.m_DisplayedScore;
        }
        return;
    }

    // -----------------------------------------------------------------------
    // Phase C: finale one-shot (timer >= finaleStart, only once)
    // Binary: edge triggered — fires when prev <= finaleStart < m_PhaseTimer.
    // m_LeaderboardSubmitted is reused as the "finale fired" latch.
    // -----------------------------------------------------------------------
    if (m_LeaderboardSubmitted == 0) {
        m_LeaderboardSubmitted = 1;
        AwardScores();
        // Defunct: LeaderboardManager::RefreshLeaderboard (online-services-audit).

        // Tally final displayed scores.
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            m_Awards[i].m_DisplayedScore = m_Awards[i].m_TierBase * m_Awards[i].m_Multiplier;
            totalDisplayed += m_Awards[i].m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
    }

    // -----------------------------------------------------------------------
    // Pulse colour update (independent of phase)
    // -----------------------------------------------------------------------
    if (m_PulseTimer > 0.0f) {
        m_PulseTimer -= dt;
        // Damped wobble toward m_PulseTarget.
        // TODO: resolve exact wobble math from binary @ 0x00132c80
        float wobble = m_PulseTimer * m_PulseField17;
        m_PulseAngle = (int16_t)((int)m_PulseAngle + (int)(wobble * 100.0f));
        (void)wobble;
    }
}

// ---------------------------------------------------------------------------
// Draw (binary @ 0x0013325C)
// ---------------------------------------------------------------------------

// Binary @ 0x0013325C -- BonusScreen::Draw
// Layout: HUDControl3d::Draw renders dialog box; then total score (pFontBlue2,
// scale 26..40 by displayed/total ratio); then per-award row loop with
// star icon + name + score (pFontMain). Row Y step = -42.0 (timerArr[5]).
// Row 0 base = pos + m_PosOffset + Vec3(-105, 40, 0).
// Loop break (NOT continue) on first not-yet-revealed award.
// ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
void BonusScreen::Draw(const Vec3& hudScale, int layerMask) {
    // Apply m_PosOffset to pos before base draw, save original.
    Vec3 savedPos = pos;
    pos.x += m_PosOffset.x;
    pos.y += m_PosOffset.y;
    pos.z += m_PosOffset.z;

    // Background dialog box.
    HUDControl3d::Draw(hudScale, layerMask);

    Game* game = Game::GetInstance();
    if (!game) {
        pos = savedPos;
        return;
    }

    // --- Total-score text (pFontBlue2, scale 26..40, alignment 0x0F) ---
    // Binary @ 0x0013325C: scale = 26 + 14 * (displayed/total), clamped when total <= 0.
    // ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%i", m_DisplayedScore);

        float scale;
        if (m_DisplayedScore < 1) {
            scale = 26.0f;
        } else {
            scale = ((float)m_DisplayedScore / (float)m_TotalScore) * 14.0f + 26.0f;
        }
        scale *= m_NameScale;

        // kScoreOffset = Vec3(40, -60, 0) — BonusScreen .bss +0x0c @ 0x0022f32C
        Vec3 textPos(pos.x + 40.0f, pos.y + (-60.0f), pos.z + 0.0f);
        // kTotalScoreColour = Colour(0, 0, 0, 0xff) — DAT @ 0x001f34d4
        Colour totalColour(0, 0, 0, 0xff);

        Mortar::Font* fontBlue2 = game_work.pFontBlue2.Get();
        if (fontBlue2) {
            Mortar::Utf8StringIterator iter(buf);
            // TODO: CopyGlobalVec2_BonusScreen — pass Vec2(0,0) until global is RE'd
            Vec2 align(0.0f, 0.0f);
            fontBlue2->DrawString(scale, 1.0f, 0.0f, iter, textPos, totalColour,
                                  align, 0x0F, 0.0f, nullptr);
        }
    }

    // --- Pre-loop offset: kStarOffset = Vec3(-105, 40, 0) — .bss +0x44 @ 0x0022f364 ---
    // ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
    pos.x += -105.0f;
    pos.y += 40.0f;
    // pos.z += 0.0f  (no z change)

    // --- Per-award row loop (pFontMain) ---
    // ASM-verified: 2026-05-18 binary @ 0x0013325C (re-analyst)
    Mortar::Font* fontMain = game_work.pFontMain.Get();
    MatrixManager& mm = MatrixManager::GetInstance();

    for (int i = 0; i < (int)m_Awards.size(); ++i) {
        // Visibility gate — break (not continue): awards revealed in order.
        if (m_PhaseTimer - kRevealStart < (float)i * kPerAward) break;

        const BonusAwardHud& entry = m_Awards[i];

        // Star quad: DrawQuadUnCached equivalent — port uses renderer.DrawQuad.
        if (entry.m_StarTex.IsValid()) {
            entry.m_StarTex->Set();
            uint32_t texW = (uint32_t)entry.m_StarTex->m_Width;
            uint32_t texH = (uint32_t)entry.m_StarTex->m_Height;
            float w = (float)texW + 1.0f;
            float h = (float)texH + 1.0f;

            Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
            mat.GlobalTranslate44(pos);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            game->renderer.DrawQuad(entry.m_Colour, 0.0f, 0.0f, 1.0f, 1.0f);
            entry.m_StarTex->UnSet();
        }

        if (fontMain) {
            // Award name: scale = 20, clamped so measure*20 <= 220.
            Mortar::Utf8StringIterator nameIter(entry.m_Name);
            float measure = fontMain->MeasureString(nameIter);
            float nameScale = 20.0f;
            if (measure * 20.0f > 220.0f) {
                nameScale = 220.0f / measure;
            }
            Vec2 align(0.0f, 0.0f);
            // Re-construct iter (MeasureString may advance it).
            Mortar::Utf8StringIterator nameIter2(entry.m_Name);
            fontMain->DrawString(nameScale, 1.0f, 0.0f, nameIter2, pos,
                                 entry.m_Colour, align, 0x0D, 0.0f, nullptr);

            // Award score: scale = entry.m_Scale * 24, pos += Vec3(250,250,250).
            // NOTE: Vec3(250,250,250) is the literal binary constant (timerArr[6] = 250.0).
            char scoreBuf[64];
            snprintf(scoreBuf, sizeof(scoreBuf), "%i", entry.m_DisplayedScore);
            Vec3 scorePos(pos.x + kTextOffsetScalar,
                          pos.y + kTextOffsetScalar,
                          pos.z + kTextOffsetScalar);
            float scoreScale = entry.m_Scale * 24.0f;
            Mortar::Utf8StringIterator scoreIter(scoreBuf);
            fontMain->DrawString(scoreScale, 1.0f, 0.0f, scoreIter, scorePos,
                                 entry.m_Colour, align, 0x0D, 0.0f, nullptr);
        }

        // Advance Y for next row: kAwardYStep = -42.0 (timerArr[5]).
        pos.y += kAwardYStep;
    }

    // Restore saved pos.
    pos = savedPos;
}
