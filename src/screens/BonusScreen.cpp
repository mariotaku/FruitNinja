// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// v1.6.1: ctor @0x00162d1c, dtor D2 @0x00162724 / D1 @0x0016283c / D0 @0x00162954,
//         Update @0x00163dd0, Draw @0x0016492c
// AddAward / AwardScores -- TODO: re-verify v1.6.1 addr (prior 0x00133664/0x0013260C stale v1.5.x)

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "Game.h"
#include "engine/audio/MortarSound.h"
#include "engine/audio/SoundManager.h"
#include "engine/asset/TextureManager.h"
#include "engine/asset/Mesh.h"
#include "engine/math/MathUtil.h"
#include "engine/math/Matrix44.h"
#include "engine/render/MatrixManager.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

using Mortar::TextureManager;

// Phase-timer rodata constants — binary @ GOT_DAT_00162cdc area.
// TODO: resolve phase-timer rodata @ DAT_00162cdc (v1.6.1 BonusScreen::Update @0x00163dd0)
static const float PRE_OFFSET     = 1.0f;
static const float AWARD_SPACING  = 0.5f;
static const float REVEAL_END     = 1.0f;
static const float FINALE_HOLD    = 0.5f;
static const float DISMISS_BUFFER = 0.5f;

// Transition-in slide duration (init value for m_Timer; binary loads from rodata).
// TODO: resolve exact init constant from ctor @0x00162d1c
static const float TRANSITION_IN_TIME = 1.0f;

// ---------------------------------------------------------------------------
// BonusScreen ctor (binary @ 0x00162d1c)
// ---------------------------------------------------------------------------

BonusScreen::BonusScreen()
    : HUDControl3d(),
      m_TotalScore(0),
      m_DisplayedScore(0),
      m_ShakeAmplitude(0.0f),
      m_ShakeTimer(0.0f),
      m_ShakeDuration(1.0f),
      m_ShakeAngle(0),
      _padShake(0),
      m_ShakeOffset(0.0f, 0.0f, 0.0f),
      m_NamePulseScale(1.0f),
      m_FinaleFired(false),
      field_0xB1(false),
      field_0xB2(false),
      _padB3(0),
      m_RushLoopSFX(nullptr),
      m_ScoreBox(nullptr),
      m_TotalBox(nullptr),
      m_bSkipIntro(false),
      m_Timer(-TRANSITION_IN_TIME),
      m_AnimPos(0.0f, 0.0f, 0.0f)
{
    m_RankLabelBoxes[0] = nullptr;
    m_RankLabelBoxes[1] = nullptr;
    m_RankLabelBoxes[2] = nullptr;
    m_RankValueBoxes[0] = nullptr;
    m_RankValueBoxes[1] = nullptr;
    m_RankValueBoxes[2] = nullptr;

    m_Awards.reserve(3);

    // ASM-spec v1.6.1 BonusScreen::BonusScreen @ 0x00162d1c: loads "arcade_diolog_box.tex"
    // (rodata @0x00281e4c); size = (tex+0x24 width, tex+0x28 height).
    // TODO: v1.6.1 0x00162d5c (BonusScreen::BonusScreen) — binary caches backing tex in a load-once static
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        TextureManager::LoadLocalisedTexture("arcade_diolog_box.tex");
    m_Texture = bgTex;
    if (bgTex.IsValid()) {
        size = Vec3((float)bgTex->GetWidth(), (float)bgTex->GetHeight(), 0.0f);
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

    // TODO: v1.6.1 BonusScreen::SetUpBonusScreen @0x0012ede8 -- create the BakedStringBox score/rank boxes
}

// ---------------------------------------------------------------------------
// BonusScreen dtor (binary D2 @ 0x00162724)
// Binary order: loop i=0..2 delete m_RankLabelBoxes[i] + m_RankValueBoxes[i],
//               then delete m_ScoreBox, then delete m_TotalBox.
//               m_Awards vector and base HUDControl3d dtors follow (implicit).
// ---------------------------------------------------------------------------

BonusScreen::~BonusScreen() {
    for (int i = 0; i < 3; ++i) {
        delete m_RankLabelBoxes[i];
        m_RankLabelBoxes[i] = 0;
        delete m_RankValueBoxes[i];
        m_RankValueBoxes[i] = 0;
    }
    delete m_ScoreBox;
    m_ScoreBox = 0;
    delete m_TotalBox;
    m_TotalBox = 0;
    // m_RushLoopSFX: not owned (pointer to a manager-owned sound), no delete.
    // m_Awards vector destructs its BonusAwardHud entries (SmartPtr released).
}

// ---------------------------------------------------------------------------
// AddAward -- TODO: re-verify v1.6.1 addr (prior 0x00133664 stale v1.5.x)
// ---------------------------------------------------------------------------

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
    entry.m_Alpha          = 1.0f;
    m_TotalScore          += tier;
    m_Awards.push_back(entry);
}


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
// AwardScores -- TODO: re-verify v1.6.1 addr (prior 0x0013260C stale v1.5.x)
// One-shot finale: coin spawn, camera shake, big particle, finish SFX.
// ---------------------------------------------------------------------------

