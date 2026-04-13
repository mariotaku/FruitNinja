//
// HUDControl3d::Draw — reimplemented from 0x14428c (57 lines)
// See docs/structs/hud.md for full decompilation.
//

#include "HUDControl3d.h"
#include "Game.h"
#include "render/MatrixManager.h"
#include <cmath>

// Rotation speed constant (verified: DAT_001443dc = 182.0)
static const float ROT_SPEED = 182.0f;

// SinIdx/CosIdx: original uses 16-bit angle lookup table
// angle_rad = (ushort)(timer * 182.0) * 2pi / 65536
static inline float SinIdx(float timer) {
    int idx = (int)(timer * ROT_SPEED) & 0xFFFF;
    return sinf((float)idx * 6.2831853f / 65536.0f);
}
static inline float CosIdx(float timer) {
    int idx = (int)(timer * ROT_SPEED) & 0xFFFF;
    return cosf((float)idx * 6.2831853f / 65536.0f);
}

// Matches 0x14428c (57 lines)
void HUDControl3d::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Step 1: if (!texture || alpha == 0) return
    if (!m_Texture || m_DrawColour.a == 0) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    // Step 2: Texture::Set
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_Texture);

    // Step 3: MatrixStack::Reset (world stack)
    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    // Step 4: Scale44(size)
    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);

    // Step 5: RotZ44 if m_Timer != 0
    if (m_Timer != 0.0f) {
        float sinA = SinIdx(m_Timer);
        float cosA = CosIdx(m_Timer);
        mat.RotZ44(sinA, cosA);
    }

    // Step 6-7: offset = Vec3(480, 320, 0) * hudScale + pos
    Vec3 offset(480.0f * hudScale.x, 320.0f * hudScale.y, 0.0f);
    Vec3 finalPos = offset + pos;
    mat.GlobalTranslate44(finalPos);

    // Step 8-9: Upload matrices
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Step 10-11: TintColour → DrawQuadUnCached
    game->renderer.DrawQuad(m_DrawColour, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    // Step 12: UnSet
    glBindTexture(GL_TEXTURE_2D, 0);
}
