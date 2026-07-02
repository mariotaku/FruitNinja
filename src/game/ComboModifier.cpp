// ComboModifier — v1.6.1 combo-bonus modifier.
// Binary ctor @ 0x00134044, ApplyModifier @ 0x00132e34,
// ComboWasCanceled @ 0x00132b7c, FruitWasSliced @ 0x00132e10.

#include "ComboModifier.h"
#include "GameWork.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include "hud/MissControl.h"
#include "engine/util/Delegate.h"
#include "engine/math/Vec3.h"
#include <cstdint>
#include <list>

uint32_t g_GameFrameFlags = 0;   // binary .bss @ 0x00332bc8 (shared frame-flags word)

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

// @ 0x00132b48 — UpdateSpecific: OR combo-active bit (0x80) into the shared
// game frame-flags word each frame.
//
// RE (binary @ 0x132b48): loads a GOT-relative pointer (literal pool
// DAT_0x132b6c=0x0019e5d4, DAT_0x132b70=0x7544; PC base 0x132b5c -> GOT entry
// 0x002d8674 -> global @ 0x00332bc8, a standalone .bss uint32_t, NOT inside
// game_work), then *p |= 0x80; returns 0.
//
// The global is a shared per-frame bitfield: different subsystems claim
// different bits each frame -- 0x80 = combo-modifier active (set here, cleared
// in ComboModifier::RemoveModifier @ 0x132d70 and PowerUpManager::SetDefaults/
// Reset which zero the whole word); 0x40 = Game::Update slice trail (set @
// 0x1b0444, cleared @ 0x1b07e8); 0x20 = tested by a DrawUpdate @ 0x1da688.
int ComboModifier::UpdateSpecific(float /*dt*/) {
    g_GameFrameFlags |= 0x80u;   // binary @ 0x132b60: orr r2,r2,#0x80 ; global @ 0x00332bc8
    return 0;
}

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
    Vec3 sum(0.0f, 0.0f, 0.0f);

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
