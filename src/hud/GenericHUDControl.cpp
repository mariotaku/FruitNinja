// GenericHUDControl  binary @ 0x00189f60 (ctor), 0x00189ae4 (PreDraw), 0x00189ed0 (Update)

#include "hud/GenericHUDControl.h"
#include "engine/render/BakedStringBox.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "engine/util/Delegate.h"
#include "engine/math/MathUtil.h"
#include "engine/render/MatrixManager.h"
#include "engine/asset/Mesh.h"
#include "game/GameWork.h"
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// ctor  binary @ 0x00189f60
// ABI: this=r0, fadeIn=s0, fadeOut=s1, tex=r1, parentRect=r2, pos=r3,
//      scale/col/flags spill to stack.
// ---------------------------------------------------------------------------

// ASM-verified: 2026-06-13T03:20Z v1.6.1 binary @ 0x00189f60 (asm-inspector)
GenericHUDControl::GenericHUDControl(float fadeIn, float fadeOut,
                                     Mortar::SmartPtr<Mortar::Texture> tex,
                                     _Vector2<float>* parentRect,
                                     _Vector3<float> pos, _Vector3<float> scale,
                                     Colour col, int flags)
    : HUDControl3d()
    , m_Sounds()
    , m_GHCTimer(0.0f)
    , m_PosTrans()
    , m_ScaleTrans()
    , m_AngleTrans()
    , m_AlphaTrans()
    , m_PosPulse()
    , m_ScalePulse()
    , m_AnglePulse()
    , m_AlphaPulse()
    , m_BasePos(pos)
    , m_AnglePosOffA(0.0f, 0.0f, 0.0f)
    , m_AnglePosOffB(0.0f, 0.0f, 0.0f)
    , m_BaseScale(0.0f, 0.0f, 0.0f)
    , m_BasePos2(0.0f, 0.0f, 0.0f)  // binary @0x00189f60: Vec3::Zero (was 1,1,1 -> +1 pos drift on every control)
    , m_BaseAngle(0.0f)
    , m_FadeIn(fadeIn)
    , m_FadeOut(fadeOut)
    , m_pLabel(NULL)
{
    // Binary: store texture into HUDControl3d base m_Texture slot.
    m_Texture = tex;

    // Binary: if parentRect non-null, copy min/max into UV rect fields at +0x64..+0x70.
    if (parentRect) {
        m_UVLeft   = parentRect[0].x;   // min.x
        m_UVTop    = parentRect[0].y;   // min.y
        m_UVRight  = parentRect[1].x;   // max.x
        m_UVBottom = parentRect[1].y;   // max.y
    }

    // ASM-verified: 2026-07-07T00:00Z v1.6.1 GenericHUDControl ctor @0x00189f60
    //   (disasm 0x18a080-0x18a110, asm-inspector-equivalent RE):
    //   auto-scale = base * scale.z (uniform scalar multiply of the whole vector);
    //   scale.z forced to 1.0 when it is 0. Textured base.z = 0.0 (NOT scale.z) before
    //   the multiply, so resolved.z == 0.0 in the textured branch.
    _Vector3<float> resolvedScale = scale;
    if (scale.x == 0.0f && scale.y == 0.0f) {
        float sz = (scale.z == 0.0f) ? 1.0f : scale.z;
        if (tex.IsValid()) {
            float sx = (float)tex->GetWidth()  * (m_UVRight - m_UVLeft);
            float sy = (float)tex->GetHeight() * (m_UVBottom - m_UVTop);
            resolvedScale = _Vector3<float>(sx * sz, sy * sz, 0.0f); // binary: Vec3(sx,sy,0.0) * sz
        } else {
            resolvedScale = _Vector3<float>(sz, sz, sz);             // binary: Vector3::One * sz
        }
    }
    m_BaseScale = resolvedScale;

    // HUDControl base field: size (written via HUDControl3d's computed scale path)
    // Binary seeds +0x20 (size/scale) from default Vec3 in ctor.
    this->size = _Vector3<float>(1.0f, 1.0f, 1.0f);

    // +0xb4 = 1.0f (subfield of m_ScaleTrans block)
    m_ScaleTrans.f4 = 1.0f;

    // +0xd0 = 0 (subfield of m_AngleTrans block) -- already zero from ctor
    // +0x148 = 0 (subfield of m_AnglePulse) -- already zero
    // +0x160 = 0 (subfield of m_AlphaPulse) -- already zero
    // (all zeroed by value-init TranisitionInfo/PulseInfo ctors)

    // Colour
    m_DrawColour = col;

    // FLAGS stored at HUDControl::m_LayerFlags (+0x34)
    m_LayerFlags = flags;

    // Label NULL (already set in initializer list)
}

