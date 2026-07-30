#ifndef FN_GAME_SCREEN_EFFECT_H
#define FN_GAME_SCREEN_EFFECT_H

// Analysed: 2026-05-03T00:00
//
// ScreenEffect — per-powerup full-screen visual overlay (0x50 bytes in binary).
//
// Binary addresses:
//   ctor          0x0011d568
//   copy ctor     0x0011bc78
//   dtor          0x0011d5a0
//   Parse         0x001491e4
//   Activate      0x00148f08
//   Update        0x00148844
//   Deactivate    0x00148510
//   LoadTextures  0x0011d1ec

#include <cstdint>
#include <vector>
#include <cstring>
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "hud/HUDControl.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/asset/ReloadableTexture.h"
#include "engine/xml/TiXmlElement.h"

namespace Mortar {
    class MortarSound;
}
class PSPParticleEmitter;

// ParseVector -- parse "x,y,z" comma-separated string into Vec3.
// Binary: _Z11ParseVectorPKc v1.6.1
_Vector3<float> ParseVector(const char* s);

// ParseMaskWords -- v1.6.1 @0x0014f404 (_Z14ParseMaskWordsPKcPmi).
// Generic helper: comma-splits str, hashes each trimmed token via StringHash,
// scans wordHashes[count]; on match ORs (1<<matchIndex) into the result bitmask.
// Used by EffectImage::Parse to decode the "transition" attribute ("scale","fade"->bits 0,1).
uint32_t ParseMaskWords(const char* str, unsigned long* wordHashes, int count);

class PowerUp;
class HUDControl3d;

// v1.6.1 ScreenEffect::Parse @0x00149800 children: "emmiter" (sic — original spelling preserved)
// 24 bytes in binary (ARM32 layout).
struct Emmiter {
    uint32_t                          m_NameHash;      // +0x00
    PSPParticleEmitter*               m_pHandle;       // +0x04
    _Vector3<float> m_Offset;        // +0x08
    _Vector3<float> m_VelocityScale; // +0x14

    Emmiter() : m_NameHash(0), m_pHandle(nullptr), m_Offset(0,0,0), m_VelocityScale(1,1,1) {}
    void Parse(TiXmlElement* xml);
};

// 124 bytes; derives from Mortar::ReloadableTexture (8 bytes at +0x00).
// Binary: EffectImage::EffectImage @ 0x0011eae8, copy ctor @ 0x0011ba7c.
// Instance size 124 (0x7c): vector stride 0x7c in _M_insert_aux @ 0x0011ec64.
struct EffectImage : public Mortar::ReloadableTexture {
    // Base: Mortar::ReloadableTexture at +0x00 (8 bytes, byte-faithful: SmartPtr<Texture>
    // @+0x00 + char* m_pPath @+0x04). Binary base ctor @ 0x001213f0.

    // Field semantics verified v1.6.1 EffectImage copy-ctor @ 0x00145bd4 and
    // ScreenEffect::Update @ 0x00148844 (re-analyst #164). Names match how Update
    // reads each slot; Ghidra's alt guesses (m_FadeIn/m_Hold/m_AlphaStart/...) are
    // the wrong interpretation and were corrected in the Ghidra DB to these names.

