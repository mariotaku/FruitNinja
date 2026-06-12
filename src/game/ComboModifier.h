#ifndef FN_GAME_COMBO_MODIFIER_H
#define FN_GAME_COMBO_MODIFIER_H

//
// ComboModifier : GameModifier — v1.6.1 combo-bonus modifier.
// Binary size 0x28. GetType() == 7.
// Registers FruitWasSliced and ComboWasCanceled delegates on ApplyModifier.
// On combo-cancel with >2 fruit sliced, posts a combo-bonus score popup.
//
// Binary addresses:
//   ctor            0x00134044
//   ApplyModifier   0x00132e34
//   ComboWasCanceled 0x00132b7c
//   FruitWasSliced  0x00132e10
//   GetType         0x001333f0

#include "GameModifier.h"
#include <list>

namespace Mortar { class Entity; }
class Fruit;
class SlashEntity;

class ComboModifier : public GameModifier {
public:
    // +0x20: 8-byte std::list<Fruit*> (Sourcery 2010q1 pre-C++11 sentinel layout).
    // Binary ctor @ 0x00134044: new(0x28)+memset; list ctor at +0x20.
    std::list<Fruit*> m_SlicedFruit;

    ComboModifier();
    ~ComboModifier() override;

    void ResetSpecific() override;
    int  UpdateSpecific(float dt) override;

    // @ 0x00132e34 — registers FruitWasSliced + ComboWasCanceled delegates
    void ApplyModifier(bool isPurchased, float* extra) override;

    int GetType() override { return 7; }

    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // @ 0x00132e10 — Delegate3<void,Fruit*,int,Mortar::Entity*> target. Sets
    // fruit->byte[0x16c]=1 and pushes into m_SlicedFruit.
    // TODO: 0x00132e34 — subscribe Delegate3<void,Fruit*,int,Mortar::Entity*>::Make(this,
    //   &ComboModifier::FruitWasSliced) to FruitManager's Event3<Fruit*,int,Mortar::Entity*>
    //   m_FruitWasSliced. FruitManager not yet ported; event owner unknown in port.
    void FruitWasSliced(Fruit* fruit, int score, Mortar::Entity* entity);

    // @ 0x00132b7c — Delegate1<void,SlashEntity*> target. On combo-cancel: if >2
    // fruit sliced, post combo-bonus popup.
    // TODO: 0x00132b7c — subscribe Delegate1<void,SlashEntity*>::Make(this,
    //   &ComboModifier::ComboWasCanceled) to SlashEntity's Event1<SlashEntity*>
    //   m_OnComboCancel (binary addr unknown — event not yet ported on SlashEntity).
    void ComboWasCanceled(SlashEntity* slash);
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(ComboModifier) == 0x28, "ComboModifier must be 0x28 bytes");
#endif

#endif // FN_GAME_COMBO_MODIFIER_H
