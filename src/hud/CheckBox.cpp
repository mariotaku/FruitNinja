//
// CheckBox : HUDControl3d
// v1.6.1 CheckBox @ 0x00166a10..0x001674d8
//

#include "CheckBox.h"
#include "hud/HUDLayer.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "engine/input/Touch.h"
#include "engine/util/StringTable.h"
#include "game/GameWork.h"
#include <cstdint>

// Class-static texture SmartPtrs.
// LoadContent loads: "checked.tex" -> s_checked, "unchecked.tex" -> s_unchecked.
Mortar::SmartPtr<Mortar::Texture> CheckBox::s_checked;
Mortar::SmartPtr<Mortar::Texture> CheckBox::s_unchecked;

// Binary @ 0x00166a10
// ASM-verified: 2026-07-11T00:00Z v1.6.1 CheckBox::CheckBox(Vec3,Vec3,char const*) @ 0x00166a10 (re-analyst)
//   Field writes: m_Label(+0x80)=label, m_Checked(+0x7C)=1, Delegate0 ctor on
//   m_OnToggle(+0x94), size(+0x20), pos(+0x8), m_TouchId(+0x84)=-1, m_LayerFlags(+0x34)=0x80.
CheckBox::CheckBox(_Vector3<float> inPos, _Vector3<float> inSize, const char* label)
    : HUDControl3d()
    , m_Checked(1)
    , m_Label(label)
    , m_TouchId(-1)
    , m_TouchCapture(0.0f, 0.0f, 0.0f)
    , m_OnToggle()
{
    _pad7D[0] = _pad7D[1] = _pad7D[2] = 0;
    pos           = inPos;
    size          = inSize;
    m_LayerFlags  = Mortar::HUD_LAYER_POST_ACTOR;  // +0x34 = 0x80
}

// Binary @ 0x00166ab8
// ASM-verified: 2026-07-11T00:00Z v1.6.1 CheckBox::CheckBox(Vec3,Vec3,LocalizedString) @ 0x00166ab8 (re-analyst)
//   Resolves the string-table id via GETSTRING_CAST_0(param_4), then duplicates the
//   char* ctor body inline with the resolved string as m_Label.
CheckBox::CheckBox(_Vector3<float> inPos, _Vector3<float> inSize, LocalizedString loc)
    : HUDControl3d()
    , m_Checked(1)
    , m_Label(GETSTRING_CAST_0(loc))
    , m_TouchId(-1)
    , m_TouchCapture(0.0f, 0.0f, 0.0f)
    , m_OnToggle()
{
    _pad7D[0] = _pad7D[1] = _pad7D[2] = 0;
    pos           = inPos;
    size          = inSize;
    m_LayerFlags  = Mortar::HUD_LAYER_POST_ACTOR;  // +0x34 = 0x80
}

CheckBox::~CheckBox() {
}

// vtable Init slot -- empty in binary (bx lr), no-op is faithful.
void CheckBox::Init() {
}

// vtable Release slot -- empty in binary (bx lr), no-op is faithful.
void CheckBox::Release() {
}

// vtable PreDraw slot -- empty in binary (bx lr), no-op is faithful.
void CheckBox::PreDraw(float* hudScaleRaw) {
    (void)hudScaleRaw;
}

// Empty in binary too. Kept for vtable / shape parity.
void CheckBox::UpdateFromGameWork() {
}

// Binary @ 0x001674d8 (returns 5)
int CheckBox::GetType() {
    return 5;
}

// Binary @ 0x001667b8
// While a touch slot is held, capture the finger's PRESS position (spawn pos)
// into m_TouchCapture. Update() tests this captured position against the rect on
// release. No-op when m_TouchId == -1.
void CheckBox::UpdateTouchPosition() {
    int id = m_TouchId;
    if (id == -1) {
        return;
    }
    m_TouchCapture = game_work.m_FingerSpawnPos[id];
}