    // +0x08  HUDControl*  runtime control ptr. v1.6.1 EffectImage::EffectImage(const&)
    //                     @0x00145bd4 copies this VERBATIM (ScreenEffect's copy-ctor/
    //                     operator= must NOT null it -- see ScreenEffect.cpp). Not
    //                     parsed from XML.
    HUDControl*  m_pHudCtrl;         // +0x08
    // +0x0c  bool  binary +0xc byte. NOT an activation gate -- v1.6.1
    //               ScreenEffect::Activate @0x00148f08 (disasm @0x0014900c) reads
    //               m_DeferKind at loop entry and unconditionally creates a HUD
    //               control per image; it never reads this byte as a skip
    //               condition. Activate's only write is `strbeq r0,[r4,#0xc]`
    //               (@0x00149184), which zeroes it ONLY when game_work's HUD is
    //               NULL -- a "HUD was null at Activate" breadcrumb. The CONSUMER
    //               is v1.6.1 ScreenEffect::Update @0x001488bc: if the HUD exists
    //               and this byte is 0, Update sets it to 1 and lazily
    //               AddControl()s the image's control (so effects activated
    //               before the HUD exists still display). A prior port revision
    //               misread it as an `if (img.m_bAddedToHUD) continue;` guard in
    //               Activate, which has no binary counterpart and silently
    //               skipped every image (ctor default below is true); that guard
    //               has been removed.
    bool         m_bAddedToHUD;      // +0x0c
    // +0x0d  uint8_t  defer kind: 0=none, 1=points (ScoreMultiplyerBoard / Arcade x2),
    //               2=time (TimeSinkControl / Berry-Blast time-sink). XML
    //               deferPoints="true" -> 1; else "defer"="none"/"points"/"time" -> 0/1/2.
    //               Activate @0x00148f08 dispatches HUDControl3d subtype on this byte.
    uint8_t      m_DeferKind;        // +0x0d
    // +0x0e..+0x0f  implicit padding for Vec3 alignment
    // +0x10  Vec3  base position (XML "pos"); Update writes ctrl->pos.
    _Vector3<float> m_Pos;              // +0x10
    // +0x1c  Vec3  anchor offset (XML "anchor"); also used as entry slide-move base.
    //         Binary: Parse @0x001491e4 writes "anchor" here.
    _Vector3<float> m_Vel;              // +0x1c
    // +0x28  uint32_t  HUD layer/group mask (XML "group"); Activate -> ctrl+0x34.
    uint32_t     m_GroupMask;        // +0x28
    // +0x2c  uint16_t  sine oscillator index; Update += dt*32760*m_Freq @0x00148844.
    uint16_t     m_SinIdx;           // +0x2c
    // +0x2e..+0x2f  implicit padding for float alignment
    // +0x30  float  sine frequency (XML "freq"); drives m_SinIdx increment.
    float        m_Freq;             // +0x30
    // +0x34  float  size oscillation amp1 (XML "amp1"); selected when sin>=0.
    float        m_Amp1;             // +0x34
    // +0x38  float  size oscillation amp2 (XML "amp2"); selected when sin<0.
    float        m_Amp2;             // +0x38
    // +0x3c  float  current visibility accumulator [0..1]; Update writes each frame.
    float        m_CurrentVis;       // +0x3c
    // +0x40  float  fade rate (XML "fade"/"transitionTime"); Update uses dt/m_FadeRate.
    float        m_FadeRate;         // +0x40
    // +0x44  Vec3  entry slide move offset (XML "transitionMoveIn" / "transitionMove").
    //         Applied to ctrl->pos during entrance transition. Default (0,0,0).
    //         v1.6.1 EffectImage::Parse @0x001491e4.
    _Vector3<float> m_SizeIn;           // +0x44
    // +0x50  Vec3  exit slide move offset (XML "transitionMoveOut" / "transitionMove").
    //         Applied to ctrl->pos during exit transition. Default (0,0,0).
    _Vector3<float> m_SizeOut;          // +0x50
    // +0x5c  float  window start time (XML "timeStart"); Update gate = param_3*m_StartT.
    float        m_StartT;           // +0x5c
    // +0x60  float  window end time (XML "timeEnd"); Update gate = param_3*m_EndT.
    float        m_EndT;             // +0x60
    // +0x64  Vec3  on-screen size source. Primary write: Parse @0x001491e4 loads the
    //         texture and writes (texWidth, texHeight, 0) here. Secondary write: "scale"
    //         attr overrides with explicit (x,y,z). "slowHardwareScale" multiplies into it.
    //         Update @0x00148844 reads m_ColourScale as the base quad size each frame.
    _Vector3<float> m_ColourScale;      // +0x64
    // +0x70  Colour  packed RGBA tint (XML "tint").
    Colour       m_Tint;             // +0x70
    // +0x74  uint32_t  transition flag bits. Bit0(1)="scale" (size fades with visibility),
    //         bit1(2)="fade" (alpha scales with visibility). XML "transition" attr parsed
    //         via ParseMaskWords; v1.6.1 ParseMaskWords @0x0014f404.
    uint32_t     m_FlagBits;         // +0x74
    // +0x78  bool  XML "scaleToScreen" -- marks this image as a full-screen overlay
    //         quad (only "ice_cover" in v1.6.1 poweruplist.xml sets it). Binary reads
    //         this byte at EffectImage+0x78; no confirmed consumer has been found in
    //         Activate/Update (a latent/dead gate in v1.6.1, or consumed by a code
    //         path not yet RE'd). DIFFERS: opt-in widescreen (Layout::HalfWidth) --
    //         this port reads it as the "is this image full-screen" detector to widen
    //         such overlays' quad width to +-HalfWidth() in widescreen (see
    //         ScreenEffect::Activate/Update); identity (no-op read) under __bada__/3:2.
    //         Renamed from m_bLowEndOnly (placeholder name) now that XML usage confirms
    //         the "scaleToScreen" semantic; padding +0x79..+0x7b to 0x7c.
    bool         m_bScaleToScreen;   // +0x78

