#ifndef FN_BOMB_BLAST_H
#define FN_BOMB_BLAST_H

//
// BombBlast : Entity (size 0x70 / 112 bytes in binary)
// Entity type 4. Expanding shockwave ring spawned by a slashed Bomb.
//
// Binary refs (see docs/entities/bomb-blast.md):
//   ctor       0x171618 / 0x171648
//   Init       0x1718ac
//   Update     0x171170
//   DrawBlast  0x171354  (called by DrawActiveBlasts 0x171aa0)
//
// Analysed: 2026-04-13T22:00
//

#include "Entity.h"
#include "math/Vec3.h"

class BombBlast : public Entity {
public:
    // +0x3C: growing radius + scale (own fields after Entity base 0x3C)
    float m_BlastRadius;
    float m_Scale;

    // m_Angle is inherited from Entity base at +0x36.
    // BombBlast::Init writes a random value to it for ring orientation.

    // +0x48 / +0x54: positions extruded along vel1/vel2
    Vec3 m_PosA;
    Vec3 m_PosB;

    // +0x54 / +0x60: per-axis expansion velocities (angle / angle+90°)
    Vec3 m_Vel1;
    Vec3 m_Vel2;

    // +0x6c: seconds since spawn — kills at 3.0s
    float m_Lifetime;

    BombBlast();
    ~BombBlast();

    // Vtable slot 2: Binary @ 0x001718ac.
    // ASM-verified: 2026-05-04T08:23Z binary @ 0x001718ac (asm-inspector)
    // All three params are unused at runtime — caller passes (this, 0, 0, 0).
    // The first arg is r0 / `this` (Ghidra's void* p1 is a free-function-rendering
    // artifact); body operates exclusively on `this`.
    void Init(void* p1, long p2, const Vec3* p3) override;
    void Update(float dt) override;
    // Binary @ 0x00171034 — no-op override. Must be present (not pure-virtual abort).
    void Draw(Renderer& r) override;
    // Binary @ 0x00171030 — no-op override. Must be present.
    void PostUpdate(float dt) override;

    // Static helper called by GameDraw to render every active BombBlast.
    // Matches DrawActiveBlasts (0x171aa0).
    static void DrawActiveBlasts();

    // Static helper called by UpdateBombHit to bulk-kill blasts once the
    // bomb hit timer crosses the 1.55s threshold. Matches
    // RemoveFlashEntities (0x169ca0).
    static void RemoveAll();

    static void LoadContent();
    static void ReleaseContent();
};

#endif
