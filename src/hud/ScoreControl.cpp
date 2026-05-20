// Analysed: 2026-04-30T00:00

#include "ScoreControl.h"
#include "game/GameMode.h"
#include "Game.h"
#include "game/ScoreState.h"
#include "game/PowerUpManager.h"
#include "entities/FruitInfo.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
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
constexpr float SCORE_LABEL_BASELINE    = 48.0f;       // DAT_00159798

// Static caches (GOT+0x45180 block in binary — 3 cxa-guard-protected statics).
static float s_SfxCooldown     = 0.0f;  // bonus SFX rate-limiter
static float s_StaticTimer      = 0.0f;  // non-Classic digit gate (0.25s)
static uint16_t s_BannerSinIdx = 0;     // sin-table idx for new-best colour pulse

// IsMultiplayer: not yet ported (same-screen MP). Always returns false.
static bool IsMultiplayer() { return false; }

// GetCurrentScore: returns score for the given player index.
// Player 1 (idx=0) uses game_work.currentScore. Player 2 is not yet ported.
// ASM-verified: 2026-05-18 binary @ 0x00148e00 (re-analyst)
// Stat persistence for P2 happens in GameOverScreen::Update @ 0x00141b34, not here.
// Same-screen MP does not split saved stats; no "Score_P2" key exists.
static int GetCurrentScore(int playerIdx) {
    if (playerIdx != 0) return 0;
    Game* game = Game::GetInstance();
    return game ? game_work.currentScore : 0;
}

// GetScoreMultiplyer: returns PowerUpManager::GetScoreGainMultiplier().
// Arcade-only in binary (DefaultScoreDelegate, DefaultScoreDelegate §5.1).
static int GetScoreMultiplyer(int /*playerIdx*/) {
    return PowerUpManager::GetInstance()->GetScoreGainMultiplier();
}

// ASM-verified: 2026-05-03T00:00 binary @ 0x0010a35c (asm-inspector)
// Binary: GetCurrentModeHighscore @ 0x0010a35c.
// pSaveData has highscore array at +0x44 (m_ModeHighScores[4]), indexed by gameMode (0..3).
static int GetCurrentModeHighscore() {
    Game* gd = Game::GetInstance();
    if (gd && game_work.gameMode < 4 && game_work.m_SaveData)
        return game_work.m_SaveData->m_ModeHighScores[game_work.gameMode];
    return 0;
}

// ctor @ 0x00158c7c
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
{
    m_Timer = 0.0f;  // super.m_Timer = 0.0f (DAT_00158d3c)

    // Load hud_fruit.tex into m_FruitDigitTex (+0xF8)
    m_FruitDigitTex = TextureManager::LoadLocalisedTexture("hud_fruit.tex");
    // Load score.tex into m_ScoreIconTex (+0xA0) — the "SCORE" wordmark that
    // appears above the digits during game-over (PreDraw Section D quad).
    // Load new_best_score.tex into m_HighscoreBannerTex (+0xA4) — the "NEW
    // BEST!" banner shown when the run beats the saved highscore (Section E).
    // Earlier port only loaded hud_fruit.tex, so Section D/E quads were
    // gated out by IsValid() and the SCORE wordmark never rendered.
    m_ScoreIconTex        = TextureManager::LoadLocalisedTexture("score.tex");
    m_HighscoreBannerTex  = TextureManager::LoadLocalisedTexture("new_best_score.tex");

    for (int i = 0; i < 16; i++) m_DigitAlpha[i] = 0.0f;

    Reset();
}

// dtor @ 0x00158418 / 0x00158394
ScoreControl::~ScoreControl() {}

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

// Reset @ 0x001582e4
// ASM-verified: 2026-05-09 binary @ 0x001582e4 (re-analyst)
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

// ASM-verified: 2026-05-14T00:00 binary @ 0x001581a0 (re-analyst)
// +0x4C (game_work.m_SaveData) + 300 (0x12C) = FruitSaveData::newBestThisGame (uint8_t).
// Prior port incorrectly tested game_work.m_LevelTransitionFlag (engine pause flag) instead.
void ScoreControl::Skip() {
    m_DisplayedScore = GetCurrentScore(m_PlayerIdx);
    Game* game = Game::GetInstance();
    if (game && game_work.m_SaveData && game_work.m_SaveData->newBestThisGame != 0) {
        m_BannerScaleTime = 1.0f;
    }
}

