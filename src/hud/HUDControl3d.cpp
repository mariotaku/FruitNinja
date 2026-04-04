//
// HUDControl3d::Draw — reimplemented from Ghidra decompilation at 0x14428c
//
// Original pipeline:
//   Texture::Set → Reset → Scale44(size) → RotZ44(SinIdx/CosIdx) →
//   offset = Vec3(480,320,0) * hudScale → GlobalTranslate44(offset + pos) →
//   Upload → TintColour → DrawQuadUnCached → UnSet
//
// In the port: positions are in screen coords (0-480, 0-320) via toScreen(),
// so the Vec3(480,320,0) offset is handled by the coordinate conversion.
// The draw uses the Renderer's matrix stack + DrawQuad.
//

#include "HUDControl3d.h"
#include "render/Renderer.h"
#include <cmath>

// Rotation speed constant (verified: DAT_001443dc = 182.0)
static const float ROT_SPEED = 182.0f;

// SinIdx/CosIdx: original uses 16-bit angle lookup
// Simplified: angle_rad = (ushort)(timer * 182.0) * 2π / 65536
static inline float SinIdx(float timer) {
    int idx = (int)(timer * ROT_SPEED) & 0xFFFF;
    return sinf((float)idx * 6.2831853f / 65536.0f);
}
static inline float CosIdx(float timer) {
    int idx = (int)(timer * ROT_SPEED) & 0xFFFF;
    return cosf((float)idx * 6.2831853f / 65536.0f);
}

void HUDControl3d::Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Step 1: check texture valid AND alpha != 0 (matches original guard)
    if (!m_Texture || m_Alpha == 0) return;

    // Step 2: Set texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_Texture);

    // Step 3: Reset matrix stack
    Mortar::MatrixManager::GetInstance().GetWorldStack().Reset();

    // Step 4: Scale44(size)
    Matrix44 mat = Matrix44::Scale44(size);

    // Step 5: RotZ44 if m_Timer != 0
    if (m_Timer != 0.0f) {
        float sinA = SinIdx(m_Timer);
        float cosA = CosIdx(m_Timer);
        mat.RotZ44(sinA, cosA);
    }

    // Step 6-7: Position
    // Original: offset = Vec3(480, 320, 0) * hudScaleParam; finalPos = offset + pos
    // hudScaleParam at runtime = (1.0, 1.0, 1.0), so offset = (480, 320, 0)
    // In port: positions already in screen coords via toScreen(), so use pos directly
    Vec3 finalPos = pos;
    mat.GlobalTranslate44(finalPos);

    // Step 8-9: Upload
    Mortar::MatrixManager::GetInstance().GetWorldStack().SetCurrentMatrix(mat);

    // Step 10-11: TintColour → DrawQuad
    Colour tint = m_DrawColour;
    tint.a = m_Alpha;
    r.DrawQuad(tint, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    // Step 12: UnSet
    glBindTexture(GL_TEXTURE_2D, 0);
}
