#ifndef FN_MORTAR_GAME_H
#define FN_MORTAR_GAME_H

#include <cstdint>
#include <cstddef>
#include "util/SmartPtr.h"

namespace Mortar {

class Texture;

// Mortar::MortarGame -- engine-side application singleton (the binary's `theGame`).
//
// Usage/contract:
//   * Exactly one instance exists; the port's ctor publishes it via s_instance so
//     GetInstance() works (the binary reaches it through the `theGame` GOT global).
//     The binary's ctor does NOT do this -- it is a port-only convenience.
//   * The ctor calls the virtual-in-name-only SelfVersion() and feeds it to
//     SetVersion(), so a subclass's version string is NOT visible here: during base
//     construction the vptr is still MortarGame's. The binary behaves identically;
//     `Game` re-runs SetVersion("1.6.1") from its own Init().
//   * All the OpenFeint / Playhaven / keyboard / orientation slots are dead on the
//     v1.6.1 Bada SKU and exist only to keep the vtable shape intact.
//   * m_StartupTexture is owned here (not in the Game subclass) and is reached
//     through the SetStartupTexture/GetStartupTexture virtuals -- the free functions
//     ::GetStartupTexture() / ::ReleaseStartupTexture() (Game.h) dispatch through
//     those two slots exactly as the binary does.
//
// Layout: 0x100 / 256 bytes. The `Game` subclass adds its own fields from +0x100.
// Vtable: _ZTVN6Mortar10MortarGameE @0x002cfa88, 0x90 bytes = 2 header words + 34 slots.
// Slot 0/1 are the D1/D0 destructors (@0x0022e070 / @0x0022e0a4) -- the binary DOES
// have a virtual destructor here, and its ctor stores &vtable+8 into the vptr.
class MortarGame {
public:
    // +0x04: raw version string, e.g. "1.6.1"
    char m_versionString[64];

    // +0x44: snprintf'd "%04i.%02i.%02i" version
    char m_formattedVersion[64];

    // +0x84: language string, set by SetLanguage(strcpy)
    char m_languageString[32];

    // +0xA4: parsed version components
    int m_versionCombined;   // major*10000 + minor*100 + patch
    int m_versionMajor;
    int m_versionMinor;
    int m_versionPatch;

    // +0xB4: hardware string, written by SetHardware (default "BADA")
    char m_hardwareString[64];

    // +0xF4: fast hardware flag
    bool m_bFastHardware;

    // +0xF8: licensed state (0=unknown, 1=licensed, 2=unlicensed)
    int m_licensedState;

    // +0xFC: startup splash texture (HB_logo.tex). Zeroed by the ctor, Clear()ed by
    // the D1 destructor (v1.6.1 Mortar::MortarGame::~MortarGame @0x0022e070).
    SmartPtr<Texture> m_StartupTexture;

    MortarGame();

    // --- Vtable (34 slots) --- v1.6.1 _ZTVN6Mortar10MortarGameE @0x002cfa88 ---

    virtual ~MortarGame();                                  // slots 0/1 v1.6.1 Mortar::MortarGame::~MortarGame @0x0022e070 (D1) / @0x0022e0a4 (D0)

