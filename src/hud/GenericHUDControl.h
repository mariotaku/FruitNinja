#ifndef FN_HUD_GENERIC_HUD_CONTROL_H
#define FN_HUD_GENERIC_HUD_CONTROL_H

//
// GenericHUDControl : HUDControl3d (binary sizeof = 0x1d8)
// Base chain (depth 3): GenericHUDControl : HUDControl3d : HUDControl.
//
// Binary addresses (v1.6.1):
//   ctor        0x00189f60  (9-arg: fadeIn,fadeOut,SmartPtr<Tex>,Vec2* parentRect,Vec3 pos,Vec3 scale,Colour,int flags)
//   dtor D0     0x00189770
//   dtor D1     0x001896f4
//   SetText     0x00189858
//   AddSound    0x0018a4c4  (const char* name, float startT, float endT)
//   SetAngle    0x001899a0  (float angleDeg, float radius)
//   PreDraw     0x00189ae4  (vtable slot 6, +0x18)
//   DrawOrder   0x00189a58  (vtable slot 9, +0x24)
//   Update      0x00189ed0  (vtable slot 10, +0x28)
//   GetType     inherits -> HUDControl3d::GetType -> returns 1
//
// Vtable (group base 0x2cd618; vptr = base+8 = 0x2cd620):
//   +0x00  0x189770  ~dtor (deleting)
//   +0x04  0x1896f4  ~dtor (complete)
//   +0x08  0x18b100  stub/Init
//   +0x0c  0x1896c4  Release
//   +0x10  0x18b108  stub/Reset
//   +0x14  0x13605c  PreDrawOrder-ish [thunk]
//   +0x18  0x189ae4  PreDraw (GenericHUDControl override)
//   +0x1c  0x18b544  Draw (HUDControl3d::Draw inherited)
//   +0x20  0x136060  PreDrawOrder (HUDControl3d inherited)
//   +0x24  0x189a58  DrawOrder (GenericHUDControl override)
//   +0x28  0x189ed0  Update (GenericHUDControl override)
//   +0x2c  0x1369e4  SetToMultiplayerState (ret false)
//   +0x30  0x136088  GetType -> 1
//   +0x34  0x136090  Skip (stub)
//   +0x38  0x136094  Save (stub)
//   +0x3c  0x136c2c  GetAdjustedPos
//   16 slots total.
//
// Struct layout (offsets in GenericHUDControl, ARM32 4-byte ptrs):
//   +0x00..+0x7b : HUDControl3d base (0x7c bytes)
//   +0x7c        : std::vector<GenericHUDControlSound> m_Sounds  (12 bytes)
//   +0x88        : float m_Timer (elapsed; Update +=dt)
//   +0x8c        : TranisitionInfo m_PosTrans (24 bytes)
//   +0xa4        : TranisitionInfo m_ScaleTrans (24 bytes)
//   +0xbc        : TranisitionInfo m_AngleTrans (24 bytes)
//   +0xd4        : TranisitionInfo m_AlphaTrans (24 bytes)
//   +0xec        : PulseInfo m_PosPulse  (40 bytes)
//   +0x114       : PulseInfo m_ScalePulse (40 bytes)
//   +0x13c       : PulseInfo m_AnglePulse (40 bytes)
//   +0x164       : PulseInfo m_AlphaPulse (40 bytes)
//   +0x18c       : Vec3 m_BasePos  (ctor param6 pos)
//   +0x198       : Vec3 m_AnglePosOffA (SetAngle, init 0)
//   +0x1a4       : Vec3 m_AnglePosOffB (SetAngle, init 0)
//   +0x1b0       : Vec3 m_BaseScale (ctor param7 scale)
//   +0x1bc       : Vec3 m_BasePos2  (default Vec3, added in PreDraw)
//   +0x1c8       : float m_BaseAngle (init 0, SetAngle sets)
//   +0x1cc       : float m_FadeIn  (ctor param1)
//   +0x1d0       : float m_FadeOut (ctor param2)
//   +0x1d4       : BakedStringBox* m_pLabel (init NULL; owned; SetText replaces+deletes)
//   Total: 0x1d8
//
// HUDControl (inherited) base fields relevant to GenericHUDControl:
//   +0x08 : Vec3 pos       (world pos; overwritten each PreDraw)
//   +0x20 : Vec3 size      (computed scale; ctor seeds Vec3::Zero, PreDraw rewrites it)
//   +0x2c : float m_Timer  (computed angle; written each PreDraw)
//   +0x34 : int m_LayerFlags / FLAGS (ctor param9 flags)
//   +0x5c : Colour m_DrawColour (tint colour; alpha set each PreDraw)
//   +0x64..+0x70 : UV rect (parent rect min/max if parentRect!=NULL)
//

