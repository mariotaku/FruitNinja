// Analysed: 2026-04-30T00:00

#include "ScoreControl.h"
#include "game/GameMode.h"
#include "network/P2PMessageHandling.h"
#include "Game.h"
#include "game/ScoreState.h"
#include "game/PowerUpManager.h"
#include "entities/FruitInfo.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "math/MathUtil.h"
#include "math/Matrix44.h"
#include "audio/GameSound.h"
#include "util/StringTable.h"
#include "screens/GameOverScreen.h"
#include "game/FruitSaveData.h"
#include "hud/HUD.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "game/GameWork.h"
#include "hud/IngamePopup.h"
#include "render/Layout.h"

// Shared TTF face for BakedStringBox labels in ScoreControl.
// v1.6.1 ScoreControl::ScoreControl @0x001ad5fc reads game_work.m_pTTFFontMain
//   (GameWork+0x614 = locale face; arabic.ttf when languageFlag==0x14, else gangofchinese.ttf).
static Mortar::FontCacheObjectTTF* GetScoreControlTTFFont() {
    if (game_work.m_pTTFFontMain) {
        return game_work.m_pTTFFontMain;
    }
    // Lazy fallback only if PreloadFontsTTF hasn't run yet.
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

using Mortar::TextureManager;

// DAT constants resolved from binary
constexpr float SCORE_PULSE_THRESHOLD   = 16384.0f;    // DAT_001588b8
constexpr float SCORE_PULSE_DECAY       = -327680.0f;  // DAT_001588d0
constexpr float SCORE_PULSE_INITIAL     = 32768.0f;    // DAT_001588d4
constexpr float SCORE_BASE_POS_X        = -218.0f;     // DAT_00158c44
constexpr float SCORE_BASE_POS_Y        = 138.0f;      // DAT_00158c48
constexpr float SCORE_MP_X_STRIDE       = 200.0f;      // DAT_00158c50
constexpr float SCORE_BANNER_TIMER_THRESH = 0.99988f;  // DAT_00158c60
constexpr float SCORE_BANNER_SIN_RATE   = 49140.0f;    // DAT_00158c64
constexpr float SCORE_FONT_SCALE        = 48.0f;       // DAT_00159094
constexpr float SCORE_LERP_CLAMP_HI     = 254.0f;      // DAT_001593ec
constexpr float SCORE_BANNER_SIN_RATE2  = 21840.0f;    // DAT_001597b8
constexpr float SCORE_BANNER_WOBBLE     = 0.15f;       // DAT_001597bc
constexpr float SCORE_BANNER_X_CENTRE   = 64.0f;       // DAT_001597c4
constexpr float SCORE_BANNER_Y_OFFSET   = 28.8f;       // DAT_001597c8
constexpr float SCORE_ICON_X_SP         = -160.0f;     // DAT_001597a4
constexpr float SCORE_ICON_X_MP_STRIDE  = 320.0f;      // DAT_001597a0

// Static caches (GOT+0x45180 block in binary — 3 cxa-guard-protected statics).
static float s_SfxCooldown     = 0.0f;  // bonus SFX rate-limiter
static float s_StaticTimer      = 0.0f;  // non-Classic digit gate (0.25s)
static uint16_t s_BannerSinIdx = 0;     // sin-table idx for new-best colour pulse

// ASM-spec v1.6.1 GetCurrentScore @0x0011a0cc: body is `return game_work.currentScore;`
// -- playerIdx is accepted and then ignored entirely; there is no per-player split.
int GetCurrentScore(int /*playerIdx*/) {
    Game* game = Game::GetInstance();
    return game ? game_work.currentScore : 0;
}

// GetScoreMultiplyer: returns PowerUpManager::GetScoreGainMultiplier().
// Arcade-only in binary (DefaultScoreDelegate, DefaultScoreDelegate §5.1).
int GetScoreMultiplyer(int /*playerIdx*/) {
    return PowerUpManager::GetInstance()->GetScoreGainMultiplier();
}

// ASM-verified: 2026-05-03T00:00 v1.6.1 GetCurrentModeHighscore @ 0x00119ee4 (asm-inspector)
// Binary: GetCurrentModeHighscore @ 0x00119ee4.
// pSaveData has highscore array at +0x44 (m_ModeHighScores[4]), indexed by gameMode (0..3).
int GetCurrentModeHighscore() {
    Game* gd = Game::GetInstance();
    if (gd && game_work.gameMode < 4 && game_work.m_SaveData)
        return game_work.m_SaveData->m_ModeHighScores[game_work.gameMode];
    return 0;
}

// ctor @ v1.6.1 0x001ad5fc
ScoreControl::ScoreControl()
    : m_bDirty(1)
    , _pad7D(0)
    , m_PulseAngle(0)
    , m_ScoreSmoothed(0.0f)
    , m_DisplayedScore(0)
    , m_HighscoreToShow(0)
    , _pad8C(-1.0f)
    , m_ScalePulse(1.0f)
    , m_DrawPosX(0.0f)
    , m_DrawPosY(0.0f)
    , m_DrawPosZ(0.0f)
    , m_BannerScaleTime(-2.0f)
    , m_BannerSinIdx(0)
    , _padAE{0, 0}
    , m_DigitCount(0)
    , m_LastDigitCount(0)
    , m_PlayerIdx(0)
    , m_pStringBox100(0)
    , m_pScoreBox(0)
{
    m_Timer = 0.0f;  // super.m_Timer = 0.0f (DAT_001ad6c8)

    // Load hud_fruit.tex into m_FruitDigitTex (+0xF8)
    m_FruitDigitTex = TextureManager::LoadLocalisedTexture("hud_fruit.tex");
    // m_ScoreIconTex (+0xA0) / m_HighscoreBannerTex (+0xA4) are left
    // default-constructed null SmartPtrs here, matching the binary: the ctor
    // @0x001ad5fc only issues ONE LoadLocalisedTexture call (feeding
    // m_FruitDigitTex); it never loads score.tex/new_best_score.tex. Those
    // fields are unread anywhere (PreDraw/Draw/Update) -- the "SCORE"
    // wordmark is drawn via m_pScoreBox (BakedStringBox) and the "NEW BEST"
    // banner via IngamePopup::Draw(popup 0x0F), both already correct below
    // in PreDraw Section D/E.

    for (int i = 0; i < 16; i++) m_DigitAlpha[i] = 0.0f;

    // m_pStringBox100 (+0x100): ctor sets 0 (lazy-alloc elsewhere).
    // m_pScoreBox (+0x104): v1.6.1 @0x001ad5fc — operator new(200);
    //   BakedStringBox(font=*(g_GameData+0x614), size=(0x8C,0x1E));
    //   SetGradient(0xFFFC5A, 0xE78308, perGlyph=0);
    //   SetText(GETSTRING(0x323="SCORE")); SetHorizontalLineSpacing(-1).
    // TODO: v1.6.1 0x001ad5fc (ScoreControl::ScoreControl) — font resolved from
    //   game_work+0x614 (FontCacheObjectTTF* at GameWork+1556), not yet in port's
    //   game_work struct; using file-local GetScoreControlTTFFont() as DIFFERS stand-in.
    Mortar::FontCacheObjectTTF* font = GetScoreControlTTFFont();
    if (font) {
        m_pScoreBox = new Mortar::BakedStringBox(
            font, 30.0f, (float)0x8C, (float)0x1E, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0.0f);
        m_pScoreBox->SetGradient(
            Colour(0xFF, 0xFC, 0x5A, 255),
            Colour(0xE7, 0x83, 0x08, 255),
            false);
        m_pScoreBox->SetText(GETSTRING_CAST_0(LSTR_SCORE));
        m_pScoreBox->SetHorizontalLineSpacing(-1);
    }

    Reset();
}

// ~ScoreControl @ v1.6.1 0x001ac3e0 (D1) / 0x001ac454 (D0)
// ASM-spec v1.6.1 ~ScoreControl @ 0x001ac3e0: delete m_pStringBox100 (+0x100),
//   delete m_pScoreBox (+0x104); then ~SmartPtr m_FruitDigitTex/Banner/Icon; then base.
ScoreControl::~ScoreControl() {
    if (m_pStringBox100) {
        m_pStringBox100->~BakedStringBox();
        operator delete(m_pStringBox100);
        m_pStringBox100 = 0;
    }
    if (m_pScoreBox) {
        m_pScoreBox->~BakedStringBox();
        operator delete(m_pScoreBox);
        m_pScoreBox = 0;
    }
    // SmartPtr members (m_FruitDigitTex, m_HighscoreBannerTex, m_ScoreIconTex)
    // are destroyed by their own dtors after this body, matching binary order.
}

// Init @ 0x00158190 — dispatches to Reset (vtable[+0x10])
void ScoreControl::Init() {
    Reset();
}

// Release @ 0x00158370 — null out all 4 texture SmartPtrs
void ScoreControl::Release() {
    m_FruitDigitTex.SetNull();
    m_Texture.SetNull();          // super.m_Texture (+0x74) — HUDControl3d GLuint
    m_ScoreIconTex.SetNull();
    m_HighscoreBannerTex.SetNull();
}

// v1.6.1 ScoreControl::Reset @ 0x001ac1c8
// ASM-verified: 2026-05-09 v1.6.1 ScoreControl::Reset @ 0x001ac1c8 (re-analyst)
void ScoreControl::Reset() {
    // Binary copies m_FruitDigitTex (+0xF8 = hud_fruit.tex) into m_Texture
    // (+0x74) unconditionally so HUDControl3d::Draw renders the watermelon
    // score-icon in Classic/Arcade. Combo mode (gameMode==1) PreDraw rebinds
    // m_Texture per fruit each frame. The earlier "DIFFERS" claim that
    // per-digit UV crop is missing was wrong -- this is the score-area icon,
    // not a digit-sheet UV target.
    m_Texture = m_FruitDigitTex;

    m_PulseAngle = 0;
    m_bDirty     = 1;

    // Binary: size *= *pHudScaleVec3 (DAT_001f4334), default (1,1,1) -- identity.
    // Static-init in _GLOBAL__I_ScoreControl_cpp @ 0x00159998. Port keeps 40.0 as-is.
    size.x = size.y = 40.0f;
    size.z = 0.0f;

    for (int i = 0; i < 16; i++) m_DigitAlpha[i] = 0.0f;

    m_DigitCount     = 0;
    m_LastDigitCount = 0;

    m_LayerFlags = 1 << m_PlayerIdx;
}

// ASM-verified: 2026-05-14T00:00 v1.6.1 ScoreControl::Skip @ 0x001abc6c (re-analyst)
// +0x4C (game_work.m_SaveData) + 300 (0x12C) = FruitSaveData::newBestThisGame (uint8_t).
// Prior port incorrectly tested game_work.bM_bPaused (engine pause flag) instead.
void ScoreControl::Skip() {
    m_DisplayedScore = GetCurrentScore(m_PlayerIdx);
    Game* game = Game::GetInstance();
    if (game && game_work.m_SaveData && game_work.m_SaveData->newBestThisGame != 0) {
        m_BannerScaleTime = 1.0f;
    }
}

// ASM-verified: 2026-05-03T00:00 v1.6.1 ScoreControl::Update @ 0x001ac5c0 (asm-inspector)
void ScoreControl::Update(float dt) {
    int currentScore = GetCurrentScore(m_PlayerIdx);

    // Player-index gate: P2 removes itself if not in multiplayer
    if (m_PlayerIdx >= 1 && !IsMultiplayer()) {
        m_bPendingRemoval = 1;
        return;
    }

    // Stage 1: per-digit alpha cascade
    // ASM-verified gate v1.6.1 ScoreControl::Update @0x001ac5c0: gameMode == 1.
    // Non-gameMode-1: static-timer driven (0.25s gate) same rates.
    // Binary @ 0x00158580: digitsActive = comboCount - 1, then clamp [0, 15].
    // g_ComboCount is from GOT[0x78f8] -> BSS @ 0x0024d764.
    int digitsActive = g_ComboCount - 1;
    if (digitsActive < 0)  digitsActive = 0;
    if (digitsActive > 15) digitsActive = 15;
    m_DigitCount = digitsActive;

    // TODO: v1.6.1 0x001ac5c0 (ScoreControl::Update) -- WaveManager::Reset g_LastSlasher
    // rename to g_ComboFruitType deferred, WAVEDIAG diagnostic in tree

    // ASM-verified v1.6.1 ScoreControl::Update @0x001ac630-64c: the cascade gate compares
    // m_LastDigitCount against Fruit::s_consecutiveType (g_ComboFruitType), not digitsActive;
    // on mismatch-then-decay-to-zero it captures g_ComboFruitType (read at function entry),
    // not the digit count. digitsActive/g_ComboCount stays the loop bound / m_DigitCount source.
    if (game_work.gameMode == GAME_MODE_COMBO /* ASM-verified: == 1 at 0x001585A8 */) {
        if (g_ComboFruitType == m_LastDigitCount) {
            for (int i = 0; i < digitsActive; i++) {
                m_DigitAlpha[i] += 6.0f * dt;
                if (m_DigitAlpha[i] > 1.0f) m_DigitAlpha[i] = 1.0f;
            }
        } else {
            for (int i = 0; i < 16; i++) {
                m_DigitAlpha[i] -= 16.0f * dt;
                if (m_DigitAlpha[i] < 0.0f) m_DigitAlpha[i] = 0.0f;
            }
            if (m_DigitAlpha[0] <= 0.0f) {
                m_LastDigitCount = g_ComboFruitType;
            }
        }
    } else {
        s_StaticTimer += dt;
        if (s_StaticTimer >= 0.25f) {
            if (g_ComboFruitType == m_LastDigitCount) {
                for (int i = 0; i < digitsActive; i++) {
                    m_DigitAlpha[i] += 6.0f * dt;
                    if (m_DigitAlpha[i] > 1.0f) m_DigitAlpha[i] = 1.0f;
                }
            } else {
                for (int i = 0; i < 16; i++) {
                    m_DigitAlpha[i] -= 16.0f * dt;
                    if (m_DigitAlpha[i] < 0.0f) m_DigitAlpha[i] = 0.0f;
                }
                if (m_DigitAlpha[0] <= 0.0f) {
                    m_LastDigitCount = g_ComboFruitType;
                }
            }
        }
    }

    // Stage 2: score easing toward currentScore
    if (m_bDirty) {
        m_bDirty = 0;
        m_ScoreSmoothed  = (float)currentScore;
        m_DisplayedScore = currentScore;
    }

    int   mult     = GetScoreMultiplyer(0);
    float baseRate = (game_work.gameMode == GAME_MODE_ARCADE) ? 10.0f : 1.0f;
    float correction = (currentScore < 0) ? -0.6f : 0.6f;   // DAT_001588a4/a8
    float catchup  = ((float)currentScore + correction - m_ScoreSmoothed) * 0.1f;  // DAT_001588b0
    float maxStep  = (float)mult * 0.3f * baseRate;          // DAT_001588ac
    m_ScoreSmoothed += std::min(catchup, maxStep);

    int prevDisplay  = m_DisplayedScore;
    m_DisplayedScore = (int)m_ScoreSmoothed;

    if (s_SfxCooldown > 0.0f) s_SfxCooldown -= dt;

    // Stage 3: score-increase pulse + Arcade bonus-count-up SFX
    if (m_DisplayedScore > prevDisplay) {
        // Binary: bonus-count-up SFX gate (Arcade end-of-game animation only).
        if (s_SfxCooldown <= 0.0f &&
            game_work.gameMode == GAME_MODE_ARCADE &&
            game_work.pGameOverScreen != nullptr &&
            game_work.pGameOverScreen->m_State > 0 &&
            game_work.pGameOverScreen->m_Timer > 0.0f) {
            s_SfxCooldown = 0.05f;  // DAT_001588b4
            if (game_work.mGameSound)
                game_work.mGameSound->SFXPlay("Bonus-count-up", 1.0f, 1.0f);
        }
        m_PulseAngle = 0x8000;  // DAT_001588d4 = 32768.0
    }

    // Stage 4: pulse angle decay
    {
        int decayed = (int)m_PulseAngle + (int)(-327680.0f * dt);  // DAT_001588d0
        if (decayed < 0) decayed = 0;
        m_PulseAngle = (uint16_t)decayed;
    }
    float pulseSin = SinIdx(m_PulseAngle);

    // waveTimer from m_TransitionTimer (g_GameData+0x0C)
    float waveTimer = game_work.m_PauseAmount;
    m_ScalePulse = (waveTimer > 0.0f) ? ((waveTimer >= 1.0f) ? 2.0f : 1.0f + waveTimer) : 1.0f;

    // Stage 5: highscore tracking
    if (game_work.bM_bPaused == 0 || currentScore == 0) {
        int modeHS = GetCurrentModeHighscore();
        m_HighscoreToShow = (modeHS != 0)
            ? std::max(m_DisplayedScore, modeHS) : 0;
    }

    // Stage 6: position + layer flags
    // ASM-verified: 2026-05-09 v1.6.1 ScoreControl::Update @ 0x001ac5c0 (re-analyst)
    // pos = base - stride * abs(m_TransitionTimer)
    // SCORE_MP_X_STRIDE (200.0 from DAT_00158c50) is the wave-transition slide
    // distance, NOT a per-player MP offset. Steady-state gameplay
    // (m_TransitionTimer == 0) leaves pos at (-218, 138, 0), on-screen.
    // DIFFERS: opt-in widescreen -- MapX the steady-state corner anchor (top-left,
    // edge-anchored via Layout.cpp's "hud.score" kOverrides entry so the SCORE
    // group hugs the widened left edge instead of drifting inward proportionally).
    // The wave-transition slide (SCORE_MP_X_STRIDE * waveScale) is left as-is: a
    // transient animation offset from this anchor, not the resting position. The
    // anchorX=-160 transition-centering target below is ALSO MapX'd (same key) --
    // it's the number's rest position once the wave lerp completes (waveTimer==1,
    // the steady in-HUD/game-over state), so it must track the wordmark's mapped
    // centre or the two drift apart at non-3:2 aspects (see anchorX comment below).
    float waveScale = fabsf(waveTimer);
    pos.x = MapX(SCORE_BASE_POS_X, "hud.score") - SCORE_MP_X_STRIDE * waveScale;
    pos.y = SCORE_BASE_POS_Y;
    pos.z = 0.0f;

    if (waveTimer > 0.0f) {
        m_LayerFlags = 8 << m_PlayerIdx;
        // base draw pos = pos + (24, 0, 0)
        m_DrawPosX = pos.x + 24.0f;
        m_DrawPosY = pos.y;
        m_DrawPosZ = pos.z;
        // wave-mode: recentre score banner via lerp toward anchor. binary @ 0x001589f0..0x00158ac6
        // ASM-verified: 2026-05-18 v1.6.1 ScoreControl::Update @ 0x001ac5c0 (re-analyst)
        // Step 1: snap drawPos to pos+(24,0,0). Step 2: lerp toward anchor by waveTimer.
        if (game_work.pFontNumbers.IsValid()) {
            char scoreBuf[32];
            // ASM-spec v1.6.1 ScoreControl::Update @0x001ac5c0: measures GetCurrentScore
            // (not m_DisplayedScore) so the centre stays stable while the count animates up.
            snprintf(scoreBuf, sizeof(scoreBuf), "%d", GetCurrentScore(m_PlayerIdx));
            // anchorX = -160 - (measured_width)/2 so the number CENTERS at -160, under the
            // (centred) SCORE label. measured_width = normalized advance * scaled font size.
            // FIX: the old MeasureWidth(scale,...) ignored its scale arg on the .fnt path, so
            // measW stayed ~1.4 (normalized) -> -measW*0.5 ~= -0.71 -> the number left-anchored
            // at ~-160 and spilled right. Multiply by m_ScalePulse*48 explicitly.
            // DIFFERS: opt-in widescreen -- the -160 centre base must go through the SAME
            // MapX("hud.score") edge-anchor shift as the SP wordmark's xPos (PreDraw Section D)
            // and the steady-state pos.x above, or the number's rest position stays pinned at
            // literal -160 while the wordmark leans toward the widened left edge, breaking
            // their (deliberate, per the comment above) shared centre alignment at 16:9. At
            // 3:2/__bada__ MapX is identity so this is a no-op (byte-identical to before).
            float measNorm = game_work.pFontNumbers->MeasureString(scoreBuf);
            float anchorX = MapX(SCORE_ICON_X_SP, "hud.score") - measNorm * m_ScalePulse * 48.0f * 0.5f;
            float anchorY = 80.0f;
            float drawStartX = pos.x + 24.0f;
            m_DrawPosX = drawStartX + (anchorX - drawStartX) * waveTimer;
            m_DrawPosY = pos.y + (anchorY - pos.y) * waveTimer;
            m_DrawPosZ = pos.z + (0.0f - pos.z) * waveTimer;
        }
    } else {
        m_LayerFlags = 1 << m_PlayerIdx;
        m_DrawPosX = pos.x + 24.0f;
        m_DrawPosY = pos.y;
        m_DrawPosZ = pos.z;
    }

    // Stage 7: highscore banner animation
    bool wantBanner = (waveTimer > SCORE_BANNER_TIMER_THRESH) &&
                      (game_work.m_SaveData && game_work.m_SaveData->newBestThisGame);
    if (wantBanner) {
        float prev = m_BannerScaleTime;
        m_BannerScaleTime = std::min(1.0f, m_BannerScaleTime + dt * 5.0f);
        if (m_BannerScaleTime >= 1.0f) {
            m_BannerSinIdx = (uint16_t)std::max(0.0f, (float)m_BannerSinIdx + dt * SCORE_BANNER_SIN_RATE);
        } else {
            m_BannerSinIdx = 0;
        }
        if (m_BannerScaleTime > 0.0f && prev <= 0.0f) {
            if (game_work.mGameSound)
                game_work.mGameSound->SFXPlay("New-best-score", 1.0f, 1.0f);
        }
    } else {
        m_BannerScaleTime = std::max(-1.5f, m_BannerScaleTime - dt * 20.0f);
    }

    // Stage 8: size pulse — size = 40 + pulseSin*10
    float sizeVal = 40.0f + pulseSin * 10.0f;  // DAT_00158c68 = 40.0
    size.x = size.y = sizeVal;
}

// Draw @ 0x001abcb0 (v1.6.1 ScoreControl::Draw) — alpha gate, delegates to HUDControl3d::Draw for +0x74 quad
void ScoreControl::Draw(float* hudScaleRaw) {
    // Skip P1 in multiplayer
    if (m_PlayerIdx == 0 && IsMultiplayer()) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    // g_GameData.someTimer >= -1.0f — uses m_TransitionTimer (+0x0C)
    if (game_work.m_PauseAmount < -1.0f) return;

    // Binary @0x1abce8: vldr.32 s15,[r3,#0x20] (HUD+0x20 = m_DrawAlpha, per-frame alpha).
    float intensity = (game_work.mHud) ? game_work.mHud->m_DrawAlpha : 1.0f;
    float alphaF = 255.0f * intensity;
    uint8_t alpha = (alphaF > 255.0f) ? 255 : (alphaF < 0.0f ? 0 : (uint8_t)alphaF);
    m_DrawColour.a = alpha;

    HUDControl3d::Draw(hudScaleRaw);
}

// PreDraw @ 0x001ace80 (v1.6.1 ScoreControl::PreDraw) — main rendering (text, multiplier, highscore banner)
void ScoreControl::PreDraw(float* /*hudScale*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    if (m_PlayerIdx == 0 && IsMultiplayer()) return;

    // Binary @0x1aceac: vldr.32 s15,[r3,#0x20] (HUD+0x20 = m_DrawAlpha, per-frame alpha).
    float cameraIntensity = (game_work.mHud) ? game_work.mHud->m_DrawAlpha : 1.0f;
    uint8_t alpha = (uint8_t)std::min(255.0f, std::max(0.0f, 255.0f * cameraIntensity));
    float transTimer = game_work.m_PauseAmount;  // g_GameData.someTimer

    if (transTimer >= -1.0f) {
        // Section A: Score digits
        if (game_work.pFontNumbers.IsValid()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", m_DisplayedScore);

            // Adaptive width clamp: if score >= 1000, compare against "000" baseline width.
            // DAT_00159090 = 96.0 (NOT 0.75 as old comment said). DIFFERS: was *0.75f.
            // binary @ 0x00158e1c section A. DAT_00159094 = 48.0, DAT_00159098 = 0.0.
            float scaleX = 1.0f;
            float offsetX = 0.0f;
            if (m_DisplayedScore >= 1000) {
                // PERF: binary caches via cxa-guard at 0x00159090 (= 96.0 = 48 * 2);
                // recomputing each frame is functionally equivalent.
                float baseline = game_work.pFontNumbers->MeasureWidth(48.0f, "000") * 96.0f;
                // ASM-spec v1.6.1 ScoreControl::PreDraw @0x001ace80: printed =
                // MeasureString(buf) * m_ScalePulse * 48.0 (world-units width of the
                // string as actually drawn); without the * m_ScalePulse * 48.0 the
                // clamp can never fire.
                float printed  = game_work.pFontNumbers->MeasureWidth(48.0f, buf) * m_ScalePulse * 48.0f;
                if (printed > baseline) {
                    scaleX  = baseline / printed;
                    offsetX = (printed - baseline) * 0.5f;
                }
            }

            Colour col(255, 255, 255, alpha);
            float drawX = m_DrawPosX + offsetX;
            float drawY = m_DrawPosY;
            float scale = 48.0f * m_ScalePulse * scaleX;
            // binary alignment = 0x0d (CENTER|MIDDLE|BOTTOM).
            // binary @ 0x00158e1c section A FontDrawString alignment.
            // CENTER (0x01) is INERT in the binary's Font::DrawString -- it
            // produces lineOffset = 0, identical to LEFT (0x00). So this call
            // effectively LEFT-anchors the digit string at (drawX, drawY),
            // which keeps multi-digit scores to the right of the watermelon
            // icon. See ASM-verified v1.6.1 Mortar::Font::DrawString @0x0024c7f0.
            game_work.pFontNumbers->DrawString(scale, 1.0f, 0.0f,
                buf, _Vector3<float>(drawX, drawY, 0.0f), col, 0x0d);
        }

        // Section B: per-digit combo overlay.
        // ASM-verified gate v1.6.1 ScoreControl::PreDraw @0x001ace80: gameMode == 1.
        if (game_work.gameMode == GAME_MODE_COMBO) {
            // Texture rebind: pick FRUIT_INFO icon by clamped combo fruit type.
            // ASM-verified v1.6.1 ScoreControl::PreDraw @0x001ad0b8-1ad0e4: reads
            // Fruit::s_consecutiveType (g_ComboFruitType) directly, not m_LastDigitCount.
            // Executed once before the per-digit loop.
            int comboCount = g_ComboFruitType;
            if (comboCount < 0) comboCount = 0;
            int fruitInfoCount = g_FruitInfoCount;
            int idx = (comboCount < 1) ? 0 : (comboCount < fruitInfoCount ? comboCount : fruitInfoCount - 1);
            const FruitInfo* fi = FruitInfo_Get(idx);
            Colour tint(255, 255, 255, alpha);
            // Rebind +0x74 (m_Texture) to per-fruit HUD icon for this frame.
            // HUDControl3d::Draw will render it after PreDraw returns.
            // m_HudTexture corresponds to fi->m_pFruitTexture in the binary doc.
            if (fi->m_HudTexture.IsValid()) m_Texture = fi->m_HudTexture;

            // Tint from FRUIT_INFO->m_FactColour (+0x2F8), alpha overridden.
            tint.r = fi->m_FactColour[0];
            tint.g = fi->m_FactColour[1];
            tint.b = fi->m_FactColour[2];
            tint.a = alpha;

            // Cursor init: MeasureString(scoreBuf) * m_ScalePulse * 48 + 5.
            // ASM-spec v1.6.1 ScoreControl::PreDraw @0x001ace80 (0x001ad16c): the
            // cursor-init measurement uses pM_Fonts[2] = pFontNumbers (the font that
            // drew the score digits), NOT pFontMain. Only the per-digit draw +
            // advance below use pFontMain ("font_fruit_ninja.fnt").
            if (game_work.pFontMain.IsValid()) {
                char scoreBuf[32];
                snprintf(scoreBuf, sizeof(scoreBuf), "%d", m_DisplayedScore);
                float cursorX = game_work.pFontNumbers->MeasureWidth(48.0f, scoreBuf) * m_ScalePulse * 48.0f + 5.0f;

                // Per-digit loop (binary @ 0x001590B8..0x001591BC)
                for (int i = 0; i < 16; i++) {
                    if (m_DigitAlpha[i] <= 0.0f) continue;

                    // Sin-eased scale: angle 0..135 deg mapped via alpha 0..1
                    // binary @ 0x001590DC..0x001590FA
                    uint16_t angle = (uint16_t)(int32_t)(135.0f * m_DigitAlpha[i] * 182.0f);
                    float s = SinIdx(angle);
                    float scale = s * (45.0f + (float)i * 6.0f);

                    char label[16];
                    snprintf(label, sizeof(label), "%d", 1 << (i + 1));  // "2","4","8","16",...

                    float drawX = m_DrawPosX + cursorX;
                    float drawY = 155.0f;  // hard-coded per binary @ 0x0015914C
                    game_work.pFontMain->DrawString(scale, 1.0f, 0.0f,
                        label, _Vector3<float>(drawX, drawY, 0.0f), tint, Mortar::FONT_ALIGN_CENTER);

                    // binary @ 0x001591b4: cursorX += MeasureString(label) * scale + 5.0
                    cursorX += game_work.pFontMain->MeasureWidth(scale, label) * scale + 5.0f;
                }
            }
        }

        // Arcade (mode==2): "x%d" when PowerUpManager::GetScoreGainMultiplier() > 1
        // Arcade x-mult font: binary loads game[+0x80] = pFontBlue2
        // ("fruit_ninja_numbers_blue2.fnt"). Verified 2026-05-09 (re-analyst
        // @ 0x00159238).
        // ASM-verified: 2026-05-10 v1.6.1 ScoreControl::PreDraw @ 0x001ace80 (re-analyst).
        // Anchor uses raw pos (m_Pos.x/y), NOT m_DrawPosX/Y:
        //   X = pos.x - 18.0   (literal 0x41900000)
        //   Y = pos.y - 52.0   (DAT_001593d4 = 0x42500000)
        // Earlier port had (m_DrawPosX, m_DrawPosY + 30.0): off by 24+18=42 px
        // horizontally (uses +24 drawPos offset AND wrong sign of -18) and
        // 82 px vertically (sign-flipped 30 vs -52).
        if (game_work.gameMode == GAME_MODE_ARCADE) {
            int mult = PowerUpManager::GetInstance()->GetScoreGainMultiplier();
            // ASM-spec v1.6.1 ScoreControl::PreDraw @0x001ace80: gated on
            // game_work.bM_bPaused == 0; scale = m_ScalePulse * 48.0 * 0.75;
            // alignment = 0x0d (CENTER|MIDDLE|BOTTOM).
            if (mult > 1 && game_work.bM_bPaused == 0 && game_work.pFontBlue2.IsValid()) {
                char multBuf[16];
                snprintf(multBuf, sizeof(multBuf), "x%d", mult);
                Colour col(255, 255, 255, alpha);
                game_work.pFontBlue2->DrawString(m_ScalePulse * 48.0f * 0.75f, 1.0f, 0.0f,
                    multBuf, _Vector3<float>(pos.x - 18.0f, pos.y - 52.0f, 0.0f),
                    col, 0x0d);
            }
        }

    }

    // Section C: Highscore banner text (inlined NewDrawBestScore @0x001abd98).
    // ASM-spec v1.6.1 ScoreControl::PreDraw @0x001ace80: NewDrawBestScore is called
    // UNCONDITIONALLY (outside the transTimer >= -1 block); its else-branch resets
    // the green-pulse cycle whenever the |t| < 1 && highscore > 0 gate fails, so the
    // banner pulses from orange again on re-entry instead of reappearing fully green.
    {
        // Active when |transTimer| < 1.0 AND m_HighscoreToShow > 0
        if (m_HighscoreToShow > 0 && transTimer > -1.0f && transTimer < 1.0f) {
            // Binary @ 0x001591BC: green-pulse lerp on highscore-reached banner.
            // ASM-verified: 2026-05-03T00:00 v1.6.1 ScoreControl::PreDraw @ 0x001ace80 (asm-inspector)
            Colour col(0xB4, 0x80, 0x05, 200);  // base orange
            if (m_HighscoreToShow == m_DisplayedScore) {
                s_BannerSinIdx += (!game_work.bM_Mode) ? 6 : 0;
                if (s_BannerSinIdx > 0xB3) s_BannerSinIdx = 0xB4;
                float t = CosIdx((int16_t)s_BannerSinIdx * 0xB6) * -0.5f + 0.5f;
                Colour green(0x64, 0x96, 0x19, 200);
                col.r = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.r + (int)((green.r - col.r) * t)));
                col.g = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.g + (int)((green.g - col.g) * t)));
                col.b = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.b + (int)((green.b - col.b) * t)));
                col.a = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.a + (int)((green.a - col.a) * t)));
            }
            col.a = alpha;
            // ASM-spec v1.6.1 ScoreControl::NewDrawBestScore @0x001abd98: BEST label =
            // BakedStringBox(TTF) "%s %d" GETSTRING_CAST_0(LSTR_BEST=200), single draw.
            {
                const char* label = GETSTRING_CAST_0(LSTR_BEST);
                char buf[512];
                snprintf(buf, sizeof(buf), "%s %d", label, m_HighscoreToShow);
                if (!m_pStringBox100) {
                    Mortar::FontCacheObjectTTF* font = GetScoreControlTTFFont();
                    if (font) {
                        m_pStringBox100 = new Mortar::BakedStringBox(font, 12.0f, 100, 20, (Mortar::ALIGNMENT_TYPE)0xd, 1, 0);
                        m_pStringBox100->SetHorizontalLineSpacing(-1);
                    }
                }
                if (m_pStringBox100) {
                    m_pStringBox100->SetColour(col, 1);
                    m_pStringBox100->SetText(buf);
                    m_pStringBox100->SetTranslation(_Vector3<float>(pos.x - 19.0f, pos.y - 23.8f, 0.0f), 0);
                    m_pStringBox100->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
                }
            }
        } else {
            // ASM-spec v1.6.1 ScoreControl::NewDrawBestScore @0x001abd98: gate
            // failed -> reset the colour-pulse cycle.
            s_BannerSinIdx = 0;
        }
    }

    // draw_quads:
    // Section D: localized TTF score wordmark via m_pScoreBox — guarded by transTimer > 0.
    // ASM-spec v1.6.1 ScoreControl::PreDraw @0x001ace80: localized TTF score wordmark via
    //   m_pScoreBox (not score.tex); SetTranslation 2nd arg=1 (preShift).
    if (m_pScoreBox && transTimer > 0.0f) {
        // DIFFERS: opt-in widescreen -- SP wordmark anchor MapX'd edge-anchored
        // (same "hud.score" key/corner as the score readout above, so the "SCORE"
        // label and the number hug the widened left edge together). MP centering
        // branch left as-is (transient wave-transition target, not the resting corner).
        // TODO: v1.6.1 ScoreControl::PreDraw @0x001ace80 -- MP wordmark x is 160*t-160
        // (deferred to the suspended feat/mp-revival work).
        float xPos = IsMultiplayer()
            ? (SCORE_BANNER_X_CENTRE * transTimer - SCORE_ICON_X_MP_STRIDE)
            : MapX(SCORE_ICON_X_SP, "hud.score");
        m_pScoreBox->SetTranslation(_Vector3<float>(xPos, m_DrawPosY + 53.0f, 0.0f), 1);
        m_pScoreBox->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }

    // Section E: NEW BEST SCORE banner (type 0x0F IngamePopup)
    // Binary ScoreControl::PreDraw @0x001ace80 -- replaced inline texture draw with
    // pM_Popups[0xF]->Draw(animScale, &pos). The scale anim (bannerScale * wobbleScale)
    // stays; the texture draw is now owned by IngamePopup.
    // ASM-spec v1.6.1 ScoreControl::PreDraw @0x001ace80: whole banner block is
    // nested inside if (0.0 < flM_PauseAmount), so it hides during pause/transition;
    // scale = SinIdx(k) / SinIdx(0x5550) * (1 + 0.15 * SinIdx(m_BannerSinIdx)) --
    // the / SinIdx(0x5550) (sin 120 deg ~= 0.866) normalises the ease to peak > 1.
    if (transTimer > 0.0f && m_BannerScaleTime > 0.0f) {
        float k = m_BannerScaleTime * SCORE_BANNER_SIN_RATE2;
        uint16_t idx = (k > 0.0f) ? (uint16_t)(int)k : 0;
        float bannerScale = SinIdx(idx) / SinIdx(0x5550);
        float wobbleScale = SinIdx(m_BannerSinIdx) * SCORE_BANNER_WOBBLE + 1.0f;
        float animScale = bannerScale * wobbleScale;

        IngamePopup* popup = GetIngamePopup(0x0F);
        if (popup) {
            // DIFFERS: opt-in widescreen -- the binary's -100.0f literal is an
            // independent constant, NOT derived from the score number's pos.x.
            // It happens to sit top-right-of-the-digits at 3:2 only because both
            // this literal and SCORE_ICON_X_SP/SCORE_BASE_POS_X were hand-tuned to
            // the same 3:2 layout. Route it through the SAME "hud.score" MapX key
            // as the SCORE wordmark/number (see Update's pos.x and PreDraw Section
            // D's xPos) so the banner leans left together with them instead of
            // staying pinned while they move -- preserves the relative top-right
            // offset from the digits at any aspect. Identity at 3:2/__bada__.
            _Vector3<float> bannerPos(MapX(-100.0f, "hud.score"), 70.0f, 0.0f);
            popup->Draw(bannerPos, animScale);
        }
    }
}

// ASM-verified: 2026-05-13 v1.6.1 ScoreControl::AddMultipliyer @ 0x001abc68 (re-analyst).
// Body is a single `bx lr` -- returns r0 (= the int arg) unchanged. No
// internal callers in the shipping binary; the multiplier path is owned
// by ScoreMultiplyerBoard. Kept here only so the port's symbol table
// matches the binary's exported names exactly.
int ScoreControl::AddMultipliyer(int x) { return x; }
