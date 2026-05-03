// Analysed: 2026-04-30T12:00

#include "TimeControl.h"
#include "Game.h"
#include "game/GameOver.h"
#include "game/ScoreState.h"
#include "audio/GameSound.h"
#include "screens/MainScreen.h"
#include "render/Font.h"
#include "game/FruitSaveData.h"
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

// TODO: full IsMultiplayer (binary @ 0x000f6e04 thunk) when split-screen MP is ported.
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
    return game->gameMode == 2 || game->gameMode == 3;
}

void TimeControl::Init() {
    Reset();
}

void TimeControl::Release() {
    // vtable[3]: SmartPtr<Texture>::SetNull(+0x74) -- port has no texture
}

void TimeControl::Reset() {
    // 0x00162168
    m_PowerupOverlay[0] = '\0';
    float startSecs = m_CountdownStart;
    if (startSecs < 0.0f) startSecs = 0.0f;
    m_TimeRemaining = startSecs;

    Game* game = Game::GetInstance();
    bool arcadeOrMP = game && (game->gameMode == 2 || IsMultiplayer());
    if (arcadeOrMP) {
        m_TimeRemaining = ARCADE_START_TIME;

        // Binary @ 0x001621ac: first-boot save-slot seed.
        if (game->pSaveData &&
            game->pSaveData->m_TimeRemainingSave == 0.0f &&
            game->mainScreen && game->mainScreen->GetCameraTransition() < 0.0f) {
            game->pSaveData->m_TimeRemainingSave = 60.9f;   // DAT_001621ec
        }
    } else {
        // Binary @ 0x001624ec: sentinel write for non-timed modes.
        if (game && game->pSaveData) {
            game->pSaveData->m_TimeRemainingSave = -1.0f;
        }
    }
    m_TickFrame = 0.0f;
    m_DrawColour = Colour(255, 255, 255, 255);
}

// ASM-verified: 2026-05-03 binary @ 0x001629xx (re-analyst)
void TimeControl::Skip() {
    // Binary @ 0x001629xx: restore from save.
    Game* game = Game::GetInstance();
    if (game && game->pSaveData) {
        m_TimeRemaining = game->pSaveData->m_TimeRemainingSave;
    }
    m_TickFrame = 0.0f;
}

void TimeControl::CountDown(float startSeconds) {
    // 0x001620f0
    m_CountdownStart = startSeconds;
}

float TimeControl::GetCountDown() const {
    // 0x00162134
    Game* game = Game::GetInstance();
    if (!game) return m_CountdownStart;
    if (game->gameMode != 2 /* Arcade */ && !IsMultiplayer())
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

    // count-up mode when m_CountdownStart <= 0. binary @ 0x001624a4 count-up branch.
    if (m_CountdownStart <= 0.0f) {
        m_TimeRemaining += dt;
        return;
    }

    m_TimeRemaining -= dt;

    // Capture colour before flash mutation for tick-tock gate.
    uint8_t entryColourR = m_DrawColour.r;

    // Colour tint bands as time runs low.
    // Binary: boolean alternation ((int)(t*N)) & 1 ? red : white.
    // Thresholds: 3/6/11.
    // binary @ 0x001624a4 flash section.
    float t = m_TimeRemaining;
    Colour tint(255, 255, 255, 255);
    static const Colour RED_TINT(255, 100, 100, 255);
    if (t < 3.0f) {
        // 8 Hz: ((int)(t*8.0)) & 1
        tint = (((int)(t * 8.0f)) & 1) ? RED_TINT : Colour(255, 255, 255, 255);
    } else if (t < 6.0f) {
        // 4 Hz: ((int)(t*4.0)) & 1
        tint = (((int)(t * 4.0f)) & 1) ? RED_TINT : Colour(255, 255, 255, 255);
    } else if (t < 11.0f) {
        // 2 Hz: ((int)(t+t)) & 1 = ((int)(t*2.0)) & 1
        tint = (((int)(t * 2.0f)) & 1) ? RED_TINT : Colour(255, 255, 255, 255);
    }
    m_DrawColour = tint;

    // Binary @ 0x00162732: tick-tock when colour band flipped this frame.
    if (m_TimeRemaining > 0.0f && m_TimeRemaining < 11.0f &&
        m_DrawColour.r != entryColourR) {
        static uint8_t s_TickTockToggle = 1;   // GOT byte at 0x001f3d80; first call -> "Time-tick"
        s_TickTockToggle ^= 1;
        const char* name = s_TickTockToggle ? "Time-tick" : "Time-tock";
        if (game->pGameSound) {
            // TODO: pass SFXDelegate when delegate API is ported
            game->pGameSound->SFXPlay(name, 1.0f, 1.0f);
        }
    }

    // Binary @ 0x00162818: persist for resume after suspend.
    if (game->pSaveData) {
        game->pSaveData->m_TimeRemainingSave = m_TimeRemaining;
    }

    // GameOver trigger: 0x001625be
    if (m_TimeRemaining < 0.5f) {
        FN::GameOver(-1, -1.0f, -1);
        m_TimeRemaining = 0.0f;    // DAT_001627a0
        // Reset combo on Arcade timeout -- binary @ 0x001625dc (g_ComboCount = 0)
        // and adjacent last-slasher write (0xFFFFFFFF = -1 sentinel).
        g_ComboCount  = 0;
        g_LastSlasher = -1;
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

    // Binary @ 0x00162a..: tick-tock UV quad branch. Dead code in shipped binary; m_SecondaryTex never assigned. Skipped.
}
