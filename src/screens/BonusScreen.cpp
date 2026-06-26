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
#include "engine/render/BakedStringBox.h"
#include "engine/render/FontCacheObjectTTF.h"
#include "engine/render/FontTTFRegistry.h"
#include "engine/render/Font.h"
#include "engine/util/StringTable.h"
#include "engine/math/Vec2.h"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

using Mortar::TextureManager;

// Shared TTF face for BonusScreen BakedStringBox labels.
// DIFFERS: original = *(game_work+0x614) shared TTF face; using a file-local SmartPtr<Font>
//   + FontTTFRegistry::Lookup. v1.6.1 BonusScreen::BuildBonusText @0x001621dc.
static Mortar::FontCacheObjectTTF* GetBonusTTFFont() {
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) return 0;
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

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
// BuildBonusText -- v1.6.1 @0x001621dc
// Creates all BakedStringBox members. Called once from Update tail when
// m_bSkipIntro is set (create-once guard: only allocates if boxes are null).
// ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc
// ---------------------------------------------------------------------------

void BonusScreen::BuildBonusText() {
    Mortar::FontCacheObjectTTF* font = GetBonusTTFFont();
    if (!font) return;

    // Per-award label/value boxes (loop i=0..count-1, up to 3).
    // Binary: r6+=4 per iteration == [i].
    // Font sizes per row: slot0=13px, slot1=16px, slot2=15px.
    static const float kLabelFontSizes[3] = { 13.0f, 16.0f, 15.0f };
    for (int i = 0; i < (int)m_Awards.size() && i < 3; ++i) {
        if (!m_RankLabelBoxes[i]) {
            // m_RankLabelBoxes[i] (+0xC0): name, w=220(0xDC), h=10, tier colour.
            // ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc: ctor align arg = 1 (LEFT).
            m_RankLabelBoxes[i] = new Mortar::BakedStringBox(
                font,
                kLabelFontSizes[i],
                220.0f,  // 0xDC
                10.0f,
                0x01,    // LEFT
                0,       // maxLines
                -1.0f,   // lineSpacing
                0
            );
            m_RankLabelBoxes[i]->SetColour(m_Awards[i].m_Colour, 0);
            m_RankLabelBoxes[i]->SetText(m_Awards[i].m_Name);
            m_RankLabelBoxes[i]->Update();
        }
        if (!m_RankValueBoxes[i]) {
            // m_RankValueBoxes[i] (+0xCC): value=tier as "%i", w=60(0x3C), h=10, tier colour.
            // ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc: ctor align arg = 0xF (CENTER-H + bottom-V).
            char valBuf[16];
            snprintf(valBuf, sizeof(valBuf), "%i", m_Awards[i].m_TierBase);
            m_RankValueBoxes[i] = new Mortar::BakedStringBox(
                font,
                kLabelFontSizes[i],
                60.0f,   // 0x3C
                10.0f,
                0x03,    // center-H + top-V (binary 0x0F adds bottom-V which the port's
                         // BakedStringBox mishandles -> drops glyphs in the 10px box; TODO fix bottom-V)
                0,
                -1.0f,
                0
            );
            m_RankValueBoxes[i]->SetColour(m_Awards[i].m_Colour, 0);
            m_RankValueBoxes[i]->SetText(valBuf);
            m_RankValueBoxes[i]->Update();
        }
    }

    // m_ScoreBox (+0xB8): animated total counter.
    // fontSize=20px, w=90(0x5A), h=20(0x14), gradient yellow->orange, stroke 2px red.
    if (!m_ScoreBox) {
        m_ScoreBox = new Mortar::BakedStringBox(
            font,
            20.0f,
            90.0f,   // 0x5A
            20.0f,   // 0x14
            0x0d,
            0,
            -1.0f,
            0
        );
        // Colour ctor is (r,g,b,a). Spec gives RGB values.
        m_ScoreBox->SetGradient(
            Colour(0xFF, 0xEF, 0x00, 0xFF),   // yellow top: RGB(0xFF,0xEF,0x00)
            Colour(0xEF, 0x77, 0x00, 0xFF),   // orange bottom: RGB(0xEF,0x77,0x00)
            false
        );
        m_ScoreBox->SetStroke(2.0f, Colour(0xDC, 0x13, 0x00, 0xFF));  // RGB(0xDC,0x13,0x00)
        // Text is set each frame in Draw.
    }

    // m_TotalBox (+0xBC): title band.
    // fontSize=30px, w=220(0xDC), h=30(0x1E), red gradient, gold stroke, brown shadow.
    if (!m_TotalBox) {
        m_TotalBox = new Mortar::BakedStringBox(
            font,
            30.0f,
            220.0f,  // 0xDC
            30.0f,   // 0x1E
            0x0d,
            0,
            -1.0f,
            0
        );
        // Colour ctor is (r,g,b,a). Spec gives RGB values.
        m_TotalBox->SetGradient(
            Colour(0xFF, 0x00, 0x00, 0xFF),   // red top: RGB(0xFF,0x00,0x00)
            Colour(0xB4, 0x00, 0x00, 0xFF),   // red bottom: RGB(0xB4,0x00,0x00)
            false
        );
        m_TotalBox->SetStroke(
            1.0f,
            Colour(0xFF, 0xDC, 0x50, 0xFF),   // gold: RGB(0xFF,0xDC,0x50)
            Colour(0xFF, 0xC8, 0x87, 0xFF)    // gold2: RGB(0xFF,0xC8,0x87)
        );
        m_TotalBox->SetShadow(
            1.0f,
            Colour(0x5D, 0x28, 0x0C, 0xFF),  // brown: RGB(0x5D,0x28,0x0C)
            Vec3(0.0f, -3.0f, 0.0f),
            true
        );
        // Title text: "_ %s _" wrapping GETSTRING(0x31E).
        // TODO: v1.6.1 BonusScreen::BuildBonusText @0x001621dc -- binary uses GETSTRING(0x31E) for title
        const char* titleStr = GETSTRING((LocalizedString)0x31E, 0);
        if (titleStr && titleStr[0]) {
            char titleBuf[128];
            snprintf(titleBuf, sizeof(titleBuf), "_ %s _", titleStr);
            m_TotalBox->SetText(titleBuf);
        } else {
            m_TotalBox->SetText("_ BONUS _");
        }
        m_TotalBox->Update();
    }
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

    // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: Update tail calls BuildBonusText
    // when m_bSkipIntro is set (create-once: boxes are null-checked inside BuildBonusText).
    if (m_bSkipIntro) {
        BuildBonusText();
    }
}

