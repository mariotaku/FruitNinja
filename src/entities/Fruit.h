#ifndef FN_FRUIT_H
#define FN_FRUIT_H

#include "Entity.h"
#include "math/Quaternion.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Mesh.h"
#include "FruitInfo.h"

struct PSPParticleEmitter;
class SlashEntity;

// Per-fruit mesh slot layout. Matches the binary's 0x24-byte
// FruitModelInfo struct allocated by LoadFruitModels (0x1794e0).
//
// Binary layout:
//   +0x00: EffectProperty* prop[2]  (per half piece)
//   +0x08: EffectProperty* prop[2..3]  (optional outline variants)
//   +0x10: Mortar::SmartPtr<Model> m_HalfA   (<name>_<c>_piece_1.mmd)
//   +0x14: Mortar::SmartPtr<Model> m_HalfB   (<name>_<c>_piece_2.mmd)
//   +0x18: Mortar::SmartPtr<Model> m_OptA    (<name>_<c>_outline.mmd etc)
//   +0x1c: Mortar::SmartPtr<Model> m_OptB    (other optional variant)
//   +0x20: ???
//
// Port simplified to just the two pieces actually rendered by the
// sliced-fruit draw path. Outline/extras deferred.
struct FruitModelInfo {
    Mortar::SmartPtr<Mortar::Model> m_HalfA;   // piece 1
    Mortar::SmartPtr<Mortar::Model> m_HalfB;   // piece 2
};

// Matches original Fruit : Mortar::Entity
// Physics: ballistic arc with quaternion rotation, 2-body split on slice
// ASM-verified: 2026-04-29T00:00Z binary @ 0x001764dc + 0x00176708 (asm-inspector, base-shift unaffected)
// Binary sizeof(Fruit) = 0x118 (280). EntityFactory @ 0x0017421c:
// operator_new(0x118). Layout cross-verified by re-analyst 2026-05-17
// against Ghidra struct + Init/Update/Ctor/IsOffscreen disassembly.
class Fruit : public Mortar::Entity {  // Entity = 60 bytes, ends at +0x3B
public:
    uint8_t  m_FruitType;                  // +0x3C  (binary: u8, NOT int)
    uint8_t  m_bNoPowerUp;                 // +0x3D
    uint8_t  _pad_3E[2];                   // +0x3E..+0x3F
    PSPParticleEmitter* m_pEmitter1;       // +0x40  (was wrongly +0x80)
    PSPParticleEmitter* m_pEmitter2;       // +0x44  (was wrongly +0x84)
    Vec3     m_SlicePos;                   // +0x48..+0x53  (was wrongly +0x78)
    uint8_t  _pad_54[12];                  // +0x54..+0x5F  (unused by Init/Update; possible Slice/CollisionResponse temp)
    int32_t  m_LifetimeCounter;            // +0x60  (init=0; Update int<->float trick)
    int32_t  m_CollisionSize;              // +0x64  (init=75; semantics: probable countdown, NOT radius)
    int32_t  m_field_0x68;                 // +0x68  (init=4; TODO: semantics; not read by Init/Update)
    float    m_SliceTimer;                 // +0x6C  (init=-1.0)
    uint16_t m_SliceAngle;                 // +0x70
    uint8_t  _pad_72[2];                   // +0x72..+0x73
    float    m_SliceImpulse;               // +0x74
    int32_t  m_SliceState;                 // +0x78  (init=0; written by Slice/CollisionResponse)
    uint8_t  m_bActive;                    // +0x7C  (gates physics in unsliced branch)
    uint8_t  _pad_7D[3];                   // +0x7D..+0x7F
    float    m_ChuckDelay;                 // +0x80  (init=0.0)
    Vec3     m_RotAxis;                    // +0x84..+0x8F
    int32_t  m_PlayerIdx;                  // +0x90  (init=0)
    float    m_TimeScale;                  // +0x94  (init=1.0)
    float    m_ZPosition;                  // +0x98
    Vec3     m_Gravity;                    // +0x9C..+0xA7
    uint8_t  _pad_A8[12];                  // +0xA8..+0xB3  (unused)
    uint8_t  m_bSliced;                    // +0xB4  (init=0)
    uint8_t  _pad_B5[3];                   // +0xB5..+0xB7
    Vec3     m_SecondPos;                  // +0xB8..+0xC3  (Ghidra name: m_HalfB_pos)
    Vec3     m_SecondVel;                  // +0xC4..+0xCF  (Ghidra name: m_HalfB_vel)
    Quaternion m_Rot1;                     // +0xD0..+0xDF
    Quaternion m_Rot2;                     // +0xE0..+0xEF
    Vec3     m_RotVel1;                    // +0xF0..+0xFB
    Vec3     m_RotVel2;                    // +0xFC..+0x107
    SlashEntity* m_pSlasher;               // +0x108  (init=0)
    uint8_t  m_bSpawnedByCriticalSplash;   // +0x10C
    uint8_t  m_bCriticalEligible;          // +0x10D  (init=1)
    uint8_t  _pad_10E[2];                  // +0x10E..+0x10F
    float    m_ScaleAnim;                  // +0x110  (init=0.0)
    uint8_t  m_bDrawWhole;                 // +0x114  (init=0)
    uint8_t  _pad_115[3];                  // +0x115..+0x117  -> total sizeof = 0x118

