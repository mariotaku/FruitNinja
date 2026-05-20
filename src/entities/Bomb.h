#ifndef FN_BOMB_H
#define FN_BOMB_H

//
// Bomb : Mortar::Entity (size = 0xB0 / 176 bytes)
// Mortar::Entity type 1. Docs: docs/entities/bomb.md
// Binary: ctor 0x171678, Init 0x172504, Update 0x1729fc, Draw 0x171be8
//
// Analysed: 2026-04-10T10:00

#include "Entity.h"
#include "math/Vec3.h"
#include "render/gl_funcs.h"
#include "util/Delegate.h"

struct PSPParticleEmitter;
class MenuButton;

// ASM-verified: 2026-04-29T00:00Z binary @ 0x001ea478 + 0x00172504 + 0x00171764 (asm-inspector, vtable slots verified)
class Bomb : public Mortar::Entity {
public:
    // +0x38: m_Col inherited from Mortar::Entity (ColSphere*, allocated in Init)

    // +0x3c: BombBlast spawn timer; Init = 0.6 (DAT_001726ac)
    float m_SpawnTimer;

    // +0x40..+0x63: hit callback delegate. Binary's `Mortar::Delegate0<void>`
    // (36 bytes). Fired when CollisionResponse hits the menu-rehit branch
    // (player slices a menu-decoration bomb that's already been registered
    // as hit). Used by Quit button to chain into the QuitGame flow.
    Mortar::Delegate0<void> m_HitCallback;

    // +0x64: bomb variant — 0=normal, 2=multiplayer
    int m_BombVariant;

    // +0x68: 0=in-flight, 1=hit/slashed
    uint8_t m_bHit;

    // +0x6c: Z layer from GetBombZPosition()
    float m_ZPosition;

    // +0x70..+0x76: rotation animation (16-bit short accumulators)
    int16_t m_RotVelX;     // +0x70: random 1..8
    int16_t m_RotVelY;     // +0x72: random 1..8
    int16_t m_RotX;        // +0x74: accumulated rotation X
    int16_t m_RotY;        // +0x76: accumulated rotation Y

    // Port specific: fractional-frame accumulators so the F7 debug
    // timescale slows bomb rotation along with physics. At 1.0x timescale
    // these hold 0 and rotation advances exactly one (m_RotVelX/Y) step
    // per frame, matching the binary. At 0.1x timescale the integer
    // part advances ~every 10 frames. No binary offset -- omitted under
    // __bada__ so the cross-build sees a binary-faithful layout.
#ifndef __bada__
    float m_RotAccumX;
    float m_RotAccumY;
#endif

    // +0x78: collision guard (prevents double-hit)
    uint8_t m_bCollisionGuard;

    // +0x7c: fuse particle emitter — lazy-created on first Update tick
    PSPParticleEmitter* m_pEmitter;

    // +0x80: 1 = physics enabled
    uint8_t m_bMovement;

    // +0x84: backref to the MenuButton that owns this bomb (Quit/back/special bombs).
    // Same +0x134 slot Fruit menu items use; cleared in KillBomb if still ours.
    // Set by screen state code (ShopScreen back-button, MainScreen Quit-bomb, etc.).
    MenuButton* m_pOwnerButton;   // +0x84

    // +0x88: 0=normal hit, 1=menu/zen hit
    uint8_t m_bMenuBombHit;

    // +0x8c..+0x94: acceleration/gravity
    Vec3 m_AccelForce;

    // +0x98..+0xa0: backup of scale from Init
    Vec3 m_OrigScale;

    // +0xa4: chuck delay timer; 0 = ready
    float m_Countdown;

    // +0xa8: speed multiplier; 1.0 normal, 0.666 for fat bombs
    float m_SpeedMult;

    // +0xac: binary has a 4-byte field here (exact purpose TBD).
    // TODO: re-verify Bomb +0xAC field from binary (gap makes sizeof 0xB0, not 0xAC).
    uint32_t m_GapField_0xAC;

    Bomb();
    ~Bomb();