// ---------------------------------------------------------------------------
// dtor  binary @ 0x00189770 (D0) / 0x001896f4 (D1)
// ---------------------------------------------------------------------------

GenericHUDControl::~GenericHUDControl() {
    if (m_pLabel) {
        m_pLabel->~BakedStringBox();
        operator delete(m_pLabel);
        m_pLabel = NULL;
    }
}

// ---------------------------------------------------------------------------
// SetText  binary @ 0x00189858
// Owns/deletes the BakedStringBox at +0x1d4.
// ---------------------------------------------------------------------------

// ASM-verified: 2026-06-13T03:20Z v1.6.1 binary @ 0x00189858 (asm-inspector)
void GenericHUDControl::SetText(Mortar::BakedStringBox* box) {
    if (m_pLabel == box) return;
    if (m_pLabel) {
        m_pLabel->~BakedStringBox();
        operator delete(m_pLabel);
        m_pLabel = NULL;
    }
    m_pLabel = box;
}

// ---------------------------------------------------------------------------
// AddSound  binary @ 0x0018a4c4
// ABI: this=r0, startT=s0, endT=s1, name=r1.
// Demangled: AddSound(const char* name, float startT, float endT)
// ---------------------------------------------------------------------------

// ASM-verified: 2026-06-13T03:20Z v1.6.1 binary @ 0x0018a4c4 (asm-inspector)
void GenericHUDControl::AddSound(const char* name, float startT, float endT) {
    GenericHUDControlSound s(m_FadeIn, -1.0f, startT, endT, name);
    m_Sounds.push_back(s);
}

// ---------------------------------------------------------------------------
// SetAngle  binary @ 0x001899a0
// ABI: this=r0, angleDeg=s0, radius=s1.
// Demangled: SetAngle(float angleDeg, float radius)
// ---------------------------------------------------------------------------

// Binary constant DAT_189a50 = 182.04445f = 65536/360 (deg-to-uint16 scaler)
static const float kDegToIdx = 182.04445f;

// ASM-verified: 2026-06-13T03:20Z v1.6.1 binary @ 0x001899a0 (asm-inspector)
void GenericHUDControl::SetAngle(float angleDeg, float radius) {
    m_Timer   = angleDeg;   // HUDControl base +0x2c (computed angle slot)
    m_BaseAngle = angleDeg;

    if (radius > 0.0f) {
        uint16_t idx = (uint16_t)((int)(angleDeg * kDegToIdx) & 0xffff);
        if (idx == 0) idx = 1;
        _Vector3<float> dir(CosIdx(idx), SinIdx(idx), 0.0f);
        _Vector3<float> off = dir * radius;
        m_AnglePosOffA = off;
        m_AnglePosOffB = off;
    }
}

// ---------------------------------------------------------------------------
// PreDraw  binary @ 0x00189ae4  (vtable slot +0x18)
// Computes world pos/scale/angle/alpha from base fields + transition/pulse state.
// Writes HUDControl base: pos(+0x08), size(+0x20), m_Timer(+0x2c), m_DrawColour.a(+0x5f).
// ---------------------------------------------------------------------------

