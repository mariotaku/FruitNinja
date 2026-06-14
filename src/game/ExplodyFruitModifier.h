#ifndef FN_GAME_EXPLODY_FRUIT_MODIFIER_H
#define FN_GAME_EXPLODY_FRUIT_MODIFIER_H

//
// ExplodyFruitModifier : GameModifier — v1.6.1 explody-fruit modifier.
// Binary size 0x34 (52 bytes): base 0x20 + 4 floats (0x10) + uint32 (0x04). GetType() == 6.
// On fruit slice, spawns a FruitSplosion HUDControl3d.
//
// Nested class FruitSplosion : HUDControl3d (size 0xa4) handles the visual.
//
// Binary addresses:
//   ctor            0x00134d10
//   ParseSpecific   0x0013514c
//   ApplyModifier   0x00135574
//   FruitWasSliced  0x001358d4  (v1.6.1; was 0x00135888 in v1.5.1)
//   GetType         0x001360c4

#include "GameModifier.h"
#include "hud/HUDControl3d.h"
#include <cstdint>

class Fruit;
namespace Mortar { class Entity; }

class ExplodyFruitModifier : public GameModifier {
public:
    // +0x20: forceMin float (UNK_00134d64; ctor=100.0f)
    float m_ForceMin;

    // +0x24: forceInc float (ctor=0.25f; post-parse: +0x28 += +0x24)
    float m_ForceInc;

    // +0x28: forceMax float (ctor=0.0f; post-parse: +0x2c += +0x28)
    float m_ForceMax;

    // +0x2c: radius float (ctor=0.2f)
    float m_Radius;

    // +0x30: fruit-type index 0..2 (FindIndex from XML "type" attr; ctor=0)
    // Forwarded as 6th arg (int) to FruitSplosion ctor.
    uint32_t m_FruitTypeIndex;

    // -----------------------------------------------------------------------
    // Nested class FruitSplosion : HUDControl3d (size 0xa4)
    // ctor @ 0x135620. Spawned per-fruit-slice; manages explody particle burst.
    // Binary fields (own, starting at HUDControl3d base 0x7c):
    //   +0x7c: entity ptr (fruit param)
    //   +0x80: const DAT_135868
    //   +0x84: m_p0 (forceMin)
    //   +0x88: m_p1 (forceInc)
    //   +0x8c: m_p2 (forceMax)
    //   +0x90: m_p3 (radius)
    //   +0x94: m_typeIndex (m_FruitTypeIndex forwarded as int)
    //   +0x98: m_pChainNext (FruitSplosion*)
    //   +0x9c: m_pChainHead (FruitSplosion*)
    //   +0xa0: m_ChainCount (int)
    // Also: +0x08 Vec3 m_Pos copied from entity+0x10..0x18 in ctor.
    // +0x34 flags=0x80; +0x38 Delegate1<HUDControl*> m_OnRemoved.
    // -----------------------------------------------------------------------
    class FruitSplosion : public HUDControl3d {
    public:
        // Own fields (HUDControl3d base = 0x7c)
        Mortar::Entity* m_pEntity;  // +0x7c
        float           m_Const80;  // +0x80: DAT_135868=0; timer accumulator (dt*wavedt)
        float           m_p0;       // +0x84: forceMin
        float           m_p1;       // +0x88: forceInc
        float           m_p2;       // +0x8c: forceMax
        float           m_p3;       // +0x90: radius
        int             m_typeIndex; // +0x94: fruit type index from m_FruitTypeIndex
        FruitSplosion*  m_pChainNext; // +0x98
        FruitSplosion*  m_pChainHead; // +0x9c
        int             m_ChainCount; // +0xa0

        // ctor(forceMin, forceInc, forceMax, radius, entity, typeIndex)
        // @ 0x135620
        FruitSplosion(float p0, float p1, float p2, float p3,
                      Mortar::Entity* entity, int typeIndex);
        ~FruitSplosion() override;

        void Update(float dt) override;
        // RE-ported: 0x00134E00 — thunks to HUDControl3d::DrawOrder (no custom body).
        void DrawOrder(const Vec3& hudScale, int layerMask) override;

        // Delegate target: called when tracked fruit is killed (nulls m_pEntity).
        // RE-ported: 0x00134DEC
        void FruitWasKilled(Fruit* fruit);
        // Delegate target: removal callback registered on chain head.
        // RE-ported: 0x00134E04
        void ADingoAteMyBaby(HUDControl* ctrl);
    };

    ExplodyFruitModifier();
    ~ExplodyFruitModifier() override;

    void ResetSpecific() override;
    int  UpdateSpecific(float dt) override;

    // @ 0x00135574 — registers FruitWasSliced (Delegate3) on ApplyModifier
    void ApplyModifier(bool isPurchased, float* extra) override;

    int GetType() override { return 6; }

    // @ 0x0013514c — parses 4 float attrs + FindIndex -> m_FruitTypeIndex; post-parse adjusts
    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // @ 0x001358d4 — Delegate3<void,Fruit*,int,Mortar::Entity*> target; subscribed in
    // ApplyModifier to g_FruitWasSliced (Fruit.cpp file-static, GOT 0x332a34).
    void FruitWasSliced(Fruit* fruit, int score, Mortar::Entity* entity);
};

#ifdef __bada__
static_assert(sizeof(ExplodyFruitModifier) == 0x34,
    "ExplodyFruitModifier must be 0x34 bytes");
static_assert(sizeof(ExplodyFruitModifier::FruitSplosion) == 0xa4,
    "ExplodyFruitModifier::FruitSplosion must be 0xa4 bytes");
#endif

#endif // FN_GAME_EXPLODY_FRUIT_MODIFIER_H
