#ifndef FN_MORTAR_GAME_H
#define FN_MORTAR_GAME_H

#include <cstdint>
#include <cstddef>

namespace Mortar {

// Matches original MortarGame base class (0xFC / 252 bytes)
// Game subclass adds 3 fields to reach 0x104 bytes total.
// Binary base vtable: v1.6.1 Mortar::MortarGame vtable, 24 slots.
// TODO: re-verify v1.6.1 Mortar::MortarGame vtable address (v1.5.x 0x001eae58 was stale code, not a vtable).
// NOTE: Binary has NO virtual destructor — slot 0 is GetHardwareString.
//       Port keeps a non-virtual dtor for C++ correctness.
//       // DIFFERS: binary has no virtual dtor on MortarGame; port keeps one for safe
//       //          polymorphic deletion. Slot 0 in binary is GetHardwareString.
class MortarGame {
public:
    // +0x04: raw version string, e.g. "1.5.1"
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

    // +0xB4: hardware string, default "BADA"
    char m_hardwareString[64];

    // +0xF4: fast hardware flag
    bool m_bFastHardware;

    // +0xF8: licensed state (0=unknown, 1=licensed, 2=unlicensed)
    int m_licensedState;

    MortarGame();
    ~MortarGame();

    // --- Vtable (24 slots) — v1.6.1 Mortar::MortarGame vtable ---
    // DIFFERS: port emits a non-virtual dtor before slot 0 for C++ safety;
    //          binary's slot 0 is GetHardwareString (no virtual dtor exists there).

    virtual const char* GetHardwareString();               // slot 0  v1.6.1 Mortar::MortarGame::GetHardwareString @0x0011fb80 (returns this+0x04 ptr)
    virtual bool IsFastHardware();                          // slot 1  v1.6.1 Mortar::MortarGame::IsFastHardware @0x0011fb88
    virtual void RenderAtHalfFrames(const char* hwName, const char* model);  // slot 2  v1.6.1 Mortar::MortarGame::RenderAtHalfFrames @0x0022de74 base no-op
    virtual float GetHighResolutionScale();                 // slot 3  v1.6.1 Mortar::MortarGame::GetHighResolutionScale @0x0022e194 returns 1.0f
    virtual const char* GetOpenFeintProductKey();           // slot 4  v1.6.1 Mortar::MortarGame::GetOpenFeintProductKey @0x0022e19c Defunct
    virtual const char* GetOpenFeintSecret();               // slot 5  v1.6.1 Mortar::MortarGame::GetOpenFeintSecret @0x0022e1a4 Defunct
    virtual const char* GetOpenDisplayName();               // slot 6  v1.6.1 Mortar::MortarGame::GetOpenDisplayName @0x0022e1ac Defunct
    virtual const char* GetPlayhavenToken();                // slot 7  v1.6.1 Mortar::MortarGame::GetPlayhavenToken @0x0022e1b4 Defunct
    virtual void* GetCacheDataArchive();                    // slot 8  v1.6.1 Mortar::MortarGame::GetCacheDataArchive @0x0011fba0 returns nullptr
    virtual void CreateFileSystems(const char* a, const char* b);  // slot 9  v1.6.1 Mortar::MortarGame::CreateFileSystems @0x0022e1bc no-op
    virtual void TellGameToStart(int multiplayer);          // slot 10 v1.6.1 Mortar::MortarGame::TellGameToStart @0x0022de8c no-op
    virtual void Update(float dt);                          // slot 11 v1.6.1 Mortar::MortarGame::Update @0x0022de80 no-op
    virtual void Draw(float dt);                            // slot 12 v1.6.1 Mortar::MortarGame::Draw @0x0022de7c no-op
    virtual void Init(int argc, char** argv);               // slot 13 v1.6.1 Mortar::MortarGame::Init @0x0022de84 no-op
    virtual MortarGame* End();                              // slot 14 v1.6.1 Mortar::MortarGame::End @0x0022de88 returns this
    virtual void Paused();                                  // slot 15 v1.6.1 Mortar::MortarGame::Paused @0x0022de90 no-op
    virtual void UnPaused();                                // slot 16 v1.6.1 Mortar::MortarGame::UnPaused @0x0022de94 no-op
    virtual const char* SelfVersion();                      // slot 17 v1.6.1 Mortar::MortarGame::SelfVersion @0x0022de9c returns "1.0.0"
    virtual void SaveOnExit();                              // slot 18 v1.6.1 Mortar::MortarGame::SaveOnExit @0x0022de98 no-op
    virtual void SetAppLicensed(bool licensed);             // slot 19 v1.6.1 Mortar::MortarGame::SetAppLicensed @0x0022deb8
    virtual int GetAppLicensedState();                      // slot 20 v1.6.1 Mortar::MortarGame::GetAppLicensedState @0x0022dee4 (NOT const — binary doesn't tag const)
    virtual void SetLanguage(const char* lang);             // slot 21 v1.6.1 Mortar::MortarGame::SetLanguage @0x0022dedc Game does NOT override
    virtual bool AllowOrientationChange(int orientation);   // slot 22 v1.6.1 Mortar::MortarGame::AllowOrientationChange @0x0011fbb0 returns false
    virtual void OrientationDidChange(int orientation);     // slot 23 v1.6.1 Mortar::MortarGame::OrientationDidChange @0x0011fbb8 no-op

    // --- Non-virtual methods ---

    // Matches v1.6.1 TellGameToQuit @0x0022e054
    void TellGameToQuit();

    // Matches v1.6.1 MortarGame::SetVersion @0x0022deec — parses "M.m.p" string, fills version fields
    // DIFFERS: binary single-digit minor/patch sections multiplied by 10
    //          (e.g. "1.5.1" -> minor=50, patch=10, combined=15010).
    //          Port keeps direct semver (combined=10501). No callers read m_versionCombined.
    void SetVersion(const char* version);

    // Matches v1.6.1 MortarGame::SetHardware @0x0022e038
    void SetHardware(const char* hw, bool fast);

    // Singleton access
    static MortarGame* GetInstance() { return s_instance; }

protected:
    static MortarGame* s_instance;
};

} // namespace Mortar

// Field-offset assertions for MortarGame (binary @ g_MortarGame, ARM32).
// Offsets are instance-relative (include the vtable pointer at +0x00).
// Guarded by __bada__ so they fire only on the cross-build / Bada toolchain
// where struct layout must match the binary exactly.
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
static_assert(sizeof(Mortar::MortarGame)                       == 0xFC, "sizeof(MortarGame) must be 0xFC");
#endif

// Free functions outside Mortar namespace (binary: file-scope in MortarGame TU).
// v1.6.1 EmptyFunction @0x1c3670: shared no-op (single bx lr).
void EmptyFunction();

// v1.6.1 ReturnsAnInstanceOfThisMortarGame @0x11f6ac: lazy singleton getter.
// Binary: if (!s_instance) { s_instance = new Game(0x308); } return s_instance;
// Port: new Game() is gated by s_instance (already set in Game ctor).
Mortar::MortarGame* ReturnsAnInstanceOfThisMortarGame();

#endif
