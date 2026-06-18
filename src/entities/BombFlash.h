#ifndef FN_ENTITIES_BOMB_FLASH_H
#define FN_ENTITIES_BOMB_FLASH_H

//
// BombFlash -- pooled standalone (NOT a Mortar::Entity subclass). Has its own vtable.
// Size: 0x44 bytes (68 bytes). Binary pool size: 0x20 (32) entries.
// White flash sprite spawned on bomb hit. Quadratic scale + alpha animation over a short
// lifetime, then deactivates and returns to the pool.
//
// DEFUNCT in v1.6.1: CreatePool / UpdateActiveFlashes / DrawActiveFlashes are bx-lr stubs
// in the binary. The pool is never allocated. Call sites in GameInit / GameUpdate / GameDraw
// are preserved for call-shape parity; they hit no-op stubs.
//
// The visible bomb flash on bomb kill is Bomb::BombFlashFull / m_BombHitTimer -- a separate
// path that is NOT stubbed here.
//
// v1.6.1 binary addresses:
//   vtable              _ZTV9BombFlash @0x002ce838
//   ctor (real)         BombFlash::BombFlash @0x001d5fa8
//   dtor (regular)      BombFlash::~BombFlash @0x001d5b80
//   dtor (deleting)     BombFlash::~BombFlash @0x001d5ac0
//   Init                BombFlash::Init @0x001d4dbc       (binary body: empty)
//   DrawUpdate          BombFlash::DrawUpdate @0x001d4dc0 (binary body: empty)
//   Update (instance)   BombFlash::Update @0x001d4dd4
//   Draw (instance)     BombFlash::Draw @0x001d6910
//   CreatePool          BombFlash::CreatePool @0x001d4cc0 (bx lr -- DEFUNCT)
//   GetFree             BombFlash::GetFree @0x001d4cc4
//   UpdateActiveFlashes BombFlash::UpdateActiveFlashes @0x001d4dc4 (bx lr -- DEFUNCT)
//   DrawActiveFlashes   BombFlash::DrawActiveFlashes @0x001d4dc8   (bx lr -- DEFUNCT)
//   RemoveAllFlashes    BombFlash::RemoveAllFlashes @0x001d4d64
//   MakeFlash           BombFlash::MakeFlash @0x001d5bf0
//   CleanUp             BombFlash::CleanUp @0x001d5afc
//
// Binary layout (root polymorphic class, no base):
//   +0x00: vptr (4B in binary, implicit)
//   +0x04: m_Timer      float
//   +0x08: m_Colour0    Colour (4B BGRA; alpha byte at +0x0B is m_MaxAlpha per Ghidra struct)
//   +0x0C: m_Colour1    Colour (4B BGRA; alpha byte at +0x0F is m_CurrentAlpha per Ghidra struct)
//   +0x10: m_SinAngle   float  (MakeFlash @0x1d5bf0 writes SinIdx(angleIdx) here)
//   +0x14: m_CosAngle   float  (MakeFlash @0x1d5bf0 writes CosIdx(angleIdx) here)
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
    // v1.6.1 BombFlash::Update @0x001d4dd4
    void Update(float dt);

    // --- Static pool API ---

    // v1.6.1 BombFlash::CreatePool @0x001d4cc0 -- bx lr stub in binary; DEFUNCT.
    // GameInit calls BombFlash::CreatePool(0x20); that call is preserved for call-shape
    // parity and hits this no-op.
    static int CreatePool(int n);

    // v1.6.1 BombFlash::MakeFlash @0x001d5bf0 -- activate a pooled flash slot.
    // Pool is never allocated (CreatePool is a no-op), so GetFree() returns null and
    // this is effectively a no-op at runtime, but the full body is preserved.
    static void MakeFlash(Colour col, Vec3 pos, Vec3 dir,
                          Mortar::SmartPtr<Mortar::Texture> tex);

    // v1.6.1 BombFlash::UpdateActiveFlashes @0x001d4dc4 -- bx lr stub in binary; DEFUNCT.
    // GameUpdate calls this per-frame; preserved for call-shape parity.
    static void UpdateActiveFlashes(float dt);

    // v1.6.1 BombFlash::DrawActiveFlashes @0x001d4dc8 -- bx lr stub in binary; DEFUNCT.
    // GameDraw calls this per-frame; preserved for call-shape parity.
    static void DrawActiveFlashes();

    // v1.6.1 BombFlash::RemoveAllFlashes @0x001d4d64 -- deactivate every pool slot.
    static void RemoveAllFlashes();

    // v1.6.1 BombFlash::CleanUp @0x001d5afc -- destructs each pool entry, frees backing memory.
    static void CleanUp();

    // v1.6.1 BombFlash::Draw @0x001d6910 -- render one active flash sprite.
    void Draw();
    // v1.6.1 BombFlash::DrawUpdate @0x001d4dc0 -- per-frame draw-state advance. Binary body is empty.
    void DrawUpdate(float);
    // v1.6.1 BombFlash::GetFree @0x001d4cc4 -- return next free pool slot for MakeFlash.
    static BombFlash* GetFree();
    // v1.6.1 BombFlash::Init @0x001d4dbc -- initialise a flash slot. Binary body is empty (no-op).
    void Init(void*, long, Vec3*);
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