    // EffectImage ctor defaults -- v1.6.1 EffectImage::EffectImage @0x0014a508
    EffectImage()
        : Mortar::ReloadableTexture()
        , m_pHudCtrl(nullptr)
        , m_bAddedToHUD(true)    // binary default = 1 (v1.6.1 @0x0014a508)
        , m_DeferKind(0)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_GroupMask(0)
        , m_SinIdx(0)
        , m_Freq(0.0f), m_Amp1(0.0f), m_Amp2(0.0f)
        , m_CurrentVis(0.0f), m_FadeRate(0.0f)
        , m_SizeIn(0,0,0), m_SizeOut(0,0,0) // binary default = (0,0,0) (v1.6.1 @0x0014a508)
        , m_StartT(1.0f), m_EndT(0.0f)      // binary default m_StartT=1.0f (v1.6.1 @0x0014a508)
        , m_ColourScale(0,0,0)              // binary default = (0,0,0); texture dims written by Parse (v1.6.1 @0x0014a508)
        , m_Tint(255,255,255,255)
        , m_FlagBits(0), m_bScaleToScreen(false)
    {
    }

    void Parse(TiXmlElement* xml);
    void LoadTextures();
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(EffectImage)                        == 0x7c, "EffectImage size");
static_assert(offsetof(EffectImage, m_pHudCtrl)          == 0x08, "EffectImage::m_pHudCtrl @ +0x08");
static_assert(offsetof(EffectImage, m_bAddedToHUD)       == 0x0c, "EffectImage::m_bAddedToHUD @ +0x0c");
static_assert(offsetof(EffectImage, m_DeferKind)         == 0x0d, "EffectImage::m_DeferKind @ +0x0d");
static_assert(offsetof(EffectImage, m_Pos)               == 0x10, "EffectImage::m_Pos @ +0x10");
static_assert(offsetof(EffectImage, m_Vel)               == 0x1c, "EffectImage::m_Vel @ +0x1c");
static_assert(offsetof(EffectImage, m_GroupMask)         == 0x28, "EffectImage::m_GroupMask @ +0x28");
static_assert(offsetof(EffectImage, m_SinIdx)            == 0x2c, "EffectImage::m_SinIdx @ +0x2c");
static_assert(offsetof(EffectImage, m_CurrentVis)        == 0x3c, "EffectImage::m_CurrentVis @ +0x3c");
static_assert(offsetof(EffectImage, m_SizeIn)            == 0x44, "EffectImage::m_SizeIn @ +0x44");
static_assert(offsetof(EffectImage, m_SizeOut)           == 0x50, "EffectImage::m_SizeOut @ +0x50");
static_assert(offsetof(EffectImage, m_ColourScale)       == 0x64, "EffectImage::m_ColourScale @ +0x64");
static_assert(offsetof(EffectImage, m_Tint)              == 0x70, "EffectImage::m_Tint @ +0x70");
static_assert(offsetof(EffectImage, m_FlagBits)          == 0x74, "EffectImage::m_FlagBits @ +0x74");
static_assert(offsetof(EffectImage, m_bScaleToScreen)    == 0x78, "EffectImage::m_bScaleToScreen @ +0x78");
#endif

