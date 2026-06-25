#ifndef FN_BOMB_H
#define FN_BOMB_H

//
// Bomb : Mortar::Entity (sizeof = 0xB0 / 176 bytes)
// v1.6.1 layout. sizeof confirmed from operator new in Bomb ctor chain.
// vtable @ 0x002CE858, 13 slots.
//

#include "Entity.h"
#include "math/Vec3.h"
#include "render/gl_funcs.h"
#include "util/Delegate.h"

struct PSPParticleEmitter;
class MenuButton;

class Bomb : public Mortar::Entity {
public:
    // +0x3C: BombBlast spawn interval; Init=0.6
    float m_SpawnTimer;                      // +0x3C

    // +0x40..+0x63: menu-bomb hit callback (Mortar::Delegate0<void> = 36B)
    Mortar::Delegate0<void> m_HitCallback;   // +0x40

    // +0x64: bomb variant (0=normal, 1=variant)
    int m_BombVariant;                       // +0x64

    // +0x68: 0=alive, 1=hit/slashed
    uint8_t m_bHit;                          // +0x68

    // +0x6C: Z layer from GetBombZPosition()
    float m_ZPosition;                       // +0x6C

    // +0x70..+0x77: rotation state (plain int16; Update does rot+=vel each frame)
    int16_t m_RotVelX;                       // +0x70
    int16_t m_RotVelY;                       // +0x72
    int16_t m_RotX;                          // +0x74
    int16_t m_RotY;                          // +0x76

    // +0x78: double-hit guard (set by Disable(); cleared in Init)
    uint8_t m_bCollisionGuard;               // +0x78

    // +0x7C: fuse smoke emitter (lazy-created on first Update tick)
    PSPParticleEmitter* m_pEmitter;          // +0x7C

    // +0x80: 1=physics enabled
    uint8_t m_bMovement;                     // +0x80

    // +0x84: owning MenuButton backref; cleared in Release/KillBomb
    MenuButton* m_pOwnerButton;              // +0x84

    // +0x88: 0=normal hit, 1=menu/arcade-survive hit
    uint8_t m_bMenuBombHit;                  // +0x88

    // +0x8C: gravity vector; Init=(0,-12,0)
    Vec3 m_AccelForce;                       // +0x8C

    // +0x98: scale backup from Init
    Vec3 m_OrigScale;                        // +0x98

    // +0xA4: chuck/fuse delay countdown; 0=spawned
    float m_Countdown;                       // +0xA4

    // +0xA8: speed multiplier; 1.0 normal, ~0.666 fat
    float m_SpeedMult;                       // +0xA8

    // +0xAC: Init writes 0.0f; not otherwise read (makes sizeof 0xB0)
    float m_Field_0xAC;                      // +0xAC

    Bomb();
    ~Bomb();

    // vtable slot 2 — v1.6.1 @ 0x1d69e0
    // p1/p2 unused; p3 = nullable scale Vec3* (default 1.0)
    void Init(void* p1, long p2, Vec3* scaleOrNull) override;

    // vtable slot 3 — v1.6.1 @ 0x1d5720
    // Drops fuse emitter, clears owner-button backref, clears highestBomb, calls Entity::Release.
    void Release() override;

    // vtable slot 4 — v1.6.1 @ 0x1d6098
    void Update(float dt) override;

    // vtable slot 5 — v1.6.1 @ 0x1d6c30
    void Draw(Renderer& r) override;

    // vtable slot 6 (DrawUpdate/PostUpdate) — v1.6.1 @ 0x1d53a0
    void PostUpdate(float dt) override;

    // vtable slot 9 — v1.6.1 @ 0x1d5d4c
    // Returns 0. Triggers bomb hit effects (arcade/zen/menu branch).
    int CollisionResponse(Mortar::Entity* hitter, unsigned long flagsA, unsigned long flagsB,
                          Vec3* bladeVelocity) override;

    // v1.6.1 @ 0x1d5660 — set ENT_KILLED + clear owner backref + clear emitter
    void KillBomb();

