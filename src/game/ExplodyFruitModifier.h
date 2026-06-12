#ifndef FN_GAME_EXPLODY_FRUIT_MODIFIER_H
#define FN_GAME_EXPLODY_FRUIT_MODIFIER_H

//
// ExplodyFruitModifier : GameModifier — v1.6.1 explody-fruit modifier.
// Binary size 0x3c (60 bytes): base 0x20 + 4 floats (0x10) + Vec3 (0x0c). GetType() == 6.
// On fruit slice, spawns a FruitSplosion HUDControl.
//
// Nested class FruitSplosion : HUDControl (size 0xa4) handles the visual.
//
// Binary addresses:
//   ctor            0x00134d10
//   ParseSpecific   0x0013514c
//   ApplyModifier   0x00135574
//   FruitWasSliced  0x00135888
//   GetType         0x001360c4

#include "GameModifier.h"
#include "hud/HUDControl.h"
#include "math/Vec3.h"
#include <cstdint>

class Fruit;
namespace Mortar { class Entity; }

class ExplodyFruitModifier : public GameModifier {
public:
    // +0x20: forceMin float (from UNK_00134d68; ctor placeholder 0.0f)
    float m_ForceMin;

    // +0x24: forceInc float (ctor default 0.25f; after parse: +0x28 += +0x24)
    float m_ForceInc;

    // +0x28: forceMax float (from UNK_00134d68; ctor placeholder 0.0f; after parse: +0x2c += +0x28)
    float m_ForceMax;

    // +0x2c: radius param float (from UNK_00134d6c; ctor placeholder 0.0f)
    float m_Radius;

    // +0x30: Vec3 param (parsed from XML; passed as 6th arg to FruitSplosion ctor)
    Vec3 m_SplosionVec;

    // -----------------------------------------------------------------------
    // Nested class FruitSplosion : HUDControl (size 0xa4)
    // Spawned per-fruit-slice; manages explody particle burst.
    // Binary addresses:
    //   ctor        (part of ExplodyFruitModifier::FruitWasSliced @ 0x00135888)
    //   Update      (vtable)
    //   DrawOrder   (vtable)
    //   FruitWasKilled (vtable)
    //   ADingoAteMyBaby (vtable)
    // -----------------------------------------------------------------------
    class FruitSplosion : public HUDControl {
    public:
        // Extra payload beyond HUDControl's 0x74 base.
        // Binary size 0xa4 => 0xa4 - 0x74 = 0x30 bytes of subclass fields.
        // TODO: 0x00135888 — resolve FruitSplosion field layout (fruit ptr, force params, timer, etc.)
        float m_Param0;   // +0x74
        float m_Param1;   // +0x78
        float m_Param2;   // +0x7c
        float m_Param3;   // +0x80
        Fruit* m_pFruit;  // +0x84
        Vec3  m_Vec;      // +0x88: Vec3 param from ExplodyFruitModifier::m_SplosionVec
        uint8_t _pad94[0x10]; // +0x94..+0xa3 — unknown fields to reach size 0xa4

        // ctor(forceMin, forceInc, forceMax, radius, fruit, splosionVec)
        FruitSplosion(float param0, float param1, float param2, float param3,
                      Fruit* fruit, const Vec3& vec);
        ~FruitSplosion() override;

        void Update(float dt) override;
        void DrawOrder(const Vec3& hudScale, int layerMask) override;

        // TODO: 0x00135888 — FruitWasKilled(Fruit*): delegate target; no-op stub
        void FruitWasKilled(Fruit* fruit);
        // TODO: 0x00135888 — ADingoAteMyBaby(HUDControl*): removal callback; no-op stub
        void ADingoAteMyBaby(HUDControl* ctrl);
    };

    ExplodyFruitModifier();
    ~ExplodyFruitModifier() override;

    void ResetSpecific() override;
    int  UpdateSpecific(float dt) override;

    // @ 0x00135574 — registers FruitWasSliced (Delegate3) on ApplyModifier
    void ApplyModifier(bool isPurchased, float* extra) override;

    int GetType() override { return 6; }

    // @ 0x0013514c — parses 4 float attrs + 1 int attr; post-parse adjusts fields
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // @ 0x00135888 — creates FruitSplosion HUDControl and adds to HUD
    // TODO: 0x00135888 — wire to FruitManager's FruitWasSliced signal
    void FruitWasSliced(Fruit* fruit, int score, Mortar::Entity* entity);
};

#ifdef __bada__
static_assert(sizeof(ExplodyFruitModifier::FruitSplosion) == 0xa4,
    "ExplodyFruitModifier::FruitSplosion must be 0xa4 bytes");
#endif

#endif // FN_GAME_EXPLODY_FRUIT_MODIFIER_H
