#ifndef FN_HUD_SPEED_CONTROL_H
#define FN_HUD_SPEED_CONTROL_H

// Analysed: 2026-05-03T00:00
//
// SpeedControl : HUDControl3d (sizeof = 0xAC)
// Binary @ 0x0016133c (ctor), 0x00160dc4 (Update).
// Combo-speed gauge (Arcade only). Created lazily by WaveManager::UpdateComboSpeed.
// Owned by WaveManager (pointer at WaveManager+0x00). Destroyed via DeleteSpeedControl.
//
// Binary addresses:
//   ctor (real)     0x0016133c
//   ctor (alias)    0x00161444
//   ctor thunk      0x000ffd38
//   dtor (regular)  0x00161558
//   dtor (inplace)  0x001615d4
//   dtor (deleting) 0x00161650
//   Update          0x00160dc4

#include "HUDControl3d.h"
#include <cstdint>

class SpeedControl : public HUDControl3d {
public:
    // +0x7C: raw speed accumulator (wraps at 65535)
    uint16_t m_RawSpeed;
    // +0x7E
    uint8_t  _pad7e[2];
    // +0x80: displayed speed value (approach target from UpdateComboSpeed)
    float    m_DisplayedSpeed;
    // +0x84: pulse scale for Draw animation
    float    m_PulseScale;
    // +0x88..+0x90: base size (set from texture dimensions in ctor)
    Vec3     m_BaseSize;
    // +0x94: speed-loss timer value (= WaveManager::field_0x4c for player 0)
    float    m_Speed;
    // +0x98: smoothed alpha [0..1]
    float    m_SmoothedAlpha;
    // +0x9C: sound object pointer (stub: always null)
    void*    m_pSound;
    // +0xA0: particle emitter pointer (stub: always null)
    void*    m_pEmitter;
    // +0xA4: sound index
    int      m_SoundIdx;
    // +0xA8: sound volume
    float    m_SoundVolume;

    SpeedControl();
    ~SpeedControl() override;

    void Update(float dt) override;
    int  GetType() override { return 1; }

public:

public:

public:

public:

public:
};

// Binary sizeof = 0xAC (ARM32, 4-byte ptrs). Port size differs on 64-bit (8-byte ptrs).
// Cross-build (GCC 4.4.1 -std=gnu++0x) may compute a different Delegate size due to
// pre-C++11 aligned_storage implementation differences -- exclude it via __cplusplus check.
#if (defined(__arm__) || (defined(_M_IX86) && !defined(_WIN64))) && (__cplusplus >= 201103L)
static_assert(sizeof(SpeedControl) == 0xAC, "SpeedControl size mismatch");
#endif

#endif // FN_HUD_SPEED_CONTROL_H
