// ComboModifier — v1.6.1 combo-bonus modifier.
// Binary ctor @ 0x00134044, ApplyModifier @ 0x00132e34,
// ComboWasCanceled @ 0x00132b7c, FruitWasSliced @ 0x00132e10.

#include "ComboModifier.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include <cstdint>

ComboModifier::ComboModifier()
    : GameModifier()
{}

ComboModifier::~ComboModifier() {}

// @ binary 0x00132e34 ResetSpecific — no-op per binary
void ComboModifier::ResetSpecific() {}

// UpdateSpecific — no-op per binary
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
// Binary: set byte at fruit+0x16c = 1; push entity into m_SlicedFruit.
void ComboModifier::FruitWasSliced(Fruit* fruit, int /*score*/, Mortar::Entity* entity) {
    if (!fruit || !entity) return;
    reinterpret_cast<uint8_t*>(fruit)[0x16c] = 1;
    m_SlicedFruit.push_back(entity);
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
    // Copy base fields
    c->m_Duration           = m_Duration;
    c->field_0x08           = field_0x08;
    c->m_Duration_remaining = m_Duration_remaining;
    c->m_bDeferred          = m_bDeferred;
    c->m_DeferStart         = m_DeferStart;
    c->m_bApplied           = m_bApplied;
    c->m_pOwner             = m_pOwner;
    return c;
}
