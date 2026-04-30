// Analysed: 2026-04-30T12:00

#include "TimeControl.h"
#include "Game.h"
#include "game/GameOver.h"
#include "audio/GameSound.h"
#include "screens/MainScreen.h"
#include "render/Font.h"
#include <cstdio>
#include <cstring>
#include <cmath>

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
    return game->gameMode == 2 || game->gameMode == 3;
}

void TimeControl::Init() {
    Reset();
}

void TimeControl::Release() {
    // vtable[3]: SmartPtr<Texture>::SetNull(+0x74) — port has no texture
}

void TimeControl::Reset() {
    // 0x00162168
    m_PowerupOverlay[0] = '\0';
    float startSecs = m_CountdownStart;
    if (startSecs < 0.0f) startSecs = 0.0f;
    m_TimeRemaining = startSecs;

    Game* game = Game::GetInstance();
    bool arcadeOrMP = game && (game->gameMode == 2);
    // TODO: IsMultiplayer() check (same-screen MP not yet ported)
    if (arcadeOrMP) {
        m_TimeRemaining = ARCADE_START_TIME;
        // TODO: FruitSaveData[0x10C] save-resume logic when save fields ported
    }
    m_TickFrame = 0.0f;
    m_DrawColour = Colour(255, 255, 255, 255);
}

void TimeControl::CountDown(float startSeconds) {
    // 0x001620f0
    m_CountdownStart = startSeconds;
}

float TimeControl::GetCountDown() const {
    // 0x00162134
    Game* game = Game::GetInstance();
    if (!game) return m_CountdownStart;
    if (game->gameMode != 2 /* Arcade */ /* && !IsMultiplayer() */)
        return ARCADE_START_TIME;    // DAT_0016215c fallback
    return m_CountdownStart;
}

void TimeControl::AddTime(float delta) {
    // 0x001204f0
    m_TimeRemaining += delta;
}

void TimeControl::SetToMultiplayerState() {
    // vtable[11]: calls Reset
    Reset();
}

void TimeControl::Skip() {
    // vtable[13]: restore from save: m_TimeRemaining = FruitSaveData[0x10C], m_TickFrame = 0
    // TODO: FruitSaveData[0x10C] lookup when save fields ported
    m_TickFrame = 0.0f;
}

void TimeControl::Update(float dt) {
    // 0x001624a4
    Game* game = Game::GetInstance();
    if (!game) return;

    // Hide for non-timed modes
    if (!IsTimedGame()) {
        m_LayerFlags = 0;
        return;
    }
    m_LayerFlags = 1;

    if (game->pauseFlag) return;

    m_TimeRemaining -= dt;

    // Visual flash: tick animation
    m_TickFrame += dt;
    if (m_TickFrame > 5.5f) m_TickFrame = 0.0f;

    // Colour tint bands as time runs low
    float t = m_TimeRemaining;
    Colour tint(255, 255, 255, 255);
    // TODO: exact binary flash frequency computation (8/4/2 Hz at 0..10/5/2s)
    // Approximation: pulse red when time < 10s
    if (t < 10.0f) {
        float phase = m_TickFrame * (t < 2.0f ? 8.0f : (t < 5.0f ? 4.0f : 2.0f));
        float s = sinf(phase * 3.14159f);
        if (s > 0.0f) {
            tint = Colour(255, (uint8_t)(255 - (int)(s * 200)), (uint8_t)(255 - (int)(s * 200)), 255);
        }
    }
    m_DrawColour = tint;

    // Tick/Tock SFX for 0..11s (once per phase)
    // TODO: proper once-per-phase gate (binary tracks per-second via integer cast)
    if (t < 11.0f && t > 0.5f) {
        // TODO: SFXPlay("Time-tick" / "Time-tock") with proper one-shot gate
    }

    // TODO: write m_TimeRemaining to FruitSaveData[0x10C] for save persistence

    // GameOver trigger: 0x001625be
    if (m_TimeRemaining < 0.5f) {
        FN::GameOver(-1, -1.0f, -1);
        m_TimeRemaining = 0.0f;    // DAT_001627a0
        if (game->pGameSound) game->pGameSound->SFXPlay("time-up", 1.0f, 1.0f);
    }
}

void TimeControl::Draw(const Vec3& hudScale, int layerMask) {
    // 0x001628d8
    (void)layerMask;

    Game* game = Game::GetInstance();
    if (!game) return;

    // Guard: camera fully transitioned to menu -> skip
    if (game->mainScreen) {
        float ct = game->mainScreen->GetCameraTransition();
        if (fabsf(ct) >= 1.0f) return;
    }

    // Guard: non-timed mode (m_LayerFlags=0 set by Update, but also gate here)
    if (!IsTimedGame()) return;

    Mortar::Font* font = game->pFontNumbers.Get();
    if (!font) return;

    // Format countdown: OS_SPrintf("%i:%02i", min, sec) — DAT_00162bc4 = "%i:%02i"
    int totalSecs = (int)m_TimeRemaining;
    int mins = totalSecs / (int)SECS_PER_MIN;
    int secs = totalSecs % (int)SECS_PER_MIN;
    snprintf(m_TextBuffer, sizeof(m_TextBuffer), "%i:%02i", mins, secs);

    // DAT_00162b04 = -0.6, DAT_00162b08 = 0.0
    float drawX = pos.x + TEXT_X_MULT * size.x;
    float drawY = pos.y;
    Vec3 drawPos(drawX, drawY, 0.0f);

    font->DrawString(1.0f, 1.0f, 0.0f,
                     m_TextBuffer, drawPos,
                     m_DrawColour, 0xe);

    // Optional powerup overlay ("+N" time bonus text)
    if (m_PowerupOverlay[0] != '\0') {
        // DAT_00162b0c = 32.0 y-offset
        Vec3 overlayPos(drawX, drawY - POWERUP_Y_OFFSET, 0.0f);
        // TODO: real GOT-relative colour (GOT+DAT_00162b1c); placeholder white
        Colour overlayTint(255, 255, 255, 255);
        font->DrawString(1.0f, 1.0f, 0.0f,
                         m_PowerupOverlay, overlayPos,
                         overlayTint, 0);
    }

    // Binary @ 0x00162a..: tick-tock UV quad branch. Dead code in shipped binary; m_SecondaryTex never assigned. Skipped.
}
