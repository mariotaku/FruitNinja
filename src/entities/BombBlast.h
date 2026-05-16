#ifndef FN_BOMB_BLAST_H
#define FN_BOMB_BLAST_H

//
// BombBlast : Mortar::Entity (size 0x70 / 112 bytes in binary)
// Mortar::Entity type 4. Expanding shockwave ring spawned by a slashed Bomb.
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

class BombBlast : public Mortar::Entity {
public:
    // +0x3C: growing radius + scale (own fields after Mortar::Entity base 0x3C)
    float m_BlastRadius;
    float m_Scale;

    // m_Angle is inherited from Mortar::Entity base at +0x36.
    // BombBlast::Init writes a random value to it for ring orientation.

    // +0x44: binary has a 4-byte field here (exact purpose TBD).
    // TODO: re-verify BombBlast +0x44 field from Init disassembly (ctor gap between m_Scale and m_PosA).
    uint32_t m_GapField_0x44;

    // +0x48 / +0x54: positions extruded along vel1/vel2
    Vec3 m_PosA;
    Vec3 m_PosB;

    // +0x60: primary expansion velocity (angle direction).
    Vec3 m_Vel1;

    // +0x6C per header comment; binary may not have a second velocity field.
    // TODO: re-verify from DrawBlast disassembly whether Vel2 is stored or computed.
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

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: BombBlast::DrawBlast -- auto stub from binary missing-symbol set
    void DrawBlast();
    // STUB: BombBlast::DrawUpdate -- auto stub from binary missing-symbol set
    void DrawUpdate(float);
    // ---- end AUTO-STUB MERGE ----
};

#ifdef __bada__
// Binary-faithful offsets (32-bit Bada cross-build). Binary total = 0x70 (112 bytes).
// Header offset comments use the form "start_of_first / start_of_second" for pairs.
// Resolved layout: m_BlastRadius(+0x3C) m_Scale(+0x40) [4B gap +0x44]
//   m_PosA(+0x48) m_PosB(+0x54) m_Vel1(+0x60) m_Lifetime(+0x6C) sizeof=0x70
// Note: only one velocity field fits at binary size 0x70; m_Vel2 may be a port
// artefact or may displace m_Lifetime -- these asserts are conservative.
// TODO: re-verify BombBlast binary field layout from ctor/Init disassembly.
static_assert(__builtin_offsetof(BombBlast, m_BlastRadius) == 0x3C, "m_BlastRadius binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_Scale)       == 0x40, "m_Scale binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_GapField_0x44) == 0x44, "m_GapField_0x44 binary offset wrong");
static_assert(__builtin_offsetof(BombBlast, m_PosA)        == 0x48, "m_PosA binary offset wrong");
// TODO: re-verify whether binary has m_Vel2 (sizeof=0x70 implies only 1 vel field). sizeof assert deferred.
// static_assert(sizeof(BombBlast)                         == 0x70, "sizeof(BombBlast) wrong (binary 0x70 / 112)");
#else
// Always-on port layout asserts (desktop x64). Offsets reflect 8-byte vtable ptr,
// int-widened entityType. No pointers added in BombBlast so the delta vs binary
// is: +0x14 for base Entity growth (vtable +4, entityType widening +4, padding +4
// around m_Col alignment, plus shifts from those).
// Binary equivalents noted in comments for parity tracking.
static_assert(offsetof(BombBlast, m_BlastRadius)   == 0x50,
    "m_BlastRadius port offset drift (binary +0x3C)");
static_assert(offsetof(BombBlast, m_Scale)         == 0x54,
    "m_Scale port offset drift (binary +0x40)");
static_assert(offsetof(BombBlast, m_GapField_0x44) == 0x58,
    "m_GapField_0x44 port offset drift (binary +0x44)");
static_assert(offsetof(BombBlast, m_PosA)          == 0x5C,
    "m_PosA port offset drift (binary +0x48)");
static_assert(offsetof(BombBlast, m_PosB)          == 0x68,
    "m_PosB port offset drift (binary +0x54)");
static_assert(offsetof(BombBlast, m_Vel1)          == 0x74,
    "m_Vel1 port offset drift (binary +0x60)");
static_assert(offsetof(BombBlast, m_Vel2)          == 0x80,
    "m_Vel2 port offset drift (binary +0x6C)");
static_assert(offsetof(BombBlast, m_Lifetime)      == 0x8C,
    "m_Lifetime port offset drift (binary +0x6C per header comment)");
static_assert(sizeof(BombBlast)                    == 0x90,
    "sizeof(BombBlast) port drift (binary 0x70; port 0x90 due to 64-bit ptrs + entityType widening)");
#endif

#endif
