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
//   Update        0x0011d664
//   Deactivate    0x0011d43c
//   LoadTextures  0x0011d1ec

#include <cstdint>
#include <vector>
#include <cstring>
#include "math/Vec3.h"
#include "math/Colour.h"
#include "hud/HUDControl.h"
#include <tinyxml2.h>

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
    void Parse(tinyxml2::XMLElement* xml);
};

// ~124 bytes; derives from ReloadableTexture (texture path + GLuint handle).
// Only fields accessed in Activate/Update/Deactivate are listed here.
// ReloadableTexture base is stubbed as a texture name + handle pair.
struct EffectImage {
    // ReloadableTexture base (stub: name[64] + GLuint)
    char     m_TexName[64];          // +0x00 (base)
    uint32_t m_TexHandle;            // +0x40 (stub, GLuint)

    HUDControl*  m_pHudCtrl;         // +0x08 (port offset; original +0x08 in derived)
    bool         m_bAddedToHUD;      // +0x0C
    bool         m_bIsMultiplyerBoard; // +0x0D
    Vec3         m_Pos;              // +0x10
    Vec3         m_Vel;              // +0x1C
    uint32_t     m_GroupMask;        // +0x28
    uint16_t     m_SinIdx;           // +0x2C
    float        m_Freq;             // +0x30
    float        m_Amp1;             // +0x34
    float        m_Amp2;             // +0x38
    float        m_CurrentVis;       // +0x3C
    float        m_FadeRate;         // +0x40
    Vec3         m_SizeIn;           // +0x44
    Vec3         m_SizeOut;          // +0x50
    float        m_StartT;           // +0x5C
    float        m_EndT;             // +0x60
    Vec3         m_ColourScale;      // +0x64
    Colour       m_Tint;             // +0x70
    uint32_t     m_FlagBits;         // +0x74
    bool         m_bLowEndOnly;      // +0x78

    // Port-side trailing fields (no binary offset — not in the 0x50-byte binary struct).
    // XML `transitionMoveOut="X,Y"` -- exit velocity used during the fade-out phase.
    // Stored alongside m_Vel which holds entry-phase velocity (transitionMoveIn).
    // Active in 8 <image> entries in powerUpList.xml.
    // TODO: Update consumer (binary @ TBD) -- currently stored but not yet
    // wired into the fade-out animation path.
    Vec3         m_VelOut;
    // XML `transition="fade"|"slide"|...` -- animation mode, stored as a hash.
    // Active value seen: "fade". Used to select mode-specific animation paths.
    // TODO: Update consumer for mode-specific animation paths (binary @ TBD).
    uint32_t     m_TransitionHash;

    EffectImage()
        : m_TexHandle(0)
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
        , m_VelOut(0,0,0), m_TransitionHash(0)
    {
        memset(m_TexName, 0, sizeof(m_TexName));
    }

    void Parse(tinyxml2::XMLElement* xml);
    void LoadTextures();
};

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

    void Parse(tinyxml2::XMLElement* xml);
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

    void Parse(tinyxml2::XMLElement* xml);
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
    void Parse(tinyxml2::XMLElement* xml);
    // Binary @ 0x0011dbb8
    void Activate();
    // Binary @ 0x0011d664
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
