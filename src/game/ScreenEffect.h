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
//   Parse         0x0011e150
//   Activate      0x0011dbb8
//   Update        0x00148844
//   Deactivate    0x0011d43c
//   LoadTextures  0x0011d1ec

#include <cstdint>
#include <vector>
#include <cstring>
#include "math/Vec3.h"
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
Vec3 ParseVector(const char* s);

class PowerUp;
class HUDControl3d;

// Binary @ 0x0011e150 children: "emmiter" (sic — original spelling preserved)
// 24 bytes in binary (ARM32 layout).
struct Emmiter {
    uint32_t                          m_NameHash;      // +0x00
    PSPParticleEmitter*               m_pHandle;       // +0x04
    Vec3                              m_Offset;        // +0x08
    Vec3                              m_VelocityScale; // +0x14

    Emmiter() : m_NameHash(0), m_pHandle(nullptr), m_Offset(0,0,0), m_VelocityScale(1,1,1) {}
    void Parse(TiXmlElement* xml);
};

// 124 bytes; derives from Mortar::ReloadableTexture (8 bytes at +0x00).
// Binary: EffectImage::EffectImage @ 0x0011eae8, copy ctor @ 0x0011ba7c.
// Instance size 124 (0x7c): vector stride 0x7c in _M_insert_aux @ 0x0011ec64.
struct EffectImage : public Mortar::ReloadableTexture {
    // Base: Mortar::ReloadableTexture at +0x00 (8 bytes).
    // Binary's base has SmartPtr<Texture> at +0x00 and char* m_pName at +0x04.
    // Port's ReloadableTexture has char m_Name[4]+GLuint m_Handle (same 8 bytes).
    // DIFFERS: binary base = SmartPtr<Texture>@+0x00 + char*@+0x04;
    //   port base = char[4]+GLuint (same 8-byte size, different field types).
    //   Binary @ 0x001213f0 base ctor.

    // Field semantics verified v1.6.1 EffectImage copy-ctor @ 0x00145bd4 and
    // ScreenEffect::Update @ 0x00148844 (re-analyst #164). Names match how Update
    // reads each slot; Ghidra's alt guesses (m_FadeIn/m_Hold/m_AlphaStart/...) are
    // the wrong interpretation and were corrected in the Ghidra DB to these names.

    // +0x08  HUDControl*  runtime control ptr; copy-ctor copies, ScreenEffect copy
    //                     resets to null. Not parsed from XML.
    HUDControl*  m_pHudCtrl;         // +0x08
    // +0x0c  bool  added-to-HUD guard. DIFFERS: binary default ctor sets 1; port
    //               initialises false (HUD attach happens in Activate either way).
    bool         m_bAddedToHUD;      // +0x0c
    // +0x0d  bool  multiplyer-board kind flag (XML "multiplyer"; Activate kind byte
    //               selects ScoreMultiplyerBoard vs HUDControl3d).
    bool         m_bIsMultiplyerBoard; // +0x0d
    // +0x0e..+0x0f  implicit padding for Vec3 alignment
    // +0x10  Vec3  base position (XML "pos"); Update writes ctrl->pos.
    Vec3         m_Pos;              // +0x10
    // +0x1c  Vec3  anchor offset (XML "anchor"); also used as entry slide-move base.
    //         Binary: Parse @0x001491e4 writes "anchor" here.
    Vec3         m_Vel;              // +0x1c
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
    Vec3         m_SizeIn;           // +0x44
    // +0x50  Vec3  exit slide move offset (XML "transitionMoveOut" / "transitionMove").
    //         Applied to ctrl->pos during exit transition. Default (0,0,0).
    Vec3         m_SizeOut;          // +0x50
    // +0x5c  float  window start time (XML "timeStart"); Update gate = param_3*m_StartT.
    float        m_StartT;           // +0x5c
    // +0x60  float  window end time (XML "timeEnd"); Update gate = param_3*m_EndT.
    float        m_EndT;             // +0x60
    // +0x64  Vec3  on-screen size source. Primary write: Parse @0x001491e4 loads the
    //         texture and writes (texWidth, texHeight, 0) here. Secondary write: "scale"
    //         attr overrides with explicit (x,y,z). "slowHardwareScale" multiplies into it.
    //         Update @0x00148844 reads m_ColourScale as the base quad size each frame.
    Vec3         m_ColourScale;      // +0x64
    // +0x70  Colour  packed RGBA tint (XML "tint").
    Colour       m_Tint;             // +0x70
    // +0x74  uint32_t  transition flag bits. Bit0(1)="scale" (size fades with visibility),
    //         bit1(2)="fade" (alpha scales with visibility). XML "transition" attr parsed
    //         via ParseMaskWords; v1.6.1 ParseMaskWords @0x0014f404.
    uint32_t     m_FlagBits;         // +0x74
    // +0x78  bool  low-end-only gate (XML "lowEndOnly"); padding +0x79..+0x7b to 0x7c.
    bool         m_bLowEndOnly;      // +0x78

