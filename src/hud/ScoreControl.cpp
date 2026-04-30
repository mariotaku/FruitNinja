// Analysed: 2026-04-30T00:00

#include "ScoreControl.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "math/MathUtil.h"
#include "math/Matrix44.h"
#include "audio/GameSound.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

using Mortar::TextureManager;

// Static caches (GOT+0x45180 block in binary — 3 cxa-guard-protected statics).
static float s_SfxCooldown     = 0.0f;  // bonus SFX rate-limiter
static float s_StaticTimer      = 0.0f;  // non-Classic digit gate (0.25s)
static uint16_t s_BannerSinIdx = 0;     // sin-table idx for new-best colour pulse

// IsMultiplayer: not yet ported (same-screen MP). Always returns false.
static bool IsMultiplayer() { return false; }

// GetCurrentScore: returns score for the given player index.
// Player 1 (idx=0) uses game->currentScore. Player 2 is not yet ported.
static int GetCurrentScore(int playerIdx) {
    if (playerIdx != 0) return 0;  // TODO: P2 score when MP is ported
    Game* game = Game::GetInstance();
    return game ? game->currentScore : 0;
}

// GetScoreMultiplyer: calls PowerUpManager in binary. Stub returns 1.
// TODO: wire to PowerUpManager::GetScoreGainMultiplier when ported.
static int GetScoreMultiplyer(int /*playerIdx*/) { return 1; }

// GetCurrentModeHighscore: reads pSaveData for the current mode's high score.
// TODO: wire to FruitSaveData once save layout is fully ported.
static int GetCurrentModeHighscore() { return 0; }

// ctor @ 0x00158c7c
ScoreControl::ScoreControl()
    : m_bDirty(1)
    , _pad7D(0)
    , m_PulseAngle(0)
    , m_ScoreSmoothed(0.0f)
    , m_DisplayedScore(0)
    , m_HighscoreToShow(0)
    , m_BannerStartTimer(-1.0f)
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
    m_Texture = 0;          // super.m_Texture (+0x74) — HUDControl3d GLuint
    m_ScoreIconTex.SetNull();
    m_HighscoreBannerTex.SetNull();
}

// Reset @ 0x001582e4
void ScoreControl::Reset() {
    // DIFFERS: binary copies m_FruitDigitTex (hud_fruit.tex) into m_Texture
    // (+0x74) so HUDControl3d::Draw renders it. hud_fruit.tex is a digit
    // spritesheet (16 frames horizontally); the binary's HUDControl3d::Draw
    // path applies per-digit UV crop via m_DigitAlpha[i] in PreDraw section B
    // and does NOT actually render the +0x74 quad whole. The port's
    // HUDControl3d::Draw lacks that UV-crop hook, so copying the GLuint here
    // would render the whole spritesheet as a 40x40 quad in the score corner
    // -- looks like a strip of fruit icons / tutorial graphic.
    // Until Section B (Classic per-digit overlay) is wired in PreDraw, leave
    // m_Texture at 0 so HUDControl3d::Draw skips it cleanly.
    // TODO: wire Classic per-digit fruit-icon overlay (PreDraw section B).
    m_Texture = 0;

    m_PulseAngle = 0;
    m_bDirty     = 1;

    // size = DAT (40.0 * globalHudScale)
    // TODO: globalHudScale from DisplayManager (DAT_001f38fc)
    // DIFFERS: using 40.0 directly; binary multiplies by global HUD scale Vec3
    size.x = size.y = 40.0f;
    size.z = 0.0f;

    for (int i = 0; i < 16; i++) m_DigitAlpha[i] = 0.0f;

    m_DigitCount     = 0;
    m_LastDigitCount = 0;

    m_LayerFlags = 1 << m_PlayerIdx;
}