    // Vtable slot 2: Binary @ 0x00172504.
    // p1 unused; p2 unused; p3 = scale Vec3* (nullable, default 1.0).
    void Init(void* p1, long p2, Vec3* scaleOrNull) override;
    void Release() override;                // 0x00171764 — fuse emitter cleanup
    void Update(float dt) override;
    void Draw(Renderer& r) override;
    void PostUpdate(float dt) override;     // 0x001714e4 — sync fuse emitter

    // Vtable slot 9: Binary @ 0x0017280c.
    // Returns 0. Triggers bomb hit effects (arcade/zen/menu branch).
    int CollisionResponse(Mortar::Entity* hitter, unsigned long flagsA, unsigned long flagsB,
                          Vec3* bladeVelocity) override;

    // Non-virtual cleanup helper: drops fuse emitter and sets ENT_INACTIVE.
    // Called by Mortar::ActorManager::Deactivate (direct, not via vtable).
    void Deactivate();

    // Matches Bomb::Chuck (0x170f68)
    void Chuck(float delay);

    // Matches Bomb::KillBomb (0x1716e8)
    void KillBomb();

    // Matches Bomb::SetCallback (0x0017121c) — installs menu-bomb hit
    // callback, stores owning MenuButton backref at +0x84, and overwrites
    // rotation state for slow menu-bomb spin.
    void SetCallback(Mortar::Delegate0<void> cb, MenuButton* button = nullptr);

    // ASM-verified: 2026-04-28T00:00 binary @ 0x001507e0 (asm-inspector)
    // Matches Bomb::Enabled (0x001507e0) — returns !m_bCollisionGuard.
    // "Enabled" == "still accepting slice collisions". Called by
    // ClearMenuItems to decide whether to fling the bomb.
    bool Enabled() const { return m_bCollisionGuard == 0; }

    // ASM-verified: 2026-04-28T00:00 binary @ 0x0012637c (asm-inspector)
    // Matches Bomb::Disable (0x0012637c) — sets m_bCollisionGuard = 1.
    // Collision ignored afterwards, but the entity keeps updating in
    // Bomb::Update's alive branch so gravity integrates and the bomb
    // falls off screen. NOT the same as Deactivate/KillBomb.
    void Disable() { m_bCollisionGuard = 1; }

    // Matches Bomb::LoadContent (0x1726c8) — called once from GameInitialise
    // Loads bomb models, textures, particle hashes into g_bombData
    static void LoadContent();

    // Matches Bomb::GetNumActiveForPlayer (0x00122a14). Returns count of
    // active Bomb entities assigned to `playerIdx` (-1 = all players).
    // When checkBombs=true counts bombs in pre-spawn state too.
    // Port specific: playerIdx filtering omitted; entity has no player-index field yet (split-screen MP stub).
    static int GetNumActiveForPlayer(int playerIdx, bool countPrespawn);

    // Matches Bomb::ClearUnspawned (0x00122ab4). Walks Mortar::ActorManager type-1
    // list and deactivates any bomb still in chuck-delay (pre-spawn) state.
    static void ClearUnspawned();

    // Matches Bomb::SetForPlayer (0x00126390). Tags bomb for a specific player
    // (used in split-screen MP to assign variant / physics mirroring).
    static void SetForPlayer(Bomb* b, int playerIdx);

    // Binary @ 0x00126384. Restores a bomb already in HIT state (different from Chuck).
    // Sets m_SpawnTimer (blast interval) and activates the entity.
    static void SetHit(Bomb* b, float speed);

    // Binary @ 0x001712c8 — float result. Used by GameUpdate to drive the
    // looping "Bomb-Fuse" SFX volume. SP path returns pos.y + 160 of the
    // visually-highest non-menu-hit bomb (filter: skip m_bMenuBombHit=1).
    // Empty / all-filtered returns -10000.0f (sentinel; GameUpdate treats
    // <= 0 as "no audible bomb"). Port omits the IsMultiplayer branch.
    static float GetHeighestBomb();