    // ---- Port specific: fields below are NOT in the 124-byte binary struct ----
    // Binary stores texture identity in the ReloadableTexture base (SmartPtr<Texture>
    // at +0x00). Port's base uses GLuint instead, so the loaded SmartPtr must be
    // cached here for transfer to HUDControl3d::m_Texture in Activate().
    // ReloadableTexture::LoadTextures trampolines to ReloadableTexture::Load @
    // 0x001213b8 which calls TextureManager::LoadLocalisedTexture("<m_pName>.tex").
    // Port specific: mirrors the base's SmartPtr slot using port-side texture handle.
#if !defined(__bada__)
    Mortar::SmartPtr<Mortar::Texture> m_Texture;
    // Port specific: texture asset name from XML "texture" attr (binary stores
    // in base ReloadableTexture::m_pName char*; port base has char m_Name[4]
    // which is too short, so we store the full name here).
    char         m_TexName[64];
    // Port specific: exit slide offset from "transitionMoveOut" XML attr.
    // Binary stores m_SizeIn/m_SizeOut as separate Vec3 fields at +0x44/+0x50.
    // Port maps transitionMoveIn->m_SizeIn, transitionMoveOut->m_VelOut (port alias).
    Vec3         m_VelOut;
#endif

    // EffectImage ctor defaults -- v1.6.1 EffectImage::EffectImage @0x0014a508
    EffectImage()
        : Mortar::ReloadableTexture()
        , m_pHudCtrl(nullptr)
        , m_bAddedToHUD(true)    // binary default = 1 (v1.6.1 @0x0014a508)
        , m_bIsMultiplyerBoard(false)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_GroupMask(0)
        , m_SinIdx(0)
        , m_Freq(0.0f), m_Amp1(0.0f), m_Amp2(0.0f)
        , m_CurrentVis(0.0f), m_FadeRate(0.0f)
        , m_SizeIn(0,0,0), m_SizeOut(0,0,0) // binary default = (0,0,0) (v1.6.1 @0x0014a508)
        , m_StartT(1.0f), m_EndT(0.0f)      // binary default m_StartT=1.0f (v1.6.1 @0x0014a508)
        , m_ColourScale(0,0,0)              // binary default = (0,0,0); texture dims written by Parse (v1.6.1 @0x0014a508)
        , m_Tint(255,255,255,255)
        , m_FlagBits(0), m_bLowEndOnly(false)
    {
#if !defined(__bada__)
        m_TexName[0] = '\0';
        m_VelOut = Vec3(0,0,0);
#endif
    }

    void Parse(TiXmlElement* xml);
    void LoadTextures();
};

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(EffectImage)                        == 0x7c, "EffectImage size");
static_assert(offsetof(EffectImage, m_pHudCtrl)          == 0x08, "EffectImage::m_pHudCtrl @ +0x08");
static_assert(offsetof(EffectImage, m_bAddedToHUD)       == 0x0c, "EffectImage::m_bAddedToHUD @ +0x0c");
static_assert(offsetof(EffectImage, m_bIsMultiplyerBoard)== 0x0d, "EffectImage::m_bIsMultiplyerBoard @ +0x0d");
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
static_assert(offsetof(EffectImage, m_bLowEndOnly)       == 0x78, "EffectImage::m_bLowEndOnly @ +0x78");
#endif

// ~40 bytes
struct ScreenTint {
    float m_CurrentT;   // +0x00
    float m_Length;     // +0x04
    float m_StartT;     // +0x08
    float m_FadeIn;     // +0x0C
    Vec3  m_ColourTo;   // +0x10
    Vec3  m_ColourFrom; // +0x1C

    ScreenTint()
        : m_CurrentT(0.0f), m_Length(0.0f), m_StartT(0.0f), m_FadeIn(0.0f)
        , m_ColourTo(1,1,1), m_ColourFrom(1,1,1)
    {}

    void Parse(TiXmlElement* xml);
};

// 44 bytes
struct SoundEffect {
    char  m_SoundName[32]; // +0x00
    float m_StartT;        // +0x20
    float m_EndT;          // +0x24
    Mortar::MortarSound* m_VoiceHandle; // +0x28

    SoundEffect() : m_StartT(0.0f), m_EndT(0.0f), m_VoiceHandle(nullptr) {
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

    // Binary @ 0x0011e150
    void Parse(TiXmlElement* xml);
    // Binary @ 0x0011dbb8
    void Activate();
    // Binary @ 0x00148844
    void Update(float dt, float currentLongest, float maxTotal);
    // Binary @ 0x0011d43c
    void Deactivate();
    // Binary @ 0x0011d1ec
    void LoadTextures();
    // Binary addr TBD — called by PowerUp::UnloadTextures and PowerUpManager::UnloadTextures
    // TODO: implement when ScreenEffect texture unload addr is RE'd
    void UnloadTextures() {}
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(ScreenEffect) == 0x50, "ScreenEffect size mismatch"); // v1.6.1 ScreenEffect @0x141c48
#endif

#endif // FN_GAME_SCREEN_EFFECT_H
