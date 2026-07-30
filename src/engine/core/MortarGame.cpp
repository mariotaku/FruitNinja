#include "core/MortarGame.h"
#include "core/SystemManager.h"
#include "asset/Texture.h"
#include "Game.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace Mortar {

MortarGame* MortarGame::s_instance = nullptr;

// v1.6.1 Mortar::MortarGame::MortarGame @0x0022e0c0 — stores &vtable+8, byte-zeroes
// m_versionString / m_formattedVersion / m_hardwareString, clears the version ints,
// m_bFastHardware, m_licensedState and m_StartupTexture, then
// `if (SelfVersion()) SetVersion(SelfVersion())`.
MortarGame::MortarGame() {
    memset(m_versionString, 0, sizeof(m_versionString));
    memset(m_formattedVersion, 0, sizeof(m_formattedVersion));
    // DIFFERS: the binary's ctor does NOT touch m_languageString (+0x84) — it relies on
    //          theGame living in zero-initialised BSS. The port heap-allocates Game via
    //          `new Game()`, so the buffer would hold garbage and Game::Init's
    //          strcmp(m_languageString, "fr"/"de"/...) language probe would read it.
    //          Port zeroes it; costs one memset, removes the UB.
    memset(m_languageString, 0, sizeof(m_languageString));
    m_versionCombined = 0;
    m_versionMajor = 0;
    m_versionMinor = 0;
    m_versionPatch = 0;
    memset(m_hardwareString, 0, sizeof(m_hardwareString));
    m_bFastHardware = false;
    m_licensedState = 0;
    // m_StartupTexture is null-constructed by SmartPtr's default ctor (binary: `str #0, [this,#0xfc]`).

    SetVersion(SelfVersion());

    // Port specific: the binary reaches the instance through the `theGame` GOT global,
    // which ReturnsAnInstanceOfThisMortarGame @0x11f6ac writes. The port publishes it here.
    s_instance = this;
}

