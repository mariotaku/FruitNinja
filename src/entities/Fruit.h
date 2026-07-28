#ifndef FN_FRUIT_H
#define FN_FRUIT_H

#include "Entity.h"
#include "math/Quaternion.h"
#include "math/Colour.h"
#include "util/SmartPtr.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include "FruitInfo.h"
#include "engine/util/Event.h"

struct PSPParticleEmitter;

// Per-fruit mesh slot layout. Matches the binary's 0x24-byte
// FruitModelInfo struct allocated by LoadFruitModels (0x1794e0).
// Binary allocates new uint32_t[N*0x24 + 8] (sizeof element = 0x24).
//
// Binary layout (LoadFruitModels @ 0x001794e0):
//   +0x00: EffectProperty* m_pHalfEffectA   (per half piece A)
//   +0x04: EffectProperty* m_pHalfEffectB   (per half piece B)
//   +0x08: EffectProperty* m_pWholeEffect   (set only if whole .mmd exists)
//   +0x0c: EffectProperty* m_pMpEffect      (set only if MP .mmd exists)
//   +0x10: Mortar::SmartPtr<Model> m_HalfA  (<name>_<c>_piece_1.mmd)
//   +0x14: Mortar::SmartPtr<Model> m_HalfB  (<name>_<c>_piece_2.mmd)
//   +0x18: Mortar::SmartPtr<Model> m_Whole  (<name>_single.mmd)
//   +0x1c: Mortar::SmartPtr<Model> m_pMpModel (MP variant .mmd)
//   +0x20: uint32_t pad (unused/reserved)
//
// g_MPModelsLoaded is NOT a FruitModelInfo field -- it lives at
// GameTaskState+0xc4 and gates the LoadFruitModels MP load path.
struct FruitModelInfo {
    Mortar::EffectProperty*  m_pHalfEffectA;                       // +0x00
    Mortar::EffectProperty*  m_pHalfEffectB;                       // +0x04
    Mortar::EffectProperty*  m_pWholeEffect;                       // +0x08
    Mortar::EffectProperty*  m_pMpEffect;                          // +0x0c
    Mortar::SmartPtr<Mortar::Model> m_HalfA;    // +0x10 piece 1
    Mortar::SmartPtr<Mortar::Model> m_HalfB;    // +0x14 piece 2
    Mortar::SmartPtr<Mortar::Model> m_Whole;    // +0x18 whole fruit (<name>_single.mmd)
    Mortar::SmartPtr<Mortar::Model> m_pMpModel; // +0x1c MP variant
    uint32_t                         m_pad;     // +0x20 unused/reserved
};

#ifdef __bada__
static_assert(sizeof(FruitModelInfo) == 0x24, "FruitModelInfo sizeof must match binary 0x24");
#endif

// Matches original Fruit : Mortar::Entity
// Physics: ballistic arc with quaternion rotation, 2-body split on slice
// ASM-spec v1.6.1 Fruit::Fruit @0x001dc4f0 (ctor), Fruit::Init @0x001e2898, Fruit::Update @0x001df828, Fruit::CheckHasGoneOffsceen @0x001df304
// Binary sizeof(Fruit) = 0x18c (396). v1.6.1 CreateEntity does operator_new(0x18c).
// Tail fields 0x118..0x18c (events + padding) added per #31 spec.
// 0x00..0x118 layout cross-verified by re-analyst 2026-05-17
// against Ghidra struct + Init/Update/Ctor/IsOffscreen disassembly.
class Fruit : public Mortar::Entity {  // Entity = 60 bytes, ends at +0x3B
public:
    // ASM-spec v1.6.1 Fruit::NEW_LIFE_AT: score-milestone that restores one life in AddToCurrentScore.
    static const int NEW_LIFE_AT = 100;