    // Port-only fields (no binary slot; appended after 0x118 boundary on
    // host build only so binary layout above is unaffected under __bada__).
#ifndef __bada__
    // Model loaded via MeshManager (shared/cached)
    Mortar::SmartPtr<Mortar::Model> m_Model;
#endif

    Fruit();
    ~Fruit();

    // Vtable slot 2: Binary @ 0x00176708.
    // p2 = fruitType (0..N-1); p3 = scale Vec3* (nullable, default 1.0); p1 unused.
    void Init(void* p1, long fruitType, Vec3* scaleOrNull) override;
    void Update(float dt) override;
    void Draw(Renderer& r) override;
    void PostUpdate(float dt) override;   // 0x0017501c — screen-edge bounce / push

    // Vtable slot 9: Binary @ 0x001780b0.
    // Returns 1 if already sliced (early-out). Otherwise records slice state,
    // spawns juice emitters, plays SFX, updates score/combo/achievements.
    int CollisionResponse(Mortar::Entity* hitter, unsigned long flagsA, unsigned long flagsB,
                          Vec3* bladeVelocity) override;

    // Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
    void Deactivate();

    // Matches Fruit::Slice (0x176d58, simplified). Flips m_bSliced,
    // computes halfVel/halfVelB from m_SliceAngle, blends with old vel,
    // marks the fruit as two-body. Called from Update when m_SliceTimer
    // hits zero.
    void Slice();

    // Matches Fruit::Sliced @ 0x001401c8. Pure predicate: returns true
    // if the fruit is already sliced (m_bSliced) OR if a slice countdown
    // is active (m_SliceTimer > -1.0f). Used by ShopScreen::ShrinkBuyButton
    // to skip the shrink trigger when the equip-button fruit is already
    // retracting.
    bool Sliced() const {
        return m_bSliced || (m_SliceTimer > -1.0f);
    }

    // Matches binary `Fruit::Chuck(float)`. Velocity is read from this->vel
    // -- callers must set `vel` before calling.
    void Chuck(float delay);

    // Matches Fruit::CheckHasGoneOffscreen (0x00175218). Returns true
    // only when BOTH halves are past the offscreen boundary with outward
    // velocity. Also bounces sliced halves off the near edge.
    bool CheckHasGoneOffscreen();

    // Matches Fruit::KillFruit (0x00176abc). Clears emitters, applies
    // miss penalty (TODO), and marks the entity killed (flags |= 0x10).
    void KillFruit(bool doMissPenalty);

    // Matches Fruit::RotateFacingUp (0x001757f4). Sets m_Rot1/m_Rot2 to a
    // random starting orientation and m_RotVel1/m_RotVel2 to axisScale * scalar.
    // When flag=true, additional q_axis * q_up composition is applied to each
    // rotation slot. See Fruit.cpp for full algorithm.
    void RotateFacingUp(bool flag, Vec3 axisScale);

