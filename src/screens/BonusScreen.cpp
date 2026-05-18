// Analysed: 2026-05-03T00:00
// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// Binary: ctor 0x00132048, dtor 0x00131F9C, Update 0x00132930,
//         Draw 0x0013325C, AddAward 0x00133664, AwardScores 0x0013260C

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "Game.h"
#include "engine/audio/MortarSound.h"
#include "engine/asset/TextureManager.h"
#include "engine/math/MathUtil.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

using Mortar::TextureManager;

// Phase-timer rodata constants — binary @ GOT_DAT_00132cdc area.
// TODO: resolve phase-timer rodata @ DAT_00132cdc
static const float PRE_OFFSET     = 1.0f;
static const float AWARD_SPACING  = 0.5f;
static const float REVEAL_END     = 1.0f;
static const float FINALE_HOLD    = 0.5f;
static const float DISMISS_BUFFER = 0.5f;

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
    // TODO: Coin::MakeCoins(m_TotalScore / 6)
    // TODO: FruitCamera::CreateCameraShake(...)
    // TODO: PSPParticleManager::AddEmitter(...) big finale particle
    // TODO: play "BonusFinale" SFX via m_RushSFX or SoundManager
    (void)m_TotalScore;
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
    // Phase A: pre-show slide-in (timer < 0)
    // -----------------------------------------------------------------------
    if (m_PhaseTimer < 0.0f) {
        // Slide-in from off-screen. m_PosOffset.y interpolates toward 0.
        // TODO: resolve exact slide-in math from binary @ 0x00132930
        m_PosOffset.y = m_PhaseTimer * PRE_OFFSET;

        // Start rush SFX once.
        // TODO: SoundManager::PreLoadSound / play "BonusRush" into m_RushSFX
        return;
    }

    // -----------------------------------------------------------------------
    // Phase B: per-award reveal (0 <= timer < REVEAL_END + awards * AWARD_SPACING)
    // -----------------------------------------------------------------------
    float revealEnd = REVEAL_END + (float)(m_Awards.size() > 0 ? (int)m_Awards.size() - 1 : 0) * AWARD_SPACING;
    if (m_PhaseTimer < revealEnd) {
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            float localT = m_PhaseTimer - (float)i * AWARD_SPACING;

            if (localT < 0.0f) {
                // Not yet revealed.
                entry.m_Scale          = 0.0f;
                entry.m_DisplayedScore = 0;
                continue;
            }

            // Just-crossed-zero this frame: spawn emitters + play SFX.
            // "Just crossed" = localT < dt (first frame localT >= 0).
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
            float scoreT = localT * 0.5f + 0.5f;
            if (scoreT > 1.0f) scoreT = 1.0f;
            entry.m_DisplayedScore = (int)((float)(entry.m_TierBase * entry.m_Multiplier) * scoreT);

            totalDisplayed += entry.m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
        return;
    }

    // -----------------------------------------------------------------------
    // Phase C: finale one-shot (timer >= REVEAL_END + ..., only once)
    // -----------------------------------------------------------------------
    float finaleStart = revealEnd;
    if (m_PhaseTimer >= finaleStart && m_LeaderboardSubmitted == 0) {
        m_LeaderboardSubmitted = 1;
        AwardScores();
        // Note: LeaderboardManager::RefreshLeaderboard -- defunct (online-services-audit).

        // Tally final displayed scores.
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            m_Awards[i].m_DisplayedScore = m_Awards[i].m_TierBase * m_Awards[i].m_Multiplier;
            totalDisplayed += m_Awards[i].m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
    }

    // -----------------------------------------------------------------------
    // Phase D: dismiss (timer past finale + hold + buffer)
    // -----------------------------------------------------------------------
    float dismissAt = finaleStart + FINALE_HOLD + DISMISS_BUFFER;
    if (m_PhaseTimer >= dismissAt) {
        m_bPendingRemoval = 1;
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
