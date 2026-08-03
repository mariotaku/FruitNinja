//
// BSButton : HUDControl3d (size = 0xe8)
// Binary ctor @ 0x15eb58
// Binary Init  @ 0x15ea40
// Binary Release @ 0x15e5c0
// Binary PreDraw @ 0x15e468
// Binary Draw  @ 0x15e60c
// Binary Update @ 0x15e470
//

#include "BSButton.h"
#include "render/BakedStringBox.h"
#include "render/Font.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/MatrixManager.h"
#include "engine/asset/Mesh.h"
#include "engine/input/Touch.h"
#include "engine/math/MathUtil.h"
#include "engine/math/_Vector2.h"
#include "engine/math/_Vector3.h"
#include "engine/math/Colour.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
// Shared TTF face for BSButton BakedStringBox labels.
// Binary: reads *(g_GameData+0x614) -- the shared localized TTF face loaded once
//   by PreloadFontsTTF @0x0011c1fc (gangofchinese.ttf or arabic.ttf).
// Port: returns game_work.m_pTTFFontMain (populated at GameInitialise time by
//   PreloadFontsTTF). Falls back to a lazy local load if somehow null.
// Port specific: the binary reads +0x614 unconditionally. The null branch below is a
//   port-only safety net with no binary counterpart.
static Mortar::FontCacheObjectTTF* GetSharedTTFFont() {
    if (game_work.m_pTTFFontMain) return game_work.m_pTTFFontMain;
    static Mortar::SmartPtr<Mortar::Font> s_TTFFont =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_TTFFont.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_TTFFont.Get());
}
} // namespace

// BSButton::BSButton(Vec3 pos, char const* label, Vec3 textOffset)
// Binary @ 0x15eb58
// Sequence:
//   1. HUDControl3d::HUDControl3d(this, pos, label)  (base ctor, 0x18b6dc)
//   2. set vptr = 0x2ccaf8
//   3. SmartPtr<Texture> ctor @ +0x7c (null)
//   4. store label (char const*) @ +0x80
//   5. Vec3 ctor @ +0xa4 from textOffset
//   6. Delegate0 ctor @ +0xc0
//   7. +0xe6 = 0xff
//   8. +0xb0 = 0.0f
//   9. copy pos Vec3 -> +0x08
//  10. +0xe4 = 1, +0xe5 = 0
BSButton::BSButton(_Vector3<float> pos, const char* label, _Vector3<float> textOffset)
    : HUDControl3d()
    , m_Texture2()
    , m_pLabel(label)
    , m_pLabelBox(0)
    , m_TouchId(-1)
    , m_TouchX(0.0f)
    , m_TouchY(0.0f)
    , m_ExtentX(0.0f)
    , m_ExtentY(0.0f)
    , m_TextOffset(textOffset)
    , m_DrawRotation(0.0f, 0.0f, 0.0f)
    , m_ClickCallback()
    , m_bEnabled(1)
    , m_bPressed(0)
    , m_AlphaOverride(0xff)
{
    // Step 9: copy pos -> +0x08 (the base HUDControl pos field)
    this->pos = pos;
    // Step 8: +0xb0 = 0.0f (already initialised by Vec3(0,0,0) above, confirm x=0)
    m_DrawRotation.x = 0.0f;
    // Steps 10: +0xe4=1, +0xe5=0 already set by member-initialiser list above.
    // +0xe6=0xff already set by member-initialiser list above.
}

// BSButton::~BSButton (D1 dtor, slot 1)
// Binary @ 0x15e888
// Sequence:
//   destroy Delegate0 @ +0xc0 (0x1105e4)
//   Release-cleanup (0x104634)
//   destroy texture SmartPtr @ +0x7c (0x104d04)
//   HUDControl3d::~ (0x10e808)
//   dealloc (0x108ecc)
BSButton::~BSButton() {
    m_ClickCallback = nullptr;
    m_Texture2.SetPtr(nullptr);
}