    uint8_t  m_FruitType;                  // +0x3C  (binary: u8, NOT int)
    uint8_t  m_bNoPowerUp;                 // +0x3D
    uint8_t  _pad_3E[2];                   // +0x3E..+0x3F
    PSPParticleEmitter* m_pEmitter1;       // +0x40  (was wrongly +0x80)
    PSPParticleEmitter* m_pEmitter2;       // +0x44  (was wrongly +0x84)
    _Vector3<float> m_SlicePos;                   // +0x48..+0x53  (was wrongly +0x78)
    uint8_t  _pad_54[12];                  // +0x54..+0x5F  (unused by Init/Update; possible Slice/CollisionResponse temp)
    // +0x60: spin-phase accumulator. Update (sliced path, frozen=0): m_SpinPhase = (int)((float)m_SpinPhase + dtScaled*1000.0f).
    // Binary @0x001e009c reads/writes this field.
    int32_t  m_SpinPhase;                  // +0x60  binary @0x001e009c
    int32_t  m_CollisionSize;              // +0x64  (init=75; semantics: probable countdown, NOT radius)
    // +0x68: write-only field, set to 4 by Init (binary @ 0x001769d0).
    // No reader exists anywhere in the binary — confirmed by full-binary
    // [reg,#0x68] scan 2026-05-20. Likely dead code from a removed feature.
    // Kept for layout fidelity; do NOT route any logic through this field.
    // Same status as m_VisualScale (+0xAC).
    int32_t m_VestigialInitFour;            // +0x68 (init=4; write-only / dead)
    // +0x6C: binary role not yet RE'd; old port misused this slot as the slice-countdown timer
    // but the real countdown lives at 0xBC (m_SliceTimer). Kept as pad for layout fidelity.
    // TODO: v1.6.1 Fruit::Init @0x001e2898 (writes field_0x6c=0) -- determine true binary semantics of the +0x6C field.
    uint8_t  _pad_6C[4];                   // +0x6C..+0x6F  (role unknown)
    uint8_t  m_bBallisticEnable;           // +0x70  u8 gate: Update runs gravity arc when !=0; CreateFruit menu-path sets =0
    uint8_t  _pad_71[3];                   // +0x71..+0x73
    float    m_SpawnDelay;                 // +0x74  chuck countdown; Chuck sets; Update decrements -> fires m_OnExpired at <=0
    _Vector3<float> m_AccelTerm;                  // +0x78..+0x83  extra accel/jerk: Update unsliced pos += m_AccelTerm*dt; damped 0.9x in PostUpdate
    int32_t  m_PlayerIdx;                  // +0x84  SetForPlayer writes; Slice/Update read for trail-hash/MakeCritical
    // +0x88: bounce timer for reverse-time un-slice. Zeroed at the top of
    // Slice (v1.6.1 Fruit::Slice @0x001dcba0). Update (sliced path): += dtScaled;
    // if (dtScaled<0 && m_SliceBounceTimer<0) un-slice back to whole. Binary @0x001df9d8.
    float    m_SliceBounceTimer;           // +0x88  binary @0x001df9d8
    // +0x8C..+0x97: velocity snapshot written at the top of Slice
    // (v1.6.1 Fruit::Slice @0x001dcba0, = vel at +0x1C);
    // copied back to vel by the reverse-time un-slice path (@0x001df9e8).
    _Vector3<float> m_SliceVelocity;              // +0x8C  (x=+0x8C, y=+0x90, z=+0x94)
    float    m_TimeScale;                  // +0x98  Update dt_eff = dt * m_TimeScale; SpawnFruit writes (p_pad+0x5c)
    float    m_ZPosition;                  // +0x9C  depth; CreateFruit sets =150.0f; Update emitter-Z; Slice -> MoveFruitZPositionToBack
    _Vector3<float> m_Gravity;                    // +0xA0..+0xAB  Update integrates vel += m_Gravity*dt; m_bMenuFling(@+0x164) == m_bExtraScore in binary: boosts gravity grow by 6.5x factor
    // +0xAC..+0xB7 -- duplicate of entity scale (+0x28); written by SetFruitType
    // for both slots but never read by Draw/Shadows/AddShadow/KillFruit (all read +0x28).
    // Vestigial write-only cache kept for binary layout fidelity.
    // Do NOT route rendering through this field.
    _Vector3<float> m_VisualScale;                // +0xAC: vestigial write-only cache; binary @ 0x00176290 stm r8,{r0,r1,r2}; SetFruitType writes 0xAC/B0/B4

