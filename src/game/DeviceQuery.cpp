// DeviceQuery.cpp -- Bada-platform device/orientation query stubs (global free functions).
// Port specific: SDL port is fixed landscape; functions return safe constants or
// delegate to SDL-side state. No accelerometer or Bada OS API is available.
//
// Binary TU addresses (v1.6.1):
//   CurrentOrientation     @ 0x0011f4c4
//   GetHardwareString (fn) @ 0x0011f4e4
//   GetSoftwareString      @ 0x0011f504
//   DeviceUpsideDown       @ 0x0011a14c
//   IsDeviceUpsideDown     @ 0x0011a154
//   UpdateUpsideDown       @ 0x0011a184
//   DlTwVal                @ 0x00152dc4

#include "Game.h"
#include "GameWork.h"
#include "FruitSaveData.h"

// CurrentOrientation -- v1.6.1 @0x0011f4c4
// Binary: reads theGame+0x104 (orientation integer stored by OrientationDidChange).
// Port specific: SDL fixed-landscape; AllowOrientationChange returns false so this
// field is never updated. Returns 0 (default/landscape constant).
int CurrentOrientation()
{
    return 0;
}

// GetHardwareString (free fn) -- v1.6.1 @0x0011f4e4
// Binary: returns pointer to theGame+0x108 (in-struct hardware string buffer).
// Port specific: delegates to MortarGame::GetHardwareString(); v1.6.1 reads theGame+0x108.
const char* GetHardwareString()
{
    Mortar::MortarGame* g = Mortar::MortarGame::GetInstance();
    return g ? g->GetHardwareString() : "";
}

// GetSoftwareString -- v1.6.1 @0x0011f504
// Binary: returns pointer to theGame+0x208 (Bada OS version string in Game object).
// Port specific: no Bada OS; v1.6.1 GetSoftwareString @0x0011f504 reads theGame+0x208.
const char* GetSoftwareString()
{
    return "SDL2";
}

// DeviceUpsideDown -- v1.6.1 @0x0011a14c
// Binary: probes Bada accelerometer via Game+0x1a4. Dead in shipped binary --
// accelerometer init path never runs; returns 0 unconditionally.
// Port specific: SDL fixed-landscape; v1.6.1 DeviceUpsideDown @0x0011a14c returns 0.
int DeviceUpsideDown()
{
    return 0;
}

// IsDeviceUpsideDown -- v1.6.1 @0x0011a154
// Returns true while the upside-down hold timer is active (> 0).
bool IsDeviceUpsideDown()
{
    return game_work.m_UpsideDownTimer > 0.0f;
}

// UpdateUpsideDown -- v1.6.1 @0x0011a184
// Drives the upside-down timer: sets to 0.75s while device inverted, decays by dt
// once upright. Returns IsDeviceUpsideDown().
bool UpdateUpsideDown(float dt)
{
    if (DeviceUpsideDown()) {
        game_work.m_UpsideDownTimer = 0.75f;
    } else {
        if (game_work.m_UpsideDownTimer > 0.0f) {
            game_work.m_UpsideDownTimer -= dt;
        }
    }
    return IsDeviceUpsideDown();
}

// DlTwVal -- v1.6.1 @0x00152dc4 ("Downloaded Tweak Value")
// Server-side configuration callback: validates key (must start with "FNT") and
// count > 0, then forwards (key, val) to FruitSaveData::DownloadedTweakValue.
// Returns 1 (success indicator).
int DlTwVal(const char* key, int val, int count, void* /*data*/)
{
    if (key && game_work.m_SaveData && count > 0
            && key[0] == 'F' && key[1] == 'N' && key[2] == 'T') {
        game_work.m_SaveData->DownloadedTweakValue(key, val);
    }
    return 1;
}
