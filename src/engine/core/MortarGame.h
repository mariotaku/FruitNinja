#ifndef MORTAR_MORTAR_GAME_H
#define MORTAR_MORTAR_GAME_H

#include <cstdint>

namespace Mortar {

// Matches original MortarGame base class (0xFC / 252 bytes)
// Game subclass adds 3 fields to reach 0x104 bytes total.
// See docs/structs/game.md for full layout and vtable.
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
    virtual ~MortarGame();

    // --- Vtable entries (16 slots, all stubs in base) ---

    virtual const char* GetHardwareString();        // 0: returns m_versionString
    virtual bool IsFastHardware();                   // 1
    virtual void RenderAtHalfFrames();               // 2: no-op
    virtual float GetHighResolutionScale();           // 3: returns 1.0f
    virtual const char* GetOpenFeintProductKey();     // 4: defunct
    virtual const char* GetOpenFeintSecret();         // 5: defunct
    virtual const char* GetOpenDisplayName();         // 6: defunct
    virtual const char* GetPlayhavenToken();          // 7: defunct
    virtual const char* GetCacheDataArchive();        // 8: returns nullptr
    virtual void CreateFileSystems();                 // 9: no-op
    virtual void TellGameToStart();                   // 10: no-op
    virtual void Update(float dt);                    // 11: no-op
    virtual void Draw(float dt);                      // 12: no-op
    virtual void Init(int argc, char** argv);         // 13: no-op
    virtual void End();                               // 14: no-op
    virtual void Paused();                            // 15: no-op

    // --- Non-virtual methods ---

    // Matches 0x0018ac64
    void TellGameToQuit();

    // Returns default version "1.0.0"; Game overrides to "1.5.1"
    virtual const char* SelfVersion();

    // Matches 0x0018aa90 — parses "M.m.p" string, fills version fields
    void SetVersion(const char* version);

    // Matches 0x0018aa70 — Game overrides to write g_GameData+0x03
    virtual void SetLanguage(const char* lang);

    // Matches 0x0018aa7c
    void SetHardware(const char* hw, bool fast);

    // Matches 0x0018aa50 — Game overrides to use g_GameData+0x18C
    virtual void SetAppLicensed(bool licensed);

    // Matches 0x0018aa68 — Game overrides to use g_GameData+0x18C
    virtual int GetAppLicensedState() const;

    // Matches 0x0018aa34 — no-op stub
    virtual void SaveOnExit();

    // Matches 0x0018aa30 — no-op stub
    virtual void UnPaused();

    // Matches 0x0018ac9c
    virtual bool AllowOrientationChange(int orientation);

    // Singleton access
    static MortarGame* GetInstance() { return s_instance; }

protected:
    static MortarGame* s_instance;
};

} // namespace Mortar

#endif
