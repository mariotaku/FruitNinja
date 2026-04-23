#ifndef FN_COIN_H
#define FN_COIN_H

// Coin : Entity — entity type 2 (0x94 / 148 bytes in binary).
//
// Stub — the full 5-state flight / homing / collect / SFX machinery
// (see docs/entities/coin.md) is not ported yet. We model only what
// CreateEntity needs: a real Entity subclass so the factory can
// instantiate it and ActorManager can pool it.
//
// Binary references:
//   Coin::Coin (C1)  0x00173394
//   Coin::Update     0x0017312C
//   Coin::_Update    0x00173790
//   Coin::Draw       0x00173CC4
//   MakeCoins        0x00173568  (caller: calls ActorManager::Add(2, true))
//
// Analysed: 2026-04-23T01:30

#include "Entity.h"

class Coin : public Entity {
public:
    Coin();
    ~Coin() override;

    void Init(int, int, int) override;
    void Update(float) override;
    void Draw(Renderer&) override;
    void Deactivate() override;

    // Stubbed — no callers in current port, but kept in the header
    // to document the binary's API surface (docs/entities/coin.md).
    static void LoadContent();
    static void UnLoadContent();
};

#endif
