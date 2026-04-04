#include "core/MortarGame.h"
#include "core/SystemManager.h"
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

// --- Vtable stubs ---

const char* MortarGame::GetHardwareString() { return m_versionString; }
bool MortarGame::IsFastHardware() { return m_bFastHardware; }
void MortarGame::RenderAtHalfFrames() {}
float MortarGame::GetHighResolutionScale() { return 1.0f; }
const char* MortarGame::GetOpenFeintProductKey() { return ""; }
const char* MortarGame::GetOpenFeintSecret() { return ""; }
const char* MortarGame::GetOpenDisplayName() { return ""; }
const char* MortarGame::GetPlayhavenToken() { return ""; }
const char* MortarGame::GetCacheDataArchive() { return nullptr; }
void MortarGame::CreateFileSystems() {}
void MortarGame::TellGameToStart() {}
void MortarGame::Update(float dt) { (void)dt; }
void MortarGame::Draw(float dt) { (void)dt; }
void MortarGame::Init(int argc, char** argv) { (void)argc; (void)argv; }
void MortarGame::End() {}
void MortarGame::Paused() {}

// --- Non-virtual methods ---

// Matches 0x0018ac64
void MortarGame::TellGameToQuit() {
    SystemManager::GetInstance().QuitGame();
}

// Base returns "1.0.0"; Game overrides to "1.5.1"
const char* MortarGame::SelfVersion() {
    return "1.0.0";
}

// Matches 0x0018aa90 — parses "M.m.p", fills version fields, sets hardware default
void MortarGame::SetVersion(const char* version) {
    if (!version) return;

    // Copy raw version string
    strncpy(m_versionString, version, sizeof(m_versionString) - 1);
    m_versionString[sizeof(m_versionString) - 1] = '\0';

    // Parse M.m.p
    m_versionMajor = 0;
    m_versionMinor = 0;
    m_versionPatch = 0;

    int parts[3] = {0, 0, 0};
    int idx = 0;
    const char* p = version;
    while (*p && idx < 3) {
        if (*p == '.') {
            idx++;
        } else if (*p >= '0' && *p <= '9') {
            parts[idx] = parts[idx] * 10 + (*p - '0');
        }
        p++;
    }
    m_versionMajor = parts[0];
    m_versionMinor = parts[1];
    m_versionPatch = parts[2];
    m_versionCombined = m_versionMajor * 10000 + m_versionMinor * 100 + m_versionPatch;

    // Format "%04i.%02i.%02i"
    snprintf(m_formattedVersion, sizeof(m_formattedVersion),
             "%04i.%02i.%02i", m_versionMajor, m_versionMinor, m_versionPatch);

    // Set default hardware string
    snprintf(m_hardwareString, sizeof(m_hardwareString), "BADA");
}

// Matches 0x0018aa70
void MortarGame::SetLanguage(const char* lang) {
    if (lang) {
        strncpy(m_languageString, lang, sizeof(m_languageString) - 1);
        m_languageString[sizeof(m_languageString) - 1] = '\0';
    }
}

// Matches 0x0018aa7c
void MortarGame::SetHardware(const char* hw, bool fast) {
    if (hw) {
        strncpy(m_hardwareString, hw, sizeof(m_hardwareString) - 1);
        m_hardwareString[sizeof(m_hardwareString) - 1] = '\0';
    }
    m_bFastHardware = fast;
}

// Matches 0x0018aa50 — can't downgrade from licensed(1) to unlicensed(2)
void MortarGame::SetAppLicensed(bool licensed) {
    if (licensed) {
        m_licensedState = 1;
    } else if (m_licensedState != 1) {
        m_licensedState = 2;
    }
}

// Matches 0x0018aa68
int MortarGame::GetAppLicensedState() const {
    return m_licensedState;
}

void MortarGame::SaveOnExit() {}
void MortarGame::UnPaused() {}
bool MortarGame::AllowOrientationChange(int orientation) { (void)orientation; return false; }

} // namespace Mortar