    virtual const char* GetHardwareString();                // slot 2  v1.6.1 Mortar::MortarGame::GetHardwareString @0x0011fb80
    virtual bool IsFastHardware();                          // slot 3  v1.6.1 Mortar::MortarGame::IsFastHardware @0x0011fb88
    virtual void RenderAtHalfFrames(const char* hwName, const char* model);  // slot 4  v1.6.1 Mortar::MortarGame::RenderAtHalfFrames @0x0022de74 base no-op
    virtual float GetHighResolutionScale();                 // slot 5  v1.6.1 Mortar::MortarGame::GetHighResolutionScale @0x0022e194 returns 1.0f
    virtual const char* GetOpenFeintProductKey();           // slot 6  v1.6.1 Mortar::MortarGame::GetOpenFeintProductKey @0x0022e19c Defunct
    virtual const char* GetOpenFeintSecret();               // slot 7  v1.6.1 Mortar::MortarGame::GetOpenFeintSecret @0x0022e1a4 Defunct
    virtual const char* GetOpenDisplayName();               // slot 8  v1.6.1 Mortar::MortarGame::GetOpenDisplayName @0x0022e1ac Defunct
    virtual const char* GetPlayhavenToken();                // slot 9  v1.6.1 Mortar::MortarGame::GetPlayhavenToken @0x0022e1b4 Defunct
    virtual double GetHurtzRate(const char* hwName, const char* model);      // slot 10 v1.6.1 Mortar::MortarGame::GetHurtzRate @0x0011fb90 returns 60.0 (vldr.64 d0)
    virtual void* GetCacheDataArchive();                    // slot 11 v1.6.1 Mortar::MortarGame::GetCacheDataArchive @0x0011fba0 returns nullptr
    virtual void CreateFileSystems(const char* a, const char* b);  // slot 12 v1.6.1 Mortar::MortarGame::CreateFileSystems @0x0022e1bc no-op
    virtual void TellGameToStart(int multiplayer);          // slot 13 v1.6.1 Mortar::MortarGame::TellGameToStart @0x0022de8c no-op
    virtual void Update(float dt);                          // slot 14 v1.6.1 Mortar::MortarGame::Update @0x0022de80 no-op
    virtual void Draw(float dt);                            // slot 15 v1.6.1 Mortar::MortarGame::Draw @0x0022de7c no-op
    virtual void Init(int argc, const char** argv);         // slot 16 v1.6.1 Mortar::MortarGame::Init @0x0022de84 no-op
    virtual MortarGame* End();                              // slot 17 v1.6.1 Mortar::MortarGame::End @0x0022de88 returns this
    virtual void Paused();                                  // slot 18 v1.6.1 Mortar::MortarGame::Paused @0x0022de90 no-op
    virtual void UnPaused();                                // slot 19 v1.6.1 Mortar::MortarGame::UnPaused @0x0022de94 no-op
    virtual const char* SelfVersion();                      // slot 20 v1.6.1 Mortar::MortarGame::SelfVersion @0x0022de9c returns "1.0.0"
    virtual void SaveOnExit();                              // slot 21 v1.6.1 Mortar::MortarGame::SaveOnExit @0x0022de98 no-op
    virtual void SetAppLicensed(bool licensed);             // slot 22 v1.6.1 Mortar::MortarGame::SetAppLicensed @0x0022deb8
    virtual int GetAppLicensedState();                      // slot 23 v1.6.1 Mortar::MortarGame::GetAppLicensedState @0x0022dedc (ldr r0,[r0,#0xf8])
    virtual void SetLanguage(const char* lang);             // slot 24 v1.6.1 Mortar::MortarGame::SetLanguage @0x0022dee4 (strcpy into this+0x84); Game does NOT override
    virtual int DefaultOrientation();                       // slot 25 v1.6.1 Mortar::MortarGame::DefaultOrientation @0x0011fba8 returns 3
    virtual bool AllowOrientationChange(int orientation);   // slot 26 v1.6.1 Mortar::MortarGame::AllowOrientationChange @0x0011fbb0 returns false
    virtual void OrientationDidChange(int orientation);     // slot 27 v1.6.1 Mortar::MortarGame::OrientationDidChange @0x0011fbb8 no-op
    virtual void SetStartupTexture(SmartPtr<Texture> tex);  // slot 28 v1.6.1 Mortar::MortarGame::SetStartupTexture @0x001208dc (vtable byte +0x70)
    virtual SmartPtr<Texture> GetStartupTexture();          // slot 29 v1.6.1 Mortar::MortarGame::GetStartupTexture @0x001208f4 (vtable byte +0x74)
    // On-screen keyboard slots: the v1.6.1 Bada SKU never routes IME callbacks here, and
    // all four bodies are `bx lr` / `mov r0,#0; bx lr` in the binary too. Stubbed bodies
    // carry the `Defunct:` markers (MortarGame.cpp).
    virtual void KeyboardProcessCharacter(int ch);          // slot 30 v1.6.1 Mortar::MortarGame::KeyboardProcessCharacter @0x0011fbbc no-op
    virtual void KeyboardProcessDelete();                   // slot 31 v1.6.1 Mortar::MortarGame::KeyboardProcessDelete @0x0011fbc0 no-op
    virtual bool KeyboardProcessDone();                     // slot 32 v1.6.1 Mortar::MortarGame::KeyboardProcessDone @0x0011fbc4 returns 0
    virtual void KeyboardProcessCancelled();                // slot 33 v1.6.1 Mortar::MortarGame::KeyboardProcessCancelled @0x0011fbcc no-op

