#ifndef MORTAR_SYSTEM_MANAGER_H
#define MORTAR_SYSTEM_MANAGER_H

#include "core/Singleton.h"
#include "core/UniqueDeviceID.h"
#include <cstdint>

// SystemManager -- polymorphic singleton (vtable @ 0x001eaee0, group base 0x001eaed8).
// Binary ctor @ 0x0018af58 writes vptr at +0x00, then member fields.
// Binary total size = 212 bytes (0xD4): 0x54 + sizeof(UniqueDeviceID)=128 = 212.
// Virtual destructor at vtable slot 0 (0x0018adc4 / deleting 0x0018af34).
class SystemManager : public Mortar::Singleton<SystemManager> {
    friend class Mortar::Singleton<SystemManager>;

    uint8_t m_bRunning;          // +0x04  (vptr at +0x00, Singleton base adds 0 bytes)
    int16_t m_LastFrameTime;     // +0x06
    int16_t m_AvgFPS;            // +0x08
    int16_t m_MinFPS;            // +0x0A
    int16_t m_MaxFPS;            // +0x0C
    uint8_t m_RingMaxIdx;        // +0x0E
    uint8_t m_RingWriteIdx;      // +0x0F
    int16_t m_FrameTimeRing[30]; // +0x10
    uint8_t m_QuitState;         // +0x4C
    // +0x4D..+0x4F padding
    // +0x50: float written 0.0f by Init (vldr.32/vstr.32 s15; DAT = 0.0f); no read
    // site identified. Reserved.
    float   m_reserved50;        // +0x50  purpose unknown
    // +0x54: UniqueDeviceID sub-object (128 bytes), ctor'd via thunk 0x000f5094
    UniqueDeviceID m_DeviceID;   // +0x54

    SystemManager();
    virtual ~SystemManager() {}

public:
    // Matches 0x0018b024: sets m_reserved50=0, m_bRunning=1, records clock base (Bada),
    // calls _RetrieveDeviceID (Bada). Port: only the two field writes are meaningful.
    void Init();

    // Returns m_bRunning. Outputs dt via pointer.
    bool Update(float* dt);

    // Sets m_bRunning = false
    void QuitGame();

    // Sets m_QuitState = 2 (graceful quit)
    void RequestQuit();

    bool IsRunning() const { return m_bRunning != 0; }
    int16_t GetAvgFPS() const { return m_AvgFPS; }
    int16_t GetMinFPS() const { return m_MinFPS; }
    int16_t GetMaxFPS() const { return m_MaxFPS; }

    // m_QuitState polled by MainScreen STATE_QUIT_WAIT (binary @ 0x0014c09e).
    // 0/1 = OS quit dialog pending; 2 = OS confirmed quit -> proceed; 3 =
    // OS dismissed/cancelled -> reset MainScreen to state 0. On platforms
    // without an OS quit dialog (SDL desktop / webOS) RequestQuit jumps
    // straight to 2.
    uint8_t GetQuitState() const { return m_QuitState; }
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(SystemManager) == 212, "SystemManager size mismatch");
// TODO(#93-followup): offsetof on private members rejected by GCC 4.4; need friend
// access or public fields to enforce field offsets on the cross-build.
// Expected: m_bRunning@0x04, m_QuitState@0x4C, m_reserved50@0x50, m_DeviceID@0x54.
#endif

// Free functions implemented in SystemManager.cpp that read from the MortarGame singleton.
// All are global-namespace free functions in the binary (not class members).

// ASM-spec v1.6.1 GetVersionMajor @0x11f440: returns theGame->m_versionMajor
int GetVersionMajor();

// ASM-spec v1.6.1 GetVersionMinor @0x11f460: returns theGame->m_versionMinor
int GetVersionMinor();

// ASM-spec v1.6.1 GetVersionPatch @0x11f480: returns theGame->m_versionPatch
int GetVersionPatch();

// ASM-spec v1.6.1 GetFormattedVersionString @0x11f3e0: returns theGame->m_formattedVersion (char[64] at +0x44)
const char* GetFormattedVersionString();

// ASM-spec v1.6.1 IsStartupTexturePortrait @0x11f4a8: reads global isStartupTexturePortrait
bool IsStartupTexturePortrait();

// Global byte set/cleared by startup texture lifecycle (GetStartupTexture/ReleaseStartupTexture).
// Defined in SystemManager.cpp; also written by the GetStartupTexture need-care function.
extern bool isStartupTexturePortrait;

#endif
