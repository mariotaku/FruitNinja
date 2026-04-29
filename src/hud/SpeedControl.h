#ifndef FN_HUD_SPEED_CONTROL_H
#define FN_HUD_SPEED_CONTROL_H

// Analysed: 2026-04-30T00:00
//
// SpeedControl : HUDControl3d (size = 0xAC)
// Struct size confirmed: operator_new(0xac) in WaveManager::UpdateComboSpeed @ 0x00122ff6.
// Combo speed/blitz gauge: scale pulse + particle emitter on combo increase, fade-in/out alpha.
// Owned by WaveManager (pointer at WaveManager+0x00). Created lazily in UpdateComboSpeed,
// destroyed by DeleteSpeedControl. NOT created by GameInit.
//
// Binary addresses:
//   ctor (real)    0x0016133c
//   ctor (alias)   0x00161444
//   ctor thunk     0x000ffd38
//   dtor (regular) 0x00161558
//   dtor (inplace) 0x001615d4
//   dtor (deleting)0x00161650
//   Update         0x00160dc4
//   DeleteSpeedControl (standalone) 0x001217d4

#include "HUDControl3d.h"
#include <cstdint>

class SpeedControl : public HUDControl3d {
public:
    // Subclass fields occupy 0xAC - 0x7C = 0x30 bytes (layout not yet fully RE'd).
    uint8_t m_fields[0x30];

    SpeedControl();
    ~SpeedControl() override;

    void Init() override {}   // vtable[2]: loads localised speed gauge texture
    void Reset() override {}
    void Update(float dt) override { (void)dt; }
    void Draw(const Vec3& hudScale, int layerMask) override { (void)hudScale; (void)layerMask; }

    int GetType() override { return 1; }
};

#endif // FN_HUD_SPEED_CONTROL_H