// 0x28 (40) bytes. v1.6.1 ScreenTint::ScreenTint ctor @0x00149f30, Parse @0x00148324.
struct ScreenTint {
    float m_CurrentT;       // +0x00  progress accumulator [0..1]; Update writes; lerp factor
    float m_TransitionTime; // +0x04  XML "transitionTime"; fade rate
    float m_TimeStart;      // +0x08  XML "timeStart"; default 1.0
    float m_TimeEnd;        // +0x0c  XML "timeEnd"; default 0.0
    _Vector3<float> m_BackTint; // +0x10  XML "backTint" (or "tint") -> HUD scales[3..5] (WORLD/background)
    _Vector3<float> m_HudTint;  // +0x1c  XML "hudTint" (or "tint") -> HUD scales[0..2] (HUD/foreground)

    // v1.6.1 ScreenTint::ScreenTint @0x00149f30: all tint components default
    // (0,0,0), m_TimeStart=1.0, rest 0.0.
    ScreenTint()
        : m_CurrentT(0.0f), m_TransitionTime(0.0f), m_TimeStart(1.0f), m_TimeEnd(0.0f)
        , m_BackTint(0,0,0), m_HudTint(0,0,0)
    {}

    void Parse(TiXmlElement* xml);
};

// 44 bytes
struct SoundEffect {
    char  m_SoundName[32]; // +0x00
    float m_StartT;        // +0x20
    float m_EndT;          // +0x24
    Mortar::MortarSound* m_VoiceHandle; // +0x28

    // ASM-spec v1.6.1 SoundEffect::SoundEffect @0x00149f10: m_StartT=1.0f, m_EndT=-1.0f.
    // Both were wrongly 0.0f/0.0f in the port, which made ScreenEffect::Update's
    // skip gate (`if (currentLongest > maxTotal * sfx.m_StartT) continue;`) fail on
    // nearly every frame -- silencing the freeze/frenzy/fourth_banana/scorex2 stinger.
    SoundEffect() : m_StartT(1.0f), m_EndT(-1.0f), m_VoiceHandle(nullptr) {
        memset(m_SoundName, 0, sizeof(m_SoundName));
    }

    void Parse(TiXmlElement* xml);
};

// sizeof = 0x50 = 80 bytes; NO virtual methods.
class ScreenEffect {
public:
    // +0x00
    std::vector<Emmiter>     m_Emmiters;
    // +0x0C
    std::vector<EffectImage> m_Images;
    // +0x18
    std::vector<ScreenTint>  m_Tints;
    // +0x24
    std::vector<SoundEffect> m_Sounds;
    // +0x30 — 16 bytes (NOT 64)
    char                     m_Name[16];
    // +0x40
    uint32_t                 m_NameHash;
    // +0x44
    PowerUp*                 m_pOwnerPowerUp;
    // +0x48
    float                    m_RemainingTime;
    // +0x4C
    float                    m_TotalDuration;

    ScreenEffect();
    ScreenEffect(const ScreenEffect& rhs);
    ~ScreenEffect();
    ScreenEffect& operator=(const ScreenEffect& rhs);

    // Binary @ 0x001491e4
    void Parse(TiXmlElement* xml);
    // Binary @ 0x00148f08
    void Activate();
    // Binary @ 0x00148844
    void Update(float dt, float currentLongest, float maxTotal);
    // Binary @ 0x00148510
    void Deactivate();
    // Binary @ 0x0011d1ec
    void LoadTextures();
    // Called by PowerUp::UnloadTextures @0x00140ae4 and, transitively, by
    // PowerUpManager::UnloadTextures @0x00140b10. Mirror of LoadTextures: walk
    // m_Images and Unload() each EffectImage's ReloadableTexture base (the
    // SmartPtr lives in the BASE at +0x00, not at an EffectImage-specific
    // offset). Idempotent -- LoadTextures()/Parse re-loads on demand.
    void UnloadTextures();
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(ScreenEffect) == 0x50, "ScreenEffect size mismatch"); // v1.6.1 PowerUp::{ctor} @0x00141c48 -- operator new(0x50) sizes ScreenEffect
#endif

#endif // FN_GAME_SCREEN_EFFECT_H
