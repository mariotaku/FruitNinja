#ifndef FN_BOMB_BLAST_H
#define FN_BOMB_BLAST_H

//
// BombBlast : Mortar::Entity (size 0x70 / 112 bytes in binary)
// Mortar::Entity type 4. Expanding shockwave ring spawned by a slashed Bomb.
//
// Binary refs:
//   ctor       0x171618 / 0x171648
//   Init       0x1718ac
//   Update     0x171170
//   DrawBlast  0x171354  (called by DrawActiveBlasts 0x171aa0)
//
// Binary layout (Entity base = 60 bytes at offset 0):
//   +0x3C: m_PosA    Vec3 (12B)
//   +0x48: m_PosB    Vec3 (12B)
//   +0x54: m_Vel1    Vec3 (12B)
//   +0x60: m_Vel2    Vec3 (12B)
//   +0x6C: m_Lifetime float (4B)
//   sizeof = 0x70 (112)
//

#include "Entity.h"
#include "math/Vec3.h"

class BombBlast : public Mortar::Entity {
public:
    // +0x3C: positions extruded along vel1/vel2 (start at vel copies, grow each frame)
    Vec3 m_PosA;

    // +0x48
    Vec3 m_PosB;

    // +0x54: primary expansion velocity (angle direction, 0.5x magnitude)
    Vec3 m_Vel1;

    // +0x60: perpendicular velocity (full magnitude)
    Vec3 m_Vel2;

    // +0x6C: seconds since spawn — kills at 3.0s
    float m_Lifetime;

#if !defined(__bada__)
    // Port-only growth accumulator (no binary counterpart). Placed after all
    // binary members so it does not shift binary offsets in the cross-build.
    float m_BlastRadius;
#endif

    BombBlast();
    ~BombBlast();

    // Vtable slot 2: Binary @ 0x001718ac.
    // ASM-verified: 2026-05-04T08:23Z binary @ 0x001718ac (asm-inspector)
    // All three params are unused at runtime — caller passes (this, 0, 0, 0).
    // The first arg is r0 / `this` (Ghidra's void* p1 is a free-function-rendering
    // artifact); body operates exclusively on `this`.
    void Init(void* p1, long p2, Vec3* p3) override;
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

    // Binary @ 0x171354 — emit this blast's 6-vertex kite (two triangles) into
    // the shared tri-list at the frame counter slot. Called per blast from
    // DrawActiveBlasts via vtable+0x34.
    void DrawBlast();
    // Binary @ 0x171030 — DrawUpdate(float): a 1-byte no-op (PostUpdate vtable
    // slot aliases it). Standalone-symbol counterpart.
    void DrawUpdate(float);
};

#if defined(__bada__)
// Binary-faithful offsets (32-bit Bada cross-build). Binary total = 0x70 (112 bytes).
static_assert(__builtin_offsetof(BombBlast, m_PosA)    == 0x3C, "m_PosA binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_PosB)    == 0x48, "m_PosB binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_Vel1)    == 0x54, "m_Vel1 binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_Vel2)    == 0x60, "m_Vel2 binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_Lifetime)== 0x6C, "m_Lifetime binary offset wrong");
static_assert(sizeof(BombBlast)                        == 0x70, "sizeof(BombBlast) wrong (binary 0x70 / 112)");
#endif

#endif
