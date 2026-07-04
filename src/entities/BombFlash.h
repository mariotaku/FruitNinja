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
    // +0x04: uninitialized by binary ctor (set later by MakeFlash/Update)
    float m_Timer;
    // +0x08 / +0x0C: default-constructed opaque black (Colour::Colour @0x0011afa8)
    Colour m_Colour0;
    Colour m_Colour1;
    // +0x10 / +0x14: uninitialized by binary ctor
    float m_SinAngle;
    float m_CosAngle;
    // +0x18: null-constructed by binary (SmartPtr<Texture>::SmartPtr @0x0010c3a4)
    Mortar::SmartPtr<Mortar::Texture> m_pTexture;
    // +0x1C / +0x28 / +0x34: uninitialized by binary ctor
    Vec3 m_Pos;
    Vec3 m_Dir;
    Vec3 m_Scale;
    // +0x40: active flag (read by GetFree stride 0x44)
    bool m_bActive;
    // +0x41: padding
    uint8_t m_pad41;
    // +0x42: uninitialized by binary ctor
    uint16_t m_AngleIdx;

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

#if defined(__bada__)
// Binary-faithful offsets (32-bit Bada cross-build). Binary total = 0x44 (68 bytes).
static_assert(__builtin_offsetof(BombFlash, m_Timer)    == 0x04, "m_Timer binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Colour0)  == 0x08, "m_Colour0 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Colour1)  == 0x0C, "m_Colour1 binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_SinAngle) == 0x10, "m_SinAngle binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_CosAngle) == 0x14, "m_CosAngle binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_pTexture) == 0x18, "m_pTexture binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Pos)      == 0x1C, "m_Pos binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Dir)      == 0x28, "m_Dir binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_Scale)    == 0x34, "m_Scale binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_bActive)  == 0x40, "m_bActive binary offset wrong");
static_assert(__builtin_offsetof(BombFlash, m_AngleIdx) == 0x42, "m_AngleIdx binary offset wrong");
static_assert(sizeof(BombFlash)                         == 0x44, "sizeof(BombFlash) wrong (binary 0x44 / 68)");
#endif

#endif // FN_ENTITIES_BOMB_FLASH_H
