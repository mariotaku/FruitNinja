// Analysed: 2026-05-04T00:00
#include "core/MortarGame.h"
#include "core/SystemManager.h"
#include "Game.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace Mortar {

MortarGame* MortarGame::s_instance = nullptr;

// Matches 0x0018ab6c — zeros all fields, calls SetVersion(SelfVersion())
MortarGame::MortarGame() {
    // Zero +0x04..+0xF8 (everything after vtable)
    memset(m_versionString, 0, sizeof(m_versionString));
    memset(m_formattedVersion, 0, sizeof(m_formattedVersion));
    memset(m_languageString, 0, sizeof(m_languageString));
    m_versionCombined = 0;
    m_versionMajor = 0;
    m_versionMinor = 0;
    m_versionPatch = 0;
    memset(m_hardwareString, 0, sizeof(m_hardwareString));
    m_bFastHardware = false;
    m_licensedState = 0;

    SetVersion(SelfVersion());

    s_instance = this;
}

MortarGame::~MortarGame() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

// --- Vtable slot implementations (binary @ 0x001eae58, 24 slots) ---

// slot 0 @ 0x0010d9d0
const char* MortarGame::GetHardwareString() { return m_hardwareString; }

// slot 1 @ 0x0010d9d4
bool MortarGame::IsFastHardware() { return m_bFastHardware; }

// slot 2 @ 0x0018aa14 — base no-op; Game overrides to check slow-hardware list
void MortarGame::RenderAtHalfFrames(const char* hwName, const char* model) {
    (void)hwName; (void)model;
}

// slot 3 @ 0x0018ac80
float MortarGame::GetHighResolutionScale() { return 1.0f; }

// slot 4 @ 0x0018ac88
// Defunct: OpenFeint — no-op stub; v1.6.1 binary @ 0x0018ac88
const char* MortarGame::GetOpenFeintProductKey() { return ""; }

// slot 5 @ 0x0018ac8c
// Defunct: OpenFeint — no-op stub; v1.6.1 binary @ 0x0018ac8c
const char* MortarGame::GetOpenFeintSecret() { return ""; }

// slot 6 @ 0x0018ac90
// Defunct: OpenFeint — no-op stub; v1.6.1 binary @ 0x0018ac90
const char* MortarGame::GetOpenDisplayName() { return ""; }

// slot 7 @ 0x0018ac94
// Defunct: Playhaven — no-op stub; v1.6.1 binary @ 0x0018ac94
const char* MortarGame::GetPlayhavenToken() { return ""; }

// slot 8 @ 0x0010d9dc
void* MortarGame::GetCacheDataArchive() { return 0; }

// slot 9 @ 0x0018ac98 — base no-op; Game overrides to setup FileSystem_Direct
void MortarGame::CreateFileSystems(const char* a, const char* b) {
    (void)a; (void)b;
}

// slot 10 @ 0x0018aa28 — base no-op; Game overrides to set HUD+WaveManager
void MortarGame::TellGameToStart(int multiplayer) { (void)multiplayer; }

// slot 11 @ 0x0018aa1c
void MortarGame::Update(float dt) { (void)dt; }

// slot 12 @ 0x0018aa18
void MortarGame::Draw(float dt) { (void)dt; }

// slot 13 @ 0x0018aa20
void MortarGame::Init(int argc, const char** argv) { (void)argc; (void)argv; }

// slot 14 @ 0x0018aa24 — base returns this
MortarGame* MortarGame::End() { return this; }

// slot 15 @ 0x0018aa2c
void MortarGame::Paused() {}

// slot 16 @ 0x0018aa30
void MortarGame::UnPaused() {}

// slot 17 @ 0x0018aa38 — base returns "1.0.0"; Game overrides to "1.5.1"
const char* MortarGame::SelfVersion() { return "1.0.0"; }

// slot 18 @ 0x0018aa34
void MortarGame::SaveOnExit() {}

// slot 19 @ 0x0018aa50 — can't downgrade from licensed(1) to unlicensed(2)
void MortarGame::SetAppLicensed(bool licensed) {
    if (licensed) {
        m_licensedState = 1;
    } else if (m_licensedState != 1) {
        m_licensedState = 2;
    }
}

// slot 20 @ 0x0018aa68
int MortarGame::GetAppLicensedState() { return m_licensedState; }

// slot 21 @ 0x0018aa70 — Game does NOT override; base writes m_languageString
void MortarGame::SetLanguage(const char* lang) {
    if (lang) {
        strcpy(m_languageString, lang);
    }
}

// slot 22 @ 0x0018ac9c
bool MortarGame::AllowOrientationChange(int orientation) { (void)orientation; return false; }

// slot 23 @ 0x0010d9e0 — no-op (single bx lr in binary)
void MortarGame::OrientationDidChange(int orientation) { (void)orientation; }

// --- Non-virtual methods ---

// Matches 0x0018ac64
void MortarGame::TellGameToQuit() {
    SystemManager::GetInstance().QuitGame();
}

// Matches 0x0018aa90 — parses "M.m.p", fills version fields, sets hardware default
// DIFFERS: binary multiplies single-digit version sections by 10
//          (e.g. "1.5.1" -> minor=50, patch=10, combined=15010);
//          port keeps direct semver (combined=10501). No callers read m_versionCombined.
//          binary @ 0x0018aa90
void MortarGame::SetVersion(const char* version) {
    if (!version) return;

    strcpy(m_versionString, version);

    m_versionMajor = 1;
    m_versionMinor = 0;
    m_versionPatch = 0;

    m_versionMajor = atoi(version);

    const char* p = version;
    while (*p && *p != '.') p++;
    if (*p != 0) {
        p++;
        m_versionMinor = atoi(p);
        const char* p2 = p + 1;
        while (*p2 && *p2 != '.') p2++;
        if (*p2 != 0) {
            m_versionPatch = atoi(p2 + 1);
        }
    }

    snprintf(m_formattedVersion, sizeof(m_formattedVersion),
             "%04i.%02i.%02i", m_versionMajor, m_versionMinor, m_versionPatch);
    m_bFastHardware = false;
    m_versionCombined = m_versionMajor * 10000 + m_versionMinor * 100 + m_versionPatch;
    snprintf(m_hardwareString, sizeof(m_hardwareString), "BADA");
}

// Matches 0x0018aa7c
void MortarGame::SetHardware(const char* hw, bool fast) {
    if (hw) {
        strcpy(m_hardwareString, hw);
    }
    m_bFastHardware = fast;
}

} // namespace Mortar

// ASM-spec v1.6.1 EmptyFunction @0x1c3670: shared no-op used as default callback body.
void EmptyFunction() {}

// ASM-spec v1.6.1 ReturnsAnInstanceOfThisMortarGame @0x11f6ac
// Binary: lazy singleton — if s_instance is null, allocates Game (0x308 bytes) and constructs it.
// Port: Game ctor sets MortarGame::s_instance = this; new Game() here is safe when called early.
Mortar::MortarGame* ReturnsAnInstanceOfThisMortarGame() {
    if (!Mortar::MortarGame::GetInstance()) {
        new Game();  // MortarGame ctor sets s_instance = this
    }
    return Mortar::MortarGame::GetInstance();
}
