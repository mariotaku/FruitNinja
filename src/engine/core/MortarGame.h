#ifndef FN_MORTAR_GAME_H
#define FN_MORTAR_GAME_H

#include <cstdint>
#include <cstddef>

namespace Mortar {

// Matches original MortarGame base class (0xFC / 252 bytes)
// Game subclass adds 3 fields to reach 0x104 bytes total.
// Binary base vtable @ 0x001eae58 (= 0x001eae50 + 8), 24 slots.
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

    // --- Vtable (24 slots) — binary @ 0x001eae58 ---
    // DIFFERS: port emits a non-virtual dtor before slot 0 for C++ safety;
    //          binary's slot 0 is GetHardwareString (no virtual dtor exists there).

    virtual const char* GetHardwareString();               // slot 0  @ 0x0010d9d0 (returns this+0x04 ptr)
    virtual bool IsFastHardware();                          // slot 1  @ 0x0010d9d4
    virtual void RenderAtHalfFrames(const char* hwName, const char* model);  // slot 2  @ 0x0018aa14 base no-op
    virtual float GetHighResolutionScale();                 // slot 3  @ 0x0018ac80 returns 1.0f
    virtual const char* GetOpenFeintProductKey();           // slot 4  @ 0x0018ac88 Defunct
    virtual const char* GetOpenFeintSecret();               // slot 5  @ 0x0018ac8c Defunct
    virtual const char* GetOpenDisplayName();               // slot 6  @ 0x0018ac90 Defunct
    virtual const char* GetPlayhavenToken();                // slot 7  @ 0x0018ac94 Defunct
    virtual void* GetCacheDataArchive();                    // slot 8  @ 0x0010d9dc returns nullptr
    virtual void CreateFileSystems(const char* a, const char* b);  // slot 9  @ 0x0018ac98 no-op
    virtual void TellGameToStart(int multiplayer);          // slot 10 @ 0x0018aa28 no-op
    virtual void Update(float dt);                          // slot 11 @ 0x0018aa1c no-op
    virtual void Draw(float dt);                            // slot 12 @ 0x0018aa18 no-op
    virtual void Init(int argc, char** argv);               // slot 13 @ 0x0018aa20 no-op
    virtual MortarGame* End();                              // slot 14 @ 0x0018aa24 returns this
    virtual void Paused();                                  // slot 15 @ 0x0018aa2c no-op
    virtual void UnPaused();                                // slot 16 @ 0x0018aa30 no-op
    virtual const char* SelfVersion();                      // slot 17 @ 0x0018aa38 returns "1.0.0"
    virtual void SaveOnExit();                              // slot 18 @ 0x0018aa34 no-op
    virtual void SetAppLicensed(bool licensed);             // slot 19 @ 0x0018aa50
    virtual int GetAppLicensedState();                      // slot 20 @ 0x0018aa68 (NOT const — binary doesn't tag const)
    virtual void SetLanguage(const char* lang);             // slot 21 @ 0x0018aa70 Game does NOT override
    virtual bool AllowOrientationChange(int orientation);   // slot 22 @ 0x0018ac9c returns false
    virtual void OrientationDidChange(int orientation);     // slot 23 @ 0x0010d9e0 no-op

    // --- Non-virtual methods ---

    // Matches 0x0018ac64
    void TellGameToQuit();

    // Matches 0x0018aa90 — parses "M.m.p" string, fills version fields
    // DIFFERS: binary single-digit minor/patch sections multiplied by 10
    //          (e.g. "1.5.1" -> minor=50, patch=10, combined=15010).
    //          Port keeps direct semver (combined=10501). No callers read m_versionCombined.
    void SetVersion(const char* version);

    // Matches 0x0018aa7c
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

#endif
