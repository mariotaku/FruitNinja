#ifndef FN_ENTITIES_BOMB_FLASH_H
#define FN_ENTITIES_BOMB_FLASH_H

// BombFlash — pooled standalone (NOT an Mortar::Entity subclass). Has its own vtable.
// Size: 0x44 bytes (68 bytes). Entry stride 0x44.
// White flash sprite spawned on bomb hit. Quadratic scale + alpha animation over a short
// lifetime, then deactivates and returns to the pool.
//
// Pool model (v1.6.1): contiguous heap allocation `pool` with `poolCount` entries.
// `currentFree` tracks the next candidate index (circular probe via modulo).
//
// v1.6.1 Binary addresses:
//   BombFlash          0x001d5fa8 / 0x001d5ff4  (ctor)
//   ~BombFlash          0x001d5ac0 / 0x001d5b80  (dtor)
//   Init                0x001d4dbc
//   Update              0x001d4dd4
//   Draw                0x001d6910
//   DrawUpdate          0x001d4dc0
//   CreatePool          0x001d4cc0  (DEFUNCT — bx lr)
//   GetFree             0x001d4cc4
//   MakeFlash           0x001d5bf0
//   UpdateActiveFlashes 0x001d4dc4  (DEFUNCT — bx lr)
//   DrawActiveFlashes   0x001d4dc8  (DEFUNCT — bx lr)
//   RemoveAllFlashes    0x001d4d64
//   CleanUp             0x001d5afc
//   Destroy             0x001d773c  (private — sets m_bActive = false)
//
// Note: BombFlashFull @ 0x001ca40c is a separate variant referenced by Bomb code.

#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>

class BombFlash {
public:
    // +0x00: vtable pointer (implicit)
    // +0x04..+0x3f: colour, texture, position, direction, scale, angles, timer
    uint8_t m_padBefore[0x3C];
    // +0x40: active flag (read by GetFree stride 0x44)
    uint8_t m_bActive;
    // +0x41..+0x43: padding to size 0x44
    uint8_t m_padAfter[0x03];

    BombFlash();
    virtual ~BombFlash();

    // Virtual dispatch slots (vtable: Init, Update, Draw, DrawUpdate)
    void Init(void*, long, Vec3*);
    void Update(float dt);
    void Draw();
    void DrawUpdate(float);

    // --- Static pool API ---

    // v1.6.1 BombFlash::CreatePool @0x001d4cc0 — DEFUNCT (bx lr).
    // Port allocates contiguous pool.
    static int CreatePool(int n);

    // v1.6.1 BombFlash::GetFree @0x001d4cc4 — find free slot via circular probe.
    static BombFlash* GetFree();

    // v1.6.1 BombFlash::MakeFlash @0x001d5bf0 — activate a pooled flash slot.
    static void MakeFlash(Colour col, Vec3 pos, Vec3 dir,
                          Mortar::SmartPtr<Mortar::Texture> tex);

    // v1.6.1 BombFlash::UpdateActiveFlashes @0x001d4dc4 — DEFUNCT (bx lr).
    static void UpdateActiveFlashes(float dt);

    // v1.6.1 BombFlash::DrawActiveFlashes @0x001d4dc8 — DEFUNCT (bx lr).
    static void DrawActiveFlashes();

    // v1.6.1 BombFlash::RemoveAllFlashes @0x001d4d64 — deactivates every slot.
    static void RemoveAllFlashes();

    // v1.6.1 BombFlash::CleanUp @0x001d5afc — destructs entries backward, frees heap block.
    static void CleanUp();
};

#endif // FN_ENTITIES_BOMB_FLASH_H
