#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

#include "HUDControl.h"
#include "gl_funcs.h"

// Matches original HUDControl3d (~0x7C bytes)
// Extends HUDControl with texture + UV rect + rotation draw
class HUDControl3d : public HUDControl {
public:
    // +0x60: main display texture
    GLuint m_Texture;
    // +0x64..+0x70: UV rect
    float m_UVLeft, m_UVTop, m_UVRight, m_UVBottom;
    // +0x74: secondary texture (used by screens)
    GLuint m_SecondaryTex;

    HUDControl3d()
        : m_Texture(0), m_UVLeft(0), m_UVTop(0), m_UVRight(1), m_UVBottom(1),
          m_SecondaryTex(0) {}

    // Matches HUDControl3d::Draw (0x14428c)
    // Scale(size) → RotZ(timer) → Translate(pos + offset) → TintColour → DrawQuad
    void Draw(Renderer& r, const Vec3& hudScale, int layerMask) override;
};

#endif
