#ifndef FN_HUD_SCORE_CONTROL_H
#define FN_HUD_SCORE_CONTROL_H

// Analysed: 2026-04-30T00:00
//
// ScoreControl : HUDControl3d (size = 0x100)
// Struct size confirmed: operator_new(0x100) in GameInit.
// Main score HUD: 16-digit display with per-digit alpha animation, sin-wobble
// pulse on score change, scale pulse driven by combo timer, new-highscore banner.
//
// Binary addresses:
//   ctor (real)    0x00158c7c
//   ctor (alias)   0x00158d4c
//   ctor thunk     0x000f6bdc
//   dtor (regular) 0x00158394
//   dtor (inplace) 0x00158418
//   dtor (deleting)0x00158494
//   Update         0x0015853c
//   Draw           ~0x00158600+
// LoadContent called via vtable slot Init (vtable[2]) from GameInit.

#include "HUDControl3d.h"
#include <cstdint>

class ScoreControl : public HUDControl3d {
public:
    // Subclass fields occupy 0x100 - 0x7C = 0x84 bytes (layout not yet fully RE'd).
    uint8_t m_fields[0x84];

    ScoreControl();
    ~ScoreControl() override;

    void Init() override {}      // vtable[2]: loads localised digit textures (0x00158c7c body)
    void Reset() override {}
    void Update(float dt) override { (void)dt; }
    void Draw(const Vec3& hudScale, int layerMask) override { (void)hudScale; (void)layerMask; }

    int GetType() override { return 1; }
};

#endif // FN_HUD_SCORE_CONTROL_H
