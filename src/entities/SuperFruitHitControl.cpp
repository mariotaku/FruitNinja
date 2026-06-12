// SuperFruitHitControl — binary @ 0x001bee10 (RemoveQuickly)
// Stub implementation: method bodies are no-ops returning safe defaults.

#include "SuperFruitHitControl.h"

SuperFruitHitControl::SuperFruitHitControl()
{
    entityType = 6;  // super-fruit entity type (binary type 6)
}

SuperFruitHitControl::~SuperFruitHitControl()
{
    // Defunct: SuperFruitHitControl -- no-op stub; binary @ 0x001bee10
}

void SuperFruitHitControl::Update(float /*dt*/)
{
    // TODO: 0x001bee10 -- SuperFruitHitControl::Update not yet RE'd
}

void SuperFruitHitControl::Draw(Renderer& /*r*/)
{
    // TODO: 0x001bee10 -- SuperFruitHitControl::Draw not yet RE'd
}

void SuperFruitHitControl::PostUpdate(float /*dt*/)
{
}

void SuperFruitHitControl::RemoveQuickly()
{
    // Binary @ 0x001bee10: sets kill flag on this entity.
    flags |= ENT_KILLED;
}
