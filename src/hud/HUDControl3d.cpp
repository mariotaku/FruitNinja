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

    // Step 2: Texture::Set (m_Texture is a raw GLuint from MenuButton-loaded
    // assets, not a Mortar::Texture* -- bypasses Texture::Set so we have to
    // update the bound-texture tracker manually for Renderer::DrawQuad's guard.)
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_Texture);
    Mortar::Texture::s_LastBoundTexId = m_Texture;

    // Step 3: MatrixStack::Reset (world stack)
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    // Step 4: Scale44(size)
    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);

    // Step 5: RotZ44 if m_Timer != 0
    // Binary: idx = (uint16_t)(int)(timer * 182.0), then SinIdx/CosIdx
    if (m_Timer != 0.0f) {
        uint16_t idx = (uint16_t)(int)(m_Timer * ROT_SPEED);
        mat.RotZ44(SinIdx(idx), CosIdx(idx));
    }

    // Step 6-7: Skipping (480,320,0) * hudScale + pos; port ortho already centers on (0,0).
    mat.GlobalTranslate44(pos);

    // Step 8-9: Upload matrices
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Step 10-11: TintColour -> DrawQuadUnCached
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0013540c (asm-inspector)
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour tinted = Colour::TintColour(m_DrawColour, tintRGB);
    game->renderer.DrawQuad(tinted, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    // Step 12: UnSet
    glBindTexture(GL_TEXTURE_2D, 0);
    Mortar::Texture::s_LastBoundTexId = 0;
}

HUDControl3d::HUDControl3d()
    : m_Texture(0),
      m_SecondaryTex(0) {
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

// STUB: HUDControl3d::Draw(float*) -- binary @ 0x???? (TODO RE)
void HUDControl3d::Draw(float* viewVec) {
    (void)viewVec;
}