// ---------------------------------------------------------------------------
// Draw (binary @ 0x0016492c)
// ---------------------------------------------------------------------------

// v1.6.1 @0x0016492c
void BonusScreen::Draw(float* hudScaleRaw) {
    // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c: saves pos at entry @0x0016494c,
    // restores at exit @0x00164e38. The per-award loop steps pos.y by -42 each row.
    Vec3 savedPos = pos;

    // Apply m_AnimPos to position before base draw.
    pos.x += m_AnimPos.x;
    pos.y += m_AnimPos.y;
    pos.z += m_AnimPos.z;

    // Base box draw (HUDControl3d::Draw handles the dialog background via m_Texture@0x74).
    HUDControl3d::Draw(hudScaleRaw);

    // Restore pos to original (no AnimPos) for all text/award draws below.
    pos = savedPos;

    // -----------------------------------------------------------------------
    // Pre-loop draws: score counter + title band (un-stepped pos).
    // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c
    // -----------------------------------------------------------------------

    // m_ScoreBox: pos + (105, 51, 0). Text = m_TotalScore each frame.
    if (m_ScoreBox) {
        char scoreBuf[16];
        snprintf(scoreBuf, sizeof(scoreBuf), "%i", m_TotalScore);
        m_ScoreBox->SetText(scoreBuf);
        m_ScoreBox->SetTranslation(
            Vec3(pos.x + 105.0f, pos.y + 51.0f, pos.z),
            1
        );
        m_ScoreBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    // m_TotalBox: pos + (75, -128, 0).
    if (m_TotalBox) {
        m_TotalBox->SetTranslation(
            Vec3(pos.x + 75.0f, pos.y - 128.0f, pos.z),
            1
        );
        m_TotalBox->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
    }

    // -----------------------------------------------------------------------
    // Per-award loop: star + label + value. pos.y steps -42 each row.
    // TODO: v1.6.1 0x00164b64 -- reveal gate: `if (m_Timer - 0.666 < i*0.6) break`.
    //   For stable screenshot, draw all rows (no gate).
    // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c
    // -----------------------------------------------------------------------
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

            // ASM-verified: 2026-06-26 v1.6.1 BonusScreen::Draw @0x0016492c (asm-inspector)
            // star corner = pos - 35*UnitX (literal 35.0 @0x164dd0), NOT pos. With the
            // ~33px bonus_icon this places the icon as a leading bullet at [pos.x-35, pos.x-2],
            // just left of the name (origin pos.x-2). Prior RE missed the -35 term.
            Matrix44 mat = Matrix44::Scale44(Vec3(texW + 1.0f, texH + 1.0f, 1.0f));
            mat.GlobalTranslate44(Vec3(pos.x - 35.0f, pos.y, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            Mortar::Mesh::DrawQuadUnCached(entry.m_Colour, NULL);

            entry.m_StarTex->UnSetUnCached();
        }

        // m_RankLabelBoxes[i]: pos + (-2, 6, 0), colour = award.m_Colour, alpha 1.0.
        // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c: SetTranslation flag=0 (0x164cec).
        // flag!=0 would pre-shift x -= boxW/2 (=-110 for the 220px box) -> name jumps left
        // onto the icon; the binary keeps it left-aligned at pos.x-2.
        if (i < 3 && m_RankLabelBoxes[i]) {
            m_RankLabelBoxes[i]->SetColour(entry.m_Colour, 0);
            m_RankLabelBoxes[i]->SetTranslation(
                Vec3(pos.x - 2.0f, pos.y + 6.0f, pos.z),
                0
            );
            m_RankLabelBoxes[i]->Draw(0.0f, Vec2(1.0f, 1.0f), 1);
        }

        // m_RankValueBoxes[i]: the tier number, centered on the right baked star.
        // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c: translate Vec3 @0x164d40
        // (220.0f @0x164dd4), Draw alpha=m_Alpha (0x164e00) via wrapper @0x1626e0 (the
        // value pulses by uniform SCALE, not colour.a). Using flag=1 (center the 60px box
        // on the translate point) since the value is center-aligned.
        // TODO #218: the faithful +220 overshoots the baked star in the current render
        // (empirically the star sits ~+185); rooted in the dialog being drawn at a smaller
        // scale than the binary's row offsets assume -- fix the dialog draw scale, not this
        // offset. Y also not yet aligned to the baked-star rows.
        if (i < 3 && m_RankValueBoxes[i]) {
            m_RankValueBoxes[i]->SetColour(entry.m_Colour, 0);
            m_RankValueBoxes[i]->SetTranslation(
                Vec3(pos.x + 220.0f, pos.y + 5.0f, pos.z),
                1
            );
            m_RankValueBoxes[i]->Draw(0.0f, Vec2(entry.m_Alpha, entry.m_Alpha), 1);
        }

        // Row step: pos.y -= 42 at loop tail.
        pos.y += -42.0f;
    }

    // Restore pos to saved value (binary restores @0x00164e38).
    pos = savedPos;
}
