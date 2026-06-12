// ComboModifier — v1.6.1 combo-bonus modifier.
// Binary ctor @ 0x00134044, ApplyModifier @ 0x00132e34,
// ComboWasCanceled @ 0x00132b7c, FruitWasSliced @ 0x00132e10.

#include "ComboModifier.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"

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
// Binary: set fruit entity's byte at +0x16c = 1, push entity ptr into m_SlicedFruit
void ComboModifier::FruitWasSliced(Fruit* fruit, int /*score*/, Mortar::Entity* entity) {
    if (!fruit || !entity) return;
    // TODO: 0x00132e10 — set fruit->byte[0x16c] = 1 (requires exact Fruit field layout)
    m_SlicedFruit.push_back(entity);
}

// @ 0x00132b7c
// Binary: clear SlashEntity->int[0x17c]=0; if set size>2, iterate up to 10 fruit,
// record fruit-type byte[+0x3c] into SlashEntity[+0x54+i] and sum positions;
// reset each fruit byte[0x16c]=0; post combo-bonus popup(count, avgPos);
// write count to SlashEntity[+0x17c].
// TODO: 0x00132b7c — combo-bonus popup call (requires HUD popup infra)
void ComboModifier::ComboWasCanceled(SlashEntity* /*slash*/) {
    // Stub: clear tracking list and return.
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
