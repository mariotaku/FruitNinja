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
    // +0x28 / +0x2c: growing radius + scale
    float m_BlastRadius;
    float m_Scale;

    // +0x36: random 16-bit angle from Init (determines ring orientation)
    uint16_t m_Angle;

    // +0x3c / +0x48: positions extruded along vel1/vel2
    Vec3 m_PosA;
    Vec3 m_PosB;

    // +0x54 / +0x60: per-axis expansion velocities (angle / angle+90°)
    Vec3 m_Vel1;
    Vec3 m_Vel2;

    // +0x6c: seconds since spawn — kills at 3.0s
    float m_Lifetime;

    BombBlast();
    ~BombBlast();

    void Init(int p1, int p2, int p3) override;
    void Update(float dt) override;
    void Draw(Renderer& r) override;

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