// ASM-verified: 2026-05-03T00:00 binary @ 0x0015853c (asm-inspector)
// Update @ 0x0015853c
void ScoreControl::Update(float dt) {
    int currentScore = GetCurrentScore(m_PlayerIdx);

    // Player-index gate: P2 removes itself if not in multiplayer
    if (m_PlayerIdx >= 1 && !IsMultiplayer()) {
        m_bPendingRemoval = 1;
        return;
    }

    Game* game = Game::GetInstance();
    if (!game) return;

    // Stage 1: per-digit alpha cascade
    // ASM-verified gate at 0x001585A8: gameMode == 1.
    // Non-gameMode-1: static-timer driven (0.25s gate) same rates.
    // Binary @ 0x00158580: digitsActive = comboCount - 1, then clamp [0, 15].
    // g_ComboCount is from GOT[0x78f8] -> BSS @ 0x0024d764.
    int digitsActive = g_ComboCount - 1;
    if (digitsActive < 0)  digitsActive = 0;
    if (digitsActive > 15) digitsActive = 15;
    m_DigitCount = digitsActive;

    if (game_work.gameMode == Mortar::GAME_MODE_COMBO /* ASM-verified: == 1 at 0x001585A8 */) {
        if (digitsActive == m_LastDigitCount) {
            for (int i = 0; i < digitsActive; i++) {
                m_DigitAlpha[i] += 6.0f * dt;
                if (m_DigitAlpha[i] > 1.0f) m_DigitAlpha[i] = 1.0f;
            }
        } else {
            bool allFaded = true;
            for (int i = 0; i < 16; i++) {
                m_DigitAlpha[i] -= 16.0f * dt;
                if (m_DigitAlpha[i] < 0.0f) m_DigitAlpha[i] = 0.0f;
                if (i == 0 && m_DigitAlpha[0] > 0.0f) allFaded = false;
                else if (i > 0) allFaded = false;
            }
            if (m_DigitAlpha[0] <= 0.0f) {
                m_LastDigitCount = digitsActive;
            }
        }
    } else {
        s_StaticTimer += dt;
        if (s_StaticTimer >= 0.25f) {
            if (digitsActive == m_LastDigitCount) {
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
                    m_LastDigitCount = digitsActive;
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
    float baseRate = (game_work.gameMode == Mortar::GAME_MODE_ARCADE) ? 10.0f : 1.0f;
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
            game_work.gameMode == Mortar::GAME_MODE_ARCADE &&
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
    float waveTimer = game_work.m_GameDt;
    m_ScalePulse = (waveTimer > 0.0f) ? ((waveTimer >= 1.0f) ? 2.0f : 1.0f + waveTimer) : 1.0f;

    // Stage 5: highscore tracking
    if (game_work.m_LevelTransitionFlag == 0 || currentScore == 0) {
        int modeHS = GetCurrentModeHighscore();
        m_HighscoreToShow = (modeHS != 0)
            ? std::max(m_DisplayedScore, modeHS) : 0;
    }

    // Stage 6: position + layer flags
    // ASM-verified: 2026-05-09 binary @ 0x0015853c (re-analyst)
    // pos = base - stride * abs(m_TransitionTimer)
    // SCORE_MP_X_STRIDE (200.0 from DAT_00158c50) is the wave-transition slide
    // distance, NOT a per-player MP offset. Steady-state gameplay
    // (m_TransitionTimer == 0) leaves pos at (-218, 138, 0), on-screen.
    float waveScale = fabsf(waveTimer);
    pos.x = SCORE_BASE_POS_X - SCORE_MP_X_STRIDE * waveScale;
    pos.y = SCORE_BASE_POS_Y;
    pos.z = 0.0f;

    if (waveTimer > 0.0f) {
        m_LayerFlags = 8 << m_PlayerIdx;
        // base draw pos = pos + (24, 0, 0)
        m_DrawPosX = pos.x + 24.0f;
        m_DrawPosY = pos.y;
        m_DrawPosZ = pos.z;
        // wave-mode: recentre score banner via lerp toward anchor. binary @ 0x001589f0..0x00158ac6
        // ASM-verified: 2026-05-18 binary @ 0x00158a64 (re-analyst)
        // Step 1: snap drawPos to pos+(24,0,0). Step 2: lerp toward anchor by waveTimer.
        if (game_work.pFontNumbers.IsValid()) {
            char scoreBuf[32];
            snprintf(scoreBuf, sizeof(scoreBuf), "%d", m_DisplayedScore);
            float measW = game_work.pFontNumbers->MeasureWidth(m_ScalePulse * 48.0f, scoreBuf);
            Vec3 drawStart(pos.x + 24.0f, pos.y, pos.z);
            Vec3 anchor(-160.0f - measW * 0.5f, 80.0f, 0.0f);
            m_DrawPosX = drawStart.x + (anchor.x - drawStart.x) * waveTimer;
            m_DrawPosY = drawStart.y + (anchor.y - drawStart.y) * waveTimer;
            m_DrawPosZ = drawStart.z + (anchor.z - drawStart.z) * waveTimer;
            // Verified binary-faithful: hardware-render drift is asset-dependent.
            // MeasureString returns advance-sum; if FontNumbers.fnt digits have non-zero
            // left-bearing the visible glyphs shift right of the SCORE wordmark centre.
            // The binary's math is identical — any drift on the original Bada device is
            // intrinsic to the .fnt asset, not a port bug.
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

// Draw @ 0x001581d4 — alpha gate, delegates to HUDControl3d::Draw for +0x74 quad
void ScoreControl::Draw(const Vec3& hudScale, int layerMask) {
    // Skip P1 in multiplayer
    if (m_PlayerIdx == 0 && IsMultiplayer()) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    // g_GameData.someTimer >= -1.0f — uses m_TransitionTimer (+0x0C)
    if (game_work.m_GameDt < -1.0f) return;

    float intensity = (game_work.mHud) ? game_work.mHud->m_globalTimeScale : 1.0f;
    float alphaF = 255.0f * intensity;
    uint8_t alpha = (alphaF > 255.0f) ? 255 : (alphaF < 0.0f ? 0 : (uint8_t)alphaF);
    m_DrawColour.a = alpha;

    HUDControl3d::Draw(hudScale, layerMask);
}

// PreDraw @ 0x00158e1c — main rendering (text, multiplier, highscore banner)
void ScoreControl::PreDraw(const Vec3& /*hudScale*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    if (m_PlayerIdx == 0 && IsMultiplayer()) return;

    float cameraIntensity = (game_work.mHud) ? game_work.mHud->m_globalTimeScale : 1.0f;
    uint8_t alpha = (uint8_t)std::min(255.0f, std::max(0.0f, 255.0f * cameraIntensity));
    float transTimer = game_work.m_GameDt;  // g_GameData.someTimer

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
                float printed  = game_work.pFontNumbers->MeasureWidth(48.0f, buf);
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
            // icon. See ASM-verified Font::DrawString @ 0x00198e44.
            game_work.pFontNumbers->DrawString(scale, 1.0f, 0.0f,
                buf, Vec3(drawX, drawY, 0.0f), col, 0x0d);
        }

        // Section B: per-digit combo overlay.
        // ASM-verified gate at 0x00158FEC: gameMode == 1.
        // (See docs/structs/hud.md ScoreControl PreDraw Section B detail.)
        if (game_work.gameMode == Mortar::GAME_MODE_COMBO) {
            // Texture rebind: pick FRUIT_INFO icon by clamped combo count.
            // Executed once before the per-digit loop.
            int comboCount = m_LastDigitCount;
            if (comboCount < 0) comboCount = 0;
            int fruitInfoCount = FruitInfo_GetCount();
            int idx = (comboCount < 1) ? 0 : (comboCount < fruitInfoCount ? comboCount : fruitInfoCount - 1);
            const FruitInfo* fi = FruitInfo_Get(idx);
            Colour tint(255, 255, 255, alpha);
            if (fi) {
                // Rebind +0x74 (m_Texture) to per-fruit HUD icon for this frame.
                // HUDControl3d::Draw will render it after PreDraw returns.
                // m_HudTexture corresponds to fi->m_pFruitTexture in the binary doc.
                if (fi->m_HudTexture.IsValid()) m_Texture = fi->m_HudTexture;

                // Tint from FRUIT_INFO->m_FactColour (+0x2F8), alpha overridden.
                tint.r = fi->m_FactColour[0];
                tint.g = fi->m_FactColour[1];
                tint.b = fi->m_FactColour[2];
                tint.a = alpha;
            }

            // Cursor init: MeasureWidth(scoreBuf) * m_ScalePulse * 48 + 5
            // (binary @ 0x00159062..0x00159086)
            // Combo overlay font: binary loads game[+0x54] = pFontMain
            // ("font_fruit_ninja.fnt"), NOT pFontNumbers. Verified
            // 2026-05-09 (re-analyst @ 0x00159116/0x00159184).
            if (game_work.pFontMain.IsValid()) {
                char scoreBuf[32];
                snprintf(scoreBuf, sizeof(scoreBuf), "%d", m_DisplayedScore);
                float cursorX = game_work.pFontMain->MeasureWidth(48.0f, scoreBuf) * m_ScalePulse * 48.0f + 5.0f;

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
                        label, Vec3(drawX, drawY, 0.0f), tint, Mortar::FONT_ALIGN_CENTER);

                    // binary @ 0x001591b4: cursorX += MeasureString(label) * scale + 5.0
                    cursorX += game_work.pFontMain->MeasureWidth(scale, label) * scale + 5.0f;
                }
            }
        }

        // Arcade (mode==2): "x%d" when PowerUpManager::GetScoreGainMultiplier() > 1
        // Arcade x-mult font: binary loads game[+0x80] = pFontBlue2
        // ("fruit_ninja_numbers_blue2.fnt"). Verified 2026-05-09 (re-analyst
        // @ 0x00159238).
        // ASM-verified: 2026-05-10 binary @ 0x00159240..0x0015925e (re-analyst).
        // Anchor uses raw pos (m_Pos.x/y), NOT m_DrawPosX/Y:
        //   X = pos.x - 18.0   (literal 0x41900000)
        //   Y = pos.y - 52.0   (DAT_001593d4 = 0x42500000)
        // Earlier port had (m_DrawPosX, m_DrawPosY + 30.0): off by 24+18=42 px
        // horizontally (uses +24 drawPos offset AND wrong sign of -18) and
        // 82 px vertically (sign-flipped 30 vs -52).
        if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
            int mult = PowerUpManager::GetInstance()->GetScoreGainMultiplier();
            if (mult > 1 && game_work.pFontBlue2.IsValid()) {
                char multBuf[16];
                snprintf(multBuf, sizeof(multBuf), "x%d", mult);
                Colour col(255, 255, 255, alpha);
                game_work.pFontBlue2->DrawString(48.0f, 1.0f, 0.0f,
                    multBuf, Vec3(pos.x - 18.0f, pos.y - 52.0f, 0.0f),
                    col, Mortar::FONT_ALIGN_CENTER);
            }
        }

        // Section C: Highscore banner text
        // Active when |transTimer| < 1.0 AND m_HighscoreToShow > 0
        if (m_HighscoreToShow > 0 && transTimer > -1.0f && transTimer < 1.0f) {
            // Binary @ 0x001591BC: green-pulse lerp on highscore-reached banner.
            // ASM-verified: 2026-05-03T00:00 binary @ 0x00159334..0x001594e0 (asm-inspector)
            Colour col(0xB4, 0x80, 0x05, 200);  // base orange
            if (m_HighscoreToShow == m_DisplayedScore) {
                s_BannerSinIdx += (!game_work.m_Paused) ? 6 : 0;
                if (s_BannerSinIdx > 0xB3) s_BannerSinIdx = 0xB4;
                float t = CosIdx((int16_t)s_BannerSinIdx * 0xB6) * -0.5f + 0.5f;
                Colour green(0x64, 0x96, 0x19, 200);
                col.r = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.r + (int)((green.r - col.r) * t)));
                col.g = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.g + (int)((green.g - col.g) * t)));
                col.b = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.b + (int)((green.b - col.b) * t)));
                col.a = (uint8_t)std::min((int)SCORE_LERP_CLAMP_HI, std::max(0, col.a + (int)((green.a - col.a) * t)));
            }
            col.a = alpha;
            // Highscore "BEST" + digits: binary uses pFontMain (+0x54) for both
            // calls and SHARES the anchor position; alignment flags (RIGHT vs
            // CENTER) do the actual positioning so the label sits left of the
            // anchor and the digit centers on it.
            //
            // ASM-verified: 2026-05-09 binary @ 0x00159500..0x001596a4 (re-analyst).
            // Constants:
            //   - both calls scale = 20.0f, anchor = pos + (cursorX + 28, -28.8, 0)
            //   - cursorX = MeasureString(label)*20 - 48 (DAT_00159798 = 48.0)
            //   - Y offset = pos.y - 28.8 (DAT_001597c8 = 0x41e66667)
            //   - X micro-shift = +28.0 (literal 0x41e00000)
            //   - label Y-scale param2 = 0.9 (DAT_0015979c = 0x3f666666)
            //   - label alignment = 0x0E (RIGHT)
            //   - digit alignment = 0x0D (CENTER)
            if (game_work.pFontMain.IsValid()) {
                char hsBuf[32];
                snprintf(hsBuf, sizeof(hsBuf), "%d", m_HighscoreToShow);
                // ASM-verified: 2026-05-10 binary @ 0x001592c4..0x001592c8
                // (re-analyst). Binary loads `movs r0, #0xb5` then BLX to
                // GETSTRING(idx). Index 0xb5 (181) maps to LSTR_BEST which
                // resolves to "BEST:" (with trailing colon) in english_us.
                const char* label = Mortar::GETSTRING_CAST_0(LSTR_BEST);
                float labelW  = game_work.pFontMain->MeasureWidth(20.0f, label);
                float cursorX = labelW * 20.0f - SCORE_LABEL_BASELINE; // -48
                // ASM-verified: 2026-05-10 binary @ 0x00159588..0x001596a6 (re-analyst).
                // Anchor uses raw pos (m_Pos.x/y at +0x8/+0xc), NOT m_DrawPosX/Y
                // at +0x94/+0x98. Earlier port pulled m_DrawPosX (= pos.x + 24)
                // which shifted the BEST label/digit +24 px right of correct.
                const Vec3 anchor(pos.x + cursorX + 28.0f,
                                  pos.y - 28.8f,
                                  0.0f);
                const int kAlignLabel = 0x02 | 0x04 | 0x08; // RIGHT|MIDDLE|BOTTOM (0x0E)
                const int kAlignDigit = 0x01 | 0x04 | 0x08; // CENTER|MIDDLE|BOTTOM (0x0D)
                // Label call: binary uses the full Font_DrawString @ 0x00198e44
                // directly (NOT the wrapper) so it can pass yLineFactor = 0.9
                // (DAT_0015979c). The port's flat wrapper hardcodes 1.0 to
                // match the binary wrapper @ 0x00199aa0; this label needs the
                // full path.
                {
                    Mortar::Utf8StringIterator iterLabel(label);
                    Vec2 maxWH(0.0f, 0.0f);
                    game_work.pFontMain->DrawString(20.0f, /*yLineFactor=*/0.9f,
                        /*rotZ=*/0.0f, iterLabel, anchor, col, maxWH,
                        kAlignLabel, /*z=*/0.0f, nullptr);
                }
                // Digit call: binary @ 0x0015969c routes through PLT
                // 0x000fd80c which resolves to Font::DrawString @ 0x00199aa0
                // -- the wrapper, NOT a separate full overload.
                // ASM-verified: 2026-05-10 binary @ 0x00199aa0 (asm-inspector).
                // Caller emits s2=0.0 (DAT 0x001597c0) which the wrapper
                // stores as anchor.z, NOT as yLineFactor. Internally, the
                // wrapper hardcodes yLineFactor=1.0 (vmov.f32 s1,#0x3f800000
                // at 0x00199b1c) when delegating to the Vec3-anchor overload
                // at 0x00198e44. So the effective yLineFactor at the
                // alignment compute is 1.0 -- which matches the port's flat
                // wrapper that also forwards through the yLineFactor=1.0
                // hardcoding path. Earlier port change to call the full
                // overload with yLineFactor=0.0 was wrong: it bypassed the
                // wrapper hardcode and produced a 9 px y mismatch vs the
                // label.
                game_work.pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                    hsBuf, anchor, col, kAlignDigit);
            }
        }
    }

    // draw_quads:
    // Section D: Score-icon texture quad (+0xA0 = score.tex) — guarded by transTimer > 0
    // ASM-verified: 2026-05-03T00:00 binary @ 0x00159726..0x00159770 (asm-inspector)
    if (m_ScoreIconTex.IsValid() && transTimer > 0.0f) {
        Mortar::Texture* tex = m_ScoreIconTex.Get();
        float texW = (tex && tex->m_Width  > 0) ? (float)tex->m_Width  : 64.0f;
        float texH = (tex && tex->m_Height > 0) ? (float)tex->m_Height : 16.0f;

        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW, texH, 1.0f);
        mat.GlobalTranslate44(Vec3(
            IsMultiplayer() ? (SCORE_BANNER_X_CENTRE * transTimer - SCORE_ICON_X_MP_STRIDE) : SCORE_ICON_X_SP,
            m_DrawPosY + 53.0f,
            0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        if (tex) {
            tex->Set();
            Colour col(255, 255, 255, alpha);
            if (game) game->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
            tex->UnSet();
        }
    }

    // Section E: Highscore banner texture (+0xA4 = new_best_score.tex)
    // guarded by m_BannerScaleTime > 0
    // ASM-verified: 2026-05-03T00:00 binary @ 0x00159842..0x0015990c (asm-inspector)
    if (m_HighscoreBannerTex.IsValid() && m_BannerScaleTime > 0.0f) {
        Mortar::Texture* tex = m_HighscoreBannerTex.Get();
        float texW = (tex && tex->m_Width  > 0) ? (float)tex->m_Width  + 1.0f : 65.0f;
        float texH = (tex && tex->m_Height > 0) ? (float)tex->m_Height + 1.0f : 17.0f;

        // Binary @ 0x00159740..0x0015975c
        float k = m_BannerScaleTime * SCORE_BANNER_SIN_RATE2;
        uint16_t idx = (k > 0.0f) ? (uint16_t)(int)k : 0;
        float bannerScale = SinIdx(idx);
        float wobbleScale = SinIdx(m_BannerSinIdx) * SCORE_BANNER_WOBBLE + 1.0f;  // DAT_001597bc

        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW * bannerScale * wobbleScale,
                                           texH * bannerScale * wobbleScale, 1.0f);
        // RotZ44 ~5 degrees: SinIdx(0xE38)/CosIdx(0xE38)
        mat.RotZ44(SinIdx(0x0E38), CosIdx(0x0E38));
        mat.GlobalTranslate44(Vec3(texW * 0.5f - SCORE_BANNER_X_CENTRE, m_DrawPosY + SCORE_BANNER_Y_OFFSET, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        if (tex) {
            tex->Set();
            Colour col(255, 255, 255, alpha);
            if (game) game->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
            tex->UnSet();
        }
    }
}

// ASM-verified: 2026-05-13 binary @ 0x0015819c (re-analyst).
// Body is a single `bx lr` -- returns r0 (= the int arg) unchanged. No
// internal callers in the shipping binary; the multiplier path is owned
// by ScoreMultiplyerBoard. Kept here only so the port's symbol table
// matches the binary's exported names exactly.
int ScoreControl::AddMultipliyer(int x) { return x; }
