#include "core/SystemManager.h"
#include "core/MortarGame.h"
#include "render/DisplayManager.h"
#include "game/GameWork.h"
#include "engine/math/Random.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#ifndef __bada__
#include <chrono>
#endif
// Game.h pulled in for LowResBackgrounds() which reads Game::m_bSlowHardware (+0x100).
#include "Game.h"

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

// v1.6.1 SystemManager::Init @0x0022e544: m_reserved50=0, m_bRunning=1,
// seeds Math::g_Random with a per-launch-varying seed (see DIFFERS below),
// then _RetrieveDeviceID.
void SystemManager::Init() {
    m_reserved50 = 0.0f;
    m_bRunning = 1;
    // ASM-spec v1.6.1 SystemManager::Init @0x0022e544: seeds Math::g_random state word with clock()
#ifdef __bada__
    uint32_t seed = (uint32_t)clock();   // v1.6.1 faithful: Bada clock() = device uptime, varies per boot
#else
    uint32_t seed;
    // Port specific: test-only override. With no override, production/dev
    // builds fall through to the wall-clock-derived seed below exactly as
    // before -- this branch changes nothing for a normal launch. TestHarness
    // (tests/test_harness.h) sets FN_RNG_SEED before game.init() so every
    // TestHarness-based test gets a reproducible Math::g_Random stream
    // instead of asserting on a wall-clock-seeded one. Same shape as
    // FN_SAVE_DIR_OVERRIDE in src/platform/SaveDirSDL.cpp.
    const char* seed_override = std::getenv("FN_RNG_SEED");
    if (seed_override && seed_override[0] != '\0') {
        seed = (uint32_t)std::strtoul(seed_override, NULL, 10);
    } else {
    // DIFFERS: original = clock() (v1.6.1 SystemManager::Init @0x0022e544); Bada clock() is
    // device-uptime and varies per boot, but on Windows/glibc clock() measures CPU time since
    // process start, which is a near-constant few ms at this early init call -> the port got the
    // same seed (and thus the same Math::g_Random.Rand32(2) coin flip, e.g. shop fruit spin
    // direction in Fruit::RotateFacingUp) on every launch. Port mixes wall-clock seconds
    // (time(NULL)) with a high_resolution_clock tick count so the seed varies both across
    // separate-second launches (1s resolution from time()) and rapid successive launches within
    // the same second (chrono's higher-resolution counter) so the global RNG varies per launch as
    // the binary intends. Portable to Windows/Linux/emscripten (no SDL/windows.h needed).
    seed = (uint32_t)(std::time(0) ^
        (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }
#endif
    // Consequence of the above: without FN_RNG_SEED the global stream
    // Math::g_Random is seeded from a per-launch-varying value, so a test that
    // boots the real engine (not via TestHarness / FN_RNG_SEED) cannot assert
    // on a g_Random-derived value and stay green -- not on a spawn sequence, a
    // draw count, a coin flip, or any subsystem RNG reseeded from it (e.g.
    // WaveManager::Reset does m_Random.Seed(Math::g_Random.Rand32(0)), which
    // just chains from whatever g_Random itself was seeded with). That
    // wall-clock variance is deliberate and matches the binary -- RNG fidelity
    // in this port is established by STATIC RE against the binary -- matching
    // draw counts, argument ranges, and call order at each Rand call site --
    // NOT by asserting on drawn values. FN_RNG_SEED (TestHarness::Init())
    // exists only to make repeated test RUNS reproducible with each other, not
    // to assert a specific value is "correct".
    Math::SeedGlobalRng(seed);
    // Defunct/no-op: _RetrieveDeviceID (v1.6.1 @0x0022e3be) confirmed `return 0;` in binary -- correctly omitted.
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

// ASM-spec v1.6.1 CombosEnabled @0x119fd0: returns game_work.gameMode != 1.
// Combos are off when gameMode == 1 (Zen mode).
bool CombosEnabled() {
    return game_work.gameMode != 1;
}

// Event2<int,int> score-notification signal. Binary @ 0x2d92e4, ctor'd by the
// Game.cpp keyed global ctor. Fired on score change; TimeSinkModifier is the
// only subscriber (ApplyModifier @ 0x0014dc88 / RemoveModifier @ 0x0014db60).
static Mortar::Event2<int,int> s_scoreNotification;

// ASM-spec v1.6.1 GetScoreNotification @0x119fb0: returns &s_scoreNotification (Event2<int,int>, ctor'd by Game.cpp global ctor)
Mortar::Event2<int,int>& GetScoreNotification() {
    return s_scoreNotification;
}

// ASM-spec v1.6.1 GetVersionFromString @0x152e78: parse "M.m.p" -> packed int.
// DIFFERS: binary scales single-digit minor/patch sections x10;
// port uses direct semver (no x10) to match MortarGame::SetVersion convention.
int GetVersionFromString(const char* s) {
    if (!s) return 10000;
    int major = atoi(s);
    const char* p = s;
    while (*p && *p != '.') ++p;
    if (!*p) return major * 10000;
    int minor = atoi(p + 1);
    const char* q = p + 1;
    int k = 0;
    while (q[k] && q[k] != '.') ++k;
    int patch = 0;
    if (q[k] == '.') {
        patch = atoi(q + k + 1);
    }
    return major * 10000 + minor * 100 + patch;
}

// ASM-spec v1.6.1 GetApparentWindowWidth @0x11bb44:
// reads DisplayManager aspect ratio; returns 480 when ar <= 1.5, else ar * 320.
float GetApparentWindowWidth() {
    float ar = Mortar::DisplayManager::GetInstance().GetAspectWvH();
    if (ar <= 1.5f) return 480.0f;
    return ar * 320.0f;
}

// ASM-spec v1.6.1 GetApparentWindowHeight @0x11baf4:
// reads DisplayManager aspect ratio; returns 320 when ar > 1.5, else GetAspectHvW * 480.
float GetApparentWindowHeight() {
    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
    float ar = dm.GetAspectWvH();
    if (1.5f < ar) return 320.0f;
    return dm.GetAspectHvW() * 480.0f;
}

// ASM-spec v1.6.1 GetVersionTotal @0x0011f420: loads theGame->m_versionCombined (+0xA4).
int GetVersionTotal() {
    return Mortar::MortarGame::GetInstance()->m_versionCombined;
}

// ASM-spec v1.6.1 LowResBackgrounds @0x0011f3c0: `ldrb r0,[theGame,#0x100]` -- the same byte
// Game::RenderAtHalfFrames @0x001207f0 sets for old-iOS-class hardware, i.e. m_bSlowHardware.
// Stays false on Bada ("BADA" never matches the iPhone-1G/3G/iPod-Touch device strings).
bool LowResBackgrounds() {
    return Game::GetInstance()->m_bSlowHardware != 0;
}
