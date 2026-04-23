#include "Coin.h"

// Coin stub — entity type 2. Full flight / collect state machine is
// deferred (see docs/entities/coin.md). This stub is enough for
// CreateEntity to return a typed Entity* and for ActorManager to pool
// instances; gameplay-relevant behaviour lands when MakeCoins is ported.
//
// Analysed: 2026-04-23T01:30

Coin::Coin() {
    entityType = 2;
}

Coin::~Coin() {}

void Coin::Init(int, int, int) {
    flags &= ~ENT_SKIP_MASK;  // activate + clear pending-kill
}

void Coin::Update(float /*dt*/) {
    // TODO: 5-state machine (waiting / flying / decel / homing / arrived)
    // at binary 0x00173790. Without it, a Coin entity sits where it
    // spawned until its KILLED bit is set.
}

void Coin::Draw(Renderer& /*r*/) {
    // TODO: Coin::Draw at 0x00173CC4 — scale × RotY(spin) × RotZ(heading)
    // × Translate, renders coin model.
}

void Coin::Deactivate() {
    // TODO: Clear m_pFlyEmitter / m_pCollectEmitter at binary 0x001731F4.
    Entity::Deactivate();
}

void Coin::LoadContent()   {}
void Coin::UnLoadContent() {}
