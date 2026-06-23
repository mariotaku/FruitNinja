// Analysed: 2026-04-30T12:00

#include "TimeControl.h"
#include "game/GameMode.h"
#include "Game.h"
#include "hud/HUDLayer.h"
#include "game/GameOver.h"
#include "game/ScoreState.h"
#include "audio/GameSound.h"
#include "screens/MainScreen.h"
#include "render/Font.h"
#include "game/FruitSaveData.h"
#include "game/PowerUpManager.h"
#include "game/WaveManager.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include "game/GameWork.h"

// DAT_001621ec / DAT_0016215c = 60.9 (Arcade/MP starting time)
static const float ARCADE_START_TIME = 60.9f;
// DAT_0016c9cc = 90.9 (Zen starting time, passed from GameInit)
// (value stored via CountDown; not used as a constant here)
// DAT_001627a0 = 0.0 (timer reset on game over)
// DAT_00162b04 = -0.6 (text x-multiplier of size.x)
static const float TEXT_X_MULT = -0.6f;
// DAT_00162b0c = 32.0 (powerup overlay y-offset)
static const float POWERUP_Y_OFFSET = 32.0f;
// DAT_001628c4 = 60.0 (seconds-per-minute)
static const float SECS_PER_MIN = 60.0f;

// ASM-verified: 2026-05-18 binary @ 0x000f6e04 (re-analyst)
// Binary IsMultiplayer() thunk -> impl @ 0x0010a470: unconditionally returns false.
// Same-screen MP exists as code structure but is gated off in the shipping build.
static inline bool IsMultiplayer() { return false; }

TimeControl::TimeControl() {
    // ctor 0x001622e8
    m_CountdownStart = -1.0f;
    // pos = ((480 - size.x)*0.5 - 5, (320 + size.y)*0.5 - 5, 0)
    // size = (0, 18, 0)
    size = Vec3(0.0f, 18.0f, 0.0f);
    pos  = Vec3((480.0f - size.x) * 0.5f - 5.0f,
                (320.0f + size.y) * 0.5f - 5.0f,
                0.0f);
    m_TextBuffer[0]      = '\0';
    m_bNoDestructor      = 0;
    m_PowerupOverlay[0]  = '\0';
    Reset();
}

bool TimeControl::IsTimedGame() const {
    Game* game = Game::GetInstance();
    if (!game) return false;
    return game_work.gameMode == Mortar::GAME_MODE_ARCADE || game_work.gameMode == Mortar::GAME_MODE_ZEN;
}

void TimeControl::Init() {
    Reset();
}

void TimeControl::Release() {
    // vtable[3]: Mortar::SmartPtr<Texture>::SetNull(+0x74) -- port has no texture
}

void TimeControl::Reset() {
    // 0x00162168
    m_PowerupOverlay[0] = '\0';
    float startSecs = m_CountdownStart;
    if (startSecs < 0.0f) startSecs = 0.0f;
    m_TimeRemaining = startSecs;

    Game* game = Game::GetInstance();
    bool arcadeOrMP = game && (game_work.gameMode == Mortar::GAME_MODE_ARCADE || IsMultiplayer());
    if (arcadeOrMP) {
        m_TimeRemaining = ARCADE_START_TIME;

        // Binary @ 0x001621ac: first-boot save-slot seed.
        if (game_work.m_SaveData &&
            game_work.m_SaveData->m_TimeRemainingSave == 0.0f &&
            game_work.mMainScreen && game_work.mMainScreen->GetCameraTransition() < 0.0f) {
            game_work.m_SaveData->m_TimeRemainingSave = 60.9f;   // DAT_001621ec
        }
    }
    m_SlowClockPhase = 0.0f;
    m_DrawColour = Colour(255, 255, 255, 255);
}

// ASM-verified: 2026-05-03 binary @ 0x001629xx (re-analyst)
void TimeControl::Skip() {
    // Binary @ 0x001629xx: restore from save.
    Game* game = Game::GetInstance();
    if (game && game_work.m_SaveData) {
        m_TimeRemaining = game_work.m_SaveData->m_TimeRemainingSave;
    }
    m_SlowClockPhase = 0.0f;
}

void TimeControl::CountDown(float startSeconds) {
    // 0x001620f0
    m_CountdownStart = startSeconds;
}

float TimeControl::GetCountDown() const {
    // 0x00162134
    Game* game = Game::GetInstance();
    if (!game) return m_CountdownStart;
    if (game_work.gameMode != Mortar::GAME_MODE_ARCADE && !IsMultiplayer())
        return ARCADE_START_TIME;    // DAT_0016215c fallback
    return m_CountdownStart;
}

void TimeControl::AddTime(float delta) {
    // 0x001204f0
    m_TimeRemaining += delta;
}

