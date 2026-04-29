//
// HUDControl3d::Draw — reimplemented from 0x14428c (57 lines)
// See docs/structs/hud.md for full decompilation.
//

#include "HUDControl3d.h"
#include "Game.h"
#include "render/MatrixManager.h"
#include "math/MathUtil.h"
#include <cmath>

// Rotation speed constant (verified: DAT_001443dc = 182.0)
static const float ROT_SPEED = 182.0f;

// Matches 0x14428c (57 lines)
// ASM-verified: 2026-04-28T16:35Z binary @ 0x0014428c (asm-inspector)
void HUDControl3d::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Step 1: two gates matching binary order (0x1442a0..0x1442b4):
    //   gate 1 — no texture: SmartPtr::operator bool on this+0x74
    //   gate 2 — byte at this+0x5f == 0 (m_DrawColour.a in port layout)
    if (!m_Texture) return;
    if (m_DrawColour.a == 0) return;

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
    // Binary: idx = (uint16_t)(int)(timer * 182.0), then SinIdx/CosIdx
    if (m_Timer != 0.0f) {
        uint16_t idx = (uint16_t)(int)(m_Timer * ROT_SPEED);
        mat.RotZ44(SinIdx(idx), CosIdx(idx));
    }

    // Step 6-7: Binary computes `drawPos = pos + (480, 320, 0) * pivot`
    // but `pivot` is zero-initialised by CopyGlobalVec3_PauseScreen for all
    // standard controls — the offset is dead code. See
    // docs/engine/coordinate-system.md. Positions are already in the
    // centred ortho space [-240..240, -160..160].
    mat.GlobalTranslate44(pos);

    // Step 8-9: Upload matrices
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Depth test: read-only. Lets the previously-drawn 3D fruit/bomb
    // mesh occlude the ring quad pixels where it sits in front. The
    // ring (quad at button pos.z) and fruit (3D mesh translated to
    // button pos) share the same nominal z; equal-z under GL_LESS fails,
    // so any fruit pixels drawn first reject the ring at the same depth.
    // Outside the fruit's silhouette the depth buffer holds the cleared
    // far value and the ring passes. Net effect: ring renders only
    // around the fruit, fruit is visible inside.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);    // do not write — preserve fruit's depth

    // Step 10-11: TintColour → DrawQuadUnCached
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0013540c (asm-inspector)
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour tinted = Colour::TintColour(m_DrawColour, tintRGB);
    game->renderer.DrawQuad(tinted, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);

    // Step 12: UnSet
    glBindTexture(GL_TEXTURE_2D, 0);
}