// Binary @ 0x00166c24
// Hit-rect is HARDCODED (pos.x +/- 36, pos.y +/- 28.5), NOT pos +/- size.
// Acquire a slot via TouchInRegion; hold while IsTouchDown == 2; on release
// (IsTouchDown == 0) toggle m_Checked ONLY if the captured press position is
// inside the rect, then fire m_OnToggle.
void CheckBox::Update(float dt) {
    (void)dt;

    float left   = pos.x - 36.0f;
    float right  = pos.x + 36.0f;
    float bottom = pos.y - 28.5f;
    float top    = pos.y + 28.5f;

    if (m_TouchId == -1) {
        m_TouchId = TouchInRegion(left, right, bottom, top, -1);
        if (m_TouchId != -1) {
            if (IsTouchDown(m_TouchId) == 2) {
                return;
            }
            m_TouchId = -1;
            return;
        }
        // No slot acquired -- falls through to UpdateTouchPosition (no-op, id == -1).
    } else {
        if (IsTouchDown(m_TouchId) == 0) {
            m_TouchId = -1;
            if (m_TouchCapture.x < left)   return;
            if (right < m_TouchCapture.x)  return;
            if (m_TouchCapture.y < bottom) return;
            if (top < m_TouchCapture.y)    return;
            m_Checked = (uint8_t)(m_Checked ^ 1);
            m_OnToggle();
            return;
        }
        // Still held -- falls through to UpdateTouchPosition.
    }
    UpdateTouchPosition();
}

// Binary @ 0x001672f8
// Label at (pos.x+64, pos.y+10), font game_work.m_Fonts[1] (= pFontMain), size 24,
// colour Yellow (tinted by hudScale). Quad = MatrixStack::Reset -> Scale44(128,64,1)
// HARDCODED -> translate pos -> Mesh::DrawQuadUnCached(White); texture = checked.tex
// when m_Checked else unchecked.tex.
void CheckBox::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    // Colour::Yellow (not defined in the port's Colour) = (255,255,0,255).
    Colour labelCol = Colour::TintColour(Colour(255, 255, 0, 255), tintRGB);

    if (m_Label && game_work.pFontMain.IsValid()) {
        Mortar::Utf8StringIterator iter(m_Label);
        game_work.pFontMain->DrawString(iter,
                                        pos.x + 64.0f, pos.y + 10.0f, 0.0f,
                                        labelCol, 24.0f, 0.0f, 0.0f, 1, NULL, 0.0f);
    }

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    Mortar::SmartPtr<Mortar::Texture>& texPtr = m_Checked ? s_checked : s_unchecked;
    if (!texPtr.IsValid()) return;

    texPtr->Set();

    Matrix44 mat = Matrix44::MakeScale(128.0f, 64.0f, 1.0f);
    mat.GlobalTranslate44(pos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);

    texPtr->UnSet();
}

// Binary @ 0x00167214
// DIFFERS: original loads "checked.tex" / "unchecked.tex" via LoadLocalisedTexture,
//   but neither texture is shipped in FruitNinjaBada/Data for v1.6.1 (the CheckBox
//   is dead code). The faithful names are kept; the SmartPtrs stay null when the
//   art is absent (Draw then no-ops on the quad). Port-supplied placeholder art is
//   a separate task; the visual test injects substitutes via SetTexturesForTest.
//   v1.6.1 CheckBox::LoadContent @ 0x00167214
void CheckBox::LoadContent() {
    s_checked   = Mortar::TextureManager::LoadLocalisedTexture("checked.tex");
    s_unchecked = Mortar::TextureManager::LoadLocalisedTexture("unchecked.tex");
}

void CheckBox::UnloadContent() {
    s_checked.SetNull();
    s_unchecked.SetNull();
}

// Port/test-only injector (no binary counterpart) -- see header note.
void CheckBox::SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& checked,
                                  const Mortar::SmartPtr<Mortar::Texture>& unchecked) {
    s_checked   = checked;
    s_unchecked = unchecked;
}
