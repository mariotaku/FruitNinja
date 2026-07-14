#ifndef FN_HUD_SPEED_CONTROL_H
#define FN_HUD_SPEED_CONTROL_H

// SpeedControl : HUDControl3d (sizeof = 0xAC)
// Combo-speed gauge (Arcade only). Created lazily by WaveManager::UpdateComboSpeed.
// Owned by WaveManager (pointer at WaveManager+0x00). Destroyed via DeleteSpeedControl.
//
// Binary addresses:
//   ctor (C1)          v1.6.1 SpeedControl::SpeedControl @0x001b892c
//   ctor (C2)          v1.6.1 SpeedControl::SpeedControl @0x001b8a64
//   dtor (base/D2)     v1.6.1 SpeedControl::~SpeedControl @0x001b8ba4
//   dtor (complete/D1) v1.6.1 SpeedControl::~SpeedControl @0x001b8c4c
//   dtor (deleting/D0) v1.6.1 SpeedControl::~SpeedControl @0x001b8cf4
//   Update             v1.6.1 SpeedControl::Update @0x001b8290
//   Draw               v1.6.1 SpeedControl::Draw @0x001b8788
//   GetType            v1.6.1 SpeedControl::GetType @0x001b9218

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
    _Vector3<float> m_BaseSize;
    // +0x94: combo timer value (= WaveManager::m_ComboTimer[0] for player 0)
    float    m_Speed;
    // +0x98: smoothed alpha [0..1]
    float    m_SmoothedAlpha;
    // +0x9C: looping combo-blitz backing-stream MortarSound* (see Update / ~SpeedControl)
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

    // vtable +0x1c (slot 7): Draw override. Renders the 4-panel scrolling
    // combo-blitz chevron speed-line strip. v1.6.1 SpeedControl::Draw @0x001b8788.
    void Draw(float* hudScale) override;

    // ---- STUBS (binary) ----
    // Binary @ 0x001b8154 -- HUDControl3d Init vtable slot. Binary body is a
    // single `bx lr` (empty no-op). SpeedControl does all its setup in the
    // ctor / Update, so this override is intentionally empty in the binary.
    void Init() override;
    // Binary @ 0x001b815c -- PreDraw vtable slot. Binary body is `bx lr`
    // (empty no-op). All gauge transform work happens in Update/Draw.
    void PreDraw(float* hudScale) override;
    // Binary @ 0x001b8150 -- Reset vtable slot. Binary body is `bx lr`
    // (empty no-op). Gauge state is reset by WaveManager recreating the
    // control, not by a per-frame Reset.
    void Reset() override;
    // Binary @ 0x001b8158 -- Skip vtable slot. Binary body is `bx lr`
    // (empty no-op). The gauge has no skippable intro animation.
    void Skip() override;
    // Binary @ 0x001b8160 -- looping-stream restart callback bound to
    // GameSound::SFXPlay's finishCallback arg. Returns false (binary
    // uint32 0). Re-issues SFXPlay when the previous loop instance
    // finishes; switches to the "heavy" variant after 6+ waves.
    bool SoundNeedsLooping(Mortar::MortarSound* sound);
    // ---- end STUBS ----
};

// Binary sizeof = 0xAC (ARM32, 4-byte ptrs). Only verified on the Bada cross-build.
#ifdef __bada__
static_assert(sizeof(SpeedControl) == 0xAC, "SpeedControl size mismatch");
#endif

#endif // FN_HUD_SPEED_CONTROL_H