    // --- 0xB8..0x16F region corrected per d3d8647 + fruit-0x60-region.md ---
    // All offsets verified against v1.6.1 binary Fruit ctor (0x1dc4f0), Update (0x1df828),
    // Slice (0x1dcba0), Draw (0x1e0524), CheckHasGoneOffscreen (0x1df304),
    // SetupSliceRotations (0x1da968), RotateFacingUp (0x1db478),
    // KillFruit (0x1deba8), MenuButton::CreateFruit (0x19b608).
    uint8_t  m_bSliced;                    // +0xB8  Slice writes=1; Update/Draw/Check/KillFruit read p_pad[0x7c]
    uint8_t  _pad_B9[3];                   // +0xB9..+0xBB  align
    float    m_SliceTimer;                 // +0xBC  Slice sets; Update countdown -= dt; init=-1; rest=-1
    uint16_t m_SliceArcAngle;              // +0xC0  Slice r/w p_pad+0x84; CollisionResponse Atan2Idx
    uint8_t  _pad_C2[2];                   // +0xC2..+0xC3  align
    float    m_SliceArcImpulse;            // +0xC4  Slice p_pad+0x88: base half speed; CollisionResponse SetMagnitude
    _Vector3<float> m_SecondPos;                  // +0xC8..+0xD3  sliced 2nd-half position (Update/Draw/Check p_pad+0x8c/90/94)
    _Vector3<float> m_SecondVel;                  // +0xD4..+0xDF  sliced 2nd-half velocity (Update/Slice/Check p_pad+0x98/9c/a0)
    Quaternion m_Rot1;                     // +0xE0..+0xEF  ctor-built p_pad+0xa4; Update/Draw/Setup half0
    Quaternion m_Rot2;                     // +0xF0..+0xFF  ctor-built p_pad+0xb4; Update/Draw/Setup half1
    _Vector3<float> m_RotVel1;                    // +0x100..+0x10B  per-half0 spin rates (RotateFacingUp/Update/Setup/CreateFruit)
    _Vector3<float> m_RotVel2;                    // +0x10C..+0x117  per-half1 spin rates
    _Vector3<float> m_SliceAxes[6];               // +0x118..+0x15F  [2 halves][3 axes] spin axes 0x48 bytes;
                                           //   SetupSliceRotations writes per-half stride 0x24;
                                           //   Update reads this+i*0x24+0x118/+0x124/+0x130
    Mortar::Entity* m_pOwner;              // +0x160  slasher/owner back-ref; ctor=0; CreateFruit sets;
                                           //   KillFruit: if(owner && owner+0x14C==this) owner+0x14C=0
    // +0x164: binary field read at @0x001df908 as "m_bExtraScore" extra-score gate (gravity 6.5x grow).
    // Also set=1 by MenuButton::CreateFruit to mark menu-context fruits (no z-push in Slice).
    // Named m_bMenuFling in port for the menu-fruit use case; both semantics reside in the same byte.
    uint8_t  m_bMenuFling;                 // +0x164  binary: m_bExtraScore @0x001df908; also menu-fling gate in Slice
    uint8_t  m_bCritical;                  // +0x165  gameplay critical/super flag (CollisionResponse writes; Update/Setup/Slice read)
    uint8_t  _pad_166[2];                  // +0x166..+0x167  align
    // +0x168: menu grow/scale-in [0..1], ramps += dt*3 toward 1.0 while m_bDrawWhole==0
    float    m_MenuGrowFade;               // +0x168
    uint8_t  m_bFrozen;                    // +0x16C  freeze gate: Update skips physics+spin when !=0
    uint8_t  _pad_16D[3];                  // +0x16D..+0x16F  align
    Mortar::Event3<Fruit*, int, Mortar::Entity*> m_OnSliced;  // +0x170 (8B) fired in CollisionResponse
    Mortar::Event1<Fruit*> m_OnKilled;     // +0x178 (8B) fired in KillFruit; SuperFruitGlow subscribes
    Mortar::Event1<Fruit*> m_OnExpired;    // +0x180 (8B) fired in Update (+0x74 <= 0 path)
    // +0x188: draw-whole / is-menu-display flag. MenuButton sets=1 when halves at rest;
    // Fruit::Draw: "p_pad[0x7c]==0 || p_pad[0x14c]!=0" -> draw whole mesh. Binary: 0x1e0524.
    // Also gates m_MenuGrowFade ramp in Update sliced-branch (grows while this==0).
    uint8_t  m_bDrawWhole;                 // +0x188
    uint8_t  _pad_189[3];                  // +0x189..+0x18B  trailing pad -> sizeof 0x18c

