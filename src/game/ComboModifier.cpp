// ComboModifier — v1.6.1 combo-bonus modifier.
// Binary ctor @ 0x00134044, ApplyModifier @ 0x00132e34,
// ComboWasCanceled @ 0x00132b7c, FruitWasSliced @ 0x00132e10.

#include "ComboModifier.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include "hud/MissControl.h"
#include "engine/util/Delegate.h"
#include "engine/math/_Vector3.h"
#include <cstdint>
#include <list>

ComboModifier::ComboModifier()
    : GameModifier()
{}

ComboModifier::~ComboModifier() {}

// @ binary 0x00132e34 ResetSpecific — unsubscribe delegates before reset.
void ComboModifier::ResetSpecific() {
    Fruit::FruitWasSlicedEvent() -=
        Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(
            this, &ComboModifier::FruitWasSliced);
    SlashEntity::OnComboCancelEvent() -=
        Mortar::Delegate1<void, SlashEntity*>::Make(
            this, &ComboModifier::ComboWasCanceled);
}

// @ 0x00132b48 — UpdateSpecific: OR the combo-active bit (0x80) into
// SlashEntity::ModPowerMask each frame.
//
// The GOT chain (literal pool DAT_0x132b6c=0x0019e5d4, DAT_0x132b70=0x7544;
// PC base 0x132b5c -> GOT entry 0x002d8674) resolves to the standalone .bss
// uint32_t @ 0x00332bc8, whose symbol is _ZN11SlashEntity12ModPowerMaskE —
// the SAME global SlashModifier::UpdateSpecific @0x0014b000, ScrollingMenu::
// Update @0x001b0440 and PowerUpManager::SetDefaults/Reset touch. Bit 0x80
// suppresses SlashEntity::Update's own combo popup (@0x001e8d08) so that
// ComboModifier::ComboWasCanceled owns the combo-bonus popup while the
// modifier is live.
int ComboModifier::UpdateSpecific(float /*dt*/) {
    SlashEntity::s_ModPowerMask |= 0x80u;   // binary @ 0x132b60: orr r2,r2,#0x80
    return 0;
}

// TODO: v1.6.1 0x00132c94 (ComboModifier::RemoveModifier) — unported vtable
// slot 6 override: unsubscribes both delegates, walks m_SlicedFruit clearing
// each fruit's m_bFrozen(+0x16c) and emptying the list, then zeroes the WHOLE
// of SlashEntity::ModPowerMask (str r0,[r3,#0] with r0==0 @ 0x132d70).

// @ 0x00132e34
// Binary: if m_BonusAccum(+0x0c)<=0 (not already active), register in order:
//   Delegate1<void,SlashEntity*>::Make(this, &ComboModifier::ComboWasCanceled)
//     += g_OnComboCancel (file-static in Slash.cpp, GOT 0x332bd8)     [1st]
//   Delegate3<void,Fruit*,int,Mortar::Entity*>::Make(this, &ComboModifier::FruitWasSliced)
//     += g_FruitWasSliced (file-static in Fruit.cpp, GOT 0x332a34)   [2nd]
// BEFORE chaining base ApplyModifier (which sets m_BonusAccum = m_Duration and
// would make the gate false if checked after). Register-under-gate first, base last.
void ComboModifier::ApplyModifier(bool isPurchased, float* extra) {
    if (m_BonusAccum <= 0) {
        // Subscribe to g_OnComboCancel — binary @ 0x132b7c (GOT load 0x332bd8) += delegate.
        SlashEntity::OnComboCancelEvent() +=
            Mortar::Delegate1<void, SlashEntity*>::Make(
                this, &ComboModifier::ComboWasCanceled);
        // Subscribe to g_FruitWasSliced — binary @ 0x132e98 (GOT load 0x332a34) += delegate.
        Fruit::FruitWasSlicedEvent() +=
            Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::Make(
                this, &ComboModifier::FruitWasSliced);
    }
    GameModifier::ApplyModifier(isPurchased, extra);
}

// @ 0x00132e10
// Binary: set byte at fruit+0x16c = 1; push fruit into m_SlicedFruit.
void ComboModifier::FruitWasSliced(Fruit* fruit, int /*score*/, Mortar::Entity* /*entity*/) {
    if (!fruit) return;
    fruit->m_bFrozen = 1;
    m_SlicedFruit.push_back(fruit);
}

// @ 0x00132b7c
// Binary: on combo cancel, if more than 2 fruit were sliced, average their
// positions and post a combo-bonus popup via MissControl.
//   slash+0x17c = m_ComboCounter (the live combo count)
//   slash+0x180 = m_ComboOnlineMode (passed to MakeCombo as entityType arg)
//   slash+0x150 = m_ComboFruitTypes[10] (fruit types written per accumulated slice)
//   fruit+0x3c  = m_FruitType (uint8); fruit+0x10 = Entity::pos (Vec3);
//   fruit+0x16c = m_bFrozen -- the "in-combo" flag set by FruitWasSliced (cleared here)
void ComboModifier::ComboWasCanceled(SlashEntity* slash) {
    // slash->m_ComboCounter (+0x17c) = the live combo count.
    slash->m_ComboCounter = 0;

    // <= 2 fruit: not a combo. Leave the list as-is (binary returns early
    // without clearing -- but FruitWasSliced only ever fills it between
    // cancels, so this path simply skips the popup).
    if (m_SlicedFruit.size() <= 2) {
        return;
    }

    // Accumulate average slice position. Binary seeds from a zero global Vec3.
    _Vector3<float> sum(0.0f, 0.0f, 0.0f);

    std::list<Fruit*>::iterator it = m_SlicedFruit.begin();
    while (it != m_SlicedFruit.end()) {
        Fruit* fruit = *it;
        int count = slash->m_ComboCounter;
        if (count < 10) {
            slash->m_ComboFruitTypes[count] = (int)fruit->m_FruitType;
            slash->m_ComboCounter = count + 1;
            sum += fruit->pos;
        }
        // Clear the in-combo flag set by FruitWasSliced, then drop from list.
        fruit->m_bFrozen = 0;
        it = m_SlicedFruit.erase(it);
    }

    sum /= (float)(slash->m_ComboCounter);

    MissControl* mc = MissControl::GetFree();
    mc->MakeCombo(sum, slash->m_ComboCounter, slash->m_ComboOnlineMode);
}

void ComboModifier::ParseSpecific(TiXmlElement* /*xml*/) {}

GameModifier* ComboModifier::Clone() {
    ComboModifier* c = new ComboModifier();
    c->m_Duration     = m_Duration;
    c->m_reserved08   = m_reserved08;
    c->m_BonusAccum   = m_BonusAccum;
    c->m_bDeferred    = m_bDeferred;
    c->m_DeferTime    = m_DeferTime;
    c->m_bApplied     = m_bApplied;
    c->m_pDeferInfo   = m_pDeferInfo;
    return c;
}
