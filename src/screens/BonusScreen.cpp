// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// v1.6.1: ctor @0x00162d1c, dtor D2 @0x00162724 / D1 @0x0016283c / D0 @0x00162954,
//         Update @0x00163dd0, Draw @0x0016492c
// AddAward / AwardScores -- TODO: re-verify v1.6.1 addr (prior 0x00133664/0x0013260C stale v1.5.x)

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "Game.h"
#include "game/GameWork.h"
#include "engine/audio/MortarSound.h"
#include "engine/audio/SoundManager.h"
#include "engine/audio/GameSound.h"
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
// REVEAL_END / FINALE_HOLD / DISMISS_BUFFER removed: superseded by the memory-verified
// revealEnd formula and m_bPendingRemoval latch below (v1.6.1 BonusScreen::Update @0x00163dd0).
// TODO: resolve phase-timer rodata @ DAT_00162cdc for the remaining two (PRE_OFFSET slide-in,
// AWARD_SPACING per-award stagger -- v1.6.1 BonusScreen::Update @0x00163dd0)
static const float PRE_OFFSET     = 1.0f;
static const float AWARD_SPACING  = 0.5f;

// SET_DEFINES globals — set on every BonusScreen::Update by SET_DEFINES() @ 0x00162090.
// Non-const so SET_DEFINES can write them; initial values match what SET_DEFINES writes.
// Values memory-verified 2026-07-04 against the resolved tuning struct @0x002d8c3c
// (v1.6.1 BonusScreen::Update @0x00163dd0, re-analyst batch1 spec).
static float TRANSITION_IN_TIME  = 0.333333f;  // 0x3eaa7efa (~1/3)
static float TRANSITION_OUT_TIME = 0.25f;       // 0x3e800000
static float TIME_PER_AWARD      = 0.6f;        // 0x3f19999a -- was WRONG 1.0f (0x3f800000)
static float FIRST_AWARD         = 0.666667f;   // 0x3f2a7efa (~2/3)
static float TOTAL_TIME          = 7.0f;        // 0x40e00000
static float AWARD_Y_DIF         = -42.0f;      // 0xc2280000
static float TOTAL_POS_X         = 50.0f;       // DAT_003144b0
static float TOTAL_POS_Y         = -88.0f;      // DAT_003144b4 (was DAT_003144b8 in spec; adj offset)
static float TOTAL_POS_Z         = 0.0f;        // DAT_003144bc

// ASM-spec v1.6.1 SET_DEFINES @ 0x00162090 (static, same TU as BonusScreen::Update).
// Called at the top of every BonusScreen::Update to (re-)initialize tuning constants.
static void SET_DEFINES() {
    TRANSITION_IN_TIME  = 0.333333f;
    TRANSITION_OUT_TIME = 0.25f;
    TIME_PER_AWARD      = 0.6f;
    FIRST_AWARD         = 0.666667f;
    TOTAL_TIME          = 7.0f;
    AWARD_Y_DIF         = -42.0f;
    TOTAL_POS_X         = 50.0f;
    TOTAL_POS_Y         = -88.0f;
    TOTAL_POS_Z         = 0.0f;
}

// ---------------------------------------------------------------------------
// BonusScreen ctor (v1.6.1 BonusScreen::BonusScreen @0x00162d1c)
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
    // Note: v1.6.1 arcade_diolog_box.tex (RGBA4444 512x256) does NOT have BONUS/TOTAL text
    //   baked in -- the texture contains only the board frame, parchment, and star decorations.
    //   The BONUS title (top band) and TOTAL text (bottom band) are drawn by code.
    //   (The docs/gallery PNG showing baked BONUS/TOTAL text is from v1.5.x, not v1.6.1.)
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

// ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b (asm-inspector)
void BonusScreen::BuildBonusText() {
    Mortar::FontCacheObjectTTF* font = GetBonusTTFFont();
    if (!font) return;

    // Per-award label/value boxes (loop i=0..count-1, up to 3).
    // Binary: r6+=4 per iteration == [i].
    // ASM-verified: label font=13px ALL rows, value font=16px ALL rows (constants, not a per-row array).
    // ASM-verified: fixed 3-entry colour palette indexed by row, NOT m_Awards[i].m_Colour:
    //   row0=Colour(0xAD,0x7E,0x00,0xFF) gold, row1=Colour(0xA0,0x05,0x05,0xFF) red,
    //   row2=Colour(0x01,0x5C,0x95,0xFF) blue.
    static const Colour kRowColours[3] = {
        Colour(0xAD, 0x7E, 0x00, 0xFF),  // row0: gold (TASTY FRUIT)
        Colour(0xA0, 0x05, 0x05, 0xFF),  // row1: red  (FRUIT MIX)
        Colour(0x01, 0x5C, 0x95, 0xFF),  // row2: blue
    };
    for (int i = 0; i < (int)m_Awards.size() && i < 3; ++i) {
        if (!m_RankLabelBoxes[i]) {
            // m_RankLabelBoxes[i] (+0xC0): name label, w=220, h=10, align=1 (LEFT).
            // ASM-verified: v1.6.1 BonusScreen::BuildBonusText @0x001621dc ctor align arg = 1.
            m_RankLabelBoxes[i] = new Mortar::BakedStringBox(
                font,
                13.0f,   // label font: 13px for ALL rows
                220.0f,  // 0xDC
                10.0f,
                (Mortar::ALIGNMENT_TYPE)0x01,    // LEFT
                0,       // maxLines
                0        // lineSpacing (binary 7th arg = 0; step = (int)(13+0) = 13px)
            );
            m_RankLabelBoxes[i]->SetColour(kRowColours[i], 0);
            m_RankLabelBoxes[i]->SetText(m_Awards[i].m_Name);
            m_RankLabelBoxes[i]->Update();
        }
        if (!m_RankValueBoxes[i]) {
            // m_RankValueBoxes[i] (+0xCC): tier value as "%i", w=60, h=10, align=0x0F.
            // ASM-verified: v1.6.1 BonusScreen::BuildBonusText @0x162324 mov r2,#0xf -> align=0x0F.
            // 0x0F = center-H (bits 1:0 = 3) + center-V (bits 3:2 = 3 = 0xC);
            // center-V path is implemented in BakedStringBox::Draw (vertAlign==0xc branch).
            char valBuf[16];
            snprintf(valBuf, sizeof(valBuf), "%i", m_Awards[i].m_TierBase);
            m_RankValueBoxes[i] = new Mortar::BakedStringBox(
                font,
                16.0f,   // value font: 16px for ALL rows
                60.0f,   // 0x3C
                10.0f,
                (Mortar::ALIGNMENT_TYPE)0x0F,    // center-H + center-V (binary @0x162324)
                0,
                0        // lineSpacing (binary 7th arg = 0; step = (int)(16+0) = 16px)
            );
            m_RankValueBoxes[i]->SetColour(kRowColours[i], 0);
            m_RankValueBoxes[i]->SetText(valBuf);
            m_RankValueBoxes[i]->Update();
        }
    }

    // m_ScoreBox (+0xB8): the "_ BONUS _" title drawn at top of board pos+(105,+51).
    // ASM-verified: v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b:
    //   fontSize=30, w=220, h=30, align=0x0F, text=sprintf("_ %s _", GETSTRING(0x31E)),
    //   gradient red->dark-red (top 0xFF0000, bottom 0xB40000),
    //   stroke(2-colour) 2.0 gold (0xFFDC50, 0xFFC887),
    //   shadow 5.0 brown 0x5D280C offset Vec3(0,-3,0). STATIC.
    if (!m_ScoreBox) {
        m_ScoreBox = new Mortar::BakedStringBox(
            font,
            30.0f,
            220.0f,  // 0xDC
            30.0f,   // 0x1E
            (Mortar::ALIGNMENT_TYPE)0x0F,
            0,
            0        // lineSpacing (binary 7th arg = 0; step = (int)(30+0) = 30px)
        );
        m_ScoreBox->SetGradient(
            Colour(0xFF, 0x00, 0x00, 0xFF),   // red top: RGB(0xFF,0x00,0x00)
            Colour(0xB4, 0x00, 0x00, 0xFF),   // dark-red bottom: RGB(0xB4,0x00,0x00)
            false
        );
        m_ScoreBox->SetStroke(2.0f,
            Colour(0xFF, 0xDC, 0x50, 0xFF),   // gold inner: RGB(0xFF,0xDC,0x50)
            Colour(0xFF, 0xC8, 0x87, 0xFF));  // gold outer: RGB(0xFF,0xC8,0x87)
        m_ScoreBox->SetShadow(
            5.0f,
            Colour(0x5D, 0x28, 0x0C, 0xFF),
            Vec3(0.0f, -3.0f, 0.0f),
            true
        );
        char bonusBuf[64];
        const char* bonusStr = GETSTRING((LocalizedString)0x31E, 0);
        snprintf(bonusBuf, sizeof(bonusBuf), "_ %s _", (bonusStr && bonusStr[0]) ? bonusStr : "BONUS");
        m_ScoreBox->SetText(bonusBuf);
        m_ScoreBox->Update();
    }

    // m_TotalBox (+0xBC): raw "TOTAL" label drawn at bottom of board pos+(75,-128).
    // ASM-verified: v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b:
    //   fontSize=20, w=90, h=20, align=0x0F, text=GETSTRING(0x31F) raw (NO wrapper),
    //   gradient yellow->orange (top 0xFFEF00, bottom 0xEF7700),
    //   stroke(single-colour) 2.0 red-orange 0xDC1300, NO shadow. STATIC.
    if (!m_TotalBox) {
        m_TotalBox = new Mortar::BakedStringBox(
            font,
            20.0f,
            90.0f,   // 0x5A
            20.0f,   // 0x14
            (Mortar::ALIGNMENT_TYPE)0x0F,
            0,
            0        // lineSpacing (binary 7th arg = 0; step = (int)(20+0) = 20px)
        );
        m_TotalBox->SetGradient(
            Colour(0xFF, 0xEF, 0x00, 0xFF),   // yellow top: RGB(0xFF,0xEF,0x00)
            Colour(0xEF, 0x77, 0x00, 0xFF),   // orange bottom: RGB(0xEF,0x77,0x00)
            false
        );
        m_TotalBox->SetStroke(2.0f, Colour(0xDC, 0x13, 0x00, 0xFF));
        const char* totalStr = GETSTRING((LocalizedString)0x31F, 0);
        m_TotalBox->SetText((totalStr && totalStr[0]) ? totalStr : "TOTAL");
        m_TotalBox->Update();
    }
}