    // --- Non-virtual methods ---

    // Matches v1.6.1 TellGameToQuit @0x0022e054
    void TellGameToQuit();

    // Matches v1.6.1 MortarGame::SetVersion @0x0022deec — parses "M.m.p" string, fills version fields
    // DIFFERS: binary single-digit minor/patch sections multiplied by 10
    //          (e.g. "1.5.1" -> minor=50, patch=10, combined=15010).
    //          Port keeps direct semver (combined=10501). No callers read m_versionCombined.
    void SetVersion(const char* version);

    // Matches v1.6.1 MortarGame::SetHardware @0x0022e038 — strcpy into this+0xB4, store bool at this+0xF4
    void SetHardware(const char* hw, bool fast);

    // Singleton access
    static MortarGame* GetInstance() { return s_instance; }

protected:
    static MortarGame* s_instance;
};

} // namespace Mortar

// Field-offset assertions for MortarGame (binary @ theGame, ARM32).
// Offsets are instance-relative (include the vtable pointer at +0x00).
// Guarded by __bada__ so they fire only on the cross-build / Bada toolchain
// where struct layout must match the binary exactly.
//
// sizeof == 0x100 is pinned by two independent binary reads, NOT by whatever makes
// the build pass: MortarGame::GetStartupTexture @0x001208f4 copy-constructs from
// `this+0xfc`, and Game::RenderAtHalfFrames @0x001207f0 writes its slow-hardware
// byte at Game+0x100 (i.e. the first subclass field starts right after +0xFC).
#ifdef __bada__
static_assert(offsetof(Mortar::MortarGame, m_versionString)    == 0x04, "MortarGame::m_versionString must be at +0x04");
static_assert(offsetof(Mortar::MortarGame, m_formattedVersion) == 0x44, "MortarGame::m_formattedVersion must be at +0x44");
static_assert(offsetof(Mortar::MortarGame, m_languageString)   == 0x84, "MortarGame::m_languageString must be at +0x84");
static_assert(offsetof(Mortar::MortarGame, m_versionCombined)  == 0xA4, "MortarGame::m_versionCombined must be at +0xA4");
static_assert(offsetof(Mortar::MortarGame, m_versionMajor)     == 0xA8, "MortarGame::m_versionMajor must be at +0xA8");
static_assert(offsetof(Mortar::MortarGame, m_versionMinor)     == 0xAC, "MortarGame::m_versionMinor must be at +0xAC");
static_assert(offsetof(Mortar::MortarGame, m_versionPatch)     == 0xB0, "MortarGame::m_versionPatch must be at +0xB0");
static_assert(offsetof(Mortar::MortarGame, m_hardwareString)   == 0xB4, "MortarGame::m_hardwareString must be at +0xB4");
static_assert(offsetof(Mortar::MortarGame, m_bFastHardware)    == 0xF4, "MortarGame::m_bFastHardware must be at +0xF4");
static_assert(offsetof(Mortar::MortarGame, m_licensedState)    == 0xF8, "MortarGame::m_licensedState must be at +0xF8");
static_assert(offsetof(Mortar::MortarGame, m_StartupTexture)   == 0xFC, "MortarGame::m_StartupTexture must be at +0xFC");
static_assert(sizeof(Mortar::MortarGame)                       == 0x100, "sizeof(MortarGame) must be 0x100");
#endif

// Free functions outside Mortar namespace (binary: file-scope in MortarGame TU).
// v1.6.1 EmptyFunction @0x1c3670: shared no-op (single bx lr).
void EmptyFunction();

// v1.6.1 ReturnsAnInstanceOfThisMortarGame @0x11f6ac: lazy singleton getter.
// Binary: if (!s_instance) { s_instance = new Game(0x308); } return s_instance;
// Port: new Game() is gated by s_instance (already set in Game ctor).
Mortar::MortarGame* ReturnsAnInstanceOfThisMortarGame();

#endif
