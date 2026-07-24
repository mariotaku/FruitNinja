// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// v1.6.1: ctor @0x00162d1c, dtor D2 @0x00162724 / D1 @0x0016283c / D0 @0x00162954,
//         Update @0x00163dd0, Draw @0x0016492c
// AddAward / AwardScores -- TODO: re-verify v1.6.1 addr (prior 0x00133664/0x0013260C stale v1.5.x)

#include "BonusScreen.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
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
#include "engine/util/StringTable.h"
#include "engine/util/StringHash.h"
#include "engine/math/_Vector2.h"
#include "engine/math/Random.h"
#include "engine/particle/PSPParticleManager.h"
#include "entities/Coin.h"
#include "game/FruitCamera.h"
#include "hud/ScoreControl.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

using Mortar::TextureManager;

// ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc: uses the shared game-wide
// TTF face (game_work+0x614 / m_pTTFFontMain) -- arabic.ttf when bM_LangId==0x14,
// gangofchinese.ttf otherwise (set once at boot by PreloadFontsTTF @0x0011c1fc).
static Mortar::FontCacheObjectTTF* GetBonusTTFFont() {
    return game_work.m_pTTFFontMain;
}

// Phase-timer rodata constants — binary @ GOT_DAT_00162cdc area.
// REVEAL_END / FINALE_HOLD / DISMISS_BUFFER removed: superseded by the memory-verified
// revealEnd formula and m_bPendingRemoval latch below (v1.6.1 BonusScreen::Update @0x00163dd0).
// AWARD_SPACING removed: superseded by TIME_PER_AWARD (0.6f), the real per-award stagger
// (see SET_DEFINES below and the Phase B ASM-spec block).
// TODO: resolve phase-timer rodata @ DAT_00162cdc for PRE_OFFSET slide-in
// -- v1.6.1 BonusScreen::Update @0x00163dd0
static const float PRE_OFFSET     = 1.0f;

