#ifndef FN_ENTITIES_BOMB_FLASH_H
#define FN_ENTITIES_BOMB_FLASH_H

//
// BombFlash — pooled standalone (NOT a Mortar::Entity subclass). Has its own vtable.
// Size: 0x44 bytes (68 bytes). Confirmed via BombFlash::CreatePool(0x20) -> 32-element pool.
// White flash sprite spawned on bomb hit. Quadratic scale + alpha animation over a short
// lifetime, then deactivates and returns to the pool.
//
// Binary addresses:
//   ctor (real)         0x00171a14
//   ctor (alias)        0x00171a50
//   dtor (regular)      0x00171f38
//   dtor (deleting)     0x00171fb8
//   CreatePool          0x00170f84  (stub in binary -- returns param; real pool via static array)
//   MakeFlash           0x001723f4
//   Update (instance)   0x00171038
//   UpdateActiveFlashes 0x00171028  (static)
//   DrawActiveFlashes   0x0017102c  (static)
//   RemoveAllFlashes    0x00170fe4  (static)
//   CleanUp             0x00171f64  (static)
//
// Note: BombFlashFull @ 0x00168f24 is a separate variant referenced by Bomb code.
//
// Binary layout (root polymorphic class, no base):
//   +0x00: vptr (4B in binary, implicit)
//   +0x04: m_Timer      float
//   +0x08: m_Colour0    Colour (4B BGRA; alpha byte at +0x0B is m_MaxAlpha per Ghidra struct)
//   +0x0C: m_Colour1    Colour (4B BGRA; alpha byte at +0x0F is m_CurrentAlpha per Ghidra struct)
//   +0x10: field_0x10   uint8_t[8]  (not written by ctor; purpose TBD)
//   +0x18: m_pTexture   SmartPtr<Texture> (4B)
//   +0x1C: field_0x1c   uint8_t[24] (not written by ctor; likely pos/anim state)
//   +0x34: m_Scale_x    float
//   +0x38: m_Scale_y    float
//   +0x3C: m_Scale_z    float
//   +0x40: m_bActive    bool
//   sizeof = 0x44 (68)
//

#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>

class BombFlash {
public:
    // Pool size matches CreatePool(0x20) argument.
    static const int POOL_SIZE = 32;

    // +0x04 (after implicit vptr at +0x00)
    float m_Timer;

    // +0x08: max-alpha colour (alpha byte == m_MaxAlpha in Ghidra struct label)
    Colour m_Colour0;

    // +0x0C: current/animated colour (alpha byte == m_CurrentAlpha in Ghidra struct label)
    Colour m_Colour1;

    // +0x10: unresolved gap (8 bytes)
    uint8_t field_0x10[8];

    // +0x18: texture smart pointer (4B in binary, ctor-initialised)
    Mortar::SmartPtr<Mortar::Texture> m_pTexture;

    // +0x1C: unresolved gap (24 bytes; likely position + animation state)
    // TODO: resolve field_0x1c contents from Init/MakeFlash disassembly
    uint8_t field_0x1c[24];

    // +0x34: scale vector components
    float m_Scale_x;
    float m_Scale_y;
    float m_Scale_z;

    // +0x40: active flag (ctor: strb 0 at this+0x40)
    bool m_bActive;

    BombFlash();
    virtual ~BombFlash();

    // Instance update (quadratic scale + alpha anim). Called by UpdateActiveFlashes.
    // @ 0x00171038
    void Update(float dt);

    // --- Static pool API ---

    // @ 0x00170f84 -- stub in binary (returns param unchanged). Pool backed by static array.
    static int CreatePool(int n);

    // @ 0x001723f4 -- activate a pooled flash slot.
    static void MakeFlash(Colour col, Vec3* pos, Vec3* dir,
                          Mortar::SmartPtr<Mortar::Texture>* tex);

    // @ 0x00171028 -- iterate pool, call Update on active slots.
    static void UpdateActiveFlashes(float dt);

    // @ 0x0017102c -- iterate pool, call Draw on active slots.
    static void DrawActiveFlashes();

    // @ 0x00170fe4 -- deactivate every pool slot (called on game reset).
    static void RemoveAllFlashes();

    // @ 0x00171f64 -- destructs each pool entry, frees backing memory.
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

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(__builtin_offsetof(BombFlash, m_Timer)    == 0x04, "m_Timer binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Colour0)  == 0x08, "m_Colour0 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Colour1)  == 0x0C, "m_Colour1 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, field_0x10) == 0x10, "field_0x10 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_pTexture) == 0x18, "m_pTexture binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, field_0x1c) == 0x1C, "field_0x1c binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale_x)  == 0x34, "m_Scale_x binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale_y)  == 0x38, "m_Scale_y binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale_z)  == 0x3C, "m_Scale_z binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_bActive)  == 0x40, "m_bActive binary offset wrong");
static_assert(sizeof(BombFlash)                         == 0x44, "sizeof(BombFlash) wrong (binary 0x44 / 68)");
#endif

#endif // FN_ENTITIES_BOMB_FLASH_H
