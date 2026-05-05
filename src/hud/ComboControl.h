#ifndef FN_HUD_COMBO_CONTROL_H
#define FN_HUD_COMBO_CONTROL_H

// Analysed: 2026-04-30T00:00
//
// ComboControl : HUDControl3d (size = 0x8C)
// Struct size: 0x8C (super 0x7C + lifetime float + comboCount int + char[8] label + max float at +0x84).
// Combo count pop-up (e.g. "x3"). 1-second lifetime, then self-removes via m_bPendingRemoval.
// Spawned by combo logic; owned by HUD until removed (fire-and-forget).
//
// Binary addresses:
//   ctor (real)    0x00136cc4
//   ctor (alias)   0x00136d1c
//   dtor (regular) 0x00136c0c
//   dtor (inplace) 0x00136c4c
//   dtor (deleting)0x00136c88
//   Reset          0x00136bdc  (no-op body)
//   Update         0x00136be4  (lifetime -= dt; if <0 m_bPendingRemoval=1)

#include "HUDControl3d.h"
#include <cstdint>

class ComboControl : public HUDControl3d {
public:
    // +0x7C: remaining display lifetime (seconds). Starts at 1.0f.
    float m_Lifetime;
    // +0x80: combo count passed to ctor; stored for label formatting.
    int m_ComboCount;
    // +0x84: label buffer (OS_SPrintf'd "x%d" string, 8 bytes per binary layout)
    char m_Label[8];

    explicit ComboControl(int comboCount);
    ~ComboControl() override;

    void Reset() override {}   // 0x00136bdc — no-op in binary
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override { (void)hudScale; (void)layerMask; }

    int GetType() override { return 1; }

public:

public:

public:

public:

public:
};

#endif // FN_HUD_COMBO_CONTROL_H
