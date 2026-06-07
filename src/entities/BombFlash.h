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
//   +0x10: m_SinAngle   float  (MakeFlash @0x1723f4 writes SinIdx(angleIdx) here)
//   +0x14: m_CosAngle   float  (MakeFlash @0x1723f4 writes CosIdx(angleIdx) here)
//   +0x18: m_pTexture   SmartPtr<Texture> (4B)
//   +0x1C: m_Pos        Vec3   (MakeFlash: = pos + dir*5; .x clamped to +/-240)
//   +0x28: m_Dir        Vec3   (MakeFlash: copy of dir; used for Atan2Idx angle)
//   +0x34: m_Scale_x    float
//   +0x38: m_Scale_y    float
//   +0x3C: m_Scale_z    float
//   +0x40: m_bActive    bool
//   +0x42: m_AngleIdx   uint16_t (MakeFlash: Atan2Idx(dir.x, -dir.y))
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

    // +0x08: max-alpha colour. Alpha byte (+0x0B) is the peak alpha the flash
    // fades up to / down from -- read by Update as m_MaxAlpha.
    Colour m_Colour0;

    // +0x0C: current/animated colour. Alpha byte (+0x0F) is the live alpha that
    // Update writes each frame -- m_CurrentAlpha. RGB is the tint used by Draw.
    Colour m_Colour1;

    // +0x10 / +0x14: cached sin/cos of the flash orientation (MakeFlash writes
    // SinIdx/CosIdx(m_AngleIdx); Draw feeds them to RotZ44).
    float m_SinAngle;
    float m_CosAngle;

    // +0x18: texture smart pointer (4B in binary, ctor-initialised)
    Mortar::SmartPtr<Mortar::Texture> m_pTexture;

    // +0x1C: world position of the flash quad (MakeFlash: pos + dir*5, x clamped).
    Vec3 m_Pos;

    // +0x28: spawn direction (copied from MakeFlash's dir arg).
    Vec3 m_Dir;

    // +0x34: scale vector components
    float m_Scale_x;
    float m_Scale_y;
    float m_Scale_z;

    // +0x40: active flag (ctor: strb 0 at this+0x40)
    bool m_bActive;

    // +0x41: padding (1 byte) so m_AngleIdx lands at +0x42.
    uint8_t m_Pad41;

    // +0x42: 16-bit angle index (MakeFlash: Atan2Idx(dir.x, -dir.y)).
    uint16_t m_AngleIdx;

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

    // Binary @ 0x00171B54 -- render one active flash sprite (textured quad, animated alpha).
    void Draw();
    // Binary @ 0x00171024 -- per-frame draw-state advance for one flash. Binary body is empty.
    void DrawUpdate(float);
    // Binary @ 0x00170F88 -- return next free pool slot for MakeFlash (rotating-index search).
    static BombFlash* GetFree();
    // Binary @ 0x00171020 -- initialise a flash slot. Binary body is empty (no-op).
    void Init(void*, int, Vec3*);
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(__builtin_offsetof(BombFlash, m_Timer)    == 0x04, "m_Timer binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Colour0)  == 0x08, "m_Colour0 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Colour1)  == 0x0C, "m_Colour1 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_SinAngle) == 0x10, "m_SinAngle binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_CosAngle) == 0x14, "m_CosAngle binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_pTexture) == 0x18, "m_pTexture binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Pos)      == 0x1C, "m_Pos binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Dir)      == 0x28, "m_Dir binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale_x)  == 0x34, "m_Scale_x binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale_y)  == 0x38, "m_Scale_y binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale_z)  == 0x3C, "m_Scale_z binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_bActive)  == 0x40, "m_bActive binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_AngleIdx) == 0x42, "m_AngleIdx binary offset wrong");
static_assert(sizeof(BombFlash)                         == 0x44, "sizeof(BombFlash) wrong (binary 0x44 / 68)");
#endif

#endif // FN_ENTITIES_BOMB_FLASH_H