bool TimeControl::SetToMultiplayerState() {
    // vtable[11]: calls Reset
    Reset();
    return HUDControl::SetToMultiplayerState();
}

// ASM-verified: 2026-05-20 v1.6.1 binary @ 0x001624a4 (re-analyst)
void TimeControl::Update(float dt) {
    // 0x001624a4
    float entrySizeX = size.x;   // cached before GetInstance call (binary s16)
    Game* game = Game::GetInstance();
    if (!game) {
        m_LayerFlags = Mortar::HUD_LAYER_NONE;
#ifndef __bada__
        if (game_work.mMainScreen) game_work.mMainScreen->m_TimeRemainingDisplay = -1.0f;
#endif // !defined(__bada__)
        return;
    }
    // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x001624ec (re-analyst)
    // Non-timed-mode early return: hide HUD, stamp sentinel into save slot,
    // and skip the LAB_00162818 timed-mode block entirely. Other subsystems
    // (Fruit::Chuck power-fruit abort gate) read -1.0f to detect "no time
    // limit" and skip the abort condition.
    if (!IsTimedGame()) {
        m_LayerFlags = Mortar::HUD_LAYER_NONE;
        if (game_work.m_SaveData) {
            game_work.m_SaveData->m_TimeRemainingSave = -1.0f;
        }
        return;
    }
    m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;

    // Binary re-anchors pos.x every frame (no IsTimedGame() gate at this level).
    pos.x = (480.0f - entrySizeX) * 0.5f - 5.0f;

    // Pause / suppress gate — binary @ 0x001624e6..0x00162510.
    // Three conditions suppress the timer tick (but NOT the LAB_00162818 mirror write / pos.y re-anchor).
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x001624e6 (re-analyst)
    bool suppress = game_work.bM_Mode
                 || game_work.bM_bPaused
                 || (game_work.m_bMPRetryPending && !game_work.m_bP2PReady);

    if (!suppress) {
        int q;
        if (m_CountdownStart <= 0.0f) {
            // ZEN count-up branch: tick time only; slow-clock join below.
            // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x001627ea (re-analyst)
            m_TimeRemaining += dt;
            q = (int)m_TimeRemaining;
        } else {
            // ARCADE / MP count-down branch.
            uint8_t entryColourR = m_DrawColour.r;

            // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00162528 (re-analyst)
            // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0016257c (re-analyst)
            if (WaveManager::PowersEnabled()) {
                PowerUpManager* pum = PowerUpManager::GetInstance();
                if (pum && pum->m_StopClockAccum > 0.0f) {
                    m_DrawColour = Colour(255, 100, 100, 255);
                    snprintf(m_PowerupOverlay, sizeof(m_PowerupOverlay),
                             "+%i", (int)pum->m_StopClockAccum + 1);
                    goto LAB_00162818;
                }
                m_PowerupOverlay[0] = '\0';
                m_TimeRemaining -= dt * (pum ? pum->m_SlowClockMult : 1.0f);
            } else {
                m_PowerupOverlay[0] = '\0';
                m_TimeRemaining -= dt;
            }

            // GameOver trigger (binary @ 0x001625be).
            if (m_TimeRemaining < 0.5f) {
                FN::GameOver(-1, -1.0f, -1);
                m_TimeRemaining = 0.0f;    // DAT_001627a0
                // Reset combo on Arcade timeout (binary @ 0x001625dc).
                g_ComboCount  = 0;
                g_LastSlasher = -1;
                m_DrawColour = Colour(255, 100, 100, 255);
                if (game_work.mGameSound) game_work.mGameSound->SFXPlay("time-up", 1.0f, 1.0f);
            } else {
                // Colour tint bands as time runs low.
                // Binary: boolean alternation ((int)(t*N)) & 1 ? red : white.
                // Thresholds: 3/6/11 seconds.
                float t = m_TimeRemaining;
                Colour tint(255, 255, 255, 255);
                static const Colour RED_TINT(255, 100, 100, 255);
                if (t < 3.0f) {
                    tint = (((int)(t * 8.0f)) & 1) ? RED_TINT : Colour(255, 255, 255, 255);
                } else if (t < 6.0f) {
                    tint = (((int)(t * 4.0f)) & 1) ? RED_TINT : Colour(255, 255, 255, 255);
                } else if (t < 11.0f) {
                    tint = (((int)(t * 2.0f)) & 1) ? RED_TINT : Colour(255, 255, 255, 255);
                }
                m_DrawColour = tint;
            }

            // Tick-tock SFX when colour band toggled this frame (binary @ 0x00162716).
            // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0016273a (re-analyst)
            // s_TickTockToggle ? "Time-tick" : "Time-tock" is correct post-XOR order;
            // first fire after Reset is "Time-tick".
            if (m_TimeRemaining > 0.0f && m_TimeRemaining < 11.0f &&
                m_DrawColour.r != entryColourR) {
                static uint8_t s_TickTockToggle = 1;   // GOT byte at 0x001f3d80
                s_TickTockToggle ^= 1;
                const char* name = s_TickTockToggle ? "Time-tick" : "Time-tock";
                if (game_work.mGameSound) {
                    game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);
                }
            }

            // Arcade q value for slow-clock join below.
            // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x001627d2 (re-analyst)
            q = (int)(m_CountdownStart - m_TimeRemaining);
        }
        // Single slow-clock join point for both Zen and Arcade paths.
        // StopClock overlay path (goto LAB_00162818) skips this.
        m_SlowClockPhase = (float)(q % 6) + 0.5f;
    }

    // LAB_00162818 — runs unconditionally for all timed modes (including when suppressed).
    // Write HUD-side timer mirror every frame (binary @ 0x00162830).
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00162830 (re-analyst)
    LAB_00162818:
#ifndef __bada__
    if (game_work.mMainScreen) {
        game_work.mMainScreen->m_TimeRemainingDisplay = m_TimeRemaining;
    }
#endif // !defined(__bada__)
    // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x00162830 (re-analyst)
    // Mirror live time to game_work.m_SaveData->m_TimeRemainingSave so other
    // subsystems (e.g. Fruit::Chuck power-fruit abort gate) can read the
    // remaining wave time without a TimeControl pointer.
    if (game_work.m_SaveData) {
        game_work.m_SaveData->m_TimeRemainingSave = m_TimeRemaining;
    }

    // Binary @ LAB_00162818 -- pos.y re-anchor every timed frame based on
    // camera transition. Non-MP branch:
    //   tiltMix = 1.0 - |cameraTransition|
    //   pos.y   = size.y * -2 * tiltMix + (2*size.y + 320) * 0.5
    // For size.y=18 and stable in-game camera (transition=0), pos.y = 142.
    float camTilt = 0.0f;
    if (game_work.mMainScreen) {
        camTilt = fabsf(game_work.mMainScreen->GetCameraTransition());
    }
    const float tiltMix = 1.0f - camTilt;   // non-MP path; SameScreenMP unported
    pos.y = size.y * -2.0f * tiltMix + (size.y * 2.0f + 320.0f) * 0.5f;
}

void TimeControl::Draw(float* hudScaleRaw) {
    // 0x001628d8
    const Vec3& hudScale = *reinterpret_cast<const Vec3*>(hudScaleRaw);

    Game* game = Game::GetInstance();
    if (!game) return;

    // Guard: camera fully transitioned to menu -> skip
    if (game_work.mMainScreen) {
        float ct = game_work.mMainScreen->GetCameraTransition();
        if (fabsf(ct) >= 1.0f) return;
    }

    // Guard: non-timed mode (m_LayerFlags=0 set by Update, but also gate here)
    if (!IsTimedGame()) return;

    Mortar::Font* font = game_work.pFontNumbers.Get();
    if (!font) return;

    // Format countdown: OS_SPrintf("%i:%02i", min, sec) -- DAT_00162bc4 = "%i:%02i"
    int totalSecs = (int)m_TimeRemaining;
    int mins = totalSecs / (int)SECS_PER_MIN;
    int secs = totalSecs % (int)SECS_PER_MIN;
    snprintf(m_TextBuffer, sizeof(m_TextBuffer), "%i:%02i", mins, secs);

    // DAT_00162b04 = -0.6, DAT_00162b08 = 0.0
    float drawX = pos.x + TEXT_X_MULT * size.x;
    float drawY = pos.y;
    Vec3 drawPos(drawX, drawY, 0.0f);

    // DAT_00162b0c = 32.0 -- font size for countdown text. binary @ 0x00162982
    font->DrawString(32.0f, 1.0f, 0.0f,
                     m_TextBuffer, drawPos,
                     m_DrawColour, 0xe);

    // Optional powerup overlay ("+N" time bonus text)
    if (m_PowerupOverlay[0] != '\0') {
        // DAT_00162b0c = 32.0 y-offset
        Vec3 overlayPos(drawX, drawY - POWERUP_Y_OFFSET, 0.0f);
        // Binary @ 0x001629d0: green powerup-overlay tint.
        // DAT_00162b1c -> GOT chain -> 0x00268f6c (Colour::Green singleton).
        Colour overlayTint(0, 255, 0, 255);
        // DAT_00162b0c = 24.0 -- powerup overlay font size. binary @ 0x00162a..
        font->DrawString(24.0f, 1.0f, 0.0f,
                         m_PowerupOverlay, overlayPos,
                         overlayTint, 0);
    }

    // Binary @ 0x00162a..: tick-tock UV quad branch. Dead code in shipped binary; m_Texture never assigned. Skipped.
}