    // Matches Fruit::FruitType (0x00175b10). Resolves a fruit name
    // string to the index in the FRUIT_INFO array by hashing and
    // comparing against m_NameHash / m_NameHashUpper. If not found:
    //   fallbackRandom=true → returns Random::Rand32(count-1)
    //   fallbackRandom=false → returns -1 (0xFFFFFFFF)
    static int FruitType(const char* name, bool fallbackRandom);

    // Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
    // Parses Data/xml/fruitlist.xml into FRUIT_INFO array
    static void LoadInfo();

    // Matches Fruit::LoadFruitModels (0x1794e0). Allocates the
    // per-fruit FruitModelInfo array and loads `<name>_<c>_piece_1.mmd`
    // and `<name>_<c>_piece_2.mmd` for each fruit entry via
    // MeshManager. Called once from GameInitialise.
    static void LoadFruitModels();

    // Accessor for a per-fruit pair of half meshes. Returns nullptr if
    // index out of range or LoadFruitModels hasn't run.
    static const FruitModelInfo* GetFruitModelInfo(int fruitType);

    // Binary @ 0x00176564: 4-path weighted selector.
    // includeOnSide=true: all fruits; false: m_bScorable-only subset.
    // Critical mode (WaveManager::CriticalMode) restricts to m_bSpecial fruits.
    static int RandomFruit(bool includeOnSide);

    // Matches Fruit::GetNumActiveForPlayer (0x00122a00). Returns count of
    // active Fruit entities assigned to `playerIdx` (-1 = all players).
    // When checkBombs=true the caller also wants bomb counts (not used here).
    // Port specific: playerIdx filtering omitted; entity has no player-index field yet (split-screen MP stub).
    static int GetNumActiveForPlayer(int playerIdx, bool checkBombs);

    // Matches Fruit::ClearUnspawned (0x001762a0). Walks Mortar::ActorManager type-0
    // list and deactivates any fruit still in pre-spawn (chuck-delay) state.
    // Binary param: false = don't deactivate already-visible fruits.
    static void ClearUnspawned(bool deactivateVisible);

    // Matches Fruit::Disable (binary @ 0x00126374). Sets m_bNoPowerUp = 1,
    // suppressing miss penalty and power-up activation for this fruit.
    // Used by ClearMenuItems on dojo-transition retract path.
    static void Disable(Fruit* f);

    // @ 0x0016ba6e — draw drop-shadows for all active fruits.
    static void DrawShadows();

    // @ 0x00175ea0 — per-fruit shadow emitter called by DrawShadows.
    // Writes 1 spawn-fade quad and up to 2 per-half quads into *out.
    void AddShadow(struct QUADCUSTOMVERTEX** out, int* outCount);

    // Binary @ 0x00174f18 — return ptr to FRUIT_INFO[type].m_Name
    static const char* FruitTypeName(long type);
    // Binary @ 0x00174f38 — return FRUIT_INFO[type].m_NameHash
    static unsigned long FruitTypeHash(long type);
    // Binary @ 0x00174f5c — return ptr to FRUIT_INFO[type].m_FactTexture
    static const char* FruitFactTexture(long type);
    // Binary @ 0x00174f80 — FRUIT_INFO[type].m_FruitColour; one-fruit override via g_SpecialFruitIdx/g_SpecialFruitColour (unresolved DATs)
    static Colour FruitTypeColour(long type);
    // Binary @ 0x00174fc8 — return FRUIT_INFO[type].m_FactColour
    static Colour FruitFactColour(long type);
    // Binary @ 0x00174ff8 — equivalent of FruitInfo_Get(); preserved for binary call-shape parity
    // Fully-qualified return type avoids GCC -Wchanges-meaning since the
    // method name shadows the struct.
    static const ::FruitInfo* FruitInfo(long type);

    // Binary @ 0x00176184 — local-MP "did a player drop their last life" check; defers to FN::GameOver
    static void CheckFruitDropped();

    // Binary @ 0x00175624 — "is either half outside the play field" predicate
    bool IsOffscreen() const;

    // Binary @ 0x00176354 — toggle collision sphere; radius = m_CollisionScale + 0.52 * m_Scale
    void EnableCollision(bool enable);