// slots 0/1 v1.6.1 Mortar::MortarGame::~MortarGame @0x0022e070 (D1) / @0x0022e0a4 (D0)
// Binary D1: restores the vptr to &vtable+8, then SmartPtr<Texture>::Clear(&m_StartupTexture).
MortarGame::~MortarGame() {
    m_StartupTexture.SetNull();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

// --- Vtable slot implementations --- v1.6.1 Mortar::MortarGame::vtable @0x002cfa88, 34 slots ---

// slot 2 v1.6.1 Mortar::MortarGame::GetHardwareString @0x0011fb80
// DIFFERS: the binary returns `this+0x04` (m_versionString), NOT the +0xB4 buffer that
//          SetHardware @0x0022e038 writes. That looks like an original-source bug; the
//          port returns m_hardwareString so DeviceQuery's GetHardwareString() reports the
//          hardware name instead of the version string.
const char* MortarGame::GetHardwareString() { return m_hardwareString; }

// slot 3 v1.6.1 Mortar::MortarGame::IsFastHardware @0x0011fb88
bool MortarGame::IsFastHardware() { return m_bFastHardware; }

// slot 4 v1.6.1 Mortar::MortarGame::RenderAtHalfFrames @0x0022de74 — base no-op; Game overrides
void MortarGame::RenderAtHalfFrames(const char* hwName, const char* model) {
    (void)hwName; (void)model;
}

// slot 5 v1.6.1 Mortar::MortarGame::GetHighResolutionScale @0x0022e194
float MortarGame::GetHighResolutionScale() { return 1.0f; }

// slot 6
// Defunct: OpenFeint — no-op stub; v1.6.1 Mortar::MortarGame::GetOpenFeintProductKey @ 0x0022e19c
const char* MortarGame::GetOpenFeintProductKey() { return ""; }

// slot 7
// Defunct: OpenFeint — no-op stub; v1.6.1 Mortar::MortarGame::GetOpenFeintSecret @ 0x0022e1a4
const char* MortarGame::GetOpenFeintSecret() { return ""; }

// slot 8
// Defunct: OpenFeint — no-op stub; v1.6.1 Mortar::MortarGame::GetOpenDisplayName @ 0x0022e1ac
const char* MortarGame::GetOpenDisplayName() { return ""; }

// slot 9
// Defunct: Playhaven — no-op stub; v1.6.1 Mortar::MortarGame::GetPlayhavenToken @ 0x0022e1b4
const char* MortarGame::GetPlayhavenToken() { return ""; }

// slot 10 v1.6.1 Mortar::MortarGame::GetHurtzRate @0x0011fb90 — constant 60.0 (double literal
// 0x404e000000000000 at 0x0011fb98); no subclass override exists in v1.6.1.
double MortarGame::GetHurtzRate(const char* hwName, const char* model) {
    (void)hwName; (void)model;
    return 60.0;
}

// slot 11 v1.6.1 Mortar::MortarGame::GetCacheDataArchive @0x0011fba0
void* MortarGame::GetCacheDataArchive() { return 0; }

// slot 12 v1.6.1 Mortar::MortarGame::CreateFileSystems @0x0022e1bc — base no-op; Game overrides
void MortarGame::CreateFileSystems(const char* a, const char* b) {
    (void)a; (void)b;
}

// slot 13 v1.6.1 Mortar::MortarGame::TellGameToStart @0x0022de8c — base no-op; Game overrides
void MortarGame::TellGameToStart(int multiplayer) { (void)multiplayer; }

// slot 14 v1.6.1 Mortar::MortarGame::Update @0x0022de80
void MortarGame::Update(float dt) { (void)dt; }

// slot 15 v1.6.1 Mortar::MortarGame::Draw @0x0022de7c
void MortarGame::Draw(float dt) { (void)dt; }

// slot 16 v1.6.1 Mortar::MortarGame::Init @0x0022de84
void MortarGame::Init(int argc, const char** argv) { (void)argc; (void)argv; }

// slot 17 v1.6.1 Mortar::MortarGame::End @0x0022de88 — base returns this
MortarGame* MortarGame::End() { return this; }

// slot 18 v1.6.1 Mortar::MortarGame::Paused @0x0022de90
void MortarGame::Paused() {}

// slot 19 v1.6.1 Mortar::MortarGame::UnPaused @0x0022de94
void MortarGame::UnPaused() {}

// slot 20 v1.6.1 Mortar::MortarGame::SelfVersion @0x0022de9c — base "1.0.0"; Game overrides to "1.6.1"
const char* MortarGame::SelfVersion() { return "1.0.0"; }

// slot 21 v1.6.1 Mortar::MortarGame::SaveOnExit @0x0022de98
void MortarGame::SaveOnExit() {}

// slot 22 v1.6.1 Mortar::MortarGame::SetAppLicensed @0x0022deb8 — can't downgrade licensed(1) to unlicensed(2)
void MortarGame::SetAppLicensed(bool licensed) {
    if (licensed) {
        m_licensedState = 1;
    } else if (m_licensedState != 1) {
        m_licensedState = 2;
    }
}

// slot 23 v1.6.1 Mortar::MortarGame::GetAppLicensedState @0x0022dedc (ldr r0,[r0,#0xf8]; bx lr)
int MortarGame::GetAppLicensedState() { return m_licensedState; }

// slot 24 v1.6.1 Mortar::MortarGame::SetLanguage @0x0022dee4 (add r0,#0x84; b strcpy) — Game does NOT override
void MortarGame::SetLanguage(const char* lang) {
    if (lang) {
        strcpy(m_languageString, lang);
    }
}

// slot 25 v1.6.1 Mortar::MortarGame::DefaultOrientation @0x0011fba8 (mov r0,#3; bx lr)
int MortarGame::DefaultOrientation() { return 3; }

// slot 26 v1.6.1 Mortar::MortarGame::AllowOrientationChange @0x0011fbb0
bool MortarGame::AllowOrientationChange(int orientation) { (void)orientation; return false; }

// slot 27 v1.6.1 Mortar::MortarGame::OrientationDidChange @0x0011fbb8 — no-op (single bx lr)
void MortarGame::OrientationDidChange(int orientation) { (void)orientation; }

// slot 28 v1.6.1 Mortar::MortarGame::SetStartupTexture @0x001208dc
// (add r0,#0xfc; b SmartPtr<Texture>::operator=) — tail-call assignment into m_StartupTexture.
void MortarGame::SetStartupTexture(SmartPtr<Texture> tex) {
    m_StartupTexture = tex;
}

// slot 29 v1.6.1 Mortar::MortarGame::GetStartupTexture @0x001208f4
// (add r1,#0xfc; bl SmartPtr<Texture>::SmartPtr(const&)) — returns a copy, so the caller
// holds its own reference.
SmartPtr<Texture> MortarGame::GetStartupTexture() {
    return m_StartupTexture;
}

// Defunct: on-screen keyboard — no-op stub; v1.6.1 Mortar::MortarGame::KeyboardProcessCharacter @ 0x0011fbbc
void MortarGame::KeyboardProcessCharacter(int ch) { (void)ch; }

// Defunct: on-screen keyboard — no-op stub; v1.6.1 Mortar::MortarGame::KeyboardProcessDelete @ 0x0011fbc0
void MortarGame::KeyboardProcessDelete() {}

// Defunct: on-screen keyboard — no-op stub; v1.6.1 Mortar::MortarGame::KeyboardProcessDone @ 0x0011fbc4
// Binary body is `mov r0,#0; bx lr`. Return type inferred as bool from the identical codegen
// of AllowOrientationChange; it is not encoded in the mangled name, so symbol-diff is unaffected.
bool MortarGame::KeyboardProcessDone() { return false; }

// Defunct: on-screen keyboard — no-op stub; v1.6.1 Mortar::MortarGame::KeyboardProcessCancelled @ 0x0011fbcc
void MortarGame::KeyboardProcessCancelled() {}

// --- Non-virtual methods ---

// Matches v1.6.1 TellGameToQuit @0x0022e054
void MortarGame::TellGameToQuit() {
    SystemManager::GetInstance().QuitGame();
}

// Matches v1.6.1 MortarGame::SetVersion @0x0022deec — parses "M.m.p", fills version fields, sets hardware default
// DIFFERS: binary multiplies single-digit version sections by 10
//          (e.g. "1.5.1" -> minor=50, patch=10, combined=15010);
//          port keeps direct semver (combined=10501). No callers read m_versionCombined.
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

// Matches v1.6.1 MortarGame::SetHardware @0x0022e038
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
