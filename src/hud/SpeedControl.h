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
#include "engine/audio/MortarSound.h"
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

    // ---- STUBS (binary) ----
    // STUB: SpeedControl::Draw -- binary @ 0x???? (TODO RE)
    void Draw(float* viewVec) override;
    // STUB: SpeedControl::Init -- binary @ 0x???? (TODO RE)
    void Init() override;
    // STUB: SpeedControl::PreDraw -- binary @ 0x???? (TODO RE)
    void PreDraw(float* viewVec);
    // STUB: SpeedControl::Reset -- binary @ 0x???? (TODO RE)
    void Reset() override;
    // STUB: SpeedControl::Skip -- binary @ 0x???? (TODO RE)
    void Skip() override;
    // STUB: SpeedControl::SoundNeedsLooping -- binary @ 0x???? (TODO RE)
    void SoundNeedsLooping(Mortar::MortarSound* sound);
    // ---- end STUBS ----
};

// Binary sizeof = 0xAC (ARM32, 4-byte ptrs). Only verified on the Bada cross-build.
#ifdef __bada__
static_assert(sizeof(SpeedControl) == 0xAC, "SpeedControl size mismatch");
#endif

#endif // FN_HUD_SPEED_CONTROL_H