    // v1.6.1 @ 0x0017121c — set menu-bomb marker + callback + slow spin
    void SetCallback(Mortar::Delegate0<void> cb, MenuButton* button = nullptr);

    // v1.6.1 @ 0x19d87c — returns !m_bCollisionGuard
    // ASM-verified: 2026-04-28T00:00 v1.6.1 binary @ 0x001507e0 (asm-inspector)
    bool Enabled() const { return m_bCollisionGuard == 0; }

    // v1.6.1 @ 0x12c9d8 — sets m_bCollisionGuard=1
    // ASM-verified: 2026-04-28T00:00 v1.6.1 binary @ 0x0012637c (asm-inspector)
    void Disable() { m_bCollisionGuard = 1; }

    // v1.6.1 @ 0x1d6fd4 — scale up + reduce speed (bomb-multiplier powerup)
    void MakeFat(bool skipSpawnFx);

    // v1.6.1 @ 0x1d4ca4 — set chuck countdown
    void Chuck(float delay);

    // v1.6.1 @ 0x1d6dd4 — called once from GameInitialise
    static void LoadContent();

    // v1.6.1 @ 0x1d5074
    static int GetNumActiveForPlayer(int playerIdx, bool countPrespawn);

    // v1.6.1 @ 0x1d56b4
    static void ClearUnspawned();

    // v1.6.1 @ 0x00126390
    static void SetForPlayer(Bomb* b, int playerIdx);

    // v1.6.1 @ 0x00126384
    static void SetHit(Bomb* b, float speed);

    // ASM-verified: 2026-06-18 v1.6.1 Bomb::GetHeighestBomb @ 0x001d5138 (asm-verify)
    static float GetHeighestBomb();

};

#ifdef __bada__
static_assert(__builtin_offsetof(Bomb, m_SpawnTimer)      == 0x3C, "m_SpawnTimer binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_HitCallback)     == 0x40, "m_HitCallback binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_BombVariant)     == 0x64, "m_BombVariant binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_bHit)            == 0x68, "m_bHit binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_ZPosition)       == 0x6C, "m_ZPosition binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_RotVelX)         == 0x70, "m_RotVelX binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_RotVelY)         == 0x72, "m_RotVelY binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_RotX)            == 0x74, "m_RotX binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_RotY)            == 0x76, "m_RotY binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_bCollisionGuard) == 0x78, "m_bCollisionGuard binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_pEmitter)        == 0x7C, "m_pEmitter binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_bMovement)       == 0x80, "m_bMovement binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_pOwnerButton)    == 0x84, "m_pOwnerButton binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_bMenuBombHit)    == 0x88, "m_bMenuBombHit binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_AccelForce)      == 0x8C, "m_AccelForce binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_OrigScale)       == 0x98, "m_OrigScale binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_Countdown)       == 0xA4, "m_Countdown binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_SpeedMult)       == 0xA8, "m_SpeedMult binary offset wrong");
static_assert(__builtin_offsetof(Bomb, m_Field_0xAC)      == 0xAC, "m_Field_0xAC binary offset wrong");
static_assert(sizeof(Bomb)                                == 0xB0, "sizeof(Bomb) wrong (binary 0xB0 / 176)");
#endif

// Free functions — binary symbols _Z<name> (NOT Bomb class members).
// v1.6.1 @ 0x1d6758
void CleanupBomb();
// v1.6.1 @ 0x1ca5c8
float GetBombZPosition();
// v1.6.1 @ 0x00168f24
bool BombFlashFull();
// v1.6.1 @ 0x1cf27c
void HitBomb(const Vec3& pos);
// v1.6.1 @ 0x1cf42c
void HitMenuBomb(const Vec3& pos);
// v1.6.1 @ 0x0016b73c
void DrawBombHit();
// v1.6.1 @ 0x0016a1a8
void UpdateBombHit(float prevTimer);

// World position of the last bomb hit; written by HitBomb / HitMenuBomb.
// Read by DrawBombHit.
extern Vec3 g_BombHitPos;

#endif