// BSButton::Init  (slot 2)
// Binary @ 0x15ea40
void BSButton::Init() {
    // SmartPtr<Texture>::SetPtr(&tex@+0x7c, null)  -- 0x104fb0
    m_Texture2.SetPtr(nullptr);

    // this->+0x88 = -1  (active touch id = none)
    m_TouchId = -1;

    // this->+0x34 = 0x80  (drawOrder = 128)
    m_LayerFlags = 0x80;

    // StackAllocatedPointer<Delegate0::BaseDelegate,32>::Delete(&click@+0xc0) -- 0x10af70
    m_ClickCallback = nullptr;

    // box = operator new(200=0xc8)
    Mortar::BakedStringBox* box = new Mortar::BakedStringBox(
        // font = *(g_GameData + 0x614) -- shared gangofchinese.ttf face.
        GetSharedTTFFont(),
        // Binary ctor args: s0=0x41200000 (fontSize=10.0f), then 0x50,0x28,0xf,1,3.
        10.0f,    // fontSize
        0x50,     // width  = 80
        0x28,     // height = 40
        (Mortar::ALIGNMENT_TYPE)0xf,      // align flags
        1,        // maxLines
        3         // lineSpacing (binary 7th arg = 3; step = (int)(10+3) = 13px)
    );
    this->m_pLabelBox = box;

    // BakedStringBox::SetText(box, this->+0x80 /*label*/)  -- 0x1055b4
    box->SetText(m_pLabel);

    // BakedStringBox::SetHorizontalLineSpacing(box, -1)  -- 0x10f074
    box->SetHorizontalLineSpacing(-1);

    // Colour col(0x32,0x32,0x32)  i.e. (50,50,50)
    Colour col(0x32, 0x32, 0x32, 0xff);
    // BakedStringBox::SetColour(box, col, 0)  -- 0x15e5f0 -> 0x11551c
    box->SetColour(col, 0);

    // Vec3 v(DAT_15eb4c, DAT_15eb4c, DAT_15eb4c)  (DAT_15eb4c = 0.0f)
    // this->+0xb0 = v.x; +0xb4 = v.y; +0xb8 = v.z  -- text-offset Vec3 = (0,0,0)
    m_DrawRotation = _Vector3<float>(0.0f, 0.0f, 0.0f);
}

// BSButton::Release  (slot 3)
// Binary @ 0x15e5c0
// Destroys label box @ +0x84: BakedStringBox::~ (0x11686c), delete (0x108ecc), zero +0x84
void BSButton::Release() {
    if (m_pLabelBox) {
        delete m_pLabelBox;
        m_pLabelBox = nullptr;
    }
}

// BSButton::PreDraw  (slot 6)
// Binary @ 0x15e468: bx lr  (empty no-op)
void BSButton::PreDraw(float* hudScale) {
    (void)hudScale;
}

