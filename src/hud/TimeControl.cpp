// Analysed: 2026-04-30T12:00

#include "TimeControl.h"
#include "game/GameMode.h"
#include "hud/HUD.h"
#include "network/P2PMessageHandling.h"
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
#include "entities/SuperFruitControl.h"
#include "render/Layout.h"

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

TimeControl::TimeControl() {
    // ctor 0x001622e8
    m_CountdownStart = -1.0f;
    // pos = ((480 - size.x)*0.5 - 5, (320 + size.y)*0.5 - 5, 0)
    // size = (0, 18, 0)
    // DIFFERS: opt-in widescreen -- timer is top-CENTER anchored, so the
    // horizontal centering uses Layout::HalfWidth()*2 in place of the literal
    // 480 (identical to 480 when !IsWideLayout()/__bada__). No MapX() here:
    // MapX proportionally spreads a fixed X value away from centre, which
    // would be wrong for an element whose whole point is to stay centered.
    size = _Vector3<float>(0.0f, 18.0f, 0.0f);
#ifdef __bada__
    const float fieldWidth = 480.0f;
#else
    const float fieldWidth = Layout::HalfWidth() * 2.0f;
#endif
    pos  = _Vector3<float>((fieldWidth - size.x) * 0.5f - 5.0f,
                           (320.0f + size.y) * 0.5f - 5.0f,
                           0.0f);
    m_TextBuffer[0]      = '\0';
    m_bNoDestructor      = 0;
    m_PowerupOverlay[0]  = '\0';
    Reset();
}

// v1.6.1 IsTimedGame @0x0011a060: `return (uint8_t)(game_work.gameMode - 2) < 2;`
// -- a free function, whole body is the range test. No Game::GetInstance.
bool TimeControl::IsTimedGame() const {
    return game_work.gameMode == GAME_MODE_ARCADE || game_work.gameMode == GAME_MODE_ZEN;
}

void TimeControl::Init() {
    Reset();
}

void TimeControl::Release() {
    // vtable[3]: Mortar::SmartPtr<Texture>::SetNull(+0x74) -- port has no texture
}

void TimeControl::Reset() {
    // v1.6.1 TimeControl::Reset @ 0x001c0930
    m_PowerupOverlay[0] = '\0';
    float startSecs = m_CountdownStart;
    if (startSecs < 0.0f) startSecs = 0.0f;
    m_TimeRemaining = startSecs;

    // v1.6.1 TimeControl::Reset @0x001c0930: no Game::GetInstance, no pM_SaveData
    // null test, and the camera term is game_work.m_PauseAmount read directly
    // (NOT MainScreen::GetCameraTransition()).
    if (game_work.gameMode == GAME_MODE_ARCADE || IsMultiplayer()) {
        m_TimeRemaining = ARCADE_START_TIME;

        // First-boot save-slot seed.
        if (game_work.m_SaveData->m_TimeRemainingSave == 0.0f &&
            game_work.m_PauseAmount < 0.0f) {
            game_work.m_SaveData->m_TimeRemainingSave = 60.9f;
        }
    }
    m_SlowClockPhase = 0.0f;
    m_DrawColour = Colour(255, 255, 255, 255);
}

// ASM-verified: 2026-07-30 v1.6.1 TimeControl::Skip @ 0x001c089c (asm-inspector)
//   Straight-line, no branches: unconditional m_TimeRemaining = game_work.m_SaveData->m_TimeRemainingSave
//   (no Game::GetInstance() call, no m_SaveData null check in the binary), then m_SlowClockPhase = 0.
void TimeControl::Skip() {
    m_TimeRemaining = game_work.m_SaveData->m_TimeRemainingSave;
    m_SlowClockPhase = 0.0f;
}

void TimeControl::CountDown(float startSeconds) {
    // v1.6.1 TimeControl::CountDown @ 0x001c0890
    m_CountdownStart = startSeconds;
}

