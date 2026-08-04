#ifndef FN_BOMB_H
#define FN_BOMB_H

//
// Bomb : Mortar::Entity (sizeof = 0xB0 / 176 bytes)
// v1.6.1 layout. sizeof confirmed from operator new in Bomb ctor chain.
// vtable @ 0x002CE858, 13 slots.
//

#include "Entity.h"
#include "math/_Vector3.h"
#include "render/gl_funcs.h"
#include "util/Delegate.h"
#include "util/SmartPtr.h"
#include "asset/Model.h"
#include "asset/Texture.h"

struct PSPParticleEmitter;
class MenuButton;
class BombFlash;
class Bomb;

// Global bomb data block (v1.6.1 binary @ 0x31785C, size 0x48 = 72 bytes).
// Mirrors the binary's contiguous static block for all Bomb global state,
// including the BombFlash pool. Fields match the binary offsets exactly.
struct BombGlobalData {
    Bomb*                             pTrackedBomb;           // +0x00 highest-drawn bomb this frame
    Mortar::SmartPtr<Mortar::Texture> s_flashTexture0;        // +0x04 Dead: declared in v1.6.1 but never loaded -- present to hold binary layout
    Mortar::SmartPtr<Mortar::Texture> s_flashTexture1;        // +0x08 Dead: declared in v1.6.1 but never loaded -- present to hold binary layout
    Mortar::SmartPtr<Mortar::Model>   model[2];               // +0x0C [0]=bomb.mmd, [1]=bomb_purple.mmd
    int                               guard_bombsHitHash;     // +0x14 __cxa_guard for CollisionResponse static-local hash
    uint32_t                          bombsHitHash;           // +0x18 StringHash("bombs_hit"), lazily set in CollisionResponse
    Mortar::SmartPtr<Mortar::Texture> texMinus10;             // +0x1C minus_10.tex
    uint8_t                           bFuseSfxFiredThisFrame; // +0x20 (s_sfxPlayedThisFrame in binary) per-frame fuse-SFX gate; Draw clears it
    // +0x21..+0x23: padding
    uint32_t                          fuseHash[2];            // +0x24 [0]=StringHash("bomb_smoke"), [1]=StringHash("purple_bomb_smoke")
    Mortar::SmartPtr<Mortar::Texture> m_blastTexture;         // +0x2C bomb_explode.tex (absorbed from former g_BombTexture global)
    bool                              loaded;                  // +0x30 s_isContentLoaded; set at end of LoadContent
    // +0x31..+0x33: padding
    uint32_t                          _pad34;                 // +0x34 DefaultBackgroundColour is a separate file-scope global in the binary; 4 bytes reserved here so s_arcadeBombModel lands at +0x38
    Mortar::SmartPtr<Mortar::Model>   s_arcadeBombModel;      // +0x38 Dead: declared in v1.6.1 (@0x317894) but never loaded/read -- present to hold binary layout
    BombFlash*                        pool;                   // +0x3C BombFlash pool heap ptr
    int                               poolCount;              // +0x40
    int                               currentFree;            // +0x44

    BombGlobalData()
        : pTrackedBomb(nullptr)
        , guard_bombsHitHash(0)
        , bombsHitHash(0)
        , bFuseSfxFiredThisFrame(0)
        , loaded(false)
        , _pad34(0)
        , pool(nullptr)
        , poolCount(0)
        , currentFree(0)
    {
        fuseHash[0] = fuseHash[1] = 0;
    }
};

