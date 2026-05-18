// Analysed: 2026-05-03T00:00
// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// Binary: ctor 0x00132048, dtor 0x00131F9C, Update 0x00132930,
//         Draw 0x0013325C, AddAward 0x00133664, AwardScores 0x0013260C

#include "BonusScreen.h"
#include "hud/HUD.h"
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
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

using Mortar::TextureManager;

// Phase-timer rodata — binary @ 0x001f3d48, five floats.
// ASM-verified: 2026-05-18 binary @ 0x001f3d48 (re-analyst)
static const float kRevealStart     = 0.66597f;  // pfVar14[0]
static const float kPerAward        = 0.6f;       // pfVar14[1]
static const float kRevealHalfBeat  = 0.33298f;   // pfVar14[2]
static const float kFinaleHoldExtra = 0.25f;      // pfVar14[3]
static const float kDismissBuffer   = 7.0f;       // pfVar14[4]

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
        if (game && game->pCamera) {
            Vec3 coinPos = coin->pos;
            game->pCamera->CreateCameraShake(coinPos, 0.3f, 0.75f);
        }
        if (game && game->pGameSound) {
            game->pGameSound->SFXPlay("Bonus-Firework-Explode", 1.0f, 1.0f);
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
      m_PulseColour(255, 255, 255, 255),
      _padA4(0),
      _padA8(0),
      m_NameScale(1.0f),
      m_LeaderboardSubmitted(0),
      _pad1(0),
      _pad2(0),
      _pad3(0),
      m_RushSFX(nullptr),
      m_PhaseTimer(0.0f),
      m_PosOffset(0.0f, 0.0f, 0.0f),
      _padfield23(0),
      _padfield24(0)
{
    m_Awards.reserve(3);

    // Load background texture into m_SecondaryTex.
    // TODO: resolve exact texture name from binary literal pool 0x00132210
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        TextureManager::LoadLocalisedTexture("dialog-box-big.tex");
    m_SecondaryTex = bgTex;

    // PreLoadSound calls — clip names at literal pool 0x00132210..0x00132224.
    // TODO: resolve clip names from binary literal pool 0x00132210..0x00132224
    // TODO: PreLoadSound("BonusRush");
    // TODO: PreLoadSound("BonusStar1");
    // TODO: PreLoadSound("BonusStar2");
    // TODO: PreLoadSound("BonusStar3");
    // TODO: PreLoadSound("BonusFinale");
    // TODO: PreLoadSound("BonusCount");
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

    // TODO: 0x0013260C — *(int*)(Game +0x34) = 3 (BonusFinalePhase flag, offset unconfirmed).

    Game* game = Game::GetInstance();
    if (game && game->pCamera) {
        game->pCamera->CreateCameraShake(spawnPos, 0.3f, 1.0f);
    }

    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        PSPParticleEmitter* e = pm.AddEmitter(StringHash("impact_fx"));
        if (e) { e->m_Pos = spawnPos; }
    }

    // Return value discarded in binary (cache-warming call).
    if (game) {
        (void)game->currentScore;
    }

    // Vol = 0.0f is the literal binary value (DAT_00132910 = 0x00000000).
    // Per-coin fireworks in AddToScoreOnArrival handle audibility.
    if (game && game->pGameSound) {
        game->pGameSound->SFXPlay("equip-unlock", 0.0f, 1.0f,
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
            printf("[BONUS_SCREEN] frame=%d phaseTimer=%.3f awards=%d\n",
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
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            // Each award reveals at kRevealStart + i * kPerAward.
            float revealTime = kRevealStart + (float)i * kPerAward;
            float localT = m_PhaseTimer - revealTime;

            if (localT < 0.0f) {
                entry.m_Scale          = 0.0f;
                entry.m_DisplayedScore = 0;
                continue;
            }

            // Just-crossed-zero this frame: spawn emitters + play SFX.
            if (localT < dt) {
                // TODO: PSPParticleManager::AddEmitter x3 for award[i]
                // TODO: FruitCamera::CreateCameraShake(...)
                // TODO: play SFX "BonusStar<i+1>" (BonusStar1/BonusStar2/BonusStar3)
            }

            // Scale pulse: sin-based scale wobble on reveal.
            // TODO: resolve exact sin formula from binary @ 0x00132a50
            entry.m_Scale = 1.0f + 0.3f * sinf(localT * 6.28f);
            if (entry.m_Scale < 0.0f) entry.m_Scale = 0.0f;

            // Score counter ramp-up.
            // TODO: resolve exact multiplier ramp math from binary @ 0x00132b00
            float scoreT = localT / kRevealHalfBeat;
            if (scoreT > 1.0f) scoreT = 1.0f;
            entry.m_DisplayedScore = (int)((float)(entry.m_TierBase * entry.m_Multiplier) * scoreT);

            totalDisplayed += entry.m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
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
        // Damped wobble around m_PulseColour.
        // TODO: resolve exact wobble math from binary @ 0x00132c80
        float wobble = m_PulseTimer * m_PulseField17;
        m_PulseAngle = (int16_t)((int)m_PulseAngle + (int)(wobble * 100.0f));
        (void)wobble;
    }
}

// ---------------------------------------------------------------------------
// Draw (binary @ 0x0013325C)
// ---------------------------------------------------------------------------

// Binary @ 0x0013325C
void BonusScreen::Draw(const Vec3& hudScale, int layerMask) {
    // Apply m_PosOffset to position before base draw.
    Vec3 savedPos = pos;
    pos.x += m_PosOffset.x;
    pos.y += m_PosOffset.y;
    pos.z += m_PosOffset.z;

    // Base box draw (HUDControl3d::Draw handles the dialog background via m_SecondaryTex).
    HUDControl3d::Draw(hudScale, layerMask);

    // Restore position.
    pos = savedPos;

    // Per-award rendering.
    for (int i = 0; i < (int)m_Awards.size(); ++i) {
        const BonusAwardHud& entry = m_Awards[i];
        if (entry.m_Scale <= 0.0f) continue;

        // TODO: set matrix scale + translate per award position
        // Award Y positions stacked vertically (TODO: resolve spacing constant)
        // float awardY = pos.y + m_PosOffset.y + (float)i * 20.0f;
        // float awardX = pos.x + m_PosOffset.x;

        // TODO: DrawQuadUnCached(entry.m_StarTex, awardX, awardY, entry.m_Scale * 16.0f, entry.m_Scale * 16.0f)
        // TODO: Font::DrawString(entry.m_Name, awardX + 20.0f, awardY, m_NameScale)
        // TODO: DrawString tier*multiplier score text

        (void)entry;
        (void)hudScale;
        (void)layerMask;
    }
}