#include "hud/HUDControl3d.h"
#include "engine/render/BakedStringBox.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/math/_Vector2.h"
#include "engine/math/_Vector3.h"
#include "engine/math/Colour.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>

// GenericHUDControlSound -- one-shot sound descriptor (24 bytes in binary).
// Binary AddSound: Sound(fadeIn, -1.0f, startT, endT, name).
// Play(curTimer, prevTimer, &s) -> true when the sound window has been crossed (one-shot).
struct GenericHUDControlSound {
    float       m_FadeIn;    // +0x00: fadeIn from control (copied from GenericHUDControl::m_FadeIn)
    float       m_MinusOne;  // +0x04: -1.0f constant
    float       m_StartT;    // +0x08: trigger window start
    float       m_EndT;      // +0x0c: trigger window end
    const char* m_Name;      // +0x10: sound name (non-owning)
    bool        m_Played;    // +0x14: set true after first play; marks for erasure
    uint8_t     _pad[3];     // +0x15: alignment to 24 bytes

    GenericHUDControlSound(float fadeIn, float minusOne, float startT, float endT, const char* name)
        : m_FadeIn(fadeIn)
        , m_MinusOne(minusOne)
        , m_StartT(startT)
        , m_EndT(endT)
        , m_Name(name)
        , m_Played(false)
    {
        _pad[0] = 0; _pad[1] = 0; _pad[2] = 0;
    }
};

// TranisitionInfo -- 24 bytes (binary spelling preserved).
// 6 floats: curve parameters for position/scale/angle/alpha animated transitions.
struct TranisitionInfo {
    float f0, f1, f2, f3, f4, f5;
    TranisitionInfo() : f0(0.0f), f1(0.0f), f2(0.0f), f3(0.0f), f4(0.0f), f5(0.0f) {}
};

// PulseInfo -- 40 bytes.
// 10 floats: parameters for sinusoidal pulse modulation on pos/scale/angle/alpha.
struct PulseInfo {
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9;
    PulseInfo()
        : f0(0.0f), f1(0.0f), f2(0.0f), f3(0.0f), f4(0.0f)
        , f5(0.0f), f6(0.0f), f7(0.0f), f8(0.0f), f9(0.0f)
    {}
};

class GenericHUDControl : public HUDControl3d {
public:
    // +0x7c: sound list
    std::vector<GenericHUDControlSound> m_Sounds;     // +0x7c (12 bytes: begin/end/cap)

    // +0x88: elapsed timer (Update: +=dt)
    float m_GHCTimer;                                  // +0x88

    // +0x8c..+0xeb: four TranisitionInfo blocks (pos, scale, angle, alpha)
    TranisitionInfo m_PosTrans;                        // +0x8c (24 bytes)
    TranisitionInfo m_ScaleTrans;                      // +0xa4 (24 bytes)
    TranisitionInfo m_AngleTrans;                      // +0xbc (24 bytes)
    TranisitionInfo m_AlphaTrans;                      // +0xd4 (24 bytes)

    // +0xec..+0x18b: four PulseInfo blocks (pos, scale, angle, alpha)
    PulseInfo m_PosPulse;                              // +0xec (40 bytes)
    PulseInfo m_ScalePulse;                            // +0x114 (40 bytes)
    PulseInfo m_AnglePulse;                            // +0x13c (40 bytes)
    PulseInfo m_AlphaPulse;                            // +0x164 (40 bytes)

    // +0x18c: base position (ctor param6)
    _Vector3<float> m_BasePos;                                    // +0x18c (12 bytes)

    // +0x198: angle-driven position offsets (SetAngle writes; init 0)
    _Vector3<float> m_AnglePosOffA;                               // +0x198 (12 bytes)
    _Vector3<float> m_AnglePosOffB;                               // +0x1a4 (12 bytes)

    // +0x1b0: base scale (ctor param7)
    _Vector3<float> m_BaseScale;                                  // +0x1b0 (12 bytes)

    // +0x1bc: default Vec3 added in PreDraw (seeded from engine default Vec3 in ctor)
    _Vector3<float> m_BasePos2;                                   // +0x1bc (12 bytes)

    // +0x1c8: base angle (init 0; SetAngle sets to param1)
    float m_BaseAngle;                                 // +0x1c8

    // +0x1cc: fade params
    float m_FadeIn;                                    // +0x1cc
    float m_FadeOut;                                   // +0x1d0

