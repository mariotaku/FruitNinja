//
// HUDControl3d — base class for 3D-positioned HUD widgets.
// All methods verified zero-divergence against binary; see
// tmp/symdiff/hudcontrol3d-spec.md for full RE notes.
//

#include "HUDControl3d.h"
#include "HUDControl.h"
#include "Game.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "math/MathUtil.h"
#include <cmath>

// Rotation speed constant (binary .rodata @ 0x001443dc = 0x43360000 = 182.0f)
static const float ROT_SPEED = 182.0f;

// HUD screen-anchor constants (binary .rodata @ 0x001443e0..0x001443e8):
//   0x43F00000 = 480.0f (width)
//   0x43A00000 = 320.0f (height)
//   0x00000000 = 0.0f   (z)
// Multiplied componentwise with m_HudScale and added to pos before translate.
static const float HUD_SCREEN_W = 480.0f;
static const float HUD_SCREEN_H = 320.0f;
static const float HUD_SCREEN_Z = 0.0f;

// ASM-verified: 2026-05-24 binary @ 0x0014428c (re-analyst)
//   - texture validity gate, m_DrawColour.a gate
//   - Scale44(size) -> optional RotZ44(SinIdx, CosIdx) -> GlobalTranslate44
//   - translate = pos + Vec3(480, 320, 0) * m_HudScale  (screen-anchor offset)
//   - TintColour(m_DrawColour, hudScale) -> DrawQuad(UVs)
//   - Texture::UnSet
void HUDControl3d::Draw(float* hudScaleRaw) {
    const Vec3& hudScale = *reinterpret_cast<const Vec3*>(hudScaleRaw);

    if (!m_Texture.IsValid()) {
        return;
    }
    if (m_DrawColour.a == 0) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    m_Texture->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);

    if (m_Timer != 0.0f) {
        uint16_t idx = (uint16_t)(int)(m_Timer * ROT_SPEED);
        mat.RotZ44(SinIdx(idx), CosIdx(idx));
    }

    // binary @ 0x00144328..0x0014435c: translation = pos + (Vec3(480,320,0) * m_HudScale)
    Vec3 screenAnchor(HUD_SCREEN_W, HUD_SCREEN_H, HUD_SCREEN_Z);
    Vec3 scaledAnchor(screenAnchor.x * m_HudScale.x,
                      screenAnchor.y * m_HudScale.y,
                      screenAnchor.z * m_HudScale.z);
    Vec3 translation = pos + scaledAnchor;
    mat.GlobalTranslate44(translation);

    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // binary @ 0x00144380..0x001443c0: TintColour(m_DrawColour, hudScale) then DrawQuad.
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour tinted = Colour::TintColour(m_DrawColour, tintRGB);
    game->renderer.DrawQuad(tinted, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    m_Texture->UnSet();
}

// ASM-verified: 2026-05-24 binary @ 0x00144434 (HUDControl3d::HUDControl3d)
// Structure: base ctor (implicit) → vtable (implicit) → SetNull(m_Texture) → SetNull(m_Model) → m_Timer = 0.
HUDControl3d::HUDControl3d() {
    m_Texture.SetNull();
    m_Model.SetNull();
    m_Timer = 0.0f;
}

// ASM-verified: 2026-05-24 binary @ 0x00144474 / 0x001444e0 / 0x00144548 (re-analyst)
// Binary body: vtable<- HUDControl3d::vtable; Release(); ~SmartPtr(m_Model);
// ~SmartPtr(m_Texture); ~HUDControl(); [operator delete for D0 variant].
// C++ auto-emits SmartPtr dtors in reverse declaration order, which matches
// the binary's order exactly (m_Model -> m_Texture). Release() is a no-op
// (bx lr) so skipping it is observationally equivalent.
HUDControl3d::~HUDControl3d() {
}

// ASM-verified: 2026-05-24 binary @ 0x00143fc4 (re-analyst)
// Single bx lr; does NOT chain to HUDControl::Release.
void HUDControl3d::Release() {}

// ASM-verified: 2026-05-24 binary @ 0x00143fc8 (re-analyst)
// Single bx lr; no-op.
void HUDControl3d::PreDraw(float* hudScale) {
    (void)hudScale;
}

// ASM-verified: 2026-05-24 binary @ 0x00143fcc (re-analyst)
// Tail-calls HUDControl::Update(this, dt).
void HUDControl3d::Update(float dt) {
    HUDControl::Update(dt);
}