// ---------------------------------------------------------------------------
// Update (binary @ 0x00163dd0) — three-phase state machine driven by m_Timer
// ---------------------------------------------------------------------------

// v1.6.1 @0x00163dd0
void BonusScreen::Update(float dt) {
    SET_DEFINES();  // v1.6.1 @ 0x00163dec (called at top of every Update)

    // Advance phase timer unconditionally each frame.
    m_Timer += dt;

    // -----------------------------------------------------------------------
    // Phase A: pre-show slide-in (timer < 0)
    // -----------------------------------------------------------------------
    if (m_Timer < 0.0f) {
        // Slide-in from off-screen. m_AnimPos.y interpolates toward 0.
        // TODO: resolve exact slide-in math from binary v1.6.1 BonusScreen::Update @0x00163dd0
        m_AnimPos.y = m_Timer * PRE_OFFSET;

        // NOTE: binary's rush-loop SFX start/stop gate (m_RushLoopSFX, "Bonus-drum-roll")
        // requires m_Timer>0, so it never fires during this slide-in phase -- it is
        // implemented below (after the revealEnd computation), not here.
        return;
    }

    // -----------------------------------------------------------------------
    // Reveal-end threshold (binary @0x00163e00-0163e24).
    // ASM-spec v1.6.1 BonusScreen::Update @0x00163e00: revealEnd = FIRST_AWARD +
    //   TIME_PER_AWARD * ((float)m_Awards.size() + 0.25f). Memory-verified against
    //   the tuning struct @0x002d8c3c and the immediate 0.25f encoded at 0x00163e14
    //   (was WRONG in the port: REVEAL_END + (count-1)*AWARD_SPACING).
    // -----------------------------------------------------------------------
    float revealEnd = FIRST_AWARD + TIME_PER_AWARD * ((float)m_Awards.size() + 0.25f);

    // ASM-spec v1.6.1 BonusScreen::Update @0x00163e28-0163e3c: sets HUDControl's
    // inherited m_bPendingRemoval (+0x33) once m_Timer passes TOTAL_TIME+TRANSITION_OUT_TIME.
    // NOTE: the batch1 re-analyst spec called this "field_0x33, new field needed" --
    // that claim was stale/wrong against the port: BonusScreen already inherits
    // m_bPendingRemoval at exactly +0x33 from HUDControl (see HUDControl.h). No new
    // field needed. Binary uses a conditional store (strbgt) -- only ever sets it to
    // true, never clears it here, matching a plain `if` with no `else`. This replaces
    // the old dismissAt (finaleStart+FINALE_HOLD+DISMISS_BUFFER) guess below.
    if (m_Timer > TOTAL_TIME + TRANSITION_OUT_TIME) {
        m_bPendingRemoval = 1;
    }

    // -----------------------------------------------------------------------
    // Rush-loop SFX start gate (binary @0x00163e40-0163ef8).
    // ASM-spec v1.6.1 BonusScreen::Update @0x00163e40: while m_RushLoopSFX(+0xb4)==
    //   nullptr and 0<m_Timer<revealEnd, starts "Bonus-drum-roll" (rodata 0x00281e62 --
    //   memory-verified, same sound preloaded in the ctor via PreLoadSoundEx) through
    //   GameSound::SFXPlay(name, vol, gain, finishCallback, pitch) @0x0010b4c8, storing
    //   the result into m_RushLoopSFX. Binary passes vol=0.0f, gain=1.0f, pitch=0.0f
    //   (memory-verified literal @0x0016426c == 0.0f, shared by both args), then
    //   immediately calls MortarSound::SetVolume(0.0f) @0x001108c4 on the freshly
    //   returned sound -- ported verbatim even though it appears to silence the loop
    //   immediately after starting it.
    // -----------------------------------------------------------------------
    if (m_RushLoopSFX == 0 && m_Timer > 0.0f && m_Timer < revealEnd) {
        Game* game = Game::GetInstance();
        if (game && game_work.mGameSound) {
            // TODO: v1.6.1 0x00162a74 (T.1223) -- binary builds a BaseDelegate thunk here
            // and wraps it into the Delegate1 finishCallback below (likely a loop-restart
            // callback consumed by GameSound::Update's finishCallback dispatch); needs a
            // follow-up RE pass to name/port it. Passing a no-op delegate until then.
            m_RushLoopSFX = game_work.mGameSound->SFXPlay(
                "Bonus-drum-roll", 0.0f, 1.0f,
                Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.0f);
            if (m_RushLoopSFX) {
                m_RushLoopSFX->SetVolume(0.0f);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Phase B: per-award reveal (0 <= timer < revealEnd)
    // -----------------------------------------------------------------------
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

            // Alpha pulse on reveal -- TODO: resolve exact formula from binary v1.6.1 BonusScreen::Update @0x00163dd0
            entry.m_Alpha = 1.0f + 0.3f * sinf(localT * 6.28f);
            if (entry.m_Alpha < 0.0f) entry.m_Alpha = 0.0f;

            // Score counter ramp-up.
            // TODO: resolve exact multiplier ramp math from binary v1.6.1 BonusScreen::Update @0x00163dd0
            float scoreT = localT * 0.5f + 0.5f;
            if (scoreT > 1.0f) scoreT = 1.0f;
            entry.m_DisplayedScore = (int)((float)(entry.m_TierBase * entry.m_Multiplier) * scoreT);

            totalDisplayed += entry.m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
        return;
    }

    // -----------------------------------------------------------------------
    // Rush-loop SFX stop gate (binary @0x00164154-0164188): fires once m_Timer has
    // left the reveal window (m_Timer >= revealEnd, guaranteed by control flow here
    // since Phase B returned above otherwise).
    // ASM-spec v1.6.1 BonusScreen::Update @0x00164160: GameSound::Release(mGameSound,
    //   m_RushLoopSFX, "Bonus-drum-roll") @0x0010dd00, then m_RushLoopSFX = nullptr.
    // -----------------------------------------------------------------------
    if (m_RushLoopSFX != 0) {
        Game* game = Game::GetInstance();
        if (game && game_work.mGameSound) {
            game_work.mGameSound->Release(m_RushLoopSFX, "Bonus-drum-roll");
        }
        m_RushLoopSFX = 0;
    }

    // -----------------------------------------------------------------------
    // Phase C: finale one-shot (timer >= revealEnd, only once)
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
    // Phase D: dismiss -- superseded by the m_bPendingRemoval latch computed
    // earlier (TOTAL_TIME+TRANSITION_OUT_TIME threshold, ASM-spec above). The old
    // dismissAt=finaleStart+FINALE_HOLD+DISMISS_BUFFER formula used unresolved
    // placeholder constants and has been removed.
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // Shake update (independent of phase)
    // -----------------------------------------------------------------------
    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;
        // Damped wobble around m_ShakeOffset.
        // TODO: resolve exact wobble math from binary v1.6.1 BonusScreen::Update @0x00163dd0
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

// ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::Draw @0x0016492c..0x00164e4c (asm-inspector)
void BonusScreen::Draw(float* hudScaleRaw) {
    // Binary saves pos at entry @0x0016494c, restores at exit @0x00164e38.
    // The per-award loop steps pos.y by -42 each row.
    Vec3 savedPos = pos;

    // Apply m_AnimPos to position before base draw.
    pos.x += m_AnimPos.x;
    pos.y += m_AnimPos.y;
    pos.z += m_AnimPos.z;

    // Base box draw (HUDControl3d::Draw handles the dialog background via m_Texture@0x74).
    HUDControl3d::Draw(hudScaleRaw);

    // Restore pos to original (no AnimPos) for all text/award draws below.
    // TODO: v1.6.1 BonusScreen::Draw @0x0016492c -- intro animation: binary shifts pos by
    //   (m_AnimPos + m_ShakeOffset) at Draw entry and draws ALL elements (board+score+total+rows)
    //   at that shifted pos, restoring only at function exit. Port restores pos immediately
    //   after the base draw and omits m_ShakeOffset. Needs asm-verify before reworking.
    pos = savedPos;

    // -----------------------------------------------------------------------
    // Pre-loop draws: "_ BONUS _" title (m_ScoreBox) + "TOTAL" label (m_TotalBox).
    // ASM-verified: 2026-06-27T00:00:00Z v1.6.1 BonusScreen::Draw @0x0016492c (asm-inspector)
    // Both boxes are static (text set once in BuildBonusText; no per-frame SetText here).
    //
    // OFFSET FIDELITY NOTE (verified by 3 independent RE passes -- do not "fix"):
    //   m_ScoreBox ("_ BONUS _") draws at pos+(105, 51, 0) -- binary literal @0x164dc0/0x164dc4.
    //   m_TotalBox ("TOTAL")     draws at pos+(75, -128, 0) -- binary literal @0x164dc8/0x164dcc.
    //   Full transform: BakedStringBox::SetTranslation(flag=1) @0x00246238 subtracts boxW/2;
    //   RebuildAlignments @0x00245c78 adds it back; net text-center = pos.x + offset.x.
    //   The board (HUDControl3d::Draw @0x0018b544) is centered at pos (m_HudScale +0x14 = 0),
    //   so BONUS lands RIGHT-of-center by design in v1.6.1 Bada.
    //
    // WARNING: do NOT center these to match Android/Froyo screenshots -- that is a different
    //   SKU. Android has baked-centered text; v1.6.1 Bada arcade_diolog_box.tex has empty bands
    //   + code-drawn text at +105. Centering DIVERGES from the Bada target.
    //   The dead literal-pool slots @0x164db4/@0x164db8 (values 105.0/51.0) are NOT the offsets
    //   and previously misled RE.
    // -----------------------------------------------------------------------

    // m_ScoreBox (+0xB8): "_ BONUS _" title at pos+(105,+51). SetTranslation flag=1.
    if (m_ScoreBox) {
        m_ScoreBox->SetTranslation(
            Vec3(pos.x + 105.0f, pos.y + 51.0f, pos.z),
            1
        );
        m_ScoreBox->Draw(Vec2(1.0f, 1.0f), 0.0f, 1);
    }

    // m_TotalBox (+0xBC): "TOTAL" label at pos+(75,-128). SetTranslation flag=1.
    if (m_TotalBox) {
        m_TotalBox->SetTranslation(
            Vec3(pos.x + 75.0f, pos.y - 128.0f, pos.z),
            1
        );
        m_TotalBox->Draw(Vec2(1.0f, 1.0f), 0.0f, 1);
    }

    // -----------------------------------------------------------------------
    // Per-award loop: star + label + value. pos.y steps -42 each row.
    // TODO: v1.6.1 0x00164b64 -- reveal gate: `if (m_Timer - 0.666 < i*0.6) break`.
    //   For stable screenshot, draw all rows (no gate).
    // ASM-verified: v1.6.1 BonusScreen::Draw @0x0016492c..0x00164e4c (asm-inspector)
    // -----------------------------------------------------------------------
    // Fixed 3-entry colour palette indexed by row; NOT entry.m_Colour.
    static const Colour kDrawRowColours[3] = {
        Colour(0xAD, 0x7E, 0x00, 0xFF),  // row0: gold
        Colour(0xA0, 0x05, 0x05, 0xFF),  // row1: red
        Colour(0x01, 0x5C, 0x95, 0xFF),  // row2: blue
    };
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
            // star corner = pos - 35*UnitX (literal 35.0 @0x164dd0).
            Matrix44 mat = Matrix44::Scale44(Vec3(texW + 1.0f, texH + 1.0f, 1.0f));
            mat.GlobalTranslate44(Vec3(pos.x - 35.0f, pos.y, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            Mortar::Mesh::DrawQuadUnCached(entry.m_Colour, NULL);

            entry.m_StarTex->UnSetUnCached();
        }

        const Colour& rowCol = (i < 3) ? kDrawRowColours[i] : kDrawRowColours[2];

        // m_RankLabelBoxes[i]: pos+(-2,+6), SetTranslation flag=0 (left-aligned).
        // ASM-verified: v1.6.1 BonusScreen::Draw @0x164cec flag=0.
        if (i < 3 && m_RankLabelBoxes[i]) {
            m_RankLabelBoxes[i]->SetColour(rowCol, 0);
            m_RankLabelBoxes[i]->SetTranslation(
                Vec3(pos.x - 2.0f, pos.y + 6.0f, pos.z),
                0
            );
            m_RankLabelBoxes[i]->Draw(Vec2(1.0f, 1.0f), 0.0f, 1);
        }

        // m_RankValueBoxes[i]: pos+(220,+5), SetTranslation flag=0.
        // ASM-verified: v1.6.1 BonusScreen::Draw @0x164d7c mov r2,#0 -> flag=0.
        if (i < 3 && m_RankValueBoxes[i]) {
            m_RankValueBoxes[i]->SetColour(rowCol, 0);
            m_RankValueBoxes[i]->SetTranslation(
                Vec3(pos.x + 220.0f, pos.y + 5.0f, pos.z),
                0
            );
            m_RankValueBoxes[i]->Draw(Vec2(entry.m_Alpha, entry.m_Alpha), 0.0f, 1);
        }

        // Row step: pos.y -= 42 at loop tail.
        pos.y += -42.0f;
    }

    // Restore pos to saved value (binary restores @0x00164e38).
    pos = savedPos;
}
