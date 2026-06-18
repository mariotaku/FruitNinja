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

    // +0x08  uint32_t  field_0x08   ctor sets 0; copy copies u32 (HUDControl* in port)
    HUDControl*  m_pHudCtrl;         // +0x08
    // +0x0c  bool      m_Enabled    ctor sets 1 in binary (port: m_bAddedToHUD init false)
    // DIFFERS: binary default ctor sets m_Enabled=1; port initialises false.
    bool         m_bAddedToHUD;      // +0x0c
    // +0x0d  bool      field_0x0d   ctor sets 0
    bool         m_bIsMultiplyerBoard; // +0x0d
    // +0x0e..+0x0f  implicit padding for Vec3 alignment
    // +0x10  Vec3      m_Vec_0x10   position
    Vec3         m_Pos;              // +0x10
    // +0x1c  Vec3      m_Vec_0x1c   entry velocity
    Vec3         m_Vel;              // +0x1c
    // +0x28  uint32_t  field_0x28   group mask
    uint32_t     m_GroupMask;        // +0x28
    // +0x2c  uint16_t  field_0x2c   sine wave index
    uint16_t     m_SinIdx;           // +0x2c
    // +0x2e..+0x2f  implicit padding for float alignment
    // +0x30  float     field_0x30
    float        m_Freq;             // +0x30
    // +0x34  float     field_0x34
    float        m_Amp1;             // +0x34
    // +0x38  float     field_0x38
    float        m_Amp2;             // +0x38
    // +0x3c  float     field_0x3c   current visibility [0..1]
    float        m_CurrentVis;       // +0x3c
    // +0x40  float     field_0x40   fade rate
    float        m_FadeRate;         // +0x40
    // +0x44  Vec3      m_Vec_0x44   size-in
    Vec3         m_SizeIn;           // +0x44
    // +0x50  Vec3      m_Vec_0x50   size-out
    Vec3         m_SizeOut;          // +0x50
    // +0x5c  float     m_Scale_0x5c ctor = 1.0f
    float        m_StartT;           // +0x5c
    // +0x60  float     field_0x60   ctor = 0.0f
    float        m_EndT;             // +0x60
    // +0x64  Vec3      m_Vec_0x64   colour scale; ctor from global Vec3 const
    Vec3         m_ColourScale;      // +0x64
    // +0x70  Colour    m_Colour     4-byte packed RGBA
    Colour       m_Tint;             // +0x70
    // +0x74  uint32_t  field_0x74   flags
    uint32_t     m_FlagBits;         // +0x74
    // +0x78  bool      field_0x78   ctor sets 0; padding +0x79..+0x7b to 0x7c
    bool         m_bLowEndOnly;      // +0x78

    // ---- Port specific: fields below are NOT in the 124-byte binary struct ----
    // Binary stores texture identity in the ReloadableTexture base (SmartPtr<Texture>
    // at +0x00). Port's base uses GLuint instead, so the loaded SmartPtr must be
    // cached here for transfer to HUDControl3d::m_Texture in Activate().
    // ReloadableTexture::LoadTextures trampolines to ReloadableTexture::Load @
    // 0x001213b8 which calls TextureManager::LoadLocalisedTexture("<m_pName>.tex").
    // Port specific: mirrors the base's SmartPtr slot using port-side texture handle.
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    Mortar::SmartPtr<Mortar::Texture> m_Texture;
    // Port specific: texture asset name from XML "texture" attr (binary stores
    // in base ReloadableTexture::m_pName char*; port base has char m_Name[4]
    // which is too short, so we store the full name here).
    char         m_TexName[64];
    // Port specific: exit velocity from "transitionMoveOut" XML attr.
    // Binary parses this into the same region as m_Vel (entry velocity field at +0x1c)
    // via a separate code path; port needs a distinct field since we can't alias.
    Vec3         m_VelOut;
    // Port specific: hash of "transition" XML attr (e.g. "fade", "slide").
    // Binary passes the raw string pointer; port hashes at parse time.
    uint32_t     m_TransitionHash;
#endif

    EffectImage()
        : Mortar::ReloadableTexture()
        , m_pHudCtrl(nullptr)
        , m_bAddedToHUD(false)
        , m_bIsMultiplyerBoard(false)
        , m_Pos(0,0,0), m_Vel(0,0,0)
        , m_GroupMask(0)
        , m_SinIdx(0)
        , m_Freq(0.0f), m_Amp1(0.0f), m_Amp2(0.0f)
        , m_CurrentVis(0.0f), m_FadeRate(0.0f)
        , m_SizeIn(1,1,1), m_SizeOut(1,1,1)
        , m_StartT(0.0f), m_EndT(0.0f)
        , m_ColourScale(1,1,1)
        , m_Tint(255,255,255,255)
        , m_FlagBits(0), m_bLowEndOnly(false)
    {
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
        m_TexName[0] = '\0';
        m_VelOut = Vec3(0,0,0);
        m_TransitionHash = 0;
#endif
    }

    void Parse(TiXmlElement* xml);
    void LoadTextures();
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
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

#endif // FN_GAME_SCREEN_EFFECT_H
