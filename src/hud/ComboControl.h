#ifndef FN_HUD_COMBO_CONTROL_H
#define FN_HUD_COMBO_CONTROL_H

// Defunct: ComboControl — unused in binary; class shape and vtable preserved
// per stub-don't-skip policy. Zero call sites in binary; combo popup is
// rendered by MissControl (binary @ 0x0017dad8). Binary @ 0x001694b8 ctor.
// re-analyst confirmed 2026-05-20.
//
// Analysed: 2026-04-30T00:00
//
// ComboControl : HUDControl3d (size = 0x8C)
// Struct size: 0x8C (super 0x7C + lifetime float + comboCount int + char[8] label + max float at +0x84).
// Combo count pop-up (e.g. "3"). 1-second lifetime, then self-removes via m_bPendingRemoval.
// Spawned by combo logic; owned by HUD until removed (fire-and-forget).
//
// Binary addresses:
//   ctor (base)     0x001694b8
//   ctor (complete) 0x00169534
//   dtor (base/D2)     0x00169410
//   dtor (complete/D1) 0x00169464
//   dtor (deleting/D0) 0x001693b4
//   Reset          0x00169384  (no-op body)
//   Update         0x0016938c  (lifetime -= dt; if <0 m_bPendingRemoval=1)

#include "HUDControl3d.h"
#include <cstdint>

class ComboControl : public HUDControl3d {
public:
    // +0x7C: remaining display lifetime (seconds). Starts at 1.0f.
    float m_Lifetime;
    // +0x80: combo count passed to ctor; stored for label formatting.
    int m_ComboCount;
    // +0x84: label buffer ("%i" formatted string, 8 bytes per binary layout)
    char m_Label[8];

    explicit ComboControl(int comboCount);
    ~ComboControl() override;

    void Reset() override;     // 0x00169384 -- no-op in binary
    void Update(float dt) override;
    void Draw(float* hudScaleRaw) override;

    // ASM-verified: 2026-05-20 v1.6.1 ComboControl::GetType @ 0x00169aa8 (re-analyst) -- returns 6
    int GetType() override { return 6; }

    void Init() override;
    void PreDraw();
    void Release() override;
    void Skip() override;
};

#endif // FN_HUD_COMBO_CONTROL_H