    Fruit();
    ~Fruit();

    // Vtable slot 2: Binary @ 0x00176708.
    // p2 = fruitType (0..N-1); p3 = scale Vec3* (nullable, default 1.0); p1 unused.
    void Init(void* p1, long fruitType, _Vector3<float>* scaleOrNull) override;
    void Update(float dt) override;
    void Draw(Renderer& r) override;
    void PostUpdate(float dt) override;   // 0x0017501c — screen-edge bounce / push

    // Vtable slot 9: Binary @ 0x001780b0.
    // Returns 1 if already sliced (early-out). Otherwise records slice state,
    // spawns juice emitters, plays SFX, updates score/combo/achievements.
    int CollisionResponse(Mortar::Entity* hitter, unsigned long flagsA, unsigned long flagsB,
                          _Vector3<float>* bladeVelocity) override;

    // Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
    void Deactivate();

    // Matches Fruit::Slice (0x176d58, simplified). Flips m_bSliced,
    // computes halfVel/halfVelB from m_SliceArcAngle, blends with old vel,
    // marks the fruit as two-body. Called from Update when m_SliceTimer
    // hits zero.
    void Slice();

    // Binary @ 0x001DA968. Fills m_SliceAxes[0..5] and m_RotVel1/2 with
    // per-half spin axes and angular velocities, then builds the initial
    // m_Rot1/2 quaternions from the slice arc angle.
    // Called at the end of Slice() with (FruitInfo->m_bIsSuperFruit, sliceDirFlag).
    // ASM-spec v1.6.1 Fruit::SetupSliceRotations @0x001da968: 2nd param is bool, not int.
    void SetupSliceRotations(bool isSuperFruit, bool sliceDirFlag);

    // Matches Fruit::Sliced @ 0x001401c8. Pure predicate: returns true
    // if the fruit is already sliced (m_bSliced) OR if a slice countdown
    // is active (m_SliceTimer > -1.0f). Used by ShopScreen::ShrinkBuyButton
    // to skip the shrink trigger when the equip-button fruit is already
    // retracting.
    bool Sliced() const {
        return m_bSliced != 0 || (m_SliceTimer > -1.0f);
    }

    // v1.6.1 Fruit::IsActive @0x00121c80: m_SpawnDelay <= 0
    // Non-virtual name-hiding of Mortar::Entity::IsActive -- NOT an override.
    // Binary declares this per-type predicate (chuck-delay gate), not the
    // base flags&0x11 test. Callers must hold a Fruit* for this to apply.
    bool IsActive() const { return m_SpawnDelay <= 0.0f; }

    // Matches binary `Fruit::Chuck(float)`. Velocity is read from this->vel
    // -- callers must set `vel` before calling.
    void Chuck(float delay);

    // Matches Fruit::CheckHasGoneOffscreen (0x00175218). Returns true
    // only when BOTH halves are past the offscreen boundary with outward
    // velocity. Also bounces sliced halves off the near edge.
    bool CheckHasGoneOffscreen();

    // Matches Fruit::KillFruit (0x00176abc). Clears emitters, applies miss
    // penalty in Classic/Combo (life loss + MissControl::MakeDisappear),
    // tracks dropped fruit in Arcade, no-op in Zen. Marks the entity killed
    // (flags |= 0x10).
    void KillFruit(bool doMissPenalty);

    // Matches Fruit::RotateFacingUp (0x001757f4). Sets m_Rot1/m_Rot2 to a
    // random starting orientation and m_RotVel1/m_RotVel2 to axisScale * scalar.
    // When flag=true, additional q_axis * q_up composition is applied to each
    // rotation slot. See Fruit.cpp for full algorithm.
    void RotateFacingUp(bool flag, _Vector3<float> axisScale);