// SET_DEFINES globals — set on every BonusScreen::Update by SET_DEFINES() @ 0x00162090.
// Non-const so SET_DEFINES can write them; initial values match what SET_DEFINES writes.
// Values memory-verified 2026-07-04 against the resolved tuning struct @0x002d8c3c
// (v1.6.1 BonusScreen::Update @0x00163dd0, re-analyst batch1 spec).
static float TRANSITION_IN_TIME  = 0.333333f;  // 0x3eaa7efa (~1/3)
static float TRANSITION_OUT_TIME = 0.25f;       // 0x3e800000
// ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0 (struct@0x2d8c3c+0x04, re-verified):
// TIME_PER_AWARD = 0.6f, staggering each award's reveal 0.6s apart. The prior 1.0f had
// grabbed the wrong tuning-struct slot; 0.6f is corroborated by the per-award one-shot
// gate math (ph crosses 0.2s into a 0.6s slot) below.
static float TIME_PER_AWARD      = 0.6f;        // 0x3f19999a
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
      m_bBonusTextBuilt(false),
      m_Timer(-TRANSITION_IN_TIME),
      m_AnimPos(0.0f, 0.0f, 0.0f)
{
    // ASM-spec v1.6.1 BonusScreen::BonusScreen @ 0x00162d1c: m_LayerFlags =
    // HUD_LAYER_POST_ACTOR (0x80) -- draws before the particle passes so
    // bonus FX land in front. HUDControl ctor default (0x01) would otherwise
    // leave this drawing under GameDraw's HUD_LAYER_DEFAULT pass, AFTER the
    // particle tiers.
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    m_RankLabelBoxes[0] = nullptr;
    m_RankLabelBoxes[1] = nullptr;
    m_RankLabelBoxes[2] = nullptr;
    m_RankValueBoxes[0] = nullptr;
    m_RankValueBoxes[1] = nullptr;
    m_RankValueBoxes[2] = nullptr;

    m_Awards.reserve(3);

    // ASM-spec v1.6.1 BonusScreen::BonusScreen @ 0x00162d1c: loads "arcade_diolog_box.tex"
    // (rodata @0x00281e4c); size = (tex+0x24 width, tex+0x28 height).
    // NOTE (correction): a prior pass claimed this texture has "empty bands only" (no baked
    //   BONUS/TOTAL text). Full plate-alignment RE (see BonusScreen::Draw FIRST_NAME_OFFSET
    //   spec below) found the v1.6.1 plate art likely has baked BONUS/TOTAL bands that the
    //   code-drawn m_ScoreBox/m_TotalBox text overlays at matching positions once the
    //   FIRST_NAME_OFFSET origin shift is applied. Not re-verified pixel-exact here; texture
    //   content itself is unchanged by this pass.
    // TODO: v1.6.1 0x00162d5c (BonusScreen::BonusScreen) — binary caches backing tex in a load-once static
    Mortar::SmartPtr<Mortar::Texture> bgTex =
        TextureManager::LoadLocalisedTexture("arcade_diolog_box.tex");
    m_Texture = bgTex;
    if (bgTex.IsValid()) {
        size = _Vector3<float>((float)bgTex->GetWidth(), (float)bgTex->GetHeight(), 0.0f);
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
// AddAward -- v1.6.1 BonusScreen::AddAward @ 0x00163234
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
    entry.m_Colour2        = colour;
    entry.m_Alpha          = 0.0f;
    m_TotalScore          += tier;
    m_Awards.push_back(entry);
}


// STUB: BonusScreen::GetTimeFirstAward -- binary @ 0x???? (TODO RE)
float BonusScreen::GetTimeFirstAward() { return 0.0f; }

// STUB: BonusScreen::GetTimePerAward -- binary @ 0x???? (TODO RE)
float BonusScreen::GetTimePerAward() { return 0.0f; }

// STUB: BonusScreen::LoadContent -- binary @ 0x???? (TODO RE)
void BonusScreen::LoadContent() {}

// v1.6.1 BonusScreen::Shake @ 0x00162054 (thunk 0x0011601c).
// ASM-spec: vstr s0->+0x94 (m_ShakeTimer), vstr s0->+0x98 (m_ShakeDuration),
// vstr s1->+0x90 (m_ShakeAmplitude), bl Math::Random::Rand32(g_random, 0xff3a)
// -> strh result -> +0x9c (m_ShakeAngle).
void BonusScreen::Shake(float duration, float amplitude) {
    m_ShakeTimer = duration;
    m_ShakeDuration = duration;
    m_ShakeAmplitude = amplitude;
    m_ShakeAngle = (uint16_t)Math::g_Random.Rand32(0xff3a);
}

// STUB: BonusScreen::UnLoadContent -- binary @ 0x???? (TODO RE)
void BonusScreen::UnLoadContent() {}

// ---------------------------------------------------------------------------
// AwardScores -- v1.6.1 BonusScreen::AwardScores @ 0x0016393c.
// One-shot finale: 1-2 coin bursts (Coin::MakeCoins), a camera shake, an
// "impact_fx" particle emitter, and an "equip-unlock" SFX.
// ---------------------------------------------------------------------------

void BonusScreen::AwardScores() {
    // ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: base = pos + m_AnimPos
    // + Vec3(-230,150,0) -- same constant reused for the coin spawn point, the
    // camera shake impact vector, and the particle emitter position below.
    _Vector3<float> base = pos + m_AnimPos + _Vector3<float>(-230.0f, 150.0f, 0.0f);

    int total = m_TotalScore;
    // ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: flyFXName/collectFXName
    // literal refs @0x001639f4/0x00163a0c (<6 branch) and @0x00163ad8/0x00163ba4
    // (>=6 branch) resolve to "bonus_star_trail"/"bonus_star_impact"; delayStep/
    // delayCap are -0.05f/-0.3f for the <6 branch and -0.05f/-0.5f for the >=6
    // branch (both bursts in that branch share the branch's tuning).
    // TODO: v1.6.1 0x0016393c (BonusScreen::AwardScores) -- baseAngle/angleSpread
    // (0, 0xff3a) reused from the analogous already-verified combo-coin burst at
    // SlashEntity.cpp:1911-1915 pending asm-inspector confirmation; whether the
    // >=6 branch's second burst spawns from a distinct "base2" position is also
    // unresolved (reusing `base` here rather than guessing a second position).
    // ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: coins spawned here pass
    // AddToScoreOnArrival (v1.6.1 @0x00162ab8), which credits coin->m_CoinValue to
    // game_work.currentScore, NOT Coin::DefaultArrivedDelegate()/CoinArrived
    // (@0x0017320C), which credits the coin wallet. The bonus-board tally must land
    // in the arcade score / high score.
    if (total < 6) {
        Coin::MakeCoins(total, 6, base, 0, 0xff3a,
                         /*target=*/nullptr, -0.05f, -0.3f,
                         "bonus_star_trail", "bonus_star_impact",
                         Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival), false);
    } else {
        Coin::MakeCoins(6, 6, base, 0, 0xff3a,
                         /*target=*/nullptr, -0.05f, -0.3f,
                         "bonus_star_trail", "bonus_star_impact",
                         Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival), false);  // ASM-verified: v1.6.1 delayCap -0.3 (all 3 AwardScores MakeCoins)
        total = m_TotalScore;  // re-read (unchanged, just re-fetched -- matches binary)
        Coin::MakeCoins(total - 6, 6, base, 0, 0xff3a,
                         /*target=*/nullptr, -0.05f, -0.3f,
                         "bonus_star_trail", "bonus_star_impact",
                         Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival), false);  // ASM-verified: v1.6.1 delayCap -0.3 (all 3 AwardScores MakeCoins)
    }

    // TODO: v1.6.1 0x0016393c (BonusScreen::AwardScores) -- Ghidra's decompile of
    // the CreateCameraShake args (impact=(0.3,1.0,extraout_s2)) is flagged as a
    // likely VFP-tracking artifact by the RE report; using the analogous
    // already-ported constant from AddToScoreOnArrival (Coin.cpp) instead.
    // asm-inspector needed to confirm exact intensity/dirScale floats.
    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->CreateCameraShake(base, 0.15f, 0.75f);
    }

    PSPParticleEmitter* fxEmitter =
        PSPParticleManager::GetInstance().AddEmitter(StringHash("impact_fx"), 0, false);
    if (fxEmitter) {
        fxEmitter->m_Pos = base;
    }

    GetCurrentScore(0);  // ASM-spec: call only, return value unused here (cache refresh side-effect).

    // TODO: v1.6.1 0x0016393c (BonusScreen::AwardScores) -- SFXPlay's exact
    // vol/gain/pitch args and whether a finishCallback is passed are unresolved
    // (Ghidra shows one confirmed 1.0f literal, second float unclear); using
    // the vol=1.0f,gain=1.0f default pattern seen elsewhere (e.g. Coin.cpp
    // AddToScoreOnArrival) until asm-inspector confirms.
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("equip-unlock", 1.0f, 1.0f);
    }
}

// ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b (asm-inspector)
// ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc (asm-inspector, fresh binary read):
//   maxLines=1 (not 0) on m_ScoreBox/m_TotalBox, and per-award row colour = m_Awards[i].m_Colour
//   (not a fixed palette) -- both corrected below; supersedes the prior (stale) verified pass.
void BonusScreen::BuildBonusText() {
    // +0xD8 build-once latch (was mis-named m_bSkipIntro; binary sets it inside
    // BuildBonusText @0x001621dc, not at the Update call site).
    if (m_bBonusTextBuilt) return;

    Mortar::FontCacheObjectTTF* font = GetBonusTTFFont();
    // Guard: m_pTTFFontMain is set once at boot by PreloadFontsTTF, but placed
    // BEFORE the latch-set below so a transiently-null font can't burn the
    // create-once latch (matches binary null-check shape at 0x001621dc).
    if (!font) return;
    m_bBonusTextBuilt = true;

    // Per-award label/value boxes (loop i=0..count-1, up to 3).
    // Binary: r6+=4 per iteration == [i].
    // ASM-verified: label font=13px ALL rows, value font=16px ALL rows (constants, not a per-row array).
    // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c / BuildBonusText @0x001621dc (asm-inspector,
    // fresh binary read): row colour is the per-award element's OWN animated m_Colour (+0x50),
    // NOT a fixed 3-entry palette. The prior "fixed gold/red/blue palette" ASM-verified marker
    // here was WRONG and has been removed.
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
            m_RankLabelBoxes[i]->SetColour(m_Awards[i].m_Colour, 0);
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
            m_RankValueBoxes[i]->SetColour(m_Awards[i].m_Colour, 0);
            m_RankValueBoxes[i]->SetText(valBuf);
            m_RankValueBoxes[i]->Update();
        }
    }

    // m_ScoreBox (+0xB8): the "_ BONUS _" title drawn at top of board pos+(105,+51).
    // ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc (asm-inspector, fresh binary read):
    //   fontSize=30, w=220, h=30, align=0x0F, maxLines=1, text=sprintf("_ %s _", GETSTRING(0x31E)),
    //   gradient red->dark-red (top 0xFF0000, bottom 0xB40000),
    //   stroke(2-colour) 2.0 gold (0xFFDC50, 0xFFC887),
    //   shadow 5.0 brown 0x5D280C offset Vec3(0,-3,0). STATIC.
    // maxLines was WRONG at 0 (unlimited wrap -- caused the trailing "_" star to wrap to a
    // second line); binary passes maxLines=1 (single line, no wrap).
    if (!m_ScoreBox) {
        m_ScoreBox = new Mortar::BakedStringBox(
            font,
            30.0f,
            220.0f,  // 0xDC
            30.0f,   // 0x1E
            (Mortar::ALIGNMENT_TYPE)0x0F,
            1,       // maxLines (was WRONG 0 -- caused 2-line wrap)
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
            _Vector3<float>(0.0f, -3.0f, 0.0f),
            true
        );
        char bonusBuf[64];
        const char* bonusStr = GETSTRING((LocalizedString)0x31E, 0);
        snprintf(bonusBuf, sizeof(bonusBuf), "_ %s _", (bonusStr && bonusStr[0]) ? bonusStr : "BONUS");
        m_ScoreBox->SetText(bonusBuf);
        m_ScoreBox->Update();
    }

    // m_TotalBox (+0xBC): raw "TOTAL" label drawn at bottom of board pos+(75,-128).
    // ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc (asm-inspector, fresh binary read):
    //   fontSize=20, w=90, h=20, align=0x0F, maxLines=1, text=GETSTRING(0x31F) raw (NO wrapper),
    //   gradient yellow->orange (top 0xFFEF00, bottom 0xEF7700),
    //   stroke(single-colour) 2.0 red-orange 0xDC1300, NO shadow. STATIC.
    // maxLines was WRONG at 0 (unlimited wrap); binary passes maxLines=1 (single line, no wrap).
    if (!m_TotalBox) {
        m_TotalBox = new Mortar::BakedStringBox(
            font,
            20.0f,
            90.0f,   // 0x5A
            20.0f,   // 0x14
            (Mortar::ALIGNMENT_TYPE)0x0F,
            1,       // maxLines (was WRONG 0 -- caused 2-line wrap)
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

    // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: Update NEVER stores m_Timer (+0xdc).
    // The phase timer is driven externally by GameOverScreen::Update @0x00187104
    // (GOS+0x90 += dt; store to bonusScreen->m_Timer -- see GameOverScreen.cpp
    // STATE_BONUS_PHASE, m_pBonusScreen->m_Timer = m_Timer). Do NOT self-advance here.

    // -----------------------------------------------------------------------
    // Phase A: pre-show slide-in (timer < 0). Sets m_AnimPos only -- NO early return:
    // binary Update has no early returns; every path converges into the per-award loop,
    // shake, and the unconditional BuildBonusText() tail below.
    // -----------------------------------------------------------------------
    if (m_Timer < 0.0f) {
        // Slide-in from off-screen. m_AnimPos.y interpolates toward 0.
        // TODO: resolve exact slide-in math from binary v1.6.1 BonusScreen::Update @0x00163dd0
        m_AnimPos.y = m_Timer * PRE_OFFSET;

        // NOTE: binary's rush-loop SFX start/stop gate (m_RushLoopSFX, "Bonus-drum-roll")
        // requires m_Timer>0, so it never fires during this slide-in phase; the m_Timer>0
        // / m_Timer<revealEnd guards below skip it while sliding in.
    } else {
        // Reveal window onward: m_AnimPos settles at Zero (RE-confirmed the per-award
        // emitter accumPos below reads pos + m_AnimPos + m_ShakeOffset directly with no
        // extra drift once m_Timer >= 0). The transition-out slide (m_Timer > TOTAL_TIME)
        // is a separate, still-unresolved gap -- see TODO at game_singleton+0x40 below.
        m_AnimPos.x = 0.0f;
        m_AnimPos.y = 0.0f;
        m_AnimPos.z = 0.0f;
    }

    // TODO: v1.6.1 0x00163dd0 (BonusScreen::Update) -- the transition-out (m_Timer >
    // TOTAL_TIME) slide-out math drives a transition object at game_singleton+0x40 that
    // isn't identified yet. m_AnimPos is pinned to Zero above for the whole m_Timer>=0
    // range (reveal window through finale) until that object is RE'd.

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
    // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0 -- per-award reveal timing:
    //   FIRST_AWARD    = 0.666667f          (initial delay before award 0's slot)
    //   TIME_PER_AWARD = 0.6f               (each award gets its own 0.6s slot,
    //                                         staggered i*0.6s after FIRST_AWARD)
    //   ph  = fmod(m_Timer - FIRST_AWARD, TIME_PER_AWARD)   -- position within CURRENT slot
    //   s15 = clamp((ph - 0.2f) / 0.1f, 0, 1)               -- this-frame alpha driver
    //   s16 = clamp((ph - dt - 0.2f) / 0.1f, 0, 1)          -- prev-frame alpha driver
    //   one-shot gate (emitters + Shake + Bonus-Explosion SFX) fires when s16<=0 && s15>0,
    //     i.e. exactly the frame ph first crosses 0.2s UPWARD into award i's slot.
    //   alpha  = s15                                        (linear 0->1 over [0.2s,0.3s))
    //   score  = (s16<=0) ? 0 : (0.5f + s16*0.5f) * TierBase*Multiplier
    // Each award's own slot is independent of the others -- this is what staggers the
    // reveal instead of firing all 3 awards' effects together.
    if (m_Timer < revealEnd) {
        int totalDisplayed = 0;
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            float localFrac = (m_Timer - FIRST_AWARD - (float)i * TIME_PER_AWARD) / TIME_PER_AWARD;

            if (localFrac < 0.0f) {
                // Not yet revealed.
                entry.m_Alpha          = 0.0f;
                entry.m_DisplayedScore = 0;
                continue;
            }

            if (localFrac > 1.0f) {
                // Fully revealed (this award's slot is behind us).
                entry.m_Alpha          = 1.0f;
                entry.m_DisplayedScore = entry.m_TierBase * entry.m_Multiplier;
                totalDisplayed += entry.m_DisplayedScore;
                continue;
            }

            float ph  = fmodf(m_Timer - FIRST_AWARD, TIME_PER_AWARD);
            float s15 = (ph - 0.2f) / 0.1f;
            if (s15 < 0.0f) s15 = 0.0f;
            if (s15 > 1.0f) s15 = 1.0f;
            float s16 = ((ph - dt) - 0.2f) / 0.1f;
            if (s16 < 0.0f) s16 = 0.0f;
            if (s16 > 1.0f) s16 = 1.0f;

            // One-shot reveal gate: fires the single frame ph first crosses 0.2s
            // UPWARD into award i's slot -- prev frame below 0.2 (s16<=0), this
            // frame at/above 0.2 (s15>0). ph increases monotonically within the
            // slot, so this pair is true for exactly one frame per award.
            if (s16 <= 0.0f && s15 > 0.0f) {
                // ASM-spec v1.6.1 BonusScreen::Update @0x00164534: memory-verified
                // literals s0=0.1f (@0x1642bc), s1=10.0f (@0x41200000).
                Shake(0.1f, 10.0f);

                // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: per-award reveal plays
                // "Bonus-Explosion-%i" (i=2n+1 -> 1,3,5), SFXPlay(name,0.0f,1.0f,delegate,0.0f),
                // once per award. vol 0.0f literal @0x1642c0 (matches Bonus-drum-roll shape).
                if (game_work.mGameSound) {
                    char sfxName[32];
                    snprintf(sfxName, sizeof(sfxName), "Bonus-Explosion-%i", 2 * i + 1);
                    game_work.mGameSound->SFXPlay(
                        sfxName, 0.0f, 1.0f,
                        Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.0f);
                }

                // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: 3 particle emitters
                // spawned at accumPos = pos + m_AnimPos + m_ShakeOffset +
                // FIRST_NAME_OFFSET(-105,+40,0) + (0.5,0,0), z-alternating red/blue by
                // (i&1) so successive awards' bursts don't z-fight.
                _Vector3<float> accumPos(
                    pos.x + m_AnimPos.x + m_ShakeOffset.x - 105.0f + 0.5f,
                    pos.y + m_AnimPos.y + m_ShakeOffset.y + 40.0f,
                    pos.z + m_AnimPos.z + m_ShakeOffset.z);

                PSPParticleManager& ppm = PSPParticleManager::GetInstance();

                PSPParticleEmitter* redFx = ppm.AddEmitter(StringHash("bonus_mode_fx_red"), 0, false);
                if (redFx) {
                    redFx->m_Pos = _Vector3<float>(accumPos.x, accumPos.y,
                        (i & 1) ? -1.0f : 1.0f);
                }
                PSPParticleEmitter* blueFx = ppm.AddEmitter(StringHash("bonus_mode_fx_blue"), 0, false);
                if (blueFx) {
                    blueFx->m_Pos = _Vector3<float>(accumPos.x, accumPos.y,
                        (i & 1) ? 1.0f : -1.0f);
                }
                PSPParticleEmitter* impactFx = ppm.AddEmitter(StringHash("impact_fx"), 0, false);
                if (impactFx) {
                    impactFx->m_Pos = _Vector3<float>(accumPos.x, accumPos.y, 10.0f);
                }
            }

            // Alpha ramp: linear 0->1 across [0.2s, 0.3s) of this award's slot.
            entry.m_Alpha = s15;

            // Score counter ramp-up: 0 before the gate fires, then 0.5->1.0 of
            // TierBase*Multiplier across the same [0.2s, 0.3s) window.
            entry.m_DisplayedScore = (s16 <= 0.0f) ? 0 :
                (int)((float)(entry.m_TierBase * entry.m_Multiplier) * (0.5f + s16 * 0.5f));

            totalDisplayed += entry.m_DisplayedScore;
        }
        m_DisplayedScore = totalDisplayed;
    }

    // -----------------------------------------------------------------------
    // Rush-loop SFX stop gate (binary @0x00164154-0164188): fires once m_Timer has
    // left the reveal window (m_Timer >= revealEnd). Now that Phase B no longer
    // early-returns, the reveal-window check is made explicit here.
    // ASM-spec v1.6.1 BonusScreen::Update @0x00164160: GameSound::Release(mGameSound,
    //   m_RushLoopSFX, "Bonus-drum-roll") @0x0010dd00, then m_RushLoopSFX = nullptr.
    // -----------------------------------------------------------------------
    if (m_Timer >= revealEnd && m_RushLoopSFX != 0) {
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

    // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: tail @0x001648f0 calls
    // BuildBonusText unconditionally every tick (create-once latch is inside
    // BuildBonusText).
    BuildBonusText();
}

// ---------------------------------------------------------------------------
// Draw (binary @ 0x0016492c)
// ---------------------------------------------------------------------------

// ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::Draw @0x0016492c..0x00164e4c (asm-inspector)
// ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c (asm-inspector, fresh binary read): added the
//   missing total-score number draw, per-row reveal gate, per-award m_Colour (not fixed palette),
//   and corrected star icon offset to pos+(-2,+6) -- see inline notes below. Supersedes the
//   prior (stale) verified pass for those specific sub-blocks; base-box / m_ScoreBox / m_TotalBox
//   offset findings above are unaffected and remain correct.
// ASM-spec v1.6.1 BonusScreen::Draw @0x00164a68 (2 independent RE passes + Bada-HLE visual
//   ground truth, not yet asm-inspector-diffed -- a prior asm-inspector run wrongly denied this
//   because it only checked static init of the global, not that the value is read-only
//   thereafter): pos += FIRST_NAME_OFFSET (global Vec3 @0x003144cc = (-105,+40,0)) is applied
//   once, after the base plate draw, before all content draws; see inline note at the shift
//   site below. This supersedes the "restore pos immediately after base draw" behaviour the
//   prior ASM-verified pass above had left in place.
void BonusScreen::Draw(float* hudScaleRaw) {
    // Binary saves pos at entry @0x0016494c, restores at exit @0x00164e38.
    // The per-award loop steps pos.y by -42 each row.
    _Vector3<float> savedPos = pos;

    // Apply m_AnimPos to position before base draw.
    pos.x += m_AnimPos.x;
    pos.y += m_AnimPos.y;
    pos.z += m_AnimPos.z;

    // Base box draw (HUDControl3d::Draw handles the dialog background via m_Texture@0x74),
    // centered on pos (savedPos + m_AnimPos), BEFORE the FIRST_NAME_OFFSET content shift below.
    HUDControl3d::Draw(hudScaleRaw);

    // Total-score NUMBER -- binary draws it BEFORE the FIRST_NAME_OFFSET shift (@0x001649b0,
    // which precedes @0x00164a68), at the UNSHIFTED pos + TOTAL_POS. So it is NOT moved -105
    // left with the boxes/rows (it lands ~+50 right of plate center, not -55 left). font =
    // game_work.pFontArcade (pM_Fonts[7], arcade_results_numbers.fnt), align 0xF, colour white.
    //   size = (m_DisplayedScore>0 ? 26 + 14*m_DisplayedScore/m_TotalScore : 26) * m_NamePulseScale.
    if (game_work.pFontArcade.IsValid()) {
        char totalBuf[16];
        snprintf(totalBuf, sizeof(totalBuf), "%d", m_DisplayedScore);
        float scoreScale = (m_DisplayedScore > 0)
            ? (26.0f + 14.0f * (float)m_DisplayedScore / (float)m_TotalScore)
            : 26.0f;
        scoreScale *= m_NamePulseScale;
        game_work.pFontArcade->DrawString(
            scoreScale, 1.0f, 0.0f, totalBuf,
            _Vector3<float>(pos.x + TOTAL_POS_X, pos.y + TOTAL_POS_Y, pos.z + TOTAL_POS_Z),
            Colour(255, 255, 255, 255), 0xF);
    }

    // TODO: v1.6.1 BonusScreen::Draw @0x00164968 -- binary applies `pos += m_AnimPos +
    //   m_ShakeOffset` (this port only adds m_AnimPos above, m_ShakeOffset still omitted).
    //   Existing gap, unrelated to the FIRST_NAME_OFFSET fix below.

    // ASM-spec v1.6.1 BonusScreen::Draw @0x00164a68: pos += FIRST_NAME_OFFSET.
    // FIRST_NAME_OFFSET is a global Vec3 @0x003144cc = (-105.0f, +40.0f, 0.0f), static-init'd
    // by the TU global ctor @0x001634fc and never modified afterward (confirmed by 2
    // independent RE passes + Bada-HLE visual ground truth -- a prior asm-inspector pass
    // wrongly denied this shift because it only checked static init, not that the value is
    // read-only thereafter). Applied ONCE here, at the origin, AFTER the base plate draw and
    // BEFORE any content draw. All content offsets below (BONUS box, TOTAL box, total-number,
    // star, per-award rows) are relative to this shifted origin and are NOT themselves
    // re-compensated by -105/+40 -- do not double-apply the shift at each content offset.
    // pos stays shifted through every content draw in this function; the binary (and this
    // port) restores pos to savedPos only once, at function exit @0x00164e38.
    pos.x += -105.0f;
    pos.y +=   40.0f;

    // -----------------------------------------------------------------------
    // Pre-loop draws: "_ BONUS _" title (m_ScoreBox) + "TOTAL" label (m_TotalBox).
    // ASM-verified: 2026-06-27T00:00:00Z v1.6.1 BonusScreen::Draw @0x0016492c (asm-inspector)
    // Both boxes are static (text set once in BuildBonusText; no per-frame SetText here).
    //
    // OFFSET FIDELITY NOTE: these offsets (+105/+51 and +75/-128) are relative to the
    //   FIRST_NAME_OFFSET-shifted pos set above, NOT the plate-centered pos.
    //   m_ScoreBox ("_ BONUS _") draws at (shifted pos)+(105, 51, 0) -- binary literal @0x164dc0/0x164dc4.
    //   m_TotalBox ("TOTAL")     draws at (shifted pos)+(75, -128, 0) -- binary literal @0x164dc8/0x164dcc.
    //   Full transform: BakedStringBox::SetTranslation(flag=1) @0x00246238 subtracts boxW/2;
    //   RebuildAlignments @0x00245c78 adds it back; net text-center = pos.x + offset.x.
    //   Net of the FIRST_NAME_OFFSET shift (-105,+40) plus this literal: BONUS lands at
    //   (0, +91) relative to the plate-centered pos -- i.e. horizontally CENTERED on the
    //   512x256 plate (the plate itself is centered on the unshifted pos by HUDControl3d::Draw).
    // -----------------------------------------------------------------------

    // m_ScoreBox (+0xB8): "_ BONUS _" title at pos+(105,+51). SetTranslation flag=1.
    if (m_ScoreBox) {
        m_ScoreBox->SetTranslation(
            _Vector3<float>(pos.x + 105.0f, pos.y + 51.0f, pos.z),
            1
        );
        m_ScoreBox->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }

    // m_TotalBox (+0xBC): "TOTAL" label at pos+(75,-128). SetTranslation flag=1.
    if (m_TotalBox) {
        m_TotalBox->SetTranslation(
            _Vector3<float>(pos.x + 75.0f, pos.y - 128.0f, pos.z),
            1
        );
        m_TotalBox->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }

    // (Total-score number is drawn ABOVE, before the FIRST_NAME_OFFSET shift -- see note there.)

    // -----------------------------------------------------------------------
    // Per-award loop: star + label + value. pos.y steps -42 each row.
    // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c (asm-inspector, fresh binary read):
    //   per-row reveal gate `m_Timer - FIRST_AWARD >= i * TIME_PER_AWARD`
    //   (initial delay FIRST_AWARD=0.666667f, interval TIME_PER_AWARD=0.6f -- re-verified,
    //   struct@0x2d8c3c+0x04; see the SET_DEFINES note above).
    //   Row colour = the per-award element's OWN animated m_Colour (+0x50), NOT a fixed palette
    //   (the prior kDrawRowColours 3-entry palette here was WRONG and has been removed).
    // -----------------------------------------------------------------------
    MatrixManager& mm = MatrixManager::GetInstance();
    for (int i = 0; i < (int)m_Awards.size(); ++i) {
        const BonusAwardHud& entry = m_Awards[i];

        bool revealed = (m_Timer - FIRST_AWARD) >= (float)i * TIME_PER_AWARD;
        if (revealed) {
            // Star icon draw (only if texture is valid).
            // Mirrors BSButton::Draw API: SetUnCached -> Scale44 -> GlobalTranslate44 ->
            // SetCurrentMatrix -> UploadModelViewOnly -> DrawQuadUnCached -> UnSetUnCached.
            if (entry.m_StarTex.IsValid()) {
                float texW = (float)entry.m_StarTex->GetWidth();
                float texH = (float)entry.m_StarTex->GetHeight();

                entry.m_StarTex->SetUnCached();

                // ASM-spec v1.6.1 BonusScreen::Draw @0x0016492c-0x0016434: star icon quad is
                // +/-0.5 centered on the translate pos, so the translate IS the star CENTER, at
                // pos+(-35,+0) (net -140 from plate center after the -105 FIRST_NAME_OFFSET); the
                // label sits at pos+(-2,+6) (net -107), 33px right of the star. (A prior
                // asm-inspector pass mis-read the star translate Y as +6 -- RE confirms +0.)
                Matrix44 mat = Matrix44::MakeScale(_Vector3<float>(texW + 1.0f, texH + 1.0f, 1.0f));
                mat.GlobalTranslate44(_Vector3<float>(pos.x - 35.0f, pos.y, pos.z));
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();

                Mortar::Mesh::DrawQuadUnCached(entry.m_Colour, NULL);

                entry.m_StarTex->UnSetUnCached();
            }

            // m_RankLabelBoxes[i]: pos+(-2,+6), SetTranslation flag=0 (left-aligned).
            // ASM-verified: v1.6.1 BonusScreen::Draw @0x164cec flag=0.
            if (i < 3 && m_RankLabelBoxes[i]) {
                m_RankLabelBoxes[i]->SetColour(entry.m_Colour, 0);
                m_RankLabelBoxes[i]->SetTranslation(
                    _Vector3<float>(pos.x - 2.0f, pos.y + 6.0f, pos.z),
                    0
                );
                m_RankLabelBoxes[i]->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
            }

            // m_RankValueBoxes[i]: pos+(220,+5), SetTranslation flag=0.
            // ASM-verified: v1.6.1 BonusScreen::Draw @0x164d7c mov r2,#0 -> flag=0.
            if (i < 3 && m_RankValueBoxes[i]) {
                m_RankValueBoxes[i]->SetColour(entry.m_Colour, 0);
                m_RankValueBoxes[i]->SetTranslation(
                    _Vector3<float>(pos.x + 220.0f, pos.y + 5.0f, pos.z),
                    0
                );
                m_RankValueBoxes[i]->Draw(_Vector2<float>(entry.m_Alpha, entry.m_Alpha), 0.0f, 1);
            }
        }

        // Row step: pos.y -= 42 at loop tail (unconditional -- runs even for unrevealed rows).
        pos.y += -42.0f;
    }

    // Restore pos to saved value (binary restores @0x00164e38).
    pos = savedPos;
}
