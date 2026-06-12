// ComboModifier — v1.6.1 combo-bonus modifier.
// Binary ctor @ 0x00134044, ApplyModifier @ 0x00132e34,
// ComboWasCanceled @ 0x00132b7c, FruitWasSliced @ 0x00132e10.

#include "ComboModifier.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include <cstdint>
#include <list>

ComboModifier::ComboModifier()
    : GameModifier()
{}

ComboModifier::~ComboModifier() {}

// @ binary 0x00132e34 ResetSpecific — no-op per binary
void ComboModifier::ResetSpecific() {}

// @ 0x00132b48 — UpdateSpecific: OR global flag *GOT |= 0x80 each frame.
// TODO: 0x00132b48 — OR into global combo-flag (GOT ptr unresolved)
int ComboModifier::UpdateSpecific(float /*dt*/) { return 0; }

// @ 0x00132e34
// Binary: chain base ApplyModifier, then register FruitWasSliced (Delegate3)
// and ComboWasCanceled (Delegate1<SlashEntity*>) on the relevant game signals.
// TODO: 0x00132e34 — register FruitWasSliced on FruitManager's slice signal
// TODO: 0x00132e34 — register ComboWasCanceled on SlashEntity's combo-cancel signal
void ComboModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
    // Delegate registration deferred — signal infrastructure not yet ported.
}

// @ 0x00132e10
// Binary: set byte at fruit+0x16c = 1; push fruit into m_SlicedFruit.
void ComboModifier::FruitWasSliced(Fruit* fruit, int /*score*/, Mortar::Entity* /*entity*/) {
    if (!fruit) return;
    reinterpret_cast<uint8_t*>(fruit)[0x16c] = 1;
    m_SlicedFruit.push_back(fruit);
}

// @ 0x00132b7c
// Binary: combo-bonus popup loop (clear slash state, sum positions, post popup).
// TODO: 0x00132b7c -- combo-bonus popup (needs popup infra)
void ComboModifier::ComboWasCanceled(SlashEntity* /*slash*/) {
    m_SlicedFruit.clear();
}

void ComboModifier::ParseSpecific(TiXmlElement* /*xml*/) {}

GameModifier* ComboModifier::Clone() {
    ComboModifier* c = new ComboModifier();
    c->m_Duration     = m_Duration;
    c->field_0x08     = field_0x08;
    c->m_BonusAccum   = m_BonusAccum;
    c->m_bDeferred    = m_bDeferred;
    c->m_DeferTime    = m_DeferTime;
    c->m_bApplied     = m_bApplied;
    c->m_pDeferInfo   = m_pDeferInfo;
    return c;
}
