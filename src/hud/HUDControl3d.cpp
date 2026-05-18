//
// HUDControl3d::Draw — reimplemented from 0x14428c (57 lines)
// See docs/structs/hud.md for full decompilation.
//

#include "HUDControl3d.h"
#include "HUDControl.h"
#include "Game.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "math/MathUtil.h"
#include <cmath>

// Rotation speed constant (verified: DAT_001443dc = 182.0)
static const float ROT_SPEED = 182.0f;

// Matches 0x14428c (57 lines)
// CORRECTION (2026-05-18): binary draws m_SecondaryTex (+0x78), not m_Texture (+0x74).
// The 2026-04-28 ASM-verified marker was a false positive on the wrong slot.
// Binary gates: SmartPtr::operator bool on +0x78 (m_SecondaryTex), then field_0x5f != 0
// (m_DrawColour.a). Subclasses that need a primary tex (MenuButton, MissControl)
// write m_Texture and either override Draw or rely on their own draw path.
void HUDControl3d::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    if (!m_SecondaryTex.IsValid()) return;
    if (m_DrawColour.a == 0) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    m_SecondaryTex->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);

    if (m_Timer != 0.0f) {
        uint16_t idx = (uint16_t)(int)(m_Timer * ROT_SPEED);
        mat.RotZ44(SinIdx(idx), CosIdx(idx));
    }

    mat.GlobalTranslate44(pos);

    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0013540c (asm-inspector)
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour tinted = Colour::TintColour(m_DrawColour, tintRGB);
    game->renderer.DrawQuad(tinted, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    m_SecondaryTex->UnSet();
}

HUDControl3d::HUDControl3d() {
    // m_Texture / m_SecondaryTex default-construct to null SmartPtrs.
    m_Timer = 0.0f;
}

// STUB: HUDControl3d::~HUDControl3d -- binary @ 0x???? (TODO RE)
HUDControl3d::~HUDControl3d() {
}

// ASM-verified: 2026-05-06T17:00 binary @ 0x00143fc4 (asm-inspector)
// Single bx lr; does NOT chain to HUDControl::Release (port previously
// chained, removed for binary fidelity).
void HUDControl3d::Release() {}

// STUB: HUDControl3d::PreDraw -- binary @ 0x???? (TODO RE)
void HUDControl3d::PreDraw(const Vec3& hudScale) {
    (void)hudScale;
}

// STUB: HUDControl3d::Update -- binary @ 0x???? (TODO RE)
void HUDControl3d::Update(float dt) {
    HUDControl::Update(dt);
}