void GenericHUDControl::PreDraw(float* /*hudScaleRaw*/) {
    float t = m_GHCTimer;
    float fadeIn  = m_FadeIn;
    float fadeOut = m_FadeOut;

    // Fade fraction
    // ASM-spec v1.6.1 GenericHUDControl::PreDraw @0x00189ae4: equal fade-in/out
    //   -> opaque (1.0), except still 0.0 before the fade-in time.
    float frac;
    if (fadeIn == fadeOut) {
        frac = (t < fadeIn) ? 0.0f : 1.0f;
    } else {
        frac = (t - fadeIn) / (fadeOut - fadeIn);
        if (frac < 0.0f) frac = 0.0f;
        else if (frac > 1.0f) frac = 1.0f;
    }

    // Position: base + angle offsets + pos transition GetAmt + pos pulse GetPulseAmt
    // TODO: v1.6.1 GenericHUDControl::PreDraw @0x00189ae4 -- TranisitionInfo::GetAmt and PulseInfo::GetPulseAmt not yet ported;
    //   using base values only until those helpers are implemented.
    _Vector3<float> worldPos = m_BasePos + m_AnglePosOffA + m_BasePos2;
    this->pos = worldPos;

    // Scale: base scale
    // TODO: v1.6.1 GenericHUDControl::PreDraw @0x00189ae4 -- scale transition/pulse not applied (pending GetAmt/GetPulseAmt)
    this->size = m_BaseScale;

    // Angle: base + angle transition + angle pulse
    // TODO: v1.6.1 GenericHUDControl::PreDraw @0x00189ae4 -- angle transition/pulse not applied
    this->m_Timer = m_BaseAngle;

    // Alpha: mapped from fade fraction through alpha transition
    // TODO: v1.6.1 GenericHUDControl::PreDraw @0x00189ae4 -- alpha transition/pulse not applied; use linear fade
    uint8_t alpha = (uint8_t)(int)(frac * 255.0f);
    this->m_DrawColour.a = alpha;
}

// ---------------------------------------------------------------------------
// DrawOrder  binary @ 0x00189a58  (vtable slot +0x24)
// Draws the textured quad (HUDControl3d::Draw) AND, when a label is set, the
// BakedStringBox label at the computed pos.
// ---------------------------------------------------------------------------

void GenericHUDControl::DrawOrder(float* hudScaleRaw, int layerMask) {
    (void)layerMask;
    Draw(hudScaleRaw);
    // ASM-spec v1.6.1 GenericHUDControl::DrawOrder @0x00189a58: if m_pLabel,
    //   m_pLabel->SetTranslation(pos, 0); m_pLabel->Draw(Vec2(size.z,size.z), m_Timer, 1).
    // Translation = HUDControl::pos (+0x08), rotation = m_Timer (+0x2c, the PreDraw
    // angle), scale = Vec2(size.z, size.z) (+0x28, both components), center = 1.
    // Label alpha is plumbed via BakedStringBox::SetColour at the setup side, not here.
    if (m_pLabel) {
        m_pLabel->SetTranslation(this->pos, 0);
        float s = this->size.z;
        m_pLabel->Draw(_Vector2<float>(s, s), this->m_Timer, 1);
    }
}

// ---------------------------------------------------------------------------
// Update  binary @ 0x00189ed0  (vtable slot +0x28)
// Advances timer and fires one-shot sounds.
// ---------------------------------------------------------------------------

void GenericHUDControl::Update(float dt) {
    float prevTimer = m_GHCTimer;
    m_GHCTimer += dt;

    // Iterate sound list: call Sound::Play equivalent; erase on one-shot trigger.
    std::vector<GenericHUDControlSound>::iterator it = m_Sounds.begin();
    while (it != m_Sounds.end()) {
        GenericHUDControlSound& s = *it;
        // Binary Sound::Play(curTimer, prevTimer, &s):
        // fires when prevTimer < startT && curTimer >= startT (crossing trigger).
        if (!s.m_Played &&
            prevTimer < s.m_StartT && m_GHCTimer >= s.m_StartT &&
            s.m_Name && s.m_Name[0] != '\0') {
            // v1.6.1 GenericHUDControl::Sound::Play @0x00189d74: SFXPlay runs with no
            // null test on the GOT-resolved game_work.mGameSound.
            game_work.mGameSound->SFXPlay(s.m_Name, 1.0f, 1.0f,
                Mortar::Delegate1<bool, Mortar::MortarSound*>());
            s.m_Played = true;
            it = m_Sounds.erase(it);
        } else {
            ++it;
        }
    }
}