void BonusScreen::AwardScores() {
    // TODO: Coin::MakeCoins(m_TotalScore / 6)
    // TODO: FruitCamera::CreateCameraShake(...)
    // TODO: PSPParticleManager::AddEmitter(...) big finale particle
    // TODO: play "BonusFinale" SFX via m_RushLoopSFX or SoundManager
    (void)m_TotalScore;
}

// ---------------------------------------------------------------------------
// Update (binary @ 0x00163dd0) — three-phase state machine driven by m_Timer
// ---------------------------------------------------------------------------

// v1.6.1 @0x00163dd0
void BonusScreen::Update(float dt) {
    // Advance phase timer unconditionally each frame.
    m_Timer += dt;

    // -----------------------------------------------------------------------
    // Phase A: pre-show slide-in (timer < 0)
    // -----------------------------------------------------------------------
    if (m_Timer < 0.0f) {
        // Slide-in from off-screen. m_AnimPos.y interpolates toward 0.
        // TODO: resolve exact slide-in math from binary @ 0x00163dd0
        m_AnimPos.y = m_Timer * PRE_OFFSET;

        // Start rush SFX once.
        // TODO: SoundManager::PreLoadSound / play "BonusRush" into m_RushLoopSFX
        return;
    }

    // -----------------------------------------------------------------------
    // Phase B: per-award reveal (0 <= timer < REVEAL_END + awards * AWARD_SPACING)
    // -----------------------------------------------------------------------
    float revealEnd = REVEAL_END + (float)(m_Awards.size() > 0 ? (int)m_Awards.size() - 1 : 0) * AWARD_SPACING;
    if (m_Timer < revealEnd) {
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            float localT = m_Timer - (float)i * AWARD_SPACING;

            if (localT < 0.0f) {
                // Not yet revealed.
                entry.m_Alpha          = 0.0f;
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

            // Alpha pulse on reveal -- TODO: resolve exact formula from binary @ 0x00163dd0
            entry.m_Alpha = 1.0f + 0.3f * sinf(localT * 6.28f);
            if (entry.m_Alpha < 0.0f) entry.m_Alpha = 0.0f;

            // Score counter ramp-up.
            // TODO: resolve exact multiplier ramp math from binary @ 0x00163dd0
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
    if (m_Timer >= finaleStart && !m_FinaleFired) {
        m_FinaleFired = true;
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
    if (m_Timer >= dismissAt) {
        m_bPendingRemoval = 1;
    }

    // -----------------------------------------------------------------------
    // Shake update (independent of phase)
    // -----------------------------------------------------------------------
    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;
        // Damped wobble around m_ShakeOffset.
        // TODO: resolve exact wobble math from binary @ 0x00163dd0
        float wobble = m_ShakeTimer * m_ShakeDuration;
        m_ShakeAngle = (uint16_t)((int)m_ShakeAngle + (int)(wobble * 100.0f));
        (void)wobble;
    }
}

// ---------------------------------------------------------------------------
// Draw (binary @ 0x0016492c)
// ---------------------------------------------------------------------------

// v1.6.1 @0x0016492c
void BonusScreen::Draw(float* hudScaleRaw) {
    // Apply m_AnimPos to position before base draw.
    Vec3 savedPos = pos;
    pos.x += m_AnimPos.x;
    pos.y += m_AnimPos.y;
    pos.z += m_AnimPos.z;

    // Base box draw (HUDControl3d::Draw handles the dialog background via m_Texture@0x74).
    HUDControl3d::Draw(hudScaleRaw);

    // Restore position.
    pos = savedPos;

    // Per-award rendering.
    // TODO: v1.6.1 0x00164b64 -- per-award reveal spacing (gate on m_Timer); draw all for now.
    MatrixManager& mm = MatrixManager::GetInstance();
    for (int i = 0; i < (int)m_Awards.size(); ++i) {
        const BonusAwardHud& entry = m_Awards[i];

        // Star icon draw (only if texture is valid).
        // Mirrors BSButton::Draw API: SetUnCached -> Scale44 -> GlobalTranslate44 ->
        // SetCurrentMatrix -> UploadModelViewOnly -> DrawQuadUnCached -> UnSetUnCached.
        if (entry.m_StarTex.IsValid()) {
            float texW = (float)entry.m_StarTex->GetWidth();
            float texH = (float)entry.m_StarTex->GetHeight();

            entry.m_StarTex->SetUnCached();

            Matrix44 mat = Matrix44::Scale44(Vec3(texW + 1.0f, texH + 1.0f, 1.0f));
            mat.GlobalTranslate44(pos);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            Mortar::Mesh::DrawQuadUnCached(entry.m_Colour, NULL);

            entry.m_StarTex->UnSetUnCached();
        }

        // TODO: v1.6.1 0x0016492c (BonusScreen::Draw) -- label/value/score text boxes
        //   (blocked on #212: BakedStringBox builds unimplemented)
        (void)entry;
    }
}
