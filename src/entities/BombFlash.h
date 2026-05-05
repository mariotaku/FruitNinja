#ifndef FN_ENTITIES_BOMB_FLASH_H
#define FN_ENTITIES_BOMB_FLASH_H

// Analysed: 2026-04-30T00:00
//
// BombFlash — pooled standalone (NOT an Mortar::Entity subclass). Has its own vtable.
// Size: 0x44 bytes (68 bytes). Confirmed via BombFlash::CreatePool(0x20) -> 32-element pool.
// White flash sprite spawned on bomb hit. Quadratic scale + alpha animation over a short
// lifetime, then deactivates and returns to the pool.
// See docs/entities/bomb-flash.md for full RE.
//
// Binary addresses:
//   ctor (real)         0x00171a14
//   ctor (alias)        0x00171a50
//   dtor (regular)      0x00171f38
//   dtor (deleting)     0x00171fb8
//   CreatePool          0x00170f84  (stub in binary — returns param; real pool via static array)
//   MakeFlash           0x001723f4
//   Update (instance)   0x00171038
//   UpdateActiveFlashes 0x00171028  (static)
//   DrawActiveFlashes   0x0017102c  (static)
//   RemoveAllFlashes    0x00170fe4  (static)
//   CleanUp             0x00171f64  (static)
//
// Note: BombFlashFull @ 0x00168f24 is a separate variant referenced by Bomb code.

#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>

class BombFlash {
public:
    // Pool size matches CreatePool(0x20) argument.
    static const int POOL_SIZE = 32;

    // +0x00: vtable pointer (implicit)
    // +0x04: active flag
    uint8_t m_bActive;
    // Remaining fields (0x44 - vtptr - 1 pad): position, lifetime, colour, texture, dir.
    // Not fully laid out — pad to binary size for informational correctness.
    uint8_t m_pad[0x3F];

    BombFlash();
    virtual ~BombFlash();

    // Instance update (quadratic scale + alpha anim). Called by UpdateActiveFlashes.
    // @ 0x00171038
    void Update(float dt);

    // --- Static pool API ---

    // @ 0x00170f84 — stub in binary (returns param unchanged). Pool backed by static array.
    static int CreatePool(int n);

    // @ 0x001723f4 — activate a pooled flash slot.
    static void MakeFlash(Colour col, Vec3* pos, Vec3* dir,
                          Mortar::SmartPtr<Mortar::Texture>* tex);

    // @ 0x00171028 — iterate pool, call Update on active slots.
    static void UpdateActiveFlashes(float dt);

    // @ 0x0017102c — iterate pool, call Draw on active slots.
    static void DrawActiveFlashes();

    // @ 0x00170fe4 — deactivate every pool slot (called on game reset).
    static void RemoveAllFlashes();

    // @ 0x00171f64 — destructs each pool entry, frees backing memory.
    static void CleanUp();

public:

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: BombFlash::Draw -- auto stub from binary missing-symbol set
    void Draw();
    // STUB: BombFlash::DrawUpdate -- auto stub from binary missing-symbol set
    void DrawUpdate(float);
    // STUB: BombFlash::GetFree -- auto stub from binary missing-symbol set
    void GetFree();
    // STUB: BombFlash::Init -- auto stub from binary missing-symbol set
    void Init(void*, int, Vec3*);
    // ---- end AUTO-STUB MERGE ----
};

#endif // FN_ENTITIES_BOMB_FLASH_H
