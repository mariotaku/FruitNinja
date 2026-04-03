#include "HUDControl3d.h"
#include "Renderer.h"
#include <cmath>

// Matches HUDControl3d::Draw (0x14428c, 57 lines)
// Renders a textured quad with rotation and colour tint
void HUDControl3d::Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
    (void)layerMask;
    if (!m_Texture || m_Alpha == 0) return;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_Texture);

    // Reset matrix stack
    r.matrix_mgr.stack.Reset();

    // Scale by control size
    Matrix44 mat = Matrix44::Scale44(size);

    // Optional rotation (if m_Timer != 0)
    if (m_Timer != 0.0f) {
        // Original uses SinIdx/CosIdx with 16-bit angle conversion
        // Simplified: m_Timer is angle in some unit, speed = DAT_001443dc = 182.0
        float angle = m_Timer * 182.0f * 3.14159f / 32768.0f;
        mat.RotZ44(sinf(angle), cosf(angle));
    }

    // Translate to position with HUD scale offset
    // Original: offset = hudScale * Vec3(480, 320, 0) then += pos
    // In our port: positions are already in screen coords, just use pos directly
    Vec3 drawPos = pos;
    mat.GlobalTranslate44(drawPos);

    r.matrix_mgr.stack.SetCurrentMatrix(mat);

    // Tint with control colour + alpha
    Colour tint = m_DrawColour;
    tint.a = m_Alpha;

    r.DrawQuad(tint, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    glBindTexture(GL_TEXTURE_2D, 0);
}
