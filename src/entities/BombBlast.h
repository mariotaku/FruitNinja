#ifndef FN_BOMB_BLAST_H
#define FN_BOMB_BLAST_H

//
// BombBlast : Mortar::Entity (size 0x70 / 112 bytes in binary)
// Mortar::Entity type 4. Expanding shockwave ring spawned by a slashed Bomb.
//
// Binary refs (v1.6.1):
//   ctor             0x001d5538
//   Init             0x001d58f8
//   Update           0x001d4f2c
//   Draw             0x001d4dd0
//   DrawUpdate       0x001d4dcc
//   DrawBlast        0x001d51e8  (called by DrawActiveBlasts 0x001d67cc)
//
// Binary layout (Entity base = 60 bytes at offset 0):
//   +0x3C: m_PosA    Vec3 (12B)
//   +0x48: m_PosB    Vec3 (12B)
//   +0x54: m_Vel1    Vec3 (12B)
//   +0x60: m_Vel2    Vec3 (12B)
//   +0x6C: m_Lifetime float (4B)
//   sizeof = 0x70 (112)
//
// Growth model: BombBlast has NO radius member of its own. The two expansion
// accumulators are the INHERITED Entity::scale components:
//   scale.x (+0x28) — seeded 5.0 by Init, integrates at 100/s, drives m_PosA
//   scale.y (+0x2C) — seeded 50.0 by Init, integrates at 2500/s, drives m_PosB
// The two axes therefore start at different sizes and grow at different rates;
// they are not a single shared radius.
//

#include "Entity.h"
#include "math/_Vector3.h"

class BombBlast : public Mortar::Entity {
public:
    // +0x3C: m_Vel1 scaled by the scale.x accumulator (narrow axis).
    _Vector3<float> m_PosA;

    // +0x48: m_Vel2 scaled by the scale.y accumulator (long axis).
    _Vector3<float> m_PosB;

    // +0x54: primary expansion velocity (angle direction, 0.5x magnitude)
    _Vector3<float> m_Vel1;

    // +0x60: perpendicular velocity (full magnitude)
    _Vector3<float> m_Vel2;

    // +0x6C: seconds since spawn — kills once strictly greater than 3.0s
    float m_Lifetime;

    BombBlast();
    ~BombBlast();

    // Vtable slot 2: v1.6.1 BombBlast::Init @ 0x001d58f8.
    // All three params are unused at runtime — caller passes (this, 0, 0, 0).
    // The first arg is r0 / `this` (Ghidra's void* p1 is a free-function-rendering
    // artifact); body operates exclusively on `this`.
    void Init(void* p1, long p2, _Vector3<float>* p3) override;

    // ASM-spec v1.6.1 BombBlast::Update @ 0x001d4f2c: the `dt` parameter is
    // IGNORED — the binary reads game_work.dt (+0x38) instead. This matters:
    // GameUpdate freezes ActorManager (passes dt=0) for the duration of a bomb
    // hit, yet blasts keep expanding because they bypass the parameter.
    void Update(float dt) override;

    // v1.6.1 BombBlast::Draw @ 0x001d4dd0 — no-op override. Must be present
    // (not pure-virtual abort). Rendering goes through DrawActiveBlasts.
    void Draw(Renderer& r) override;
    // v1.6.1 BombBlast::DrawUpdate @ 0x001d4dcc — no-op override. Must be present.
    void PostUpdate(float dt) override;

    // Static helper called by GameDraw to render every active BombBlast.
    // Matches DrawActiveBlasts (v1.6.1 @ 0x001d67cc).
    static void DrawActiveBlasts();

    // Static helper called by UpdateBombHit to bulk-kill blasts once the
    // bomb hit timer crosses the 1.55s threshold. Matches
    // RemoveFlashEntities (v1.6.1 @ 0x001cb4b0).
    static void RemoveAll();

    static void LoadContent();
    static void ReleaseContent();

    // v1.6.1 BombBlast::DrawBlast @ 0x001d51e8 — emit this blast's 6-vertex kite
    // (two triangles) into the shared tri-list at the frame counter slot. Called
    // per blast from DrawActiveBlasts via vtable+0x34.
    // TODO: v1.6.1 0x001d51e8 (BombBlast::DrawBlast) — geometry/UV/colour below
    // were RE'd against v1.5.1 and have NOT been re-verified against v1.6.1.
    void DrawBlast();
    // v1.6.1 BombBlast::DrawUpdate @ 0x001d4dcc: a bare `return` (no-op). The
    // PostUpdate vtable slot aliases it. Standalone-symbol counterpart.
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
