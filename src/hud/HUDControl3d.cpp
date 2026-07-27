//
// HUDControl3d — base class for 3D-positioned HUD widgets.
// All methods verified zero-divergence against binary.
//

#include "HUDControl3d.h"
#include "HUDControl.h"
#include "Game.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "math/MathUtil.h"
#include <cmath>

// Rotation speed constant (0x43360000 = 182.0f; .rodata literal read by
// v1.6.1 HUDControl3d::Draw @0x0018b544)
static const float ROT_SPEED = 182.0f;

// HUD screen-anchor constants (.rodata literals read by v1.6.1
// HUDControl3d::Draw @0x0018b544):
//   0x43F00000 = 480.0f (width)
//   0x43A00000 = 320.0f (height)
//   0x00000000 = 0.0f   (z)
// Multiplied componentwise with m_HudScale and added to pos before translate.
static const float HUD_SCREEN_W = 480.0f;
static const float HUD_SCREEN_H = 320.0f;
static const float HUD_SCREEN_Z = 0.0f;

// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d::Draw @ 0x0018b544 (re-analyst)
//   - texture validity gate, m_DrawColour.a gate
//   - Scale44(size) -> optional RotZ44(SinIdx, CosIdx) -> GlobalTranslate44
//   - translate = pos + Vec3(480, 320, 0) * m_HudScale  (screen-anchor offset)
//   - TintColour(m_DrawColour, hudScale) -> DrawQuad(UVs)
//   - Texture::UnSet
void HUDControl3d::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

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

    // v1.6.1 HUDControl3d::Draw @0x0018b544: translation = pos + (Vec3(480,320,0) * m_HudScale)
    _Vector3<float> screenAnchor(HUD_SCREEN_W, HUD_SCREEN_H, HUD_SCREEN_Z);
    _Vector3<float> scaledAnchor(screenAnchor.x * m_HudScale.x,
                                 screenAnchor.y * m_HudScale.y,
                                 screenAnchor.z * m_HudScale.z);
    _Vector3<float> translation = pos + scaledAnchor;
    mat.GlobalTranslate44(translation);

    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // v1.6.1 HUDControl3d::Draw @0x0018b544: TintColour(m_DrawColour, hudScale) then DrawQuad.
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour tinted = Colour::TintColour(m_DrawColour, tintRGB);
    game->renderer.DrawQuad(tinted, m_UVLeft, m_UVRight, m_UVTop, m_UVBottom);

    m_Texture->UnSet();
}

// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d::HUDControl3d C1 @ 0x0018b72c (re-analyst)
// (C2 variant @0x0018b6dc; C1 is the one CoinCounter and friends call.)
// Structure: base ctor (implicit) → vtable (implicit) → SetNull(m_Texture) → SetNull(m_Model) → m_Timer = 0.
HUDControl3d::HUDControl3d() {
    m_Texture.SetNull();
    m_Model.SetNull();
    m_Timer = 0.0f;
}

// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d::~HUDControl3d D0 @ 0x0018b77c /
//   D1 @ 0x0018b814 / D2 @ 0x0018b8a4 (re-analyst)
// Binary body: vtable<- HUDControl3d::vtable; Release(); ~SmartPtr(m_Model);
// ~SmartPtr(m_Texture); ~HUDControl(); [operator delete for D0 variant].
// C++ auto-emits SmartPtr dtors in reverse declaration order, which matches
// the binary's order exactly (m_Model -> m_Texture). Release() is a no-op
// (bx lr) so skipping it is observationally equivalent.
HUDControl3d::~HUDControl3d() {
}

// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d::Release @ 0x0018b134 (re-analyst)
// Single bx lr; does NOT chain to HUDControl::Release.
void HUDControl3d::Release() {}

// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d::PreDraw @ 0x0018b138 (re-analyst)
// Single bx lr; no-op.
void HUDControl3d::PreDraw(float* hudScale) {
    (void)hudScale;
}

// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d::Update @ 0x0018b13c (re-analyst)
// Tail-calls HUDControl::Update(this, dt).
void HUDControl3d::Update(float dt) {
    HUDControl::Update(dt);
}

