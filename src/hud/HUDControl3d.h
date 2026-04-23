#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d : HUDControl (size = 0x7C)
// Verified from Ghidra: ctor at 0x1443f4, Draw at 0x14428c (57 lines)
// See docs/structs/hud.md for full layout.
//

#include "HUDControl.h"
#include "render/gl_funcs.h"

class HUDControl3d : public HUDControl {
public:
    // +0x60: main display texture (nullptr/0 = don't draw)
    // Original: SmartPtr<Texture>; port uses raw GLuint
    GLuint m_Texture;

    // +0x64..+0x70: UV rectangle
    float m_UVLeft, m_UVTop, m_UVRight, m_UVBottom;

    // +0x74: secondary texture (used by screens)
    GLuint m_SecondaryTex;

    // +0x78
    int field_0x78;

    HUDControl3d()
        : m_Texture(0),
          m_UVLeft(0.0f), m_UVTop(0.0f), m_UVRight(1.0f), m_UVBottom(1.0f),
          m_SecondaryTex(0), field_0x78(0) {
        m_Timer = 0.0f;
    }

    // vtable +0x1c: Draw — matches 0x14428c (57 lines)
    void Draw(const Vec3& hudScale, int layerMask) override;

    // vtable +0x30
    int GetType() override { return 1; }
};

#endif
