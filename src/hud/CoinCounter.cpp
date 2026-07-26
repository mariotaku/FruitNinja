// v1.6.1 CoinCounter @0x0016765c

#include "CoinCounter.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "game/GameWork.h"
#include <cstring>

// ctor @ v1.6.1 0x0016765c
// DIFFERS: original ctor (v1.6.1 CoinCounter::CoinCounter @0x0016765c) writes only
//   0.0f -> +0x80/+0x88/+0x90, 0 -> +0x7c (strh)/+0x84, 1 -> +0x4, leaving +0x8c
//   (m_AnimScale) and +0x94.. (m_CountText) UNINITIALISED; port zeroes them so the
//   dead `m_AnimScale > 0` Draw gate is deterministic.
CoinCounter::CoinCounter()
    : m_Flags(0)
    , _pad7E{0, 0}
    , m_Field80(0.0f)
    , m_CoinCount(0)
    , m_Field88(0.0f)
    , m_AnimScale(0.0f)
    , m_ScaleReset(0.0f)
{
    std::memset(m_CountText, 0, sizeof(m_CountText));
    // v1.6.1 CoinCounter::CoinCounter @0x001676a4: mov r3,#1; strb r3,[r4,#0x4]
    // (HUDControl::m_Singular, +0x4). Without this HUD::SetToMultiplayerState()
    // (via Game::TellGameToStart) sweeps the CoinCounter at game start, same
    // bug class as MissControl.cpp:104-107.
    m_Singular = 1;
}

// dtor @ v1.6.1 0x001675f4
CoinCounter::~CoinCounter() {}

// Init @ v1.6.1 0x00167568 — binary body is an empty no-op (immediate return)
void CoinCounter::Init() {}

// Update @ v1.6.1 0x001675e8: no-op
void CoinCounter::Update(float dt) { (void)dt; }

// Reset @ v1.6.1 0x00167574
// ASM-spec v1.6.1 CoinCounter::Reset @ 0x00167574:
//   clamp m_AnimScale(+0x8C) to [0,1]; m_ScaleReset(+0x90) = 1.0f.
//   Disasm: vldr s15,[r0,#0x8c]; clamp path; vstr s14(=1.0),[r0,#0x90].
void CoinCounter::Reset() {
    if (m_AnimScale < 0.0f) m_AnimScale = 0.0f;
    if (m_AnimScale > 1.0f) m_AnimScale = 1.0f;
    m_ScaleReset = 1.0f;
}

// Draw @ v1.6.1 0x00167730 — ported for shape fidelity only; the gate never
// fires in v1.6.1 (see Defunct marker in CoinCounter.h), so this body is
// unreachable. Unlike HUDControl3d::Draw there is NO m_Texture.IsValid() gate
// and NO m_DrawColour.a gate, no 480/320 screen anchor, no m_HudScale, no RotZ.
// game_work.pFontReserved1 (binary pM_Fonts[3]) is NULL in practice — no
// port-invented null guard, faithful to the binary.
void CoinCounter::Draw(float* hudScaleRaw) {
    if (m_AnimScale > 0.0f) {
        const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
        mat.GlobalTranslate44(pos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        m_Texture->Set();
        // TintWhite @0x00167d20: Colour from clamped hudScale RGB, alpha 255.
        Colour tint = Colour::TintWhite(hudScale.x, hudScale.y, hudScale.z);
        Mortar::Mesh::DrawQuadUnCached(tint, NULL);
        m_Texture->UnSet(true);

        _Vector3<float> textPos = pos;
        textPos.x -= 15.0f;  // imm 0x41700000
        // alignment 0x0E passed raw from the binary. Under the port's decode
        // (Font.cpp @0x0024c7f0): &3==2 -> RIGHT; &0xC!=0 fires the vertical
        // translate with bit 0x4 set -> 0.5 factor (MIDDLE); the extra 0x8
        // (BOTTOM) bit is inert once 0x4 is set. Not renamed to a flag combo
        // because MIDDLE|BOTTOM doubled bits have no named meaning.
        game_work.pFontReserved1->DrawString(
            /*scale*/30.0f /*0x41f00000*/, /*yLineFactor*/1.0f, /*rotZ*/0.0f,
            Mortar::Utf8StringIterator(m_CountText), textPos, Colour::White,
            _Vector2<float>::Zero(), /*alignment*/0x0E, /*z*/0.0f, /*clipRect*/NULL);
    }
}

// Release @ v1.6.1 0x0016756c — binary body is an empty no-op (immediate return)
void CoinCounter::Release() {}

// PreDraw @ v1.6.1 0x001675e4 — binary body is an empty no-op (immediate return)
void CoinCounter::PreDraw(float* hudScale) { (void)hudScale; }

// Skip @ v1.6.1 0x001675e8 — binary body is an empty no-op (immediate return)
void CoinCounter::Skip() {}
