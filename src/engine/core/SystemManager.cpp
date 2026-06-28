#include "core/SystemManager.h"
#include "core/MortarGame.h"
#include <algorithm>

SystemManager::SystemManager()
    : m_bRunning(1)
    , m_LastFrameTime(0x3C)
    , m_AvgFPS(0x3C)
    , m_MinFPS(0x3C)
    , m_MaxFPS(0x3C)
    , m_RingMaxIdx(0)
    , m_RingWriteIdx(0)
    , m_QuitState(3)
    , m_reserved50(0.0f)
{
    for (int i = 0; i < 30; i++) {
        m_FrameTimeRing[i] = 0;
    }
}

// Matches 0x0018b024: m_reserved50=DAT_0018b078(=0), m_bRunning=1,
// then records clock() into a Bada clock-calibration struct (port-skip),
// then _RetrieveDeviceID (port-skip: Bada device ID).
void SystemManager::Init() {
    m_reserved50 = 0.0f;  // DAT_0018b078 = 0x00000000 (float literal)
    m_bRunning = 1;
    // Port specific: Bada clock calibration and _RetrieveDeviceID omitted
}

bool SystemManager::Update(float* dt) {
    // Original (0x0018ade0): outputs FIXED dt = DAT_0018ae84 = 1/60 ≈ 0.01667
    // Original hardcodes m_LastFrameTime = 59 (0x3b)
    // All game logic (lerps, physics) is tuned for this fixed timestep
    static const float FIXED_DT = 1.0f / 60.0f;  // DAT_0018ae84 = 0x3C888889

    if (dt) {
        *dt = FIXED_DT;
    }

    int16_t fps = 59;  // original hardcodes 0x3b
    m_LastFrameTime = fps;

    // Write to ring buffer
    m_FrameTimeRing[m_RingWriteIdx] = fps;
    m_RingWriteIdx++;
    if (m_RingWriteIdx >= 30) {
        m_RingWriteIdx = 0;
    }
    if (m_RingMaxIdx < 29) {
        m_RingMaxIdx++;
    }

    // Scan ring buffer for min, max, average
    int16_t minFps = m_FrameTimeRing[0];
    int16_t maxFps = m_FrameTimeRing[0];
    int32_t sum = 0;
    int count = m_RingMaxIdx + 1;
    for (int i = 0; i < count; i++) {
        int16_t val = m_FrameTimeRing[i];
        if (val < minFps) minFps = val;
        if (val > maxFps) maxFps = val;
        sum += val;
    }
    m_MinFPS = minFps;
    m_MaxFPS = maxFps;
    m_AvgFPS = static_cast<int16_t>(sum / count);

    return m_bRunning != 0;
}

void SystemManager::QuitGame() {
    m_bRunning = 0;
}

void SystemManager::RequestQuit() {
    m_QuitState = 2;
}

// Global byte read by IsStartupTexturePortrait; also written by GetStartupTexture (need-care).
bool isStartupTexturePortrait = false;

// ASM-spec v1.6.1 GetVersionMajor @0x11f440: loads theGame->m_versionMajor (+0xA8)
int GetVersionMajor() {
    return Mortar::MortarGame::GetInstance()->m_versionMajor;
}

// ASM-spec v1.6.1 GetVersionMinor @0x11f460: loads theGame->m_versionMinor (+0xAC)
int GetVersionMinor() {
    return Mortar::MortarGame::GetInstance()->m_versionMinor;
}

// ASM-spec v1.6.1 GetVersionPatch @0x11f480: loads theGame->m_versionPatch (+0xB0)
int GetVersionPatch() {
    return Mortar::MortarGame::GetInstance()->m_versionPatch;
}

// ASM-spec v1.6.1 GetFormattedVersionString @0x11f3e0: returns ptr to theGame->m_formattedVersion (+0x44)
const char* GetFormattedVersionString() {
    return Mortar::MortarGame::GetInstance()->m_formattedVersion;
}

// ASM-spec v1.6.1 IsStartupTexturePortrait @0x11f4a8: reads global isStartupTexturePortrait byte
bool IsStartupTexturePortrait() {
    return isStartupTexturePortrait;
}
