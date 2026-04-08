#include "core/SystemManager.h"
#include <SDL.h>
#include <algorithm>

namespace Mortar {

SystemManager::SystemManager()
    : m_bRunning(1)
    , m_LastFrameTime(0x3C)
    , m_AvgFPS(0x3C)
    , m_MinFPS(0x3C)
    , m_MaxFPS(0x3C)
    , m_RingMaxIdx(0)
    , m_RingWriteIdx(0)
    , m_QuitState(3)
    , m_field50(0)
{
    for (int i = 0; i < 30; i++) {
        m_FrameTimeRing[i] = 0;
    }
}

// Matches 0x0018b024: m_field50=DAT_0018b078(=0), m_bRunning=1,
// then records clock() into a Bada clock-calibration struct (port-skip),
// then _RetrieveDeviceID (port-skip: Bada device ID).
void SystemManager::Init() {
    m_field50 = 0;  // DAT_0018b078 = 0x00000000
    m_bRunning = 1;
    // Port specific: Bada clock calibration and _RetrieveDeviceID omitted
}

bool SystemManager::Update(float* dt) {
    // Compute dt from SDL ticks (port replacement for constant dt in original)
    static Uint32 lastTicks = 0;
    Uint32 now = SDL_GetTicks();
    if (lastTicks == 0) {
        lastTicks = now;
    }
    Uint32 elapsed = now - lastTicks;
    lastTicks = now;

    // Output dt in seconds
    if (dt) {
        *dt = elapsed / 1000.0f;
    }

    // Convert elapsed to FPS for ring buffer (guard against zero)
    int16_t fps = (elapsed > 0) ? static_cast<int16_t>(1000 / elapsed) : 60;
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

} // namespace Mortar