    // Binary @ 0x001dc054 — set m_FruitType and recalculate visual scale + collision sphere.
    // Called from ShopScreen::SetSelected when the player browses the equipment ring.
    // scaleParam multiplies the collision radius (1.0f at all known call sites).
    void SetFruitType(long fruitType, float scaleParam);

    // v1.6.1 Fruit::FruitType @0x001db6c8. Resolves a fruit name
    // string to the index in the FRUIT_INFO array by hashing and
    // comparing against m_NameHash / m_NameHashUpper. If not found:
    //   fallbackRandom=true -> WaveManager::GetInstance()->GetRandom().Rand32(count-1)
    //     (WaveManager's OWN stream, not Math::g_random -- this call does not
    //      perturb the shared gameplay RNG)
    //   fallbackRandom=false -> returns -1 (0xFFFFFFFF)
    static int FruitType(const char* name, bool fallbackRandom);

    // Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
    // Parses Data/xml/fruitlist.xml into FRUIT_INFO array
    static void LoadInfo();

    // Matches Fruit::LoadFruitModels (0x1794e0). Allocates the
    // per-fruit FruitModelInfo array and loads `<name>_<c>_piece_1.mmd`
    // and `<name>_<c>_piece_2.mmd` for each fruit entry via
    // MeshManager. Called once from GameInitialise.
    static void LoadFruitModels();

#if defined(FN_BLOCK_PRELOAD)
    // Boot trim (task #59). Re-runs the per-fruit texture loads
    // that FruitInfo_Load's boot pass skips (hud_%s.tex/zen_%s.tex --
    // gameplay HUD only, never shown at menu; fruit_shadow.tex loads at
    // boot unconditionally), assigning straight into the FruitInfo array's
    // m_HudTexture/m_ZenTexture members (their natural strong-ref home --
    // no BlockLoader holder vector needed). Called once from
    // BlockLoader::PreloadBlock(RES_BLOCK_INGAME); safe to call more than
    // once (LoadLocalisedTexture cache-checks).
    static void LoadHudTextures();
#endif

    // Accessor for a per-fruit pair of half meshes. Returns nullptr if
    // index out of range or LoadFruitModels hasn't run.
    static const FruitModelInfo* GetFruitModelInfo(int fruitType);

    // Binary @ 0x00176564: 4-path weighted selector.
    // includeOnSide=true: all fruits; false: m_bScorable-only subset.
    // Critical mode (WaveManager::CriticalMode) restricts to m_bSpecial fruits.
    static int RandomFruit(bool includeOnSide);

    // Matches Fruit::GetNumActiveForPlayer (0x00175928).
    // byPlayerMode==false: counts INACTIVE fruits (ignores playerIdx).
    // byPlayerMode==true : counts fruits where m_PlayerIdx == playerIdx.
    static int GetNumActiveForPlayer(int playerIdx, bool byPlayerMode);

    // Matches Fruit::ClearUnspawned (0x00176d14). Walks Mortar::ActorManager type-0
    // list and kills fruits via KillFruit(0). clearAll=false: only pre-spawn
    // (m_SpawnDelay > 0) fruits; clearAll=true: every fruit.
    static void ClearUnspawned(bool clearAll);

    // Matches Fruit::Disable (binary @ 0x00126374). Sets m_bNoPowerUp = 1,
    // suppressing miss penalty and power-up activation for this fruit.
    // Used by ClearMenuItems on dojo-transition retract path.
    void Disable();

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

    // Binary static: critical / charged fruit blend target colour for blade flash,
    // AND the CriticalFlash tint colour in CollisionResponse. Loaded from
    // fruitlist.xml's <critical colour="R,G,B,A"/> attr by FruitInfo_Load
    // (see FruitInfo_GetCriticalColour) and copied here by Fruit::LoadInfo --
    // NOT a compile-time constant. ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084.
    static Colour CRITICAL_COLOUR;
    // Binary @ 0x00174fc8 — return FRUIT_INFO[type].m_FactColour
    static Colour FruitFactColour(long type);
    // Binary @ 0x00174ff8 — equivalent of FruitInfo_Get(); preserved for binary call-shape parity
    // Fully-qualified return type avoids GCC -Wchanges-meaning since the
    // method name shadows the struct.
    static const ::FruitInfo* FruitInfo(long type);

