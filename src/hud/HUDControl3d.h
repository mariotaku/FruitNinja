#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d — extends HUDControl with textured quad rendering
// Verified from Ghidra: ctor at 0x1443f4, Draw at 0x14428c (57 lines)
//
// Field layout (extends HUDControl at +0x60):
//   +0x60  SmartPtr<Texture> m_Texture (NULL = don't draw)
//   +0x64  float m_UVLeft
//   +0x68  float m_UVTop
//   +0x6c  float m_UVRight
//   +0x70  float m_UVBottom
//   +0x74  SmartPtr<Texture> m_SecondaryTex
//   +0x78  int field_0x78 = 0
//
// Draw pipeline (verified):
//   1. if (!texture || alpha==0) return
//   2. Texture::Set
//   3. MatrixStack::Reset
//   4. Scale44(size)
//   5. if (m_Timer != 0): RotZ44(SinIdx(timer * 182.0), CosIdx(timer * 182.0))
//   6. offset = Vec3(480, 320, 0) * hudScaleParam
//   7. finalPos = offset + pos
//   8. GlobalTranslate44(finalPos)
//   9. UploadMatrices
//  10. TintColour(m_DrawColour) → alpha applied
//  11. DrawQuadUnCached(tint, uvLeft, uvRight, uvTop, uvBottom)
//  12. Texture::UnSet
//

#include "HUDControl.h"
#include "gl_funcs.h"

class HUDControl3d : public HUDControl {
public:
    // +0x60: main display texture (NULL/0 = don't draw)
    GLuint m_Texture;

    // +0x64..+0x70: UV rectangle
    float m_UVLeft, m_UVTop, m_UVRight, m_UVBottom;

    // +0x74: secondary texture (used by screens for title etc.)
    GLuint m_SecondaryTex;

    HUDControl3d()
        : m_Texture(0),
          m_UVLeft(0.0f), m_UVTop(0.0f), m_UVRight(1.0f), m_UVBottom(1.0f),
          m_SecondaryTex(0) {
        // Original ctor: m_Timer = 0.0 (DAT_00144468 verified = 0.0)
        m_Timer = 0.0f;
    }

    // vtable +0x24: Draw — matches 0x14428c
    void Draw(Renderer& r, const Vec3& hudScale, int layerMask) override;
};

#endif
