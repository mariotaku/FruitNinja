// Analysed: 2026-05-03T00:00
// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// Binary: ctor 0x00132048, dtor 0x00131F9C, Update 0x00132930,
//         Draw 0x0013325C, AddAward 0x00133664, AwardScores 0x0013260C

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "Game.h"
#include "engine/audio/MortarSound.h"
#include "engine/audio/SoundManager.h"
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

    // ASM-spec v1.6.1 BonusScreen::BonusScreen @ 0x00162d1c: loads "arcade_diolog_box.tex"
    // (rodata @0x00281e4c); size = (tex+0x24 width, tex+0x28 height).
    // TODO: v1.6.1 0x00162d5c (BonusScreen::BonusScreen) — binary caches backing tex in a load-once static
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        TextureManager::LoadLocalisedTexture("arcade_diolog_box.tex");
    m_SecondaryTex = bgTex;
    if (bgTex.IsValid()) {
#if !defined(__bada__)
        size = Vec3((float)bgTex->m_Width, (float)bgTex->m_Height, 0.0f);
#endif
    }

    // ASM-spec v1.6.1 BonusScreen::BonusScreen @ 0x00162ea4: preloads
    // Bonus-Firework-Explode, Bonus-drum-roll(count=1), equip-unlock,
    // Bonus-Explosion-1/3/5.
    Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();
    sm.PreLoadSound("Bonus-Firework-Explode");   // rodata 0x00281e13
    // DIFFERS: binary uses the count=1 preload variant for "Bonus-drum-roll"
    // (PreLoadSoundEx slot1, count arg=1); port maps this to PreLoadSoundEx(name, true).
    sm.PreLoadSoundEx("Bonus-drum-roll", true);  // rodata 0x00281e62
    sm.PreLoadSound("equip-unlock");             // rodata 0x0027f955
    sm.PreLoadSound("Bonus-Explosion-1");        // rodata 0x00281e72
    sm.PreLoadSound("Bonus-Explosion-3");        // rodata 0x00281e84
    sm.PreLoadSound("Bonus-Explosion-5");        // rodata 0x00281e96
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
void BonusScreen::AddAward(Colour colour, Mortar::SmartPtr<Mortar::Texture> tex,
                           const char* name, int tier) {
    BonusAwardHud entry;
    if (name) {
        std::strncpy(entry.m_Name, name, sizeof(entry.m_Name) - 1);
        entry.m_Name[sizeof(entry.m_Name) - 1] = '\0';
    }
    entry.m_StarTex        = tex;
    entry.m_TierBase       = tier;
    entry.m_DisplayedScore = 0;
    entry.m_Colour         = colour;
    entry.m_Scale          = 1.0f;
    m_TotalScore          += tier;
    m_Awards.push_back(entry);
}

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
    // Advance phase timer unconditionally each frame.
    m_PhaseTimer += dt;

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
