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
#include "engine/input/Touch.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include <cstdio>
#include <cstdlib>
#include <new>

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
BSButton::BSButton(Vec3 pos, const char* label, Vec3 textOffset)
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
        // font = *(GLOBAL[0x614])  (global font registry)
        // TODO: 0x15ea40 -- resolve *(GOT+0x614) global FontCacheObjectTTF* for BakedStringBox ctor
        nullptr,
        // size=0x50 (80.0f as float scaled by param? -- spec gives 0x50=80), w=0x28 (40.0f)
        // but spec says scale=10.0f, size=0x50, w=0x28, flags=0xf, b=1, c=3
        // BakedStringBox(font, scale, size, w, flags, b, c) -> (font, fontSize, width, height, align, wrapMode, lineSpacing)
        // per spec: size=0x50(80), w=0x28(40), flags=0xf(15), b=1, c=3, scale=10.0f
        // mapping: fontSize=10.0f, width=0x50(80), height=0x28(40), align=0xf, wrapMode=1, lineSpacing=3
        10.0f,    // fontSize (scale in spec = 10.0f / 0x41200000)
        0x50,     // width = 80
        0x28,     // height = 40
        0xf,      // flags / align
        1,        // b / wrapMode
        3         // c / lineSpacing
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
    m_DrawRotation = Vec3(0.0f, 0.0f, 0.0f);
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
void BSButton::PreDraw(const Vec3& hudScale) {
    (void)hudScale;
}

// BSButton::Draw  (slot 7)
// Binary @ 0x15e60c
// TODO: 0x15e60c -- Draw body: DAT_15e874 (pressed-grow scale factor),
//   DAT_15e878 (angle->SinIdx/CosIdx index scale), DAT_15e884 (default Colour struct),
//   MatrixStack::Reset/Scale44/RotZ44/GlobalTranslate44/_UploadCurrentMatrices sequence,
//   Mesh::DrawQuadUnCached, SinIdx/CosIdx calls, texture bind/unbind (vtable[+0xc]/[+0x10]),
//   BakedStringBox::SetTranslation and BakedStringBox::Draw at end.
void BSButton::Draw(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    (void)layerMask;
    // Not yet ported -- Draw constants not decoded in this pass.
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
        int id = Mortar::TouchInRegion(left, right, bottom, top, -1);
        m_TouchId = id;
        if (id == -1) {
            // TODO: 0x10b32c -- UpdateTouchPosition free function (writes +0x8c/+0x90).
            //   Port needs this function ported to update m_TouchX/m_TouchY from live slot.
            return;
        }
        // Check for just-pressed (phase == 2).
        if (Mortar::IsTouchDown(id) != 2) {
            m_TouchId = -1;
        }
    } else {
        int state = Mortar::IsTouchDown(m_TouchId);
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
            // TODO: 0x10b32c -- UpdateTouchPosition free function: update m_TouchX/m_TouchY.
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