    // Matches Bomb::MakeFat (0x00171d78). Bomb-multiplier-powerup upgrade:
    // scales up bomb and reduces speed. skipSpawnFx=false plays the spawn FX.
    void MakeFat(bool skipSpawnFx);
};

#ifdef __bada__
// Binary-faithful offsets (32-bit Bada cross-build). entityType = uint8_t,
// m_RotAccumX/Y absent. Binary total = 0xB0 (176 bytes).
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
static_assert(sizeof(Bomb)                                == 0xB0, "sizeof(Bomb) wrong (binary 0xB0 / 176)");
#else
// Always-on port layout asserts (desktop x64). Offsets reflect: 8-byte vtable,
// int-widened entityType (+4), 40-byte Delegate0<void>, 8-byte pointers,
// and two extra port floats m_RotAccumX/Y (8 bytes after m_RotY).
// Binary equivalents noted in comments for parity tracking.
static_assert(offsetof(Bomb, m_SpawnTimer)      == 0x50,
    "m_SpawnTimer port offset drift (binary +0x3C)");
static_assert(offsetof(Bomb, m_HitCallback)     == 0x58,
    "m_HitCallback port offset drift (binary +0x40)");
static_assert(offsetof(Bomb, m_BombVariant)     == 0x80,
    "m_BombVariant port offset drift (binary +0x64)");
static_assert(offsetof(Bomb, m_bHit)            == 0x84,
    "m_bHit port offset drift (binary +0x68)");
static_assert(offsetof(Bomb, m_ZPosition)       == 0x88,
    "m_ZPosition port offset drift (binary +0x6C)");
static_assert(offsetof(Bomb, m_RotVelX)         == 0x8C,
    "m_RotVelX port offset drift (binary +0x70)");
static_assert(offsetof(Bomb, m_RotVelY)         == 0x8E,
    "m_RotVelY port offset drift (binary +0x72)");
static_assert(offsetof(Bomb, m_RotX)            == 0x90,
    "m_RotX port offset drift (binary +0x74)");
static_assert(offsetof(Bomb, m_RotY)            == 0x92,
    "m_RotY port offset drift (binary +0x76)");
static_assert(offsetof(Bomb, m_RotAccumX)       == 0x94,
    "m_RotAccumX port offset drift (port-specific, no binary equivalent)");
static_assert(offsetof(Bomb, m_RotAccumY)       == 0x98,
    "m_RotAccumY port offset drift (port-specific, no binary equivalent)");
static_assert(offsetof(Bomb, m_bCollisionGuard) == 0x9C,
    "m_bCollisionGuard port offset drift (binary +0x78)");
static_assert(offsetof(Bomb, m_pEmitter)        == 0xA0,
    "m_pEmitter port offset drift (binary +0x7C)");
static_assert(offsetof(Bomb, m_bMovement)       == 0xA8,
    "m_bMovement port offset drift (binary +0x80)");
static_assert(offsetof(Bomb, m_pOwnerButton)    == 0xB0,
    "m_pOwnerButton port offset drift (binary +0x84)");
static_assert(offsetof(Bomb, m_bMenuBombHit)    == 0xB8,
    "m_bMenuBombHit port offset drift (binary +0x88)");
static_assert(offsetof(Bomb, m_AccelForce)      == 0xBC,
    "m_AccelForce port offset drift (binary +0x8C)");
static_assert(offsetof(Bomb, m_OrigScale)       == 0xC8,
    "m_OrigScale port offset drift (binary +0x98)");
static_assert(offsetof(Bomb, m_Countdown)       == 0xD4,
    "m_Countdown port offset drift (binary +0xA4)");
static_assert(offsetof(Bomb, m_SpeedMult)       == 0xD8,
    "m_SpeedMult port offset drift (binary +0xA8)");
static_assert(offsetof(Bomb, m_GapField_0xAC)  == 0xDC,
    "m_GapField_0xAC port offset drift (binary +0xAC)");
static_assert(sizeof(Bomb)                      == 0xE0,
    "sizeof(Bomb) port drift (binary 0xB0; port 0xE0 due to 64-bit ptrs + entityType widening + m_RotAccum*)");
#endif

#endif