// Skip @ 0x001581a0 — restore from save
void ScoreControl::Skip() {
    m_DisplayedScore = GetCurrentScore(m_PlayerIdx);
    Game* game = Game::GetInstance();
    // if game-over flag set, force banner active
    if (game && game->pauseFlag) {
        m_BannerScaleTime = 1.0f;
    }
}

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
    // Classic (gameMode==0): if digitsActive == m_LastDigitCount, ramp up at +6/sec
    //   else fade m_DigitAlpha[0..15] down at -16/sec; once index 0 hits 0, commit m_LastDigitCount
    // Non-Classic: static-timer driven (0.25s gate) same rates
    int digitsActive = m_DigitCount;  // TODO: clamp(comboCount, 0, 15) from g_GameData; stub = 0

    if (game->gameMode == 0 /* Classic */) {
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
    float baseRate = (game->gameMode == 2) ? 10.0f : 1.0f;  // gameMode==2 = Arcade
    float correction = (currentScore < 0) ? -0.6f : 0.6f;   // DAT_001588a4/a8
    float catchup  = ((float)currentScore + correction - m_ScoreSmoothed) * 0.1f;  // DAT_001588b0
    float maxStep  = (float)mult * 0.3f * baseRate;          // DAT_001588ac
    m_ScoreSmoothed += std::min(catchup, maxStep);

    int prevDisplay  = m_DisplayedScore;
    m_DisplayedScore = (int)m_ScoreSmoothed;

    if (s_SfxCooldown > 0.0f) s_SfxCooldown -= dt;

    // Stage 3: score-increase pulse + Arcade bonus-count-up SFX
    if (m_DisplayedScore > prevDisplay) {
        // Arcade SFX: bonus-count-up, gated on active wave
        // TODO: g_GameData.pCurrentWave[0x80] / [0x84] when WAVE_INFO is accessible here
        // if (s_SfxCooldown <= 0.0f && game->gameMode == 2 && waveActive && waveTimer > 0.0f)
        if (false) {
            s_SfxCooldown = 0.05f;  // DAT_001588b4
            if (game->pGameSound)
                game->pGameSound->SFXPlay("Bonus-count-up", 1.0f, 1.0f);
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
    float waveTimer = game->m_TransitionTimer;
    m_ScalePulse = (waveTimer > 0.0f) ? ((waveTimer >= 1.0f) ? 2.0f : 1.0f + waveTimer) : 1.0f;

    // Stage 5: highscore tracking
    if (game->pauseFlag == 0 || currentScore == 0) {
        int modeHS = GetCurrentModeHighscore();
        m_HighscoreToShow = (modeHS != 0)
            ? std::max(m_DisplayedScore, modeHS) : 0;
    }

    // Stage 6: position + layer flags
    // base pos: DAT_00158c44/48/4c = (-218, 138, 0)
    float posX = -218.0f;
    float posY =  138.0f;
    float posZ =    0.0f;

    // MP offset: DAT_00158c50 = 200.0
    float playerScale = (IsMultiplayer() && m_PlayerIdx == 1) ? -1.0f : 1.0f;
    posX -= 200.0f * playerScale;

    pos.x = posX;
    pos.y = posY;
    pos.z = posZ;

    m_DrawPosX = posX + 24.0f;
    m_DrawPosY = posY;
    m_DrawPosZ = posZ;

    if (waveTimer > 0.0f) {
        m_LayerFlags = 8 << m_PlayerIdx;
        // wave-mode: center text by measured width
        // TODO: full centering calc using Font::MeasureWidth when Font is accessible here
        m_DrawPosX = posX + 24.0f;
        m_DrawPosY = posY;
    } else {
        m_LayerFlags = 1 << m_PlayerIdx;
    }

    // Stage 7: highscore banner animation
    // wantBanner: waveTimer > 0.99 AND pSaveData[300] (new-best flag)
    // TODO: pSaveData[300] lookup when save-data layout is ported
    bool wantBanner = (waveTimer > 0.99f) && false;  // TODO: pSaveData new-best flag
    if (wantBanner) {
        float prev = m_BannerScaleTime;
        m_BannerScaleTime = std::min(1.0f, m_BannerScaleTime + dt * 5.0f);
        if (m_BannerScaleTime >= 1.0f) {
            m_BannerSinIdx = (uint16_t)std::max(0.0f, (float)m_BannerSinIdx + dt * 49140.0f);
        } else {
            m_BannerSinIdx = 0;
        }
        if (m_BannerScaleTime > 0.0f && prev <= 0.0f) {
            if (game->pGameSound)
                game->pGameSound->SFXPlay("New-best-score", 1.0f, 1.0f);
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
    if (game->m_TransitionTimer < -1.0f) return;

    float alphaF = 255.0f * 1.0f;  // TODO: g_GameData.cameraIntensity (not yet ported)
    uint8_t alpha = (alphaF > 255.0f) ? 255 : (alphaF < 0.0f ? 0 : (uint8_t)alphaF);
    m_DrawColour.a = alpha;

    HUDControl3d::Draw(hudScale, layerMask);
}

// PreDraw @ 0x00158e1c — main rendering (text, multiplier, highscore banner)
void ScoreControl::PreDraw(const Vec3& /*hudScale*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    if (m_PlayerIdx == 0 && IsMultiplayer()) return;

    float cameraIntensity = 1.0f;  // TODO: g_GameData.cameraIntensity
    uint8_t alpha = 255;
    float transTimer = game->m_TransitionTimer;  // g_GameData.someTimer

    if (transTimer >= -1.0f) {
        // Section A: Score digits
        if (game->pFontNumbers.IsValid()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", m_DisplayedScore);

            // Adaptive width clamp: if score >= 1000, compare against "000" baseline width
            // DAT_00159090 = 0.75, DAT_00159094 = 48.0, DAT_00159098 = 1.0
            float scaleX = 1.0f;
            float offsetX = 0.0f;
            if (m_DisplayedScore >= 1000) {
                // TODO: cache "000" width (cxa-guard) — compute each time for now
                float baseline = game->pFontNumbers->MeasureWidth(48.0f, "000") * 0.75f;
                float printed  = game->pFontNumbers->MeasureWidth(48.0f, buf);
                if (printed > baseline) {
                    scaleX  = baseline / printed;
                    offsetX = (printed - baseline) * 0.5f;
                }
            }

            Colour col(255, 255, 255, alpha);
            float drawX = m_DrawPosX + offsetX;
            float drawY = m_DrawPosY;
            float scale = 48.0f * m_ScalePulse * scaleX;
            game->pFontNumbers->DrawString(scale, 1.0f, 0.0f,
                buf, Vec3(drawX, drawY, 0.0f), col, Mortar::FONT_ALIGN_CENTER);
        }

        // Section B: Per-mode multiplier overlay
        // Classic (mode==0): per-digit fruit-icon overlays via m_DigitAlpha[i]
        // TODO: full Classic digit-overlay — needs FRUIT_INFO texture rebind and per-digit draw
        // Arcade (mode==2): "x%d" when PowerUpManager::GetScoreGainMultiplier() > 1
        // PowerUpManager stub returns 1.0, so this branch never fires; left as TODO.

        // Section C: Highscore banner text
        // Active when |transTimer| < 1.0 AND m_HighscoreToShow > 0
        if (m_HighscoreToShow > 0 && transTimer > -1.0f && transTimer < 1.0f) {
            // Base colour: Colour(0xB4, 0x80, 0x05, 200) — orange-ish
            // If m_HighscoreToShow == m_DisplayedScore: pulse between base and (0x64,0x96,0x19,200)
            // via CosIdx(s_BannerSinIdx * 0xB6) * -0.5 + 0.5
            // TODO: full colour lerp and GETSTRING(0xB5, 0) localised label
            if (game->pFontNumbers.IsValid()) {
                Colour bannerCol(0xB4, 0x80, 0x05, 200);
                char hsBuf[32];
                snprintf(hsBuf, sizeof(hsBuf), "%d", m_HighscoreToShow);
                float drawX = m_DrawPosX;
                float drawY = m_DrawPosY - 30.0f;  // offset above score — TODO: exact from binary
                game->pFontNumbers->DrawString(48.0f, 1.0f, 0.0f,
                    hsBuf, Vec3(drawX, drawY, 0.0f), bannerCol, Mortar::FONT_ALIGN_CENTER);
            }
        }
    }

    // draw_quads:
    // Section D: Score-icon texture quad (+0xA0 = score.tex) — guarded by transTimer > 0
    if (m_ScoreIconTex.IsValid() && transTimer > 0.0f) {
        Mortar::Texture* tex = m_ScoreIconTex.Get();
        float texW = (tex && tex->m_Width  > 0) ? (float)tex->m_Width  : 64.0f;
        float texH = (tex && tex->m_Height > 0) ? (float)tex->m_Height : 16.0f;

        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW, texH, 1.0f);
        // Position: single-player anchor (IsMultiplayer ? 64*cameraTimer-mpAnchor : spAnchor)
        // TODO: exact anchor values from binary
        mat.GlobalTranslate44(Vec3(m_DrawPosX, m_DrawPosY + 5.5f, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex ? tex->m_TexId : 0);
        Colour col(255, 255, 255, alpha);
        if (game) game->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Section E: Highscore banner texture (+0xA4 = new_best_score.tex)
    // guarded by m_BannerScaleTime > 0
    if (m_HighscoreBannerTex.IsValid() && m_BannerScaleTime > 0.0f) {
        Mortar::Texture* tex = m_HighscoreBannerTex.Get();
        float texW = (tex && tex->m_Width  > 0) ? (float)tex->m_Width  + 1.0f : 65.0f;
        float texH = (tex && tex->m_Height > 0) ? (float)tex->m_Height + 1.0f : 17.0f;

        // bannerScale = SinIdx(m_BannerScaleTime * RATE) — TODO: RATE from binary
        // wobbleScale = SinIdx(m_BannerSinIdx) * 0.15 + 1.0
        float wobbleScale = SinIdx(m_BannerSinIdx) * 0.15f + 1.0f;  // DAT_001597bc
        float bannerScale = m_BannerScaleTime;  // TODO: SinIdx-based scale

        Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(texW * bannerScale * wobbleScale,
                                           texH * bannerScale * wobbleScale, 1.0f);
        // RotZ44 ~5 degrees: SinIdx(0xE38)/CosIdx(0xE38)
        mat.RotZ44(SinIdx(0x0E38), CosIdx(0x0E38));
        mat.GlobalTranslate44(Vec3(texW * 0.5f - 64.0f, m_DrawPosY + 0.5f, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex ? tex->m_TexId : 0);
        Colour col(255, 255, 255, alpha);
        if (game) game->renderer.DrawQuad(col, 0.0f, 0.0f, 1.0f, 1.0f);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
