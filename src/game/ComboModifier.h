#ifndef FN_GAME_COMBO_MODIFIER_H
#define FN_GAME_COMBO_MODIFIER_H

//
// ComboModifier : GameModifier — v1.6.1 combo-bonus modifier.
// Binary size ~0x24 (36 bytes). GetType() == 7.
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
#include "util/Delegate.h"
#include <vector>

namespace Mortar { class Entity; }
class Fruit;
class SlashEntity;

class ComboModifier : public GameModifier {
public:
    // +0x20: tracking container for sliced fruit entities during a combo.
    // Binary holds a StackAllocatedPointer<BaseDelegate,32> inline delegate slot
    // here; port uses a std::vector<Mortar::Entity*> as a compile-clean stub.
    // TODO: 0x00134044 — replace with binary-exact StackAllocatedPointer<BaseDelegate,32>
    // when delegate registration infra is fully ported.
    std::vector<Mortar::Entity*> m_SlicedFruit;

    ComboModifier();
    ~ComboModifier() override;

    void ResetSpecific() override;
    int  UpdateSpecific(float dt) override;

    // @ 0x00132e34 — registers FruitWasSliced + ComboWasCanceled delegates
    void ApplyModifier(bool isPurchased, float* extra) override;

    int GetType() override { return 7; }

    void ParseSpecific(TiXmlElement* xml) override;

    GameModifier* Clone() override;

    // @ 0x00132e10 — sets fruit->byte[0x16c]=1 and pushes into m_SlicedFruit
    // TODO: 0x00132e10 — wire to FruitManager's FruitWasSliced signal
    void FruitWasSliced(Fruit* fruit, int score, Mortar::Entity* entity);

    // @ 0x00132b7c — on combo-cancel: if >2 fruit sliced, post combo-bonus popup
    // TODO: 0x00132b7c — wire to SlashEntity's combo-cancel signal
    void ComboWasCanceled(SlashEntity* slash);
};

#endif // FN_GAME_COMBO_MODIFIER_H