float TimeControl::GetCountDown() const {
    // ASM-spec v1.6.1 TimeControl::GetCountDown @ 0x001c08e8: 13 instructions total --
    // ldrb game_work.gameMode / cmp #2 / beq const / bl IsMultiplayer / cmp #0 /
    // vldreq s0,[this,#0xc0] / vldr s0,[pc]. No Game::GetInstance() call and no null guard.
    if (game_work.gameMode == GAME_MODE_ARCADE || IsMultiplayer())
        return ARCADE_START_TIME;    // DAT_001c0924 = 60.9
    return m_CountdownStart;
}

void TimeControl::AddTime(float delta) {
    m_TimeRemaining += delta;
}

bool TimeControl::SetToMultiplayerState() {
    // vtable[11]: calls Reset
    Reset();
    return HUDControl::SetToMultiplayerState();
}

// ASM-spec v1.6.1 TimeControl::Update @ 0x001c0a48
//   (downgraded from ASM-verified 2026-06-24: the stamp covered a body that
//    carried port-added `Game::GetInstance()`, `m_SaveData` and
//    `PowerUpManager::GetInstance()` null guards the binary has not.)
//   Count-down branch: two independent PowersEnabled gates @0x001c0afc/@0x001c0b80 with
//   IsInSuperFruitState between them overriding dt to 0.0 (DAT_001c0e54) @0x001c0b70 (true
//   freeze); m_StopClockAccum +0x68, m_SlowClockMult +0x6c. Gate structure + field offsets
//   instruction-faithful.
void TimeControl::Update(float dt) {
    // 0x001c0a48
    float entrySizeX = size.x;   // cached before the IsTimedGame call (binary s16)
    // Non-timed-mode early return: hide HUD, stamp sentinel into save slot,
    // and skip the LAB_001c0f00 timed-mode block entirely. Other subsystems
    // (Fruit::Chuck power-fruit abort gate) read -1.0f to detect "no time
    // limit" and skip the abort condition.
    // Binary stores through pM_SaveData unguarded (@0x001c0a48 entry block).
    if (!IsTimedGame()) {
        m_LayerFlags = Mortar::HUD_LAYER_NONE;
        game_work.m_SaveData->m_TimeRemainingSave = -1.0f;
        return;
    }
    m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;

    // Binary re-anchors pos.x every frame (no IsTimedGame() gate at this level).
    // DIFFERS: opt-in widescreen -- see ctor note; centered, not MapX'd.
#ifdef __bada__
    const float fieldWidthU = 480.0f;
#else
    const float fieldWidthU = Layout::HalfWidth() * 2.0f;
#endif
    pos.x = (fieldWidthU - entrySizeX) * 0.5f - 5.0f;

    // Pause / suppress gate — three conditions suppress the timer tick
    // (but NOT the LAB_00162818 mirror write / pos.y re-anchor).
    bool suppress = game_work.bM_Mode
                 || game_work.bM_bPaused
                 // v1.6.1 TimeControl::Update @0x001c0ad0-0x001c0ae4:
                 // ldrb [r3,#0x174] ... ldrb [r3,#0x1a1] -- the opponent-ready byte
                 // is +0x1A1, NOT the dead +0x199 relic the port used to read.
                 || (game_work.m_bMPRetryPending && !game_work.m_bP2POpponentReady);

    if (!suppress) {
        int q;
        if (m_CountdownStart <= 0.0f) {
            // ZEN count-up branch: tick time only; slow-clock join below.
            m_TimeRemaining += dt;
            q = (int)m_TimeRemaining;
        } else {
            // ARCADE / MP count-down branch.
            // Cache the BLUE channel: the low-time flash toggles red(255,100,100)
            // <-> white(255,255,255), which differ in G/B, NOT R (R is 255 in both).
            // Binary gates tick-tock on m_DrawColour.m_B @ this+0x5c (@0x001c0a48/0dcc).
            uint8_t entryColourB = m_DrawColour.b;

            if (PowersEnabled()) {
                PowerUpManager* pum = PowerUpManager::GetInstance();
                if (pum->m_StopClockAccum > 0.0f) {
                    m_DrawColour = Colour(255, 100, 100, 255);
                    snprintf(m_PowerupOverlay, sizeof(m_PowerupOverlay),
                             "+%i", (int)pum->m_StopClockAccum + 1);
                    goto LAB_00162818;
                }
                m_PowerupOverlay[0] = '\0';
            }
            // v1.6.1 @0x001c0b70 -- super-fruit freezes the clock: override dt to 0.0
            // (asm @0x001c0b74 vldr s15 <- DAT_001c0e54 = 0.0f; vmovne s17). The decrement
            // below then subtracts 0 -> the timer is paused (true freeze) while a super fruit
            // is on screen. IsInSuperFruitState @0x001b9828 reads a global singleton.
            float effDt = dt;
            if (SuperFruitControl::IsInSuperFruitState()) {
                effDt = 0.0f;
            }
            // v1.6.1 @0x001c0b80 -- second, INDEPENDENT PowersEnabled() gate (separate call).
            if (PowersEnabled()) {
                PowerUpManager* pum2 = PowerUpManager::GetInstance();
                m_TimeRemaining -= effDt * pum2->m_SlowClockMult;
            } else {
                m_TimeRemaining -= effDt;
            }

            // GameOver trigger.
            if (m_TimeRemaining < 0.5f) {
                GameOver(-1, -1.0f, -1);
                m_TimeRemaining = 0.0f;
                // Reset combo on Arcade timeout.
                g_ComboCount     = 0;
                g_ComboFruitType = -1;
                m_DrawColour = Colour(255, 100, 100, 255);
                // v1.6.1 TimeControl::Update @0x001c0a48: SFXPlay unguarded.
                game_work.mGameSound->SFXPlay("time-up", 1.0f, 1.0f);
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

            // Tick-tock SFX when colour band toggled this frame.
            // s_TickTockToggle ? "Time-tick" : "Time-tock" is correct post-XOR order;
            // first fire after Reset is "Time-tick".
            if (m_TimeRemaining > 0.0f && m_TimeRemaining < 11.0f &&
                m_DrawColour.b != entryColourB) {
                static uint8_t s_TickTockToggle = 1;   // GOT byte at 0x001f3d80
                s_TickTockToggle ^= 1;
                const char* name = s_TickTockToggle ? "Time-tick" : "Time-tock";
                game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);
            }

            // Arcade q value for slow-clock join below.
            q = (int)(m_CountdownStart - m_TimeRemaining);
        }
        // Single slow-clock join point for both Zen and Arcade paths.
        // StopClock overlay path (goto LAB_00162818) skips this.
        m_SlowClockPhase = (float)(q % 6) + 0.5f;
    }

    // LAB_00162818 — runs unconditionally for all timed modes (including when suppressed).
    // Write HUD-side timer mirror every frame.
    LAB_00162818:
#ifndef __bada__
    if (game_work.mMainScreen) {
        game_work.mMainScreen->m_TimeRemainingDisplay = m_TimeRemaining;
    }
#endif // !defined(__bada__)
    // Mirror live time to game_work.m_SaveData->m_TimeRemainingSave so other
    // subsystems (e.g. Fruit::Chuck power-fruit abort gate) can read the
    // remaining wave time without a TimeControl pointer.
    // Binary @0x001c0f00 stores through pM_SaveData unguarded.
    game_work.m_SaveData->m_TimeRemainingSave = m_TimeRemaining;

    // pos.y re-anchor every timed frame based on camera transition. Non-MP branch:
    //   tiltMix = 1.0 - |cameraTransition|
    //   pos.y   = size.y * -2 * tiltMix * m_globalTimeScale + (2*size.y + 320) * 0.5
    // For size.y=18, stable in-game camera (transition=0) and timeScale=1, pos.y = 142.
    // ASM-spec v1.6.1 TimeControl::Update @0x001c0f90: the tilt term is scaled by
    // HUD+0x24 (m_globalTimeScale), so during slow-mo the clock slides toward
    // size.y+160. The binary derefs game_work.m_pHud (+0x40) unguarded.
    float camTilt = 0.0f;
    if (game_work.mMainScreen) {
        camTilt = fabsf(game_work.mMainScreen->GetCameraTransition());
    }
    const float tiltMix = 1.0f - camTilt;   // non-MP path; SameScreenMP unported
    pos.y = (size.y * -2.0f) * (tiltMix * game_work.mHud->m_globalTimeScale) +
            (size.y * 2.0f + 320.0f) * 0.5f;
}

