// BonusScreen -- post-game bonus award display (HUDControl3d subclass).
// v1.6.1: ctor @0x00162d1c, dtor D2 @0x00162724 / D1 @0x0016283c / D0 @0x00162954,
//         Update @0x00163dd0, Draw @0x0016492c
// AddAward @0x00163234, AwardScores @0x0016393c (v1.6.1 confirmed).

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

// v1.6.1 BonusScreen::BonusScreen @0x00162d5c: backing tex cached in a file-static
// SmartPtr (plain !IsValid() guard, not __cxa_guard); every ctor copies it into m_Texture.
static Mortar::SmartPtr<Mortar::Texture> s_bonusScreenBacking;

// Phase-timer rodata constants — binary @ GOT_DAT_00162cdc area.
// REVEAL_END / FINALE_HOLD / DISMISS_BUFFER removed: superseded by the memory-verified
// revealEnd formula and m_bPendingRemoval latch below (v1.6.1 BonusScreen::Update @0x00163dd0).
// AWARD_SPACING removed: superseded by TIME_PER_AWARD (0.6f), the real per-award stagger
// (see SET_DEFINES below and the Phase B ASM-spec block).

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
      m_DisplayedScore(0),
      m_TotalScore(0),
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
    if (!s_bonusScreenBacking.IsValid())
        s_bonusScreenBacking = TextureManager::LoadLocalisedTexture("arcade_diolog_box.tex");
    m_Texture = s_bonusScreenBacking;
    if (m_Texture.IsValid()) {
        size = _Vector3<float>((float)m_Texture->GetWidth(), (float)m_Texture->GetHeight(), 0.0f);
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

    // NOTE: BakedStringBox creation is BonusScreen::BuildBonusText @0x001621dc (already
    // ported + ASM-verified, called every Update tick via the m_bBonusTextBuilt latch).
    // BonusManager::SetUpBonusScreen @0x0012ede8 is a DIFFERENT method (on BonusManager,
    // already ported in BonusManager.cpp) that fills m_Awards via AddAward beforehand.
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


// v1.6.1 BonusScreen::GetTimeFirstAward @0x00162010: getter, struct@0x2d8c3c+0x00 = 0.666667f.
// Dead accessor (no binary xrefs; Update reads FIRST_AWARD directly).
float BonusScreen::GetTimeFirstAward() { return FIRST_AWARD; }

// v1.6.1 BonusScreen::GetTimePerAward @0x00162030: getter, struct@0x2d8c3c+0x04 = 0.6f.
// Dead accessor (no binary xrefs; Update reads TIME_PER_AWARD directly).
float BonusScreen::GetTimePerAward() { return TIME_PER_AWARD; }

// v1.6.1 BonusScreen::LoadContent @0x00162008: bx lr (empty).
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

// v1.6.1 BonusScreen::UnLoadContent @0x0016200c: bx lr (empty).
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
    // (>=6 branch) resolve to "bonus_star_trail"/"bonus_star_impact"; delayStep is
    // -0.05f in all 3 calls. delayCap: -0.5f for the <6 branch (@0x00163a1c),
    // -0.3f for both bursts of the >=6 branch (@0x00163af4, @0x00163bcc).
    // ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: coins spawned here pass
    // AddToScoreOnArrival (v1.6.1 @0x00162ab8), which credits coin->m_CoinValue to
    // game_work.currentScore, NOT Coin::DefaultArrivedDelegate()/CoinArrived
    // (@0x0017320C), which credits the coin wallet. The bonus-board tally must land
    // in the arcade score / high score.
    if (total < 6) {
        Coin::MakeCoins(total, 6, base, 0, 0xff3a,
                         /*target=*/nullptr, -0.05f, -0.5f,
                         "bonus_star_trail", "bonus_star_impact",
                         Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival), false);  // ASM-spec v1.6.1 delayCap -0.5f @0x00163a1c (<6 branch)
    } else {
        Coin::MakeCoins(6, 6, base, 0, 0xff3a,
                         /*target=*/nullptr, -0.05f, -0.3f,
                         "bonus_star_trail", "bonus_star_impact",
                         Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival), false);  // ASM-verified: v1.6.1 delayCap -0.3 @0x00163af4 (>=6 branch, 1st burst)
        total = m_TotalScore;  // re-read (unchanged, just re-fetched -- matches binary)
        Coin::MakeCoins(total - 6, 6, base, 0, 0xff3a,
                         /*target=*/nullptr, -0.05f, -0.3f,
                         "bonus_star_trail", "bonus_star_impact",
                         Mortar::Delegate1<void, Coin*>::MakeFree(&AddToScoreOnArrival), false);  // ASM-verified: v1.6.1 delayCap -0.3 @0x00163bcc (>=6 branch, 2nd burst)
    }

    // ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: primes the file-static
    // g_oneInThree counter (Coin.cpp, shared with AddToScoreOnArrival @0x00162ab8)
    // to 3 right before this CreateCameraShake, so the next coin landing
    // deterministically hits the ==3 firework branch (@0x00163c08).
    Coin_PrimeOneInThree(3);

    // ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: CreateCameraShake args
    // are (base, 0.3f, 1.0f) -- distinct from AddToScoreOnArrival's own (0.15,0.75)
    // shake in Coin.cpp; do not conflate the two call sites.
    if (game_work.m_FruitCamera) {
        game_work.m_FruitCamera->CreateCameraShake(base, 0.3f, 1.0f);
    }

    PSPParticleEmitter* fxEmitter =
        PSPParticleManager::GetInstance().AddEmitter(StringHash("impact_fx"), 0, false);
    if (fxEmitter) {
        fxEmitter->m_Pos = base;
    }

    GetCurrentScore(0);  // ASM-spec: call only, return value unused here (cache refresh side-effect).

    // ASM-verified: 2026-07-24T00:00Z v1.6.1 BonusScreen::AwardScores tally SFXPlay @0x00163d80 (asm-inspector)
    // "equip-unlock" (s0=0,s1=1,s2=0) => full volume, NOT silenced. Confirmed key is "equip-unlock", NOT "bonus-count-up".
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("equip-unlock", 0.0f, 1.0f);
    }
}

// ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b (asm-inspector)
// ASM-spec v1.6.1 BonusScreen::BuildBonusText @0x001621dc (asm-inspector, fresh binary read):
//   maxLines=1 (not 0) on m_ScoreBox/m_TotalBox, and per-award row colour = m_Awards[i].m_Colour
//   (not a fixed palette) -- both corrected below; supersedes the prior (stale) verified pass.
// ASM-verified: 2026-07-24T00:00Z v1.6.1 BonusScreen::BuildBonusText @0x001621dc (re-analyst)
//   maxLines=1 confirmed by direct disassembly (mov r2,#0x1 / str r2,[sp,#0x4] immediately before
//   each ctor call) for ALL FOUR BakedStringBox ctors in this function: m_RankLabelBoxes[i]
//   @0x001622ac, m_RankValueBoxes[i] @0x00162330, m_ScoreBox, m_TotalBox. Supersedes the prior
//   pass above, which only caught m_ScoreBox/m_TotalBox and left the per-row boxes at maxLines=0.
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
    // maxLines=1 for BOTH boxes (label @0x001622ac, value @0x00162330) -- same fix as m_ScoreBox
    // below; with maxLines=0 a long award name wraps to a 2nd line and overlaps the next row
    // (42px row pitch was never meant for 2 lines).
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
                1,       // maxLines: binary passes 1 (single line, no wrap) @0x001622ac -- long names must NOT wrap into the next row (same fix as m_ScoreBox)
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
                1,       // maxLines=1 @0x00162330
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
    // Phase A: m_AnimPos 3-way state machine (timer vs [0, TOTAL_TIME]). Sets m_AnimPos
    // only -- NO early return: binary Update has no early returns; every path converges
    // into the per-award loop, shake, and the unconditional BuildBonusText() tail below.
    // -----------------------------------------------------------------------
    // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: m_AnimPos 3-way on m_Timer.
    // Slide span 240.0f (rodata @0x164270), NOT tuning[+0x18]=250.
    if (m_Timer > TOTAL_TIME) {
        // Transition-out @0x163f20: eases back offscreen (+Y) as f^2, f in [0,1] over
        // TRANSITION_OUT_TIME after TOTAL_TIME.
        float f = (m_Timer - TOTAL_TIME) / TRANSITION_OUT_TIME;
        m_AnimPos = _Vector3<float>(0.0f, 240.0f * f * f, 0.0f);
    } else if (m_Timer < 0.0f) {
        // Slide-in one-shot SFX @0x00163fb4 (SFXPlay @0x0016401c): fires the single
        // frame m_Timer crosses (0.2f - TRANSITION_IN_TIME) upward -- prev <= thr < cur
        // with prev = m_Timer - dt. Sound "Pause" (string @0x27F971), args
        // (s0=0, s1=1, s2=0); return value discarded (nothing stored to +0xB4).
        // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
        {
            float thr = 0.2f - TRANSITION_IN_TIME;
            if (m_Timer - dt <= thr && thr < m_Timer && game_work.mGameSound) {
                game_work.mGameSound->SFXPlay(
                    "Pause", 0.0f, 1.0f,
                    Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.0f);
            }
        }

        // Pre-show slide-in @0x0016405c: SINE ease from -240 (offscreen) to 0.
        // fp = m_Timer/TRANSITION_IN_TIME + 1 (0->1 since m_Timer<0);
        // e = SinIdx(fp*100deg)/SinIdx(100deg) (182 = 65536/360, 18200 = 100*182);
        // AnimPos = (0,-240,0)*(1-e). 240.0f from pool @0x164270, negated via
        // _Vector3 unary operator- @0x0010e9f4.
        // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
        float fp = m_Timer / TRANSITION_IN_TIME + 1.0f;
        float e = Math::SinIdx((uint16_t)(fp * 100.0f * 182.0f)) /
                  Math::SinIdx((uint16_t)18200);
        m_AnimPos = (-_Vector3<float>(0.0f, 240.0f, 0.0f)) * (1.0f - e);

        // NOTE: binary's rush-loop SFX start/stop gate (m_RushLoopSFX, "Bonus-drum-roll")
        // requires m_Timer>0, so it never fires during this slide-in phase; the m_Timer>0
        // / m_Timer<revealEnd guards below skip it while sliding in.
    } else {
        // Settle @0x164154: reveal window through finale, m_AnimPos pinned to Zero.
        m_AnimPos = _Vector3<float>(0.0f, 0.0f, 0.0f);
    }

    // v1.6.1 BonusScreen::Update @0x163f20/@0x16405c/@0x1641a4: co-lerp mHud scales[3..5]->0.5.
    // Visually inert in port scope (HUD::Draw reads only scales[0..2]) but faithful.
    if (game_work.mHud) {
        float* s = game_work.mHud->scales;
        if (m_Timer > TOTAL_TIME) {
            float f = 1.0f - (m_Timer - TOTAL_TIME) / TRANSITION_OUT_TIME;
            for (int k = 3; k < 6; ++k) s[k] += (0.5f - s[k]) * f;
        } else if (m_Timer < 0.0f) {
            // Co-lerp factor is fp = m_Timer/TRANSITION_IN_TIME + 1 (0->1), the same
            // driver as the slide-in ease -- NOT -m_Timer/TRANSITION_IN_TIME (1->0,
            // inverted). The transition-OUT arm above (1-f) already matches the binary.
            // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
            float f = m_Timer / TRANSITION_IN_TIME + 1.0f;   // 0..1
            for (int k = 3; k < 6; ++k) s[k] += (0.5f - s[k]) * f;
        } else {
            s[3] = s[4] = s[5] = 0.5f;                 // settle: hard-set
        }
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
    // Starts "Bonus-drum-roll" at volume 0; the per-frame ramp below (after the
    // stop gate) raises it to 0.5..1.166 while the handle is live.
    // -----------------------------------------------------------------------
    if (m_RushLoopSFX == 0 && m_Timer > 0.0f && m_Timer < revealEnd) {
        Game* game = Game::GetInstance();
        if (game && game_work.mGameSound) {
            // v1.6.1 @0x00162a74 (T.1223): binds free-fn DefaultSoundRemovedCallback (engine
            // no-op default); the default-constructed Delegate1 here is behaviorally
            // equivalent. (DefaultSoundRemovedCallback returns int, not bool, so it can't be
            // passed to MakeFree<Delegate1<bool,MortarSound*>> without inventing a wrapper.)
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
    // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
    // Per-award reveal timing -- TWO animation channels, both driven by
    //   ph   = fmod(m_Timer - FIRST_AWARD, TIME_PER_AWARD)  -- position within CURRENT slot
    //   FIRST_AWARD    = 0.666667f          (initial delay before award 0's slot)
    //   TIME_PER_AWARD = 0.6f               (each award gets its own 0.6s slot,
    //                                         staggered i*0.6s after FIRST_AWARD)
    // Channel 1 -- text/star fade (entry.m_Colour.a, entry+0x53 = Colour{b,g,r,a}+3):
    //   aRamp = clamp(ph / 0.1f, 0, 1)                      -- @0x164358-0x1643a4, NO -0.2 bias
    //   m_Colour.a = (uint8_t)(aRamp * 255.0f)
    // Channel 2 -- value-box pop scale (entry.m_Alpha, entry+0x54):
    //   cur  = clamp((ph - 0.2f) / 0.1f, 0, 1)              -- @0x1643c8 this-frame driver
    //   prev = clamp((ph - dt - 0.2f) / 0.1f, 0, 1)         -- @0x1643b0 prev-frame driver
    //   one-shot gate (emitters + Shake + Bonus-Explosion SFX) fires when prev<=0 && cur>0,
    //     i.e. exactly the frame ph first crosses 0.2s UPWARD into award i's slot.
    //   m_Alpha = SinIdx((uint16_t)(cur*120.0f*182.0f)) / SinIdx((uint16_t)21840)
    //             -- @0x16460c-0x164660, sine arc over 120deg, peaks ~1.155. This is the
    //             value-box Draw SCALE (BakedStringBox::Draw(Vec2(a,a), 0, 1)), NOT an
    //             alpha multiplier.
    //   score   = (cur<=0) ? 0 : (0.5f + cur*0.5f) * TierBase*Multiplier   -- cur driver
    // Each award's own slot is independent of the others -- this is what staggers the
    // reveal instead of firing all 3 awards' effects together.
    // ASM-spec v1.6.1 BonusScreen::Update @0x001642e8: the per-award loop runs
    // EVERY tick, NOT revealEnd-gated. An earlier `if (m_Timer < revealEnd)`
    // wrapper here froze entry.m_Alpha (and each entry's DisplayedScore) at the
    // last reveal-tick value once m_Timer >= revealEnd, so at the finale the
    // tier-value numbers (drawn at scale entry.m_Alpha) vanished while the
    // stars (not alpha-gated) stayed. Out-of-range awards (localFrac <0 or >1)
    // just re-assert their settled alpha/score every frame (converge block
    // @0x164640). The revealEnd gate that IS real lives in the tail below
    // (m_DisplayedScore/m_NamePulseScale), not around this loop.
    {
        for (int i = 0; i < (int)m_Awards.size(); ++i) {
            BonusAwardHud& entry = m_Awards[i];
            float localFrac = (m_Timer - FIRST_AWARD - (float)i * TIME_PER_AWARD) / TIME_PER_AWARD;

            if (localFrac < 0.0f || localFrac > 1.0f) {
                // Out-of-range converge arm @0x164640: taken BOTH before this award's
                // slot (localFrac < 0) and after it (localFrac > 1). Not-yet-revealed
                // rows are hidden by Draw's per-row reveal gate, NOT by alpha/score
                // (a previous port arm zeroed m_Alpha/score pre-reveal -- wrong).
                // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
                entry.m_Colour.a       = 0xFF;
                entry.m_Alpha          = 1.0f;
                entry.m_DisplayedScore = entry.m_TierBase * entry.m_Multiplier;
                continue;
            }

            // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
            float ph = fmodf(m_Timer - FIRST_AWARD, TIME_PER_AWARD);

            // Channel 1: text/star fade -- ramps m_Colour.a over [0s, 0.1s) of the
            // slot (no -0.2 bias; @0x164358-0x1643a4).
            float aRamp = ph / 0.1f;
            if (aRamp < 0.0f) aRamp = 0.0f;
            if (aRamp > 1.0f) aRamp = 1.0f;
            entry.m_Colour.a = (uint8_t)(aRamp * 255.0f);

            // Channel 2 drivers (binary regs; port previously had these two swapped
            // as s15/s16).
            float cur = (ph - 0.2f) / 0.1f;          // @0x1643c8 this-frame
            if (cur < 0.0f) cur = 0.0f;
            if (cur > 1.0f) cur = 1.0f;
            float prev = ((ph - dt) - 0.2f) / 0.1f;  // @0x1643b0 prev-frame
            if (prev < 0.0f) prev = 0.0f;
            if (prev > 1.0f) prev = 1.0f;

            // One-shot reveal gate: fires the single frame ph first crosses 0.2s
            // UPWARD into award i's slot -- prev frame below 0.2 (prev<=0), this
            // frame at/above 0.2 (cur>0). ph increases monotonically within the
            // slot, so this pair is true for exactly one frame per award.
            if (prev <= 0.0f && cur > 0.0f) {
                // ASM-spec v1.6.1 BonusScreen::Update @0x00164534: memory-verified
                // literals s0=0.1f (@0x1642bc), s1=10.0f (@0x41200000).
                Shake(0.1f, 10.0f);

                // ASM-verified: 2026-07-24T00:00Z v1.6.1 BonusScreen::Update explosion SFXPlay @0x00164598 (asm-inspector)
                // "Bonus-Explosion-%i" (i=2n+1 -> 1,3,5), (s0=0,s1=1,s2=0) => full volume, NOT silenced.
                if (game_work.mGameSound) {
                    char sfxName[32];
                    snprintf(sfxName, sizeof(sfxName), "Bonus-Explosion-%i", 2 * i + 1);
                    game_work.mGameSound->SFXPlay(
                        sfxName, 0.0f, 1.0f,
                        Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.0f);
                }

                // ASM-spec v1.6.1 BonusScreen::Update @0x00163dd0: 3 particle emitters
                // spawned at accumPos = pos + m_AnimPos + m_ShakeOffset +
                // FIRST_NAME_OFFSET(-105,+40,0) + (0.5,0,0); red/blue alternate their
                // X SIGN by (i & 1) (mirrored vs each other) so successive awards
                // throw their bursts to opposite sides; both use z = 0.
                // ASM-spec v1.6.1 BonusScreen::Update @0x00164664: the emitter Y is a
                // running accumulator stepped by AWARD_Y_DIF(-42) per award i (sp+0x108
                // += -42 at the loop tail), so each award's burst lands on ITS row --
                // base + i*(-42). Base X/Z + FIRST_NAME_OFFSET(-105,+40,0) + (0.5,0,0).
                _Vector3<float> accumPos(
                    pos.x + m_AnimPos.x + m_ShakeOffset.x - 105.0f + 0.5f,
                    pos.y + m_AnimPos.y + m_ShakeOffset.y + 40.0f + (float)i * AWARD_Y_DIF,
                    pos.z + m_AnimPos.z + m_ShakeOffset.z);

                PSPParticleManager& ppm = PSPParticleManager::GetInstance();

                PSPParticleEmitter* redFx = ppm.AddEmitter(StringHash("bonus_mode_fx_red"), 0, false);
                if (redFx) {
                    // ASM-spec v1.6.1 BonusScreen::Update @0x00164440-0x00164468:
                    // x = accumX * ((i & 1) ? -1 : +1), z = 0.0f literal @0x001642c0.
                    redFx->m_Pos = _Vector3<float>(
                        accumPos.x * ((i & 1) ? -1.0f : 1.0f), accumPos.y, 0.0f);
                }
                PSPParticleEmitter* blueFx = ppm.AddEmitter(StringHash("bonus_mode_fx_blue"), 0, false);
                if (blueFx) {
                    // ASM-spec v1.6.1 BonusScreen::Update @0x001644b0-0x001644d8:
                    // mirrored vs red -- x = accumX * ((i & 1) ? +1 : -1), z = 0.0f.
                    blueFx->m_Pos = _Vector3<float>(
                        accumPos.x * ((i & 1) ? 1.0f : -1.0f), accumPos.y, 0.0f);
                }
                PSPParticleEmitter* impactFx = ppm.AddEmitter(StringHash("impact_fx"), 0, false);
                if (impactFx) {
                    // ASM-spec v1.6.1 BonusScreen::Update @0x00164520-0x00164530:
                    // accumPos copied verbatim (the nearby 10.0f is Shake's amplitude
                    // arg @0x00164528, not an emitter z).
                    impactFx->m_Pos = accumPos;
                }
            }

            // Value-box pop scale: sine arc over 120deg across [0.2s, 0.3s) of this
            // award's slot, peaks ~1.155 then settles to 1.0. Consumed as the
            // value-box Draw SCALE, not an alpha (@0x16460c-0x164660; 182 = 65536/360,
            // 21840 = 120*182).
            // ASM-verified: 2026-07-26T07:00Z v1.6.1 BonusScreen::Update @0x00163dd0 (re-analyst)
            entry.m_Alpha = Math::SinIdx((uint16_t)(cur * 120.0f * 182.0f)) /
                            Math::SinIdx((uint16_t)21840);

            // Score counter ramp-up: 0 before the gate fires, then 0.5->1.0 of
            // TierBase*Multiplier across the same [0.2s, 0.3s) window -- driven by
            // cur (the port previously used the prev-frame driver here).
            entry.m_DisplayedScore = (cur <= 0.0f) ? 0 :
                (int)((float)(entry.m_TierBase * entry.m_Multiplier) * (0.5f + cur * 0.5f));
        }
        // NOTE: the binary loop DOES accumulate each award's displayed score into
        // m_DisplayedScore (+0x7c += @0x00164684-0x00164690, zeroed @0x001641d0),
        // but it is a dead store -- the tail overwrites +0x7c unconditionally
        // (@0x00164724 / @0x00164768). Not ported; behaviour is identical.
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
    // Rush-loop per-frame volume ramp. Binary order is stop-then-ramp: once the
    // handle is released past revealEnd, no SetVolume happens. The 2.0f is
    // hardcoded in the binary (NOT the award count); pool constants 0x001642a8 =
    // 0.666f, 0x001642ac = 1.166f. SetVolume takes a GAIN (vol*255 -> u8).
    // ASM-verified: 2026-07-26T04:30Z v1.6.1 BonusScreen::Update drum-roll ramp @ 0x0016423c..0x001642e4 + SetVolume(s18) @ 0x001646a4..0x001646b4 (asm-inspector)
    // -----------------------------------------------------------------------
    if (m_RushLoopSFX) {
        float ratio = m_Timer / (FIRST_AWARD + 2.0f * TIME_PER_AWARD);
        float vol = (ratio <= 0.0f) ? 0.5f
                  : (ratio < 1.0f) ? 0.5f + ratio * 0.666f
                  : 1.166f;
        m_RushLoopSFX->SetVolume(vol);
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
    // ASM-spec v1.6.1 BonusScreen::Update @0x001647fc: wobble writes m_ShakeOffset.
    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;   // post-decrement timer feeds mag below
        float mag = m_ShakeTimer * m_ShakeAmplitude / m_ShakeDuration;
        _Vector3<float> wobbleVec(Math::SinIdx(m_ShakeAngle) * mag,
                                   Math::CosIdx(m_ShakeAngle) * mag, 0.0f);
        _Vector3<float> diff = wobbleVec - m_ShakeOffset;
        if (diff.MagnitudeSqr() >= 25.0f) {   // 0x41c80000
            // angle bumped by a random step only when displacement^2 >= 25.
            // v1.6.1 BonusScreen::Update @0x00164918-0x00164920 (T.1212 @0x00162690 = RandF):
            // step = (150.0f +- up to 60.0f jitter) * 182.0f (182 == 65536/360, deg->angle-idx).
            m_ShakeAngle = (uint16_t)((int)m_ShakeAngle +
                (int)((150.0f + Math::g_Random.RandF(1.0f) * 60.0f) * 182.0f));
        }
        m_ShakeOffset += diff * 0.2f;   // lerp toward wobbleVec
    }

    // -----------------------------------------------------------------------
    // Total-number reveal + pop pulse (binary tail @0x001646b8, after the
    // per-award loop, before BuildBonusText).
    // -----------------------------------------------------------------------
    // ASM-spec v1.6.1 BonusScreen::Update @0x001646b8-0x00164724: total-number reveal,
    // pop pulse, and finale count-up. During the reveal phase the total is hidden (0);
    // when the finale starts (m_Timer > revealEnd) m_NamePulseScale pops in with a 0.2s
    // sine ease (slight overshoot >1 then settles to 1.0) and m_DisplayedScore
    // (field +0x7C) simultaneously tallies from 0.5*m_TotalScore up to 1.0*m_TotalScore
    // over the same 0.2s window (int truncation, not round; @0x0016470c-0x00164724).
    // Overwrites the per-award loop's m_DisplayedScore accumulation while still in the
    // reveal window, and overrides the Phase C finale-fired snapshot once past it.
    if (m_Timer <= revealEnd) {
        m_DisplayedScore = 0;
        m_NamePulseScale = 0.0f;
    } else {
        float ratio = (m_Timer - revealEnd) / 0.2f;  // 0.2f @0x001646e4
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        m_DisplayedScore = (int)((float)m_TotalScore * (0.5f + ratio * 0.5f));
        // 115deg arc in SinIdx units; 182 = 65536/360, 20930 = 115*182 = sin(115deg) denom.
        uint16_t angleIdx = (uint16_t)(ratio * 115.0f * 182.0f);
        m_NamePulseScale = Math::SinIdx(angleIdx) / Math::SinIdx((uint16_t)20930);
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

    // ASM-spec v1.6.1 BonusScreen::Draw @0x0016494c: pos += m_AnimPos + m_ShakeOffset
    // (both applied before the base plate draw).
    pos += m_AnimPos + m_ShakeOffset;

    // Base box draw (HUDControl3d::Draw handles the dialog background via m_Texture@0x74),
    // centered on pos (savedPos + m_AnimPos + m_ShakeOffset), BEFORE the FIRST_NAME_OFFSET
    // content shift below.
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