// BSButton::Draw  (slot 7)
// Binary @ 0x15e60c
void BSButton::Draw(float* hudScaleRaw) {
    (void)hudScaleRaw;

    // Gated on m_bEnabled (+0xe4); disabled buttons draw nothing.
    if (m_bEnabled == 0) {
        return;
    }

    MatrixManager& mm = MatrixManager::GetInstance();

    // Reset the world matrix stack, then bind this button's texture (slot +0xc).
    mm.GetWorldStack().Reset();
    m_Texture2->SetUnCached();

    // Base scale comes from the button's own extents (+0x98 / +0x9c), not hudScale.
    Matrix44 mat = Matrix44::MakeScale(_Vector3<float>(m_ExtentX, m_ExtentY, 1.0f));

    // White tint copied from the shared global Colour @ DAT_15e884 (== 0x34e2f8,
    // the engine's "white" mod-colour written by SlashEntity::InitModColours).
    Colour drawColour(255, 255, 255, 255);

    if (m_bPressed != 0) {
        // Pressed-grow: rebuild the scale matrix at DAT_15e874 (= 0.95f) and dim
        // the tint to 50% per channel (TintColour with {0.5,0.5,0.5}).
        mat = Matrix44::MakeScale(_Vector3<float>(m_ExtentX * 0.95f, m_ExtentY * 0.95f, 1.0f));
        const float kDim[3] = { 0.5f, 0.5f, 0.5f };
        drawColour = Colour::TintColour(drawColour, kDim);
    }

    // Optional Z-rotation: m_DrawRotation.x (+0xb0) is degrees; DAT_15e878 (= 182.0f)
    // converts degrees -> 16-bit angle index for SinIdx/CosIdx.
    if (m_DrawRotation.x != 0.0f) {
        const uint16_t idx = (uint16_t)((int)(m_DrawRotation.x * 182.0f));
        mat.RotZ44(SinIdx(idx), CosIdx(idx));
    }

    mat.GlobalTranslate44(pos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawQuadUnCached(drawColour, NULL);

    // Unbind the texture (slot +0x10).
    m_Texture2->UnSetUnCached();

    // Draw the label box at pos + the label offset Vec3 stored at +0xb4
    // (m_DrawRotation.y/.z + padding; the trailing word is zero padding).
    if (m_pLabelBox) {
        _Vector3<float> labelOffset(m_DrawRotation.y, m_DrawRotation.z, 0.0f);
        m_pLabelBox->SetTranslation(pos + labelOffset, 0);
        m_pLabelBox->Draw(_Vector2<float>(1.0f, 1.0f), m_DrawRotation.x, 1);
    }
}

// BSButton::UpdateTouchPosition  (free thiscall helper)
// Binary @ 0x15e428 (thunked via 0x10b32c -> PTR @ 0x2d3f08).
// Sequence:
//   slot = this->m_TouchId (+0x88); if slot == -1, return.
//   rec  = touchTableBase + slot*0xc + 0xa4   (legacy 16-slot input table)
//   this->+0x8c = rec[0]; +0x90 = rec[1]; +0x94 = rec[2]   (3 words: x, y, phase/z)
// DIFFERS: original reads the legacy 16-slot Mortar input table (GOT global,
//   base resolved to 0x2d931c; records {x,y,phase} stride 0xc at +0xa4). That
//   BSS table has zero live writers in this binary (see Touch.h note); the
//   single live touch source is Mortar::Touch::states1, which the port's
//   TouchInRegion/IsTouchDown already read. So we read currX/currY from the
//   latched slot via Touch::GetSlot, which is numerically the same live data.
//   Binary copies a 3rd word into +0x94 (z/phase); the port only needs X/Y for
//   the Update() hit-test (m_TouchX/m_TouchY), so the 3rd word (_pad94) is
//   intentionally not mirrored.
void BSButton::UpdateTouchPosition() {
    if (m_TouchId == -1) {
        return;
    }
    const Mortar::TouchState* s = Mortar::Touch::GetInstance().GetSlot(m_TouchId);
    if (s == 0) {
        return;
    }
    m_TouchX = (float)s->currX;
    m_TouchY = (float)s->currY;
}

// BSButton::Update  (slot 10)
// Binary @ 0x15e470
// Touch hit-test state machine: TouchInRegion, IsTouchDown, fire click Delegate0 on release.
void BSButton::Update(float dt) {
    (void)dt;

    // Region computed each frame from pos + extents.
    // right  = pos.x + m_ExtentX * 0.5f
    // left   = pos.x + m_ExtentX * (-0.5f)
    // bottom = pos.y + m_ExtentY * (-0.5f)
    // top    = pos.y + m_ExtentY * 0.5f
    float cx = pos.x;
    float cy = pos.y;
    float hx = m_ExtentX;
    float hy = m_ExtentY;
    float right  = cx + hx *  0.5f;
    float left   = cx + hx * -0.5f;
    float bottom = cy + hy * -0.5f;
    float top    = cy + hy *  0.5f;

    if (m_TouchId == -1) {
        // Not currently tracking a touch.
        int id = TouchInRegion(left, right, bottom, top, -1);
        m_TouchId = id;
        if (id == -1) {
            UpdateTouchPosition();
            return;
        }
        // Check for just-pressed (phase == 2).
        if (IsTouchDown(id) != 2) {
            m_TouchId = -1;
        }
    } else {
        int state = IsTouchDown(m_TouchId);
        if (state == 0) {
            // Touch released.
            m_TouchId = -1;
            // If release point still inside region AND enabled: fire click.
            if (m_TouchX >= left && m_TouchX <= right &&
                m_TouchY >= bottom && m_TouchY <= top &&
                m_bEnabled != 0) {
                m_ClickCallback();
                m_bPressed = 0;
            }
        } else {
            // Still held.
            UpdateTouchPosition();
            // +0xe5 = (touch point inside region) ? 1 : 0  (highlight while pressed)
            m_bPressed = (m_TouchX >= left && m_TouchX <= right &&
                          m_TouchY >= bottom && m_TouchY <= top) ? 1 : 0;
        }
    }
}

// BSButton::SetVisible  (slot 16, new virtual)
// Binary @ 0x15f3ec: strb r1,[r0,#0xe4]
void BSButton::SetVisible(bool v) {
    m_bEnabled = v ? 1 : 0;
}

// BSButton::SetDrawOrder  (slot 17, new virtual)
// Binary @ 0x15f3f4: str r1,[r0,#0x34]
void BSButton::SetDrawOrder(int order) {
    m_LayerFlags = order;
}

// BSButton::SetCallback
// ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc: stores Delegate0 into m_ClickCallback (+0xc0).
void BSButton::SetCallback(Mortar::Delegate0<void> cb) {
    m_ClickCallback = cb;
}

// BSButton::SetTexture  binary @ 0x0015ee34
// ASM-spec v1.6.1 BSButton::SetTexture @0x0015ee34: m_Texture2=tex; if(param2) UpdateBoundsToTex();
void BSButton::SetTexture(Mortar::SmartPtr<Mortar::Texture> tex, bool updateBounds) {
    m_Texture2 = tex;
    if (updateBounds) UpdateBoundsToTex();
}

// BSButton::UpdateBoundsToTex  binary @ 0x0015ede8
// ASM-spec v1.6.1 BSButton::UpdateBoundsToTex @0x0015ede8:
//   if (!m_Texture) return;
//   m_ExtentX = texW(+0x24) * m_TextOffset.x;
//   m_ExtentY = texH(+0x28) * m_TextOffset.y;
void BSButton::UpdateBoundsToTex() {
    if (!m_Texture2.IsValid()) return;
    m_ExtentX = (float)m_Texture2->GetWidth()  * m_TextOffset.x;
    m_ExtentY = (float)m_Texture2->GetHeight() * m_TextOffset.y;
}

// BSButton::SetPosition
// ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc: writes to base pos field (+0x08).
void BSButton::SetPosition(const _Vector3<float>& p) {
    pos = p;
}

// BSButton::SetTextOffset
// ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc: the offset Vec3 (-29,3) is
//   stored at m_DrawRotation.y (+0xb4) and m_DrawRotation.z (+0xb8). Draw uses
//   Vec3(m_DrawRotation.y, m_DrawRotation.z, 0) as the label translate, so the
//   offset's X (the -29 horizontal pull onto the bomb) must land in .y and the
//   Y (3) in .z -- i.e. take o.x/o.y, NOT o.y/o.z. (The old o.y/o.z mapping
//   dropped the -29, leaving "QUIT" off-screen to the right of the bomb.)
void BSButton::SetTextOffset(const _Vector3<float>& o) {
    m_DrawRotation.y = o.x;
    m_DrawRotation.z = o.y;
}

// ASM-spec v1.6.1 DefaultCreateButtonDelegate @0x15f52c
// Default "create button" predicate for Mortar::Delegate1<bool,float>; always true.
bool DefaultCreateButtonDelegate(float /*arg*/) {
    return true;
}