// v1.6.1 TimeControl::Draw @0x001c12d4
void TimeControl::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

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
    _Vector3<float> drawPos(drawX, drawY, 0.0f);

    // DAT_00162b0c = 32.0 -- font size for countdown text. binary @ 0x00162982
    // ASM-spec v1.6.1 TimeControl::Draw @0x001c12d4: tint at 0x001c1364 -- m_DrawColour * hudScale before DrawString.
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour drawColour = Colour::TintColour(m_DrawColour, tintRGB);
    font->DrawString(32.0f, 1.0f, 0.0f,
                     m_TextBuffer, drawPos,
                     drawColour, 0xe);

    // Optional powerup overlay ("+N" time bonus text)
    if (m_PowerupOverlay[0] != '\0') {
        // DAT_00162b0c = 32.0 y-offset
        _Vector3<float> overlayPos(drawX, drawY - POWERUP_Y_OFFSET, 0.0f);
        // Binary @ 0x001629d0: green powerup-overlay tint.
        // DAT_00162b1c -> GOT chain -> 0x00268f6c (Colour::Green singleton).
        Colour overlayTint(0, 255, 0, 255);
        // ASM-spec v1.6.1 TimeControl::Draw @0x001c12d4: tint at 0x001c1414 -- overlayTint * hudScale before DrawString.
        overlayTint = Colour::TintColour(overlayTint, tintRGB);
        // DAT_00162b0c = 24.0 -- powerup overlay font size. binary @ 0x00162a..
        font->DrawString(24.0f, 1.0f, 0.0f,
                         m_PowerupOverlay, overlayPos,
                         overlayTint, 0);
    }

    // Verified dead branch: v1.6.1 TimeControl::m_Texture (HUDControl3d +0x74) is never
    // assigned anywhere in the binary, so the IsValid()-guarded clock-icon block in
    // v1.6.1 TimeControl::Draw @0x001c12d4 (MatrixStack Reset/Scale/Translate/Upload,
    // TintWhite(hudScale) @0x001c1520, Mesh::DrawQuadUnCached @0x001c154c, texture
    // Set/UnSet) never executes. Correctly NOT ported -- porting it would draw an icon
    // the original never shows.
    // Traced: HUDControl3d ctor @0x0018b72c (SmartPtr default-ctor only), TimeControl
    // ctor @0x001c0fe0, Init @0x001c087c (vtable slot 4 -> Reset @0x001c0930),
    // Reset, Update @0x001c0a48, CountDown @0x001c0890, Skip @0x001c089c,
    // SetToMultiplayerState @0x001c08d0, Release @0x001c11c8 (SetNull, not a load).
    // Decisive: the construction site in GameInit @0x001ce1c0 (~0x001ce560-0x001ce598)
    // does ctor -> Init -> CountDown(90.9) -> HUD::AddControl with NO
    // LoadLocalisedTexture call -- unlike the sibling ScoreControl and CoinCounter
    // built in that same function, which do load and assign their textures there.
}