    // ASM-spec v1.6.1 Fruit::CheckFruitDropped @0x001dbf70: reads .LANCHOR1+4/+8
    // (@0x002842C0 `outOfFruitTime`, .rodata const C.589 = {255,255,255,255}); both > 0
    // so the body folds to GameOver(-1, -1.0f, 0); return true.
    // ONLY call site is GameUpdate @0x001cfa90, gated on IsMultiplayer() (a hard 0), so
    // this never runs in v1.6.1. Call it ONLY behind that gate -- unguarded it would end
    // the game every frame. Nothing to do with miss counting (that is
    // Fruit::Update -> CheckHasGoneOffscreen -> KillFruit(true)).
    static bool CheckFruitDropped();

    // ASM-spec v1.6.1 Fruit::NumberOfPowerupFruits @0x001db0ac
    // Counts active type-0 fruit entities whose FruitInfo::m_pPowers != nullptr.
    static int NumberOfPowerupFruits();


    // Accessor for the file-scope global g_FruitWasSliced event (binary GOT 0x332a34).
    // Binary subscribe sites load [GOT,0x6e04] to get the event address; port uses this
    // accessor for cross-TU subscribe/unsubscribe.
    // DIFFERS: original = direct GOT access on every subscribe site; using static accessor
    // because port has no GOT, preserving single-definition semantics.
    static Mortar::Event3<Fruit*, int, Mortar::Entity*>& FruitWasSlicedEvent();

    // Binary @ 0x00175624 — "is either half outside the play field" predicate
    bool IsOffscreen() const;

    // Binary @ 0x00176354 — toggle collision sphere; radius = m_CollisionScale + 0.52 * m_Scale
    void EnableCollision(bool enable);

    // Binary @ 0x001db778 (v1.6.1) — sets m_PlayerIdx; online-MP side-effect: partition 2 radius *= 0.66 (Defunct)
    void SetForPlayer(int playerIdx);

    // Binary @ 0x001761d8 — virtual Mortar::Entity::Release override
    void Release() override;

    // Binary @ 0x00175ba4 — fact-of-the-day picker with save-data round-robin
    static const char* GetFact(int* outType, int* outFactIdx, int fruitType, int factIdx);

    // Binary @ 0x001756dc — replace m_pEmitter1 with a custom trail emitter
    bool SetTrailParticles(unsigned long emitterHash);

    // Binary @ 0x001bff08 — slice-direction unit vector for a slice index, offset by the
    // fruit's blade angle (m_SliceArcAngle, +0xc0): returns (SinIdx(a), CosIdx(a), 0) where
    // a = sliceIdx + m_SliceArcAngle.
    _Vector3<float> GetSliceDir(uint16_t sliceIdx);

    // Binary @ 0x001db2a8 — release both trail/juice emitters (m_pEmitter1/+0x40,
    // m_pEmitter2/+0x44) back to the particle manager and null them. Idempotent.
    void RemoveTrailParticles();

    // Binary @ 0x00175988 — push bombs away from this fruit (X axis) when within 70px and matching velocity
    void UpdateBombAvoidance(float dt);

    // Binary @ 0x0017911c — releases the FruitModelInfo[] array + per-MP-player SmartPtr slots; called on shutdown
    static void DestroyFruitModels();

    // (MoveFruitZPositionToBack de-method'd to free fn — see below)
};

// v1.6.1 CleanupFruit @ 0x001defd4 — full fruit-subsystem teardown (shutdown path).
// Nulls 7 texture SmartPtrs, destroys model SmartPtrs for each fruit type (3 fields),
// destroys s_slices list and s_pool, walks+destroys s_fruitModels array.
// Distinct from Fruit::DestroyFruitModels (the mid-game reload path).
// Called from GameDestroy after CleanupBomb, before CleanUpSplat.
void CleanupFruit();

// Binary @ 0x001690cc — return next z-slot for a newly spawned fruit.
// Decrements a static counter (step=100, range [-2499..-500], wraps to -500).
float GetFruitZPosition();