extern BombGlobalData g_bombData;

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

    // +0x70..+0x77: rotation state. Update: alive arm does a plain wrapping
    // int16 rot+=vel (@0x1d6654); menu-hit arm adds vel*dtNorm with u32
    // truncation (@0x1d624c).
    // DIFFERS: opt-in time-scaled alive-bomb spin (FN::g_BombSpinTimeScaled,
    // src/debug/DebugFlags.h) multiplies the alive arm's step by dtNorm too, so
    // slow-mo slows the bomb like it slows the fruit. Default OFF = the faithful
    // unscaled add; the flag does not exist under __bada__.
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
    _Vector3<float> m_AccelForce;                       // +0x8C

    // +0x98: scale backup from Init
    _Vector3<float> m_OrigScale;                        // +0x98

    // +0xA4: chuck/fuse delay countdown; 0=spawned
    float m_Countdown;                       // +0xA4

    // +0xA8: speed multiplier; 1.0 normal, ~0.666 fat
    float m_SpeedMult;                       // +0xA8

    // +0xAC: binary writes 0.0f in Init/ctor and never reads it (pads sizeof to 0xB0).
    float m_Field_0xAC;                      // +0xAC

    Bomb();
    ~Bomb();

    // vtable slot 2 — v1.6.1 @ 0x1d69e0
    // p1/p2 unused; p3 = nullable scale Vec3* (default 1.0)
    void Init(void* p1, long p2, _Vector3<float>* scaleOrNull) override;

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
                          _Vector3<float>* bladeVelocity) override;

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

    // v1.6.1 Bomb::IsActive @0x001e2ec0: m_Countdown <= 0
    // Non-virtual name-hiding of Mortar::Entity::IsActive -- NOT an override.
    // Binary declares this per-type predicate (chuck-delay gate), not the
    // base flags&0x11 test. Callers must hold a Bomb* for this to apply.
    bool IsActive() const { return m_Countdown <= 0.0f; }

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

    // v1.6.1 @ 0x1d5030 — disables every active type-1 bomb (SuperFruitControl
    // finale bomb-suppress window). Static; ignores its Bomb* arg in the binary.
    static void DeactivateAll();

    // ASM-spec v1.6.1 Bomb::SetForPlayer @0x0012702c
    void SetForPlayer(int playerIdx);

    // ASM-spec v1.6.1 Bomb::SetHit @0x0012c9e4
    void SetHit(float speed);

    // ASM-verified: 2026-06-18 v1.6.1 Bomb::GetHeighestBomb @ 0x001d5138 (asm-verify)
    static float GetHeighestBomb();

    // v1.6.1 Bomb::GetWait @0x00155e68 -- thunk returning the chuck/fuse delay
    // countdown (m_Countdown, +0xA4); the Bomb analogue of Fruit::m_SpawnDelay.
    // Read by SaveGame's <ent> bomb pass to persist the airborne bomb's wait.
    float GetWait();

};

#ifdef __bada__
static_assert(__builtin_offsetof(BombGlobalData, model)             == 0x0C, "BombGlobalData::model binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, bombsHitHash)      == 0x18, "BombGlobalData::bombsHitHash binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, texMinus10)        == 0x1C, "BombGlobalData::texMinus10 binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, m_blastTexture)    == 0x2C, "BombGlobalData::m_blastTexture binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, loaded)            == 0x30, "BombGlobalData::loaded binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, s_arcadeBombModel) == 0x38, "BombGlobalData::s_arcadeBombModel binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, pool)              == 0x3C, "BombGlobalData::pool binary offset wrong");
static_assert(__builtin_offsetof(BombGlobalData, currentFree)       == 0x44, "BombGlobalData::currentFree binary offset wrong");
static_assert(sizeof(BombGlobalData)                                == 0x48, "sizeof(BombGlobalData) wrong (binary 0x48 / 72)");
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
// v1.6.1 @ 0x001ca5e8 — single `bx lr` no-op in binary; serves both Bomb + Fruit TUs via PLT.
void SetupLighting(const Mortar::SmartPtr<Mortar::Model>&);
// v1.6.1 @ 0x1d6758
void CleanupBomb();
// v1.6.1 @ 0x1ca5c8
float GetBombZPosition();
// v1.6.1 @ 0x00168f24
bool BombFlashFull();
// v1.6.1 @ 0x1cf27c
void HitBomb(_Vector3<float> pos);
// v1.6.1 @ 0x1cf42c
void HitMenuBomb(_Vector3<float> pos);
// v1.6.1 @ 0x0016b73c
void DrawBombHit();
// v1.6.1 @ 0x0016a1a8
void UpdateBombHit(float prevTimer);

// World position of the last bomb hit; written by HitBomb / HitMenuBomb.
// Read by DrawBombHit.
extern _Vector3<float> g_BombHitPos;

#endif