    // +0x1d4: owned BakedStringBox label (init NULL; SetText replaces+deletes)
    Mortar::BakedStringBox* m_pLabel;                  // +0x1d4

    // ctor  binary @ 0x00189f60
    // ABI: this=r0, fadeIn=s0, fadeOut=s1, tex=r1, parentRect=r2, pos=r3, scale/col/flags on stack.
    GenericHUDControl(float fadeIn, float fadeOut,
                      Mortar::SmartPtr<Mortar::Texture> tex,
                      _Vector2<float>* parentRect,
                      _Vector3<float> pos, _Vector3<float> scale,
                      Colour col, int flags);

    // dtor  binary @ 0x00189770 (D0) / 0x001896f4 (D1)
    virtual ~GenericHUDControl();

    // SetText  binary @ 0x00189858
    // Owns/deletes the BakedStringBox at +0x1d4. If box==current, no-op.
    void SetText(Mortar::BakedStringBox* box);

    // AddSound  binary @ 0x0018a4c4
    // Appends a one-shot sound descriptor to the sound vector.
    void AddSound(const char* name, float startT, float endT);

    // SetAngle  binary @ 0x001899a0
    // Sets the base angle and optionally computes a radial position offset.
    void SetAngle(float angleDeg, float radius);

    // vtable overrides
    // PreDraw  binary @ 0x00189ae4  (slot +0x18)
    void PreDraw(float* hudScaleRaw) override;

    // DrawOrder  binary @ 0x00189a58  (slot +0x24)
    void DrawOrder(float* hudScaleRaw, int layerMask) override;

    // Update  binary @ 0x00189ed0  (slot +0x28)
    void Update(float dt) override;
};

#ifdef __bada__
static_assert(sizeof(GenericHUDControl) == 0x1d8, "GenericHUDControl size mismatch");
static_assert(offsetof(GenericHUDControl, m_Sounds)      == 0x7c,  "GenericHUDControl::m_Sounds offset");
static_assert(offsetof(GenericHUDControl, m_GHCTimer)    == 0x88,  "GenericHUDControl::m_GHCTimer offset");
static_assert(offsetof(GenericHUDControl, m_PosTrans)    == 0x8c,  "GenericHUDControl::m_PosTrans offset");
static_assert(offsetof(GenericHUDControl, m_ScaleTrans)  == 0xa4,  "GenericHUDControl::m_ScaleTrans offset");
static_assert(offsetof(GenericHUDControl, m_AngleTrans)  == 0xbc,  "GenericHUDControl::m_AngleTrans offset");
static_assert(offsetof(GenericHUDControl, m_AlphaTrans)  == 0xd4,  "GenericHUDControl::m_AlphaTrans offset");
static_assert(offsetof(GenericHUDControl, m_PosPulse)    == 0xec,  "GenericHUDControl::m_PosPulse offset");
static_assert(offsetof(GenericHUDControl, m_ScalePulse)  == 0x114, "GenericHUDControl::m_ScalePulse offset");
static_assert(offsetof(GenericHUDControl, m_AnglePulse)  == 0x13c, "GenericHUDControl::m_AnglePulse offset");
static_assert(offsetof(GenericHUDControl, m_AlphaPulse)  == 0x164, "GenericHUDControl::m_AlphaPulse offset");
static_assert(offsetof(GenericHUDControl, m_BasePos)     == 0x18c, "GenericHUDControl::m_BasePos offset");
static_assert(offsetof(GenericHUDControl, m_AnglePosOffA)== 0x198, "GenericHUDControl::m_AnglePosOffA offset");
static_assert(offsetof(GenericHUDControl, m_AnglePosOffB)== 0x1a4, "GenericHUDControl::m_AnglePosOffB offset");
static_assert(offsetof(GenericHUDControl, m_BaseScale)   == 0x1b0, "GenericHUDControl::m_BaseScale offset");
static_assert(offsetof(GenericHUDControl, m_BasePos2)    == 0x1bc, "GenericHUDControl::m_BasePos2 offset");
static_assert(offsetof(GenericHUDControl, m_BaseAngle)   == 0x1c8, "GenericHUDControl::m_BaseAngle offset");
static_assert(offsetof(GenericHUDControl, m_FadeIn)      == 0x1cc, "GenericHUDControl::m_FadeIn offset");
static_assert(offsetof(GenericHUDControl, m_FadeOut)     == 0x1d0, "GenericHUDControl::m_FadeOut offset");
static_assert(offsetof(GenericHUDControl, m_pLabel)      == 0x1d4, "GenericHUDControl::m_pLabel offset");
#endif

#endif // FN_HUD_GENERIC_HUD_CONTROL_H