// Binary: _Z24MoveFruitZPositionToBackRf @0x001ca674 (v1.6.1)
// Push sliced-half z behind all unsliced fruits: z = (z+500)*0.5f - 2600.
void MoveFruitZPositionToBack(float& z);

#ifdef __bada__
static_assert(sizeof(Fruit) == 0x18c, "Fruit sizeof must match binary 0x18c");
static_assert(__builtin_offsetof(Fruit, m_FruitType)       == 0x3C,  "");
static_assert(__builtin_offsetof(Fruit, m_bNoPowerUp)      == 0x3D,  "");
static_assert(__builtin_offsetof(Fruit, m_pEmitter1)       == 0x40,  "");
static_assert(__builtin_offsetof(Fruit, m_pEmitter2)       == 0x44,  "");
static_assert(__builtin_offsetof(Fruit, m_SlicePos)        == 0x48,  "");
static_assert(__builtin_offsetof(Fruit, m_SpinPhase)       == 0x60,  "");
static_assert(__builtin_offsetof(Fruit, m_CollisionSize)   == 0x64,  "");
static_assert(__builtin_offsetof(Fruit, m_bBallisticEnable) == 0x70,  "");
static_assert(__builtin_offsetof(Fruit, m_SpawnDelay)      == 0x74,  "");
static_assert(__builtin_offsetof(Fruit, m_AccelTerm)       == 0x78,  "");
static_assert(__builtin_offsetof(Fruit, m_PlayerIdx)           == 0x84,  "");
static_assert(__builtin_offsetof(Fruit, m_SliceBounceTimer)    == 0x88,  "");
static_assert(__builtin_offsetof(Fruit, m_SliceVelocity)       == 0x8C,  "");
static_assert(__builtin_offsetof(Fruit, m_TimeScale)       == 0x98,  "");
static_assert(__builtin_offsetof(Fruit, m_ZPosition)       == 0x9C,  "");
static_assert(__builtin_offsetof(Fruit, m_Gravity)         == 0xA0,  "");
static_assert(__builtin_offsetof(Fruit, m_VisualScale)     == 0xAC,  "");
static_assert(__builtin_offsetof(Fruit, m_bSliced)         == 0xB8,  "");
static_assert(__builtin_offsetof(Fruit, m_SliceTimer)      == 0xBC,  "");
static_assert(__builtin_offsetof(Fruit, m_SliceArcAngle)   == 0xC0,  "");
static_assert(__builtin_offsetof(Fruit, m_SliceArcImpulse) == 0xC4,  "");
static_assert(__builtin_offsetof(Fruit, m_SecondPos)       == 0xC8,  "");
static_assert(__builtin_offsetof(Fruit, m_SecondVel)       == 0xD4,  "");
static_assert(__builtin_offsetof(Fruit, m_Rot1)            == 0xE0,  "");
static_assert(__builtin_offsetof(Fruit, m_Rot2)            == 0xF0,  "");
static_assert(__builtin_offsetof(Fruit, m_RotVel1)         == 0x100, "");
static_assert(__builtin_offsetof(Fruit, m_RotVel2)         == 0x10C, "");
static_assert(__builtin_offsetof(Fruit, m_SliceAxes)       == 0x118, "");
static_assert(__builtin_offsetof(Fruit, m_pOwner)          == 0x160, "");
static_assert(__builtin_offsetof(Fruit, m_bMenuFling)      == 0x164, "");
static_assert(__builtin_offsetof(Fruit, m_bCritical)       == 0x165, "");
static_assert(__builtin_offsetof(Fruit, m_MenuGrowFade)    == 0x168, "");
static_assert(__builtin_offsetof(Fruit, m_bFrozen)         == 0x16C, "");
static_assert(__builtin_offsetof(Fruit, m_OnSliced)        == 0x170, "");
static_assert(__builtin_offsetof(Fruit, m_OnKilled)        == 0x178, "");
static_assert(__builtin_offsetof(Fruit, m_OnExpired)       == 0x180, "");
static_assert(__builtin_offsetof(Fruit, m_bDrawWhole)      == 0x188, "");
#endif

#endif
