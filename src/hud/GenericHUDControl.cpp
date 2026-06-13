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

// ASM-verified: 2026-06-13T03:20Z binary @ 0x00189f60 (asm-inspector)
GenericHUDControl::GenericHUDControl(float fadeIn, float fadeOut,
                                     Mortar::SmartPtr<Mortar::Texture> tex,
                                     Vec2* parentRect,
                                     Vec3 pos, Vec3 scale,
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
    , m_BaseScale(scale)
    , m_BasePos2(1.0f, 1.0f, 1.0f)
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

    // Binary: position auto-placement when pos==(0,0,0) branch omitted;
    // callers always supply an explicit non-zero pos per call-site evidence.
    // TODO: 0x00189f60 -- pos==(0,0,0) auto-placement branch (tex-scale multiply)

    // HUDControl base field: size (written via HUDControl3d's computed scale path)
    // Binary seeds +0x20 (size/scale) from default Vec3 in ctor.
    this->size = Vec3(1.0f, 1.0f, 1.0f);

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

// ASM-verified: 2026-06-13T03:20Z binary @ 0x00189858 (asm-inspector)
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

// ASM-verified: 2026-06-13T03:20Z binary @ 0x0018a4c4 (asm-inspector)
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

// ASM-verified: 2026-06-13T03:20Z binary @ 0x001899a0 (asm-inspector)
void GenericHUDControl::SetAngle(float angleDeg, float radius) {
    m_Timer   = angleDeg;   // HUDControl base +0x2c (computed angle slot)
    m_BaseAngle = angleDeg;

    if (radius > 0.0f) {
        uint16_t idx = (uint16_t)((int)(angleDeg * kDegToIdx) & 0xffff);
        if (idx == 0) idx = 1;
        Vec3 dir(CosIdx(idx), SinIdx(idx), 0.0f);
        Vec3 off = dir * radius;
        m_AnglePosOffA = off;
        m_AnglePosOffB = off;
    }
}

// ---------------------------------------------------------------------------
// PreDraw  binary @ 0x00189ae4  (vtable slot +0x18)
// Computes world pos/scale/angle/alpha from base fields + transition/pulse state.
// Writes HUDControl base: pos(+0x08), size(+0x20), m_Timer(+0x2c), m_DrawColour.a(+0x5f).
// ---------------------------------------------------------------------------

void GenericHUDControl::PreDraw(const Vec3& /*hudScale*/) {
    float t = m_GHCTimer;
    float fadeIn  = m_FadeIn;
    float fadeOut = m_FadeOut;

    // Fade fraction: clamp01((t - fadeIn) / (fadeOut - fadeIn))
    float frac = 0.0f;
    if (fadeOut != fadeIn) {
        frac = (t - fadeIn) / (fadeOut - fadeIn);
    }
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    // Position: base + angle offsets + pos transition GetAmt + pos pulse GetPulseAmt
    // TODO: 0x00189ae4 -- TranisitionInfo::GetAmt and PulseInfo::GetPulseAmt not yet ported;
    //   using base values only until those helpers are implemented.
    Vec3 worldPos = m_BasePos + m_AnglePosOffA + m_BasePos2;
    this->pos = worldPos;

    // Scale: base scale (m_BasePos2 as scale seed from ctor default Vec3)
    // TODO: 0x00189ae4 -- scale transition/pulse not applied (pending GetAmt/GetPulseAmt)
    this->size = m_BaseScale;

    // Angle: base + angle transition + angle pulse
    // TODO: 0x00189ae4 -- angle transition/pulse not applied
    this->m_Timer = m_BaseAngle;

    // Alpha: mapped from fade fraction through alpha transition
    // TODO: 0x00189ae4 -- alpha transition/pulse not applied; use linear fade
    uint8_t alpha = (uint8_t)(int)(frac * 255.0f);
    this->m_DrawColour.a = alpha;
}

// ---------------------------------------------------------------------------
// DrawOrder  binary @ 0x00189a58  (vtable slot +0x24)
// Delegates to Draw via the inherited base chain (HUDControl3d::Draw).
// ---------------------------------------------------------------------------

void GenericHUDControl::DrawOrder(const Vec3& hudScale, int layerMask) {
    Draw(hudScale, layerMask);
    if (m_pLabel) {
        // TODO: 0x00189a58 -- label draw call (BakedStringBox::Draw at world pos)
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
            if (game_work.mGameSound) {
                game_work.mGameSound->SFXPlay(s.m_Name, 1.0f, 1.0f,
                    Mortar::Delegate1<bool, Mortar::MortarSound*>());
            }
            s.m_Played = true;
            it = m_Sounds.erase(it);
        } else {
            ++it;
        }
    }
}