    // Binary @ 0x00175b78 — sets m_PlayerIdx; online-MP side-effect: P2 collision radius *= 0.66 (Defunct)
    void SetForPlayer(int playerIdx);

    // Binary @ 0x001761d8 — virtual Mortar::Entity::Release override
    void Release() override;

    // Binary @ 0x00175ba4 — fact-of-the-day picker with save-data round-robin
    static const char* GetFact(int* outType, int* outFactIdx, int fruitType, int factIdx);

    // Binary @ 0x001756dc — replace m_pEmitter1 with a custom trail emitter
    bool SetTrailParticles(unsigned long emitterHash);

    // Binary @ 0x00175988 — push bombs away from this fruit (X axis) when within 70px and matching velocity
    void UpdateBombAvoidance(float dt);

    // Binary @ 0x0017911c — releases the FruitModelInfo[] array + per-MP-player SmartPtr slots; called on shutdown
    static void DestroyFruitModels();
};

#ifdef __bada__
static_assert(sizeof(Fruit) == 0x118, "Fruit sizeof must match binary 0x118");
static_assert(__builtin_offsetof(Fruit, m_FruitType)                == 0x3C, "");
static_assert(__builtin_offsetof(Fruit, m_bNoPowerUp)               == 0x3D, "");
static_assert(__builtin_offsetof(Fruit, m_pEmitter1)                == 0x40, "");
static_assert(__builtin_offsetof(Fruit, m_pEmitter2)                == 0x44, "");
static_assert(__builtin_offsetof(Fruit, m_SlicePos)                 == 0x48, "");
static_assert(__builtin_offsetof(Fruit, m_LifetimeCounter)          == 0x60, "");
static_assert(__builtin_offsetof(Fruit, m_CollisionSize)            == 0x64, "");
static_assert(__builtin_offsetof(Fruit, m_SliceTimer)               == 0x6C, "");
static_assert(__builtin_offsetof(Fruit, m_SliceAngle)               == 0x70, "");
static_assert(__builtin_offsetof(Fruit, m_SliceImpulse)             == 0x74, "");
static_assert(__builtin_offsetof(Fruit, m_SliceState)               == 0x78, "");
static_assert(__builtin_offsetof(Fruit, m_bActive)                  == 0x7C, "");
static_assert(__builtin_offsetof(Fruit, m_ChuckDelay)               == 0x80, "");
static_assert(__builtin_offsetof(Fruit, m_RotAxis)                  == 0x84, "");
static_assert(__builtin_offsetof(Fruit, m_PlayerIdx)                == 0x90, "");
static_assert(__builtin_offsetof(Fruit, m_TimeScale)                == 0x94, "");
static_assert(__builtin_offsetof(Fruit, m_ZPosition)                == 0x98, "");
static_assert(__builtin_offsetof(Fruit, m_Gravity)                  == 0x9C, "");
static_assert(__builtin_offsetof(Fruit, m_bSliced)                  == 0xB4, "");
static_assert(__builtin_offsetof(Fruit, m_SecondPos)                == 0xB8, "");
static_assert(__builtin_offsetof(Fruit, m_SecondVel)                == 0xC4, "");
static_assert(__builtin_offsetof(Fruit, m_Rot1)                     == 0xD0, "");
static_assert(__builtin_offsetof(Fruit, m_Rot2)                     == 0xE0, "");
static_assert(__builtin_offsetof(Fruit, m_RotVel1)                  == 0xF0, "");
static_assert(__builtin_offsetof(Fruit, m_RotVel2)                  == 0xFC, "");
static_assert(__builtin_offsetof(Fruit, m_pSlasher)                 == 0x108, "");
static_assert(__builtin_offsetof(Fruit, m_bSpawnedByCriticalSplash) == 0x10C, "");
static_assert(__builtin_offsetof(Fruit, m_bCriticalEligible)        == 0x10D, "");
static_assert(__builtin_offsetof(Fruit, m_ScaleAnim)                == 0x110, "");
static_assert(__builtin_offsetof(Fruit, m_bDrawWhole)               == 0x114, "");
#endif

#endif
