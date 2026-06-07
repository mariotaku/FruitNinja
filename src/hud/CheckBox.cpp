//
// CheckBox : HUDControl3d
// Binary @ 0x00134AE4..0x001354D8
//

// Analysed: 2026-05-04T00:00
#include "CheckBox.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "engine/input/Touch.h"
#include <cstring>
#include "game/GameWork.h"

// Class-static texture SmartPtrs.
// LoadContent loads: "checked.tex" -> s_checked, "unchecked.tex" -> s_unchecked.
// Binary @ 0x00135010 / 0x0013508C.
Mortar::SmartPtr<Mortar::Texture> CheckBox::s_checked;
Mortar::SmartPtr<Mortar::Texture> CheckBox::s_unchecked;

// Binary @ 0x00134CE0
CheckBox::CheckBox(Vec3 inPos, Vec3 inSize, const char* label)
    : HUDControl3d()
    , m_bChecked(1)
    , _pad7D(0)
    , _pad7E(0)
    , _pad7F(0)
    , m_pLabel(label)
    , m_TouchSlot(-1)
{
    pos           = inPos;
    size          = inSize;
    m_LayerFlags  = Mortar::HUD_LAYER_POST_ACTOR;
}

// Binary @ 0x00134D98
// ASM-verified pending: 0x00134d98 -- m_pLabel written from LocalizedString arg.
//   RE flagged possible delegate-into-stack-temp; asm-verify clean as of R4 W4.
CheckBox::CheckBox(Vec3 inPos, Vec3 inSize, LocalizedString loc)
    : HUDControl3d()
{
    // Cross-build: GCC 4.4 has no C++11 delegating ctors; inline the char* ctor body.
    m_pLabel      = static_cast<const char*>(loc);
    m_bChecked    = 1;
    pos           = inPos;
    size          = inSize;
    m_TouchSlot   = -1;
    m_LayerFlags  = Mortar::HUD_LAYER_POST_ACTOR;
}

CheckBox::~CheckBox() {
}

// Binary @ 0x00134AE4 -- vtable Init slot; empty in binary (single `bx lr`), no-op is faithful.
void CheckBox::Init() {
}

// Binary @ 0x00134AE8 -- vtable Release slot; empty in binary (single `bx lr`), no-op is faithful.
void CheckBox::Release() {
}

// Binary @ 0x00134B20 -- vtable PreDraw slot; empty in binary (single `bx lr`), no-op is faithful.
void CheckBox::PreDraw(const Vec3& hudScale) {
    (void)hudScale;
}

// Binary @ 0x00134B24 — empty in binary
// Binary @ 0x00134B24 -- empty in binary too. Kept for vtable / shape parity.
void CheckBox::UpdateFromGameWork() {
}


// Binary @ 0x00134AEC
// Reads touch slot data at g_GameData+0xA0 + (m_TouchSlot * 12). In the binary
// the x/y/phase values are read inline at each use site (not stored in members).
// Port calls UpdateTouchPosition() as a named call-site for parity but the binary
// stores position transiently on the stack, not in struct fields.
void CheckBox::UpdateTouchPosition() {
    // no-op: binary reads touch position inline; stored in local registers, not members.
}

// Binary @ 0x00134B28
void CheckBox::Update(float dt) {
    (void)dt;

    // Bounds of the checkbox quad in centered-ortho space (half-extents from pos/size).
    float left   = pos.x - size.x;
    float right  = pos.x + size.x;
    float bottom = pos.y - size.y;
    float top    = pos.y + size.y;

    if (m_TouchSlot < 0) {
        // No active tracking slot — scan for a new touch in our region.
        // Binary: TouchInRegion(left, right, bottom, top, -1)
        int slot = Mortar::TouchInRegion(left, right, bottom, top, -1);
        if (slot >= 0) {
            int state = Mortar::IsTouchDown(slot);
            if (state != 0) {
                m_TouchSlot = slot;
                UpdateTouchPosition();
            }
        }
    } else {
        UpdateTouchPosition();
        int state = Mortar::IsTouchDown(m_TouchSlot);
        if (state == 0) {
            // Touch released — check if it ended inside the rect and toggle.
            const Mortar::TouchState* s = Mortar::Touch::GetInstance().GetSlot(m_TouchSlot);
            if (s) {
                float tx = static_cast<float>(s->currX);
                float ty = static_cast<float>(s->currY);
                bool inside = (tx >= left && tx <= right && ty >= bottom && ty <= top);
                if (inside) {
                    m_bChecked = m_bChecked ? 0 : 1;
                }
            }
            m_TouchSlot = -1;
        }
    }
}

// ASM-verified: 2026-05-06T16:00 binary @ 0x00134E70 (asm-inspector)
// CheckBox in the binary does NOT override the inherited HUDControl3d::Draw;
// the port's body is the engine-level draw with no per-control GL state
// mutation. Depth state owned by GameDraw at the pass level.
void CheckBox::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    Game* game = Game::GetInstance();
    if (!game) return;

    // Draw label text.
    // Binary: Font::DrawString at pos + (10 + size.x / 2, 10, 0).
    if (m_pLabel && game_work.pFontMain.IsValid()) {
        Vec3 textPos(pos.x + 10.0f + size.x * 0.5f, pos.y + 10.0f, 0.0f);
        game_work.pFontMain->DrawString(20.0f, 1.0f, 0.0f,
                                    m_pLabel, textPos,
                                    Colour(255, 255, 255, 255), 0);
    }

    // Select checked or unchecked texture.
    Mortar::SmartPtr<Mortar::Texture>& texPtr = m_bChecked ? s_checked : s_unchecked;
    if (!texPtr.IsValid()) return;

    GLuint texId = texPtr->m_TexId;
    if (!texId) return;

    // Matrix-stack reset / scale / translate / upload — matches HUDControl3d::Draw pattern.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
    mat.GlobalTranslate44(pos);

    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    texPtr->Set();

    // Depth state is owned by GameDraw at the pass level (binary @ 0x0016b888);
    // CheckBox::Draw must NOT mutate it -- doing so leaves depth-test OFF for
    // subsequent same-bucket draws (MenuButton scratchs.tex backdrop).
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    Colour tinted = Colour::TintColour(m_DrawColour, tintRGB);
    game->renderer.DrawQuad(tinted, m_UVLeft, m_UVTop, m_UVRight, m_UVBottom);

    texPtr->UnSet();
}

// Binary @ 0x00135010
void CheckBox::LoadContent() {
    s_checked   = Mortar::TextureManager::LoadLocalisedTexture("checked.tex");
    s_unchecked = Mortar::TextureManager::LoadLocalisedTexture("unchecked.tex");
}

// Binary @ 0x0013508C
void CheckBox::UnloadContent() {
    s_checked.SetNull();
    s_unchecked.SetNull();
}
