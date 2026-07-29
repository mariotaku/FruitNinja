#include "Fruit.h"
#include "SuperFruitControl.h"
#include "debug/Logger.h"
#include "game/GameMode.h"
#include "ActorManager.h"
#include "FruitInfo.h"
#include "Bomb.h"
#include "SlashEntity.h"
#include "SplatEntity.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/Mesh.h"
#include "asset/MeshManager.h"
#include "asset/TextureManager.h"
#include "particle/PSPParticleManager.h"
#include "hud/MenuButton.h"
#include "hud/SliceEffect.h"
#include "util/MemoryPool.h"
#include "hud/MissControl.h"
#include "game/BombHit.h"
#include "game/ScoreState.h"
#include "game/WaveManager.h"
#include "game/PowerUpManager.h"
#include "game/ItemManager.h"
#include "game/GameOver.h"
#include "Coin.h"
#include "math/Random.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/P2PMessageHandling.h"
#include "engine/util/StringTable.h"
#include "engine/util/Event.h"
#include "game/FruitSaveData.h"
#include "game/AchievementManager.h"
#include "hud/ScoreControl.h"
#include "util/StringHash.h"
#include "Game.h"
#include "engine/asset/File.h"
#include "audio/GameSound.h"
#include "math/math3d.h"
#include "math/MathUtil.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include "game/GameWork.h"

// Binary static: critical/charged fruit blend-target colour for blade flash,
// AND the CriticalFlash tint colour in CollisionResponse. Real value comes
// from fruitlist.xml's <critical colour="0,140,245,170"/> attr, loaded by
// Fruit::LoadInfo -> FruitInfo_Load (see FruitInfo_GetCriticalColour). This
// initializer is only the pre-XML-load fallback.
// ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084 / SlashEntity::UpdatePoints @0x001e6914.
Colour Fruit::CRITICAL_COLOUR(255, 128, 0, 255);

// <critical> tuning globals -- see the contract block in Fruit.h. These are the
// binary's .data initialisers; the <critical> parse inside Fruit::LoadInfo
// (v1.6.1 @0x001e1084, port body in FruitInfo.cpp) overwrites all but
// NEW_LIFE_AT from the shipped fruitlist.xml before any slice happens.
// Non-const on purpose: LoadInfo writes them and consumers must emit a global
// load, not a folded literal.
int   Fruit::CRITICAL_SPLATS           = 15;    // @0x002d8d38
float Fruit::CRITICAL_SPLAT_SCALE      = 1.25f; // @0x002d8d3c
float Fruit::CRITICAL_SPLAT_SPREAD     = 1.25f; // @0x002d8d40
float Fruit::CRITICAL_DISAPPEAR_SPEED  = 1.0f;  // @0x002d8d44 (parsed, never read)
int   Fruit::CRITICAL_SCORE            = 10;    // @0x002d8d48
int   Fruit::CRITICAL_CHANCE           = 50;    // @0x002d8d4c
int   Fruit::CRITICAL_CHANCE_START_INC = 30;    // @0x002d8d50
int   Fruit::NEW_LIFE_AT               = 100;   // @0x002d8d60 (XML omits the attr)

// File-scope global: multicast event fired on every fruit slice.
// Binary: file-static in Fruit.cpp, ctor'd in global.ctors (v1.6.1 global.constructors.keyed.to.Fruit.cpp @0x001e206c).
// GOT-resolved address: 0x00332a34.
// Subscribers: ComboModifier, ExplodyFruitModifier, TimeSinkModifier.
// DIFFERS: binary accesses via GOT load on every subscribe site; port exposes via
// accessor Fruit::FruitWasSlicedEvent() returning a reference to this static
// so cross-TU subscribers compile without a raw extern (GOT not applicable in port).
static Mortar::Event3<Fruit*, int, Mortar::Entity*> g_FruitWasSliced;

// File-scope global: fired in KillFruit (binary GOT, referenced at 0x1def10).
// Fires BEFORE the per-fruit m_OnKilled event. Arg: (Fruit* this).
static Mortar::Event1<Fruit*> g_FruitKilled;

// File-scope global: fired in Update (binary GOT, referenced at 0x1dfc0c).
// Fires AFTER the per-fruit m_OnExpired event. Arg: (Fruit* this).
static Mortar::Event1<Fruit*> g_FruitExpired;

Mortar::Event3<Fruit*, int, Mortar::Entity*>& Fruit::FruitWasSlicedEvent() {
    return g_FruitWasSliced;
}

// Analysed: 2026-04-29T00:00

// Fruit.cpp uses FruitInfo as both a struct name (global) and a static
// method name (Fruit::FruitInfo). Alias the struct at file scope so all
// local declarations can use the unambiguous name FruitInfoData.
typedef ::FruitInfo FruitInfoData;

// Count of active power-fruits (FruitInfo::m_pPowers != nullptr) on screen.
// Decremented by KillFruit on natural-expiry path (flag 0x10 not yet set).
// TODO: re-verify v1.6.1 Fruit::KillFruit @0x001deba8 inner offset (was: 0x00176cc8..0x00176cd4 stale v1.5.x): unconditional store, clamped to >= 0 (not >= 1).
static int g_PowerFruitCount = 0;

// Per-frame gate for "Throw-fruit" SFX. Binary stores at *(g_fruitGlobal + 0x48)
// (DAT_00177960 + 0x48). Reset in Fruit::Chuck so each new launch can fire once.
// Prevents multiple fruits chucked in the same frame from each playing the SFX.
static bool s_FruitThrowSfxFiredThisFrame = false;

// Base slice-rotation axes — written by global.constructors.keyed.to.Fruit.cpp
// (v1.6.1 global.constructors.keyed.to.Fruit.cpp @0x001e206c). Values: world unit basis X/Y/Z.
//   0x2D9EE4 -> kSliceBaseAxis[0] = (1,0,0)   slice axis[0]
//   0x2D9ED8 -> kSliceBaseAxis[1] = (0,1,0)   slice axis[1]
//   0x3328AC -> kSliceBaseAxis[2] = (0,0,1)   slice axis[2]
static const _Vector3<float> kSliceBaseAxis[3] = {
    _Vector3<float>(1.0f, 0.0f, 0.0f),
    _Vector3<float>(0.0f, 1.0f, 0.0f),
    _Vector3<float>(0.0f, 0.0f, 1.0f),
};

// GetFruitZPosition counter (v1.6.1 GetFruitZPosition @0x001ca61c).
// Decrements by 100 per call, wraps back to -500 when it falls below -2499.
// Constants from binary DATs: step=100, lower=-2499, reset=-500.
// TODO: re-verify v1.6.1 DAT slots for step/lower/reset (old 0x00169108/10c/110 are stale v1.5.x).
static float s_FruitZCounter = -500.0f;

// Static Colour constants from global.constructors.keyed.to.Fruit.cpp
// (TODO: re-verify v1.6.1 Fruit.cpp static-init offset; was: _GLOBAL__I_Fruit.cpp @ 0x0017a354 stale v1.5.x).
// *DAT_0017a678: Colour(0x80, 0x80, 0xff, 0x80) = RGBA(128, 128, 255, 128)
// Likely g_FruitOutlineTint or g_FruitGlowTint. Exact consumer not yet RE'd -- TODO.
static Colour g_FruitTint1(128, 128, 255, 128);  // DAT_0017a678 (TODO: re-verify v1.6.1 DAT)
// DAT_0017a670: copy-ctor from BLUE singleton
// == BLUE created in global.constructors.keyed.to.Colour.cpp (v1.6.1 @0x0021e9b8).
// TODO: re-verify v1.6.1 Fruit.cpp static-init offset (was: ASM-verified 0x0017a512..0x0017a51c stale v1.5.x).
static Colour g_FruitTint2(0, 0, 255, 255);      // DAT_0017a670: blue

// Binary constants for fruit slicing.
// Resolved from DATs near CollisionResponse (v1.6.1 @0x001dd500) and Slice (v1.6.1 @0x001dcba0).
static const float SLICE_TIMER_BASE    = 0.03f;   // DAT_001784dc -- TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
static const float SLICE_BLADE_SCALE   = 0.1f;    // DAT_001784e0 -- TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0

// Bomb-hit cinematic window: LTF in {2,3} and timer in (-0.1f, 0.95f).
// Shared by both the power-up-activation gate and the score+save gate
// in CollisionResponse. TODO: re-verify v1.6.1 Fruit::CollisionResponse @0x001dd500 inner offset (same game_work GOT entry; was: 0x001788f4 stale v1.5.x).
static const float kBombHitMax = 0.95f;
static const float kBombHitMin = -0.1f;
static const float SLICE_CLAMP_MIN_NRM = 4.0f;    // normal fruit clamp
static const float SLICE_CLAMP_MAX     = 8.0f;
// Fruit::SetFruitType (v1.6.1 @0x001dc054) collision radius formula.
// ASM-spec v1.6.1 Fruit::SetFruitType @0x001dc054 (TODO: re-verify inner offset; was: 0x0017630e..0x0017631e stale v1.5.x):
//   vldr s14, [r3, #0x244]   ; s14 = m_Scale            (XML "scale")
//   vldr s15, [r3, #0x248]   ; s15 = m_CollisionScale   (XML "collision")
//   vmla s15, s13, s14       ; s15 += 0.52 * s14
//   vmul s15, s15, scale     ; s15 *= scaleParam (1.0 typical)
// →  radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam
// Defaults from LoadInfo: m_Scale = 25.0 @ 0x41c80000,
//                         m_CollisionScale = 1.0 @ 0x3f800000.
static const float COL_RADIUS_FACTOR = 0.52f;   // DAT_00176340

// Slice juice-burst tuning. The three critical-splat knobs are game globals
// written by Fruit::LoadInfo (v1.6.1 @0x001e1084) from the <critical> element
// of fruitlist.xml, NOT compile-time constants -- their .data initialisers
// (CRITICAL_SPLATS @0x002d8d38 = 10, CRITICAL_SPLAT_SCALE @0x002d8d3c = 1.5,
// CRITICAL_SPLAT_SPREAD @0x002d8d40 = 1.2) only exist at link time and are
// overwritten before any slice runs. Read them through the FruitInfo_Get*
// accessors: shipped XML gives 15 / 1.25 / 1.25.
//   splatCount override: both sites (v1.6.1 @0x001dce14 crit, @0x001dce80
//   special/super) read the SAME global CRITICAL_SPLATS.
// Per-splat taper applied after MakeSplat:
//   factor = clamp(1 - (i-2)/splatCount, kSplatTaperMin, 1.0)
//   m_Vel.z *= factor; past index 2 (gate @0x001dd028) the X/Y velocity
//   (spread, @0x001dd044) and scale (@0x001dd064) get boosted.
// kSplatTaperMin IS a genuine instruction-level literal (DAT_001dcf04), so it
// stays hardcoded.
static const float kSplatTaperMin    = 0.3f;     // DAT_001dcf04

// ASM-spec v1.6.1 RandomStartAngle @0x001db39c
void RandomStartAngle(Quaternion& out, bool use2D) {
    if (!use2D) {
        Math::Random& rng = WaveManager::GetInstance()->GetRandom();
        float ax = rng.RandF(2.0f) - 1.0f;
        float ay = rng.RandF(2.0f) - 1.0f;
        float az = rng.RandF(2.0f) - 1.0f;
        _Vector3<float> axis(ax, ay, az);
        axis.Normalise();
        uint32_t angle16 = rng.Rand32(0xff3aU) & 0xffff;
        out = Quaternion::Identity();
        out.CreateFromAxisAngle(axis.x, axis.y, axis.z, angle16);
    } else {
        out = Quaternion::Identity();
        out.CreateFromAxisAngle(-1.0f, 0.0f, 0.0f, 0xce2c);
    }
}

// SetupLighting is a no-op free function at 0x001ca5e8 (see Bomb.h / Bomb.cpp).
// The binary's single symbol serves both Bomb + Fruit TUs via PLT trampoline.
// Forward-declared here for call sites in LoadFruitModels.

Fruit::Fruit()
    : m_FruitType(0)
    , m_bNoPowerUp(0)
    , m_pEmitter1(nullptr)
    , m_pEmitter2(nullptr)
    , m_SlicePos(0, 0, 0)
    , m_SpinPhase(0)
    , m_CollisionSize(0)
    , m_VestigialInitFour(0)
    , m_bBallisticEnable(0)
    , m_SpawnDelay(0.0f)
    , m_AccelTerm(0, 0, 0)
    , m_PlayerIdx(0)
    , m_SliceBounceTimer(0.0f)
    , m_SliceVelocity(0, 0, 0)
    , m_TimeScale(1.0f)
    , m_ZPosition(0.0f)
    , m_Gravity(0, -12.0f, 0)
    , m_bSliced(0)
    , m_SliceTimer(-1.0f)
    , m_SliceArcAngle(0)
    , m_SliceArcImpulse(0.0f)
    , m_SecondPos(0, 0, 0)
    , m_SecondVel(0, 0, 0)
    , m_pOwner(nullptr)
    , m_bMenuFling(0)
    , m_bCritical(0)
    , m_MenuGrowFade(0.0f)
    , m_bFrozen(0)
    , m_bDrawWhole(0)
{
    entityType = 0;
}

Fruit::~Fruit() {
    delete m_Col;
    m_Col = nullptr;
    // Model released by SmartPtr destructor
}

// ASM-spec v1.6.1 Fruit::Init @0x001e2898: bActive and bCriticalEligible default to 1, not 0;
//   RNG source, field writes, flags bit-op; power-fruit counter increment + non-arcade path;
//   arcade pineapple-blitz dedup + power-fruit gate left as TODO.
// (Re-stamped from stale v1.5.x 0x00176708 -> FruitFactLeaderboard region; re-verify behavior
//  against v1.6.1 Fruit::Init before re-marking ASM-verified.)
// v1.6.1 Fruit::Init @0x001e2898 — vtable slot 2. p2=fruitType; p3=scale (nullable).
void Fruit::Init(void* /*p1*/, long fruitType, _Vector3<float>* /*scaleOrNull*/) {
    // v1.6.1 Fruit::Init @0x001e2898: reset per-fruit event lists on pooled reuse (drops stale GPO subscribers so a recycled fruit never fires a delegate bound to a freed object).
    m_OnSliced.Clear();
    m_OnKilled.Clear();
    m_OnExpired.Clear();
    // v1.6.1 Fruit::Init @0x001e2898: range-check fruitType; out-of-range falls back to RandomFruit(true).
    if (fruitType >= 0 && fruitType < (long)g_FruitInfoCount) {
        m_FruitType = (uint8_t)fruitType;
    } else {
        m_FruitType = (uint8_t)RandomFruit(true);
    }
    m_SpinPhase = 0;
    m_bBallisticEnable = 1;
    m_bSliced = 0;
    m_bDrawWhole = 0;
    m_bCritical = 0;
    m_bMenuFling = 0;
    m_bNoPowerUp = 0;
    m_pOwner = nullptr;
    m_TrackerID = 0;
    m_MenuGrowFade = 0.0f;
    m_bFrozen = 0;
    m_SpawnDelay = 0.0f;
    m_PlayerIdx = 0;
    m_TimeScale = 1.0f;
    m_CollisionSize = 75;         // v1.6.1 Fruit::Init @0x001e2898: str r3, [r0, #0x4b] = 0x4B
    m_VestigialInitFour = 4;      // v1.6.1 Fruit::Init @0x001e2898: write-only dead field
    // ASM-spec v1.6.1 Fruit::Init @0x001e2898:
    // orr r1,r1,#0x2 ; bfc r1,#0x4,#0x1
    flags = (flags & ~ENT_KILLED) | ENT_HAS_COLLISION;

    m_ZPosition = GetFruitZPosition();

    // Reset slice state (binary Fruit::Init — m_SliceTimer = -1).
    m_SliceTimer      = -1.0f;
    m_SliceArcAngle   = 0;
    m_SliceArcImpulse = 0.0f;
    m_SlicePos        = _Vector3<float>(0, 0, 0);
    m_pEmitter1    = nullptr;
    m_pEmitter2    = nullptr;

    // Seed m_SliceAxes with unit basis so the pre-slice spin loop uses
    // world-space axes. SetupSliceRotations overwrites these on the first slice.
    m_SliceAxes[0] = kSliceBaseAxis[0];
    m_SliceAxes[1] = kSliceBaseAxis[1];
    m_SliceAxes[2] = kSliceBaseAxis[2];
    m_SliceAxes[3] = kSliceBaseAxis[0];
    m_SliceAxes[4] = kSliceBaseAxis[1];
    m_SliceAxes[5] = kSliceBaseAxis[2];

    // Random rotation velocity (matches v1.6.1 Fruit::Init @0x001e2898):
    // one triple of random values, stored IDENTICALLY into both m_RotVel1
    // and m_RotVel2 — the two halves tumble in sync.
    // RNG source: binary uses WaveManager-owned PRNG (ASM-spec v1.6.1 Fruit::Init @0x001e2898).
    {
        Math::Random& rng = WaveManager::GetInstance()->GetRandom();
        m_RotVel1 = _Vector3<float>(rng.RandF(11.0f) - 5.5f,
                                    rng.RandF(11.0f) - 5.5f,
                                    rng.RandF(11.0f) - 5.5f);
    }
    m_RotVel2 = m_RotVel1;

    // Random start rotation — random axis + random angle.
    // Binary: Fruit::Init calls RandomStartAngle(@0x001db39c) with use2D=false.
    RandomStartAngle(m_Rot1, false);
    m_Rot2 = m_Rot1;

    // ASM-spec v1.6.1 Fruit::Init super-fruit override @0x001e2898 (rng=Math::g_Random; z=GetRandBetween(3,5,0.5))
    // ASM-verified: 2026-07-15T00:00Z v1.6.1 Fruit::Init super-fruit override @ 0x001e2c94..0x001e2cf8 (asm-inspector)
    {
        const ::FruitInfo* info = Fruit::FruitInfo((long)m_FruitType);
        if (info != nullptr && info->m_bIsSuperFruit) {
            float angleBase = GetRandBetween(12.0f, 40.0f, 0.0f, 0.0f);
            uint32_t angle16 = (uint32_t)((int)(angleBase * 182.0f)) & 0xffff;
            m_Rot1 = Quaternion::Identity();
            m_Rot1.CreateFromAxisAngle(0.0f, 1.0f, 0.0f, angle16);
            m_Rot2 = m_Rot1;

            float rotVelZ = GetRandBetween(3.0f, 5.0f, 0.5f, 0.0f);
            m_RotVel1 = _Vector3<float>(10.0f, 0.0f, rotVelZ);
            m_RotVel2 = m_RotVel1;
        }
    }

    // Default gravity — confirmed from v1.6.1 Fruit::Init @0x001e2898: literal -12.0, y-DAT=0.0
    m_Gravity = _Vector3<float>(0.0f, -12.0f, 0.0f);

    // Extra accel/jerk term — init to zero.
    // v1.6.1 Fruit::Init @0x001e2898 reads *globalConfigVec3 (GOT 0x001f4328);
    // BSS Vec3 initialised by _GLOBAL__I_Fruit.cpp to (0,0,0).
    m_AccelTerm = _Vector3<float>(0.0f, 0.0f, 0.0f);

    // Matches Fruit::SetFruitType @0x001dc054:
    // visualScale = globalScaleVec * FruitInfo[type].scale * VISUAL_SCALE_MULT (0.01)
    // globalScaleVec is at BSS 0x1F4334, initialized to (0,0,0) by static init
    // but overwritten at runtime before fruit creation.
    // Per-fruit scale from Data/xml/fruitlist.xml (e.g. watermelon=75)
    {
        // Vec3::One at BSS 0x1F4334 — a constant singleton for (1,1,1), not a
        // mutable scale variable. Matches binary: _Vector3::operator*(Vec3*, float*)
        // in Fruit::SetFruitType @0x001dc054 multiplies Vec3::One by m_Scale then 0.01.
        // (scale is an Entity member SetFruitType itself never touches -- Init sets
        // it directly here, separate from the SetFruitType call below.)
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        float fruitScale = info->m_Scale * 0.01f;
        scale = _Vector3<float>::One() * fruitScale;

        // ASM-spec v1.6.1 Fruit::Init @0x001e2898: calls SetFruitType(this, m_FruitType, scaleVec.x)
        // for m_VisualScale + collision-sphere setup. scaleVec is not supplied at this call site
        // (common-case scaleParam == 1.0). SetFruitType internally gates the sphere on
        // base<=0.0 (e.g. locked black_pineapple equip fruit: scale=60, collision=-105 ->
        // base=-73.8 -> no ColSphere -> unsliceable/unequippable).
        SetFruitType(m_FruitType, 1.0f);
    }

    // ASM-spec v1.6.1 Fruit::Init @0x001e2898: power-fruit spawn gate =
    // NumberOfPowerupFruits() < 2 (live scan @0x001db0ac), freeze exempt, re-roll "banana".
    if (game_work.gameMode == 2 && game_work.m_PauseAmount < 1.0f) {
        // (1) Re-roll while we'd spawn a plain banana this frame (DAT_00280768 = "banana";
        //     the banana model is reserved for the power-fruit skin, never a plain fruit).
        static const int kBananaType = Fruit::FruitType("banana", false);
        while ((int)m_FruitType == kBananaType) {
            m_FruitType = (uint8_t)Fruit::RandomFruit(true);
        }

        // (2) Power-fruit spam gate. Live scan, not the write-only g_PowerFruitCount
        // (that counter goes stuck >=1 whenever a fruit is destroyed without KillFruit,
        // permanently insta-killing every subsequent power-fruit). The scan counts
        // active power-fruits including self (this fruit is already registered in
        // ActorManager by the time Init runs), so >=2 means "another one is already live".
        const FruitInfoData* gateInfo = FruitInfo_Get(m_FruitType);
        if (gateInfo->m_pPowers) {
            static const uint32_t kFreezeHash = StringHash("freeze");
            bool kill = false;
            if (NumberOfPowerupFruits() >= 2) {
                kill = true;
            } else {
                const float tRem = (game_work.m_SaveData
                                    ? game_work.m_SaveData->m_TimeRemainingSave
                                    : 0.0f);
                if (tRem < 8.0f
                        && gateInfo->m_pPowers->m_pArray
                        && gateInfo->m_pPowers->m_pArray[0].m_PowerHash != kFreezeHash) {
                    kill = true;
                } else if (gateInfo->m_pPowers->AnyActivePowers()) {
                    kill = true;
                }
            }
            if (kill) {
                flags |= ENT_KILLED;
                return;
            }
        }
    }

    // ASM-spec v1.6.1 Fruit::Init @0x001e2898.
    // Increment global active-power-fruit counter for power-fruits. Pairs with
    // KillFruit's natural-expiry decrement.
    const FruitInfoData* spawnInfo = FruitInfo_Get(m_FruitType);
    if (spawnInfo->m_pPowers) {
        ++g_PowerFruitCount;
    }

    // Defunct: online-MP -- no-op stub; v1.6.1 Fruit::Init @0x001e2898
    // Binary: BOMB_PINEAPPLE count decrement for online multiplayer sync packet.
    // Dead on this platform -- P2P online MP was removed.

}

// Binary @ 0x001dc054 — set m_FruitType and recalculate visual scale + collision sphere.
// Called from ShopScreen::SetSelected when browsing the equipment ring (scaleParam=1.0).
//
// ASM-spec v1.6.1 Fruit::SetFruitType @0x001dc054: base = m_CollisionScale + COL_RADIUS_FACTOR*m_Scale.
// When base <= 0.0, the binary DELETES the ColSphere (vtable slot1 dtor) and nulls m_Col (+0x38)
// instead of storing a negative radius. Locked shop equip fruits use a negative m_CollisionScale
// (e.g. black_pineapple: m_Scale=60, m_CollisionScale=-105 -> base=-73.8) specifically so they have
// NO collision sphere and can't be sliced/equipped. Downstream radius*radius squaring made a stored
// negative radius test as positive -- that was the port bug (locked items sliceable+equippable).
void Fruit::SetFruitType(long fruitType, float scaleParam) {
    m_FruitType = (uint8_t)fruitType;
    const FruitInfoData* info = FruitInfo_Get(fruitType);
    float fruitScale = info->m_Scale * 0.01f;
    m_VisualScale = _Vector3<float>::One() * fruitScale;
    const float fScale   = info->m_Scale;
    const float fColBase = info->m_CollisionScale;
    const float base = fColBase + COL_RADIUS_FACTOR * fScale;
    if (base <= 0.0f) {
        delete m_Col;
        m_Col = nullptr;
    } else {
        if (!m_Col) m_Col = new ColSphere();
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center() = _Vector3<float>(pos.x, pos.y, 0.0f);
        cs->radius = base * scaleParam;
    }
}

// ASM-spec v1.6.1 Fruit::Chuck @0x001db5f0
// Binary semantics: cache pos into m_SecondPos, clamp negative delay to
// 0.125, set m_SpawnDelay. NO flags write, NO m_ScaleAnim write,
// NO s_FruitThrowSfxFired reset -- those belong in Init.
void Fruit::Chuck(float delay) {
    m_SecondPos = pos;
    if (delay < 0.0f) delay = 0.125f;
    m_SpawnDelay = delay;
    // ASM-spec v1.6.1 Fruit::Chuck @0x001db5f0
    // Power-fruit cancel-if-thrown-too-late: when this fruit carries a non-freeze
    // power-up and the wave will end within 8s of when its delay elapses, abort
    // (mark dead, decrement g_PowerFruitCount).
    // "freeze" string @ binary 0x001BA2BF. Live wave-time mirror is
    // game_work.m_SaveData->m_TimeRemainingSave -- TimeControl::Update writes it
    // every frame (binary @ 0x00162830).
    const FruitInfoData* chuckInfo = FruitInfo_Get(m_FruitType);
    if (chuckInfo->m_pPowers && chuckInfo->m_pPowers->m_pArray
            && game_work.m_SaveData) {
        static const uint32_t kFreezeHash = StringHash("freeze");
        const float waveTimer = game_work.m_SaveData->m_TimeRemainingSave;
        if ((waveTimer - delay) < 8.0f
                && chuckInfo->m_pPowers->m_pArray[0].m_PowerHash != kFreezeHash) {
            --g_PowerFruitCount;  // raw decrement, no clamp (binary: subs r2,#1; str)
            flags |= ENT_KILLED;  // 0x10
        }
    }

    // v1.6.1 super-fruit: notify when this fruit is thrown.
    // Binary @ 0x001bbf48: SuperFruitControl::SuperFruitThrown gates on FruitInfo[+0x330].
    SuperFruitControl::SuperFruitThrown(this);
}

// ASM-verified: 2026-06-15T00:00Z v1.6.1 binary @ 0x001df828 (asm-inspector)
// binary @0x001df828 -- m_TimeScale(0x98) applied to integration dt; dtNorm = dtScaled*60 (DAT_1dfb90=1/60)
void Fruit::Update(float dt) {
    // binary @0x001df828: dtScaled = dt * m_TimeScale(0x98); all integration uses dtScaled.
    const float dtScaled = dt * m_TimeScale;
    const float dtNorm   = dtScaled * 60.0f;  // equivalent to dtScaled / (1/60)

    // binary @0x001df860: outer gate is m_bSliced(+0xB8) vs unsliced — no early
    // returns before the branch. IsActive() and m_SpawnDelay checks live
    // INSIDE the unsliced path only.
    if (!m_bSliced) {
        // === UNSLICED FRUIT ===

        // Launch delay (unsliced path only)
        // binary @0x001dfa5c -- chuck-countdown: m_SpawnDelay(0x74) countdown uses game_work.dt
        // (fixed real-time step, NOT per-fruit dt*m_TimeScale) and is gated by global game state:
        //   - paused -> no countdown
        //   - bomb-hit slow-mo active -> no countdown
        //   - level transition cinematic (early phase) -> no countdown
        // The SFX edge fires when delay crosses 0.2f going down, gated by
        // a per-frame global (s_FruitThrowSfxFiredThisFrame) that Fruit::Draw
        // resets unconditionally at function entry.
        if (m_SpawnDelay > 0.0f) {
            static const float THROW_FRUIT_SFX_THRESHOLD = 0.2f;  // DAT_00177950
            const float prevSpawnDelay = m_SpawnDelay;

            const bool gate =
                !game_work.bM_Mode
                && game_work.m_BombHitTimer <= 0.0f
                && (   (game_work.gameMode == 2 && game_work.m_PauseAmount < 1.0f)
                    ||  game_work.bM_bPaused == 0);
            if (gate) {
                m_SpawnDelay -= game_work.dt;   // +0x38, NOT dt*m_TimeScale
            }

            // SFX edge runs even if the gate is closed this frame -- binary @0x001dfb14
            if (prevSpawnDelay > THROW_FRUIT_SFX_THRESHOLD
                && m_SpawnDelay <= THROW_FRUIT_SFX_THRESHOLD
                && !s_FruitThrowSfxFiredThisFrame
                && game_work.bM_bPaused == 0)
            {
                s_FruitThrowSfxFiredThisFrame = true;
                if (game_work.mGameSound)
                    game_work.mGameSound->SFXPlay("Throw-fruit", 1.0f, 1.0f);
            }

            // binary @0x001dfb38: when delay is still positive after the gated countdown
            // (paused frame, bomb-hit slow-mo frame, level-transition frame), binary
            // early-returns from Update entirely. Skips integration AND common tail
            // (rotation / m_Col / emitter writes). Fruits waiting in the launch queue
            // freeze completely until the gate opens.
            if (m_SpawnDelay > 0.0f) {
                return;
            }
            m_SpawnDelay = 0.0f;

            // binary @0x001dfc08: m_OnExpired fires ONCE on the transition frame when
            // m_SpawnDelay crosses from positive to non-positive. Fires BEFORE the
            // ENT_KILLED early-return check. Per-fruit event fires first, then global.
            m_OnExpired(this);
            g_FruitExpired(this);

            // binary @0x001dfc14: ENT_KILLED(0x10) guard on transition frame —
            // if the m_OnExpired callback killed us, skip the rest of the transition.
            if (flags & ENT_KILLED) {
                return;
            }

            // binary @0x001dfc1c -- unsliced transition: trail re-arm fires ONCE on the
            // transition frame when m_SpawnDelay crosses from positive to non-positive.
            // Binary nests this inside the if (m_SpawnDelay > 0.0f) block, after the
            // early-return guard and after m_OnExpired.
            {
                const FruitInfoData* info = FruitInfo_Get(m_FruitType);
                bool ok = false;
                if (info->m_TrailHash) {
                    ok = SetTrailParticles(info->m_TrailHash);
                }
                if (!ok && Game::GetInstance()->IsFastHardware()) {
                    // MP/SP trail-effect fallback. With NetworkManager::IsOnlineMultiplayer
                    // returning false on this platform, the !onlineMP branch always
                    // wins -> pick = StringHash("fruit_flight").
                    static const uint32_t kHashFruitFlight     = StringHash("fruit_flight");
                    static const uint32_t kHashScoreX2Trail    = StringHash("scorex2_trail");
                    static const uint32_t kHashBlueFruitFlight = StringHash("blue_fruit_flight");
                    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();
                    const bool onlineMP = nm && nm->IsOnlineMultiplayer();
                    uint32_t pick;
                    if ((onlineMP && m_PlayerIdx < 2) || m_PlayerIdx == 3) {
                        if      (m_PlayerIdx == 0) pick = kHashFruitFlight;
                        else if (m_PlayerIdx == 3) pick = kHashScoreX2Trail;
                        else                       pick = kHashBlueFruitFlight;
                    } else {
                        pick = StringHash("fruit_flight");
                    }
                    SetTrailParticles(pick);
                }
                // ASM-spec v1.6.1 Fruit::Update @0x001df828 (block @0x001dfdb0-0x001dfdf0):
                // per-tick m_pEmitter1 follow-position is
                //   m_Pos = pos + _Vector3<float>::UnitZ * m_ZPosition
                // raw -- no -20 bias and no z override on this path.
                // UnitZ is _Vector3<float>::UnitZ @0x003328ac, a guard-protected .bss
                // static (_ZGVN8_Vector3IfE5UnitZE @0x003328a8) constructed at runtime
                // as (0,0,1) by global.constructors.keyed.to.Fruit.cpp @0x001e2624; it
                // reads as zeros in the file image only because it lives in .bss.
                if (m_pEmitter1) {
                    m_pEmitter1->m_Pos   = pos;
                    m_pEmitter1->m_Pos.z = pos.z + m_ZPosition;
                }
            }

            // binary @0x001dfd80 -- cascade fruit-spawn fires ONCE on the transition
            // frame when m_SpawnDelay crosses from positive to non-positive. Binary
            // nests this inside the if (m_SpawnDelay > 0.0f) block, after trail re-arm.
            // WaveManager::m_FruitChance (+0x70) is the per-frame fruit multiplier set by
            // PowerUp::FruitMultiplyer. For value N: spawn (N-1) extras from a
            // random side-template. For value < 1: warp this fruit off-screen so
            // CheckHasGoneOffscreen kills it next frame.
            {
                WaveManager* wm = WaveManager::GetInstance();
                float countF = wm->m_FruitChance;
                int   cnt    = (int)countF;
                // Stochastic round-up. Epsilon=0.01 (DAT_00177cec), scale=100 (DAT_00177cf0).
                if ((float)cnt + 0.01f < countF) {
                    uint32_t r = wm->GetRandom().Rand32(100);
                    if ((countF - (float)cnt) * 100.0f > (float)r) {
                        cnt++;
                    }
                }
                if (cnt < 1) {
                    // Self-warp off-screen. Binary @ 0x001dfdc8.
                    // NOTE: no flags write; relies on CheckHasGoneOffscreen later.
                    m_SpawnDelay = 0.0f;
                    pos.y        = -320.0f;
                    vel          = _Vector3<float>(0.0f, -1.0f, 0.0f);
                } else if (cnt != 1) {
                    // Multiplier cascade: pick one of three templates and spawn ONE extra
                    // fruit. Binary (Fruit::Update @0x001df828) calls SpawnFruit(cnt-1, ...)
                    // once, but v1.6.1 SpawnFruit spawns exactly one fruit (count is only the
                    // >=1 guard), so cnt-1 here yields a single extra fruit.
                    // template[0]=TOP (slow drift down), [1]=LEFT, [2]=RIGHT.
                    SPAWNER_INFO templates[3];
                    templates[0].m_SpawnType  = PLACEMENT_TOP;
                    templates[0].m_Gravity_x  = 0.0f;
                    templates[0].m_Gravity_y  = -0.05f;
                    templates[0].m_Gravity_z  = 0.0f;
                    templates[0].m_SpawnTimer = -3.0f;
                    templates[1].m_SpawnType  = PLACEMENT_LEFT;
                    templates[1].m_SpawnTimer = -3.0f;
                    templates[1].m_HorizMin   = -1.0f;
                    templates[1].m_HorizMax   = -0.5f;
                    templates[2].m_SpawnType  = PLACEMENT_RIGHT;
                    templates[2].m_SpawnTimer = -3.0f;
                    templates[2].m_HorizMin   = -1.0f;
                    templates[2].m_HorizMax   = -0.5f;
                    uint32_t pick = wm->GetRandom().Rand32(3);
                    wm->SpawnFruit(cnt - 1, /*fruitType=*/-1, &templates[pick], /*playerIdx=*/0);
                }
                // cnt == 1: fall through to normal single-fruit path.
            }
        }

        // binary @0x001dff88 -- unsliced ballistic integration:
        //   pos += (vel*dtScaled + 0.5*g*dtScaled^2) * 60.0   (DAT_001e0414 = 60.0; g=m_Gravity@0xA0)
        //   vel += m_Gravity(0xA0) * dtScaled
        //   pos += m_AccelTerm(0x78) * dtScaled         (NO x60 here)
        // Gravity integration gate is m_bBallisticEnable (Fruit+0x70), NOT IsActive():
        //   @0x001dff88 ldrb [+0x70]; beq -> skip whole block (menu fruit pins here: CreateFruit sets +0x70=0)
        //   @0x001dffac inner gate m_bFrozen (+0x16c): bne skips vel+=, pos+=, accel+= together.
        // Gameplay fruit (Init sets +0x70=1) still integrates; menu fruit (=0) does not.
        const float POS_INTEGRATION_SCALE = 60.0f;  // DAT_001e0414
        if (m_bBallisticEnable) {
            if (!m_bFrozen) {
                _Vector3<float> step = (vel * dtScaled + m_Gravity * (0.5f * dtScaled * dtScaled)) *
                    POS_INTEGRATION_SCALE;
                vel += m_Gravity * dtScaled;
                pos += step;

                // Accel term drift -- dtScaled, no x60.
                pos += m_AccelTerm * dtScaled;
            }
        }

        // binary @0x001e009c -- m_SecondPos/Vel backup runs unconditionally (unsliced tail)
        m_SecondPos = pos;
        m_SecondVel = vel;

        // binary @0x001e00c4 -- slice-timer countdown: set positive by CollisionResponse,
        // triggers the actual split when it hits 0.
        if (m_SliceTimer > 0.0f) {
            if (!m_bFrozen) m_SliceTimer -= dtScaled;   // v1.6.1 Fruit::Update @0x001e00bc: decrement gated on !m_bFrozen
            if (m_SliceTimer <= 0.0f) {
                m_SliceTimer = 0.0f;
                Slice();
            }
        }
    } else {
        // binary @0x001df868 -- grow-fade ramp: ramps m_MenuGrowFade toward 1.0 at
        // dtScaled*3.0 units/sec while m_bDrawWhole==0. Frozen once MenuButton
        // sets the draw-whole flag (fruit already fully "present").
        // binary @0x001df874: 3.0=DAT_1dfb70; clamp 1.0=DAT_1dfb7c; uses dtScaled NOT dtNorm.
        if (!m_bDrawWhole) {
            m_MenuGrowFade += dtScaled * 3.0f;  // binary @0x001df874 -- DAT_1dfb70=3.0, DAT_1dfb90=1/60
            if (m_MenuGrowFade > 1.0f) m_MenuGrowFade = 1.0f;
        }

        // binary sliced physics @0x001df864-0x001df9d8 runs unconditionally -- no +0x16c gate here.
        // The grow-fade ramp (above) is also outside any frozen gate.
        // The ONLY frozen gate in the sliced path is the spin-quaternion loop further below @0x001e02a8.

        // binary @0x001df890 -- sliced gravity-grow: whole block gated on non-zero gravity
        // (DAT_1dfba0 = Vector3::ZERO, confirmed). Zero-gravity fruits skip both sub-gates.
        if (m_Gravity != _Vector3<float>(0.0f, 0.0f, 0.0f)) {
            // binary @0x001df8d4 -- normal (non-critical) sliced fruit; gravity magnitude grows.
            // 0.2=DAT_1dfb94, 4.5 @0x1df8fc
            if (m_bCritical == 0) {
                float len = m_Gravity.Normalise();  // unit-izes, returns old magnitude
                m_Gravity *= len + 0.2f * dtNorm * 4.5f;
            }
            // binary @0x001df908 -- m_bMenuFling (+0x164) == binary m_bExtraScore;
            // set=1 by MenuButton::CreateFruit for menu-context fruits.
            // 6.5 @0x1df930, 0.2=DAT_1dfb94
            if (m_bMenuFling != 0) {
                float len = m_Gravity.Normalise();
                m_Gravity *= len + 0.2f * dtNorm * 6.5f;
            }
        }

        // binary @0x001df93c -- two-body integration: same x60 position scale as unsliced.
        const float POS_INTEGRATION_SCALE = 60.0f;
        vel        += m_Gravity * dtScaled;
        m_SecondVel += m_Gravity * dtScaled;
        pos        += vel        * dtScaled * POS_INTEGRATION_SCALE;
        m_SecondPos += m_SecondVel * dtScaled * POS_INTEGRATION_SCALE;

        // binary @0x001df9d8 -- spin-phase: in sliced path, after two-body integration.
        // 1000.0=DAT_1dfb98.
        m_SpinPhase = (int)((float)m_SpinPhase + dtScaled * 1000.0f);

        // binary @0x001df9d8 -- m_SliceBounceTimer advance + reverse-time un-slice.
        // Accumulates dtScaled; when negative (time-rewind) and timer < 0, un-slices.
        m_SliceBounceTimer += dtScaled;
        if (dtScaled < 0.0f && m_SliceBounceTimer < 0.0f) {
            m_bSliced = 0;
            vel = m_SliceVelocity;
            m_SliceTimer = -1.0f;
            m_SecondPos = pos;
            m_SecondVel = vel;
        }
    }

    // binary @0x001e02a8 -- bomb-avoidance pushes nearby bombs away on X.
    // Called unconditionally; the m_bSliced gate lives inside UpdateBombAvoidance itself.
    UpdateBombAvoidance(dtScaled);

    // binary @0x001e0114 -- quaternion spin loop: per half, per axis k:
    //   CreateFromAxisAngle(m_SliceAxes[idx*3+k], (uint16)(int)(rotVel.k * dtNorm * 182.0))
    //   m_Rot[idx] = ((m_Rot[idx]*qx)*qy)*qz; Normalise.
    // Entry @0x001e02a8. Gated by m_bFrozen: when frozen the entire spin loop is skipped.
    // Super-fruit recomputes m_SliceAxes[idx*3+0] from m_Rot1.Matrix33()*Vec3(1,0,0) each frame.
    // (When NOT sliced the axes in m_SliceAxes are the unit basis from Init/SetupSliceRotations.)
    {
        const FruitInfoData* spinInfo = FruitInfo_Get((long)m_FruitType);
        const bool isSuperFruit = (spinInfo->m_bIsSuperFruit != 0);
        Quaternion* rotSlots[2] = { &m_Rot1, &m_Rot2 };
        _Vector3<float>* velSlots[2] = {&m_RotVel1, &m_RotVel2};
        for (int idx = 0; idx < 2; ++idx) {
            if (m_bFrozen != 0) break;
            // Super-fruit: recompute axis0 from current m_Rot1 each frame.
            if (isSuperFruit && m_bSliced) {
                Matrix44 mat = m_Rot1.ToMatrix44();
                // Matrix44 col-major: col0 = (m[0],m[1],m[2]) = mat * (1,0,0)
                m_SliceAxes[idx * 3 + 0] = _Vector3<float>(mat.m[0], mat.m[1], mat.m[2]);
            }
            _Vector3<float>& rv = *velSlots[idx];
            Quaternion& rot = *rotSlots[idx];
            _Vector3<float>& ax0 = m_SliceAxes[idx * 3 + 0];
            _Vector3<float>& ax1 = m_SliceAxes[idx * 3 + 1];
            _Vector3<float>& ax2 = m_SliceAxes[idx * 3 + 2];
            Quaternion qx, qy, qz;
            qx.CreateFromAxisAngle(ax0.x, ax0.y, ax0.z,
                (uint32_t)((int)(rv.x * dtNorm * 182.0f) & 0xFFFF));
            qy.CreateFromAxisAngle(ax1.x, ax1.y, ax1.z,
                (uint32_t)((int)(rv.y * dtNorm * 182.0f) & 0xFFFF));
            qz.CreateFromAxisAngle(ax2.x, ax2.y, ax2.z,
                (uint32_t)((int)(rv.z * dtNorm * 182.0f) & 0xFFFF));
            rot = ((rot * qx) * qy) * qz;
            rot = rot.normalized();
        }
    }

    // binary @0x001e02cc -- offscreen->KillFruit / m_Col update / emitter-detach / emitter pos
    // Binary stm writes pos.x/y/z to m_Col+4/+8/+12, then vstr overwrites
    // m_Col+12 with DAT_001e042c = 0x00000000 = 0.0f.
    if (m_Col) {
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center().x = pos.x;
        cs->center().y = pos.y;
        cs->center().z = 0.0f;  // DAT_001e042c
    }

    // binary @0x001e0330 -- pause-detach: when scaled dt is zero (paused or m_TimeScale==0),
    // release the juice emitters so they stop tracking the fruit's position while frozen.
    // Re-armed on next slice/SetTrailParticles.
    if (dtScaled == 0.0f) {
        RemoveTrailParticles();
    }

    // binary @0x001e034e -- per-frame emitter position/rotation tracking.
    //
    // ASM-spec v1.6.1 Fruit::Update @0x001df828: the emitter direction is COMPUTED
    // per frame from the fruit's spin, identically for BOTH tails -- emitter2 also
    // uses m_Rot1, NOT m_Rot2:
    //   d = Matrix33(m_Rot1) * _Vector3<float>::UnitZ   // = column 2 of ToMatrix44
    //   a = Math::Atan2Idx(d.x, d.y)   // arg order is (x, y) -- NOT atan2(y, x)
    //   e->m_DirSin = Math::SinIdx(a);  // +0x34, written FIRST
    //   e->m_DirCos = Math::CosIdx(a);  // +0x30, written SECOND
    // UnitZ is _Vector3<float>::UnitZ @0x003328ac, a guard-protected .bss static
    // (_ZGVN8_Vector3IfE5UnitZE @0x003328a8) constructed at runtime as (0,0,1) by
    // global.constructors.keyed.to.Fruit.cpp @0x001e2624-0x001e263c; it reads as
    // zeros in the file image only because it lives in .bss.
    //
    // Positions: emitter1 (@0x001e034e) is pos + UnitZ*(m_ZPosition - 20.0f) with z
    // then overwritten by DAT_001e0420 = -5000.0f, so the UnitZ term is dead there.
    // emitter2 (@0x001e0438-0x001e0474) is m_SecondPos + UnitZ*m_ZPosition raw --
    // no -20 bias and no z override.
    if (m_pEmitter1) {
        Matrix44 rotMat = m_Rot1.ToMatrix44();
        _Vector3<float> dir(rotMat.m[8], rotMat.m[9], rotMat.m[10]);
        unsigned short ang = (unsigned short)Math::Atan2Idx(dir.x, dir.y);
        m_pEmitter1->m_Pos     = pos;
        m_pEmitter1->m_Pos.z   = -5000.0f;  // DAT_001e0420
        m_pEmitter1->m_DirSin  = Math::SinIdx(ang);
        m_pEmitter1->m_DirCos  = Math::CosIdx(ang);
    }
    if (m_pEmitter2) {
        Matrix44 rotMat = m_Rot1.ToMatrix44();
        _Vector3<float> dir(rotMat.m[8], rotMat.m[9], rotMat.m[10]);
        unsigned short ang = (unsigned short)Math::Atan2Idx(dir.x, dir.y);
        m_pEmitter2->m_Pos     = m_SecondPos;  // binary calls this m_HalfB_pos; slot +0xC8
        m_pEmitter2->m_Pos.z   = m_SecondPos.z + m_ZPosition;
        m_pEmitter2->m_DirSin  = Math::SinIdx(ang);
        m_pEmitter2->m_DirCos  = Math::CosIdx(ang);
    }

    if (CheckHasGoneOffscreen()) {
        KillFruit(true);
    }
}

// Zen-mode "mirror bounce at X limits" flag. Reads bit 0x20 of
// SlashEntity::s_ModPowerMask (binary .bss 0x00332bc8) — a uint bitmask
// that active SlashModifier instances OR their bits into each frame.
// Bit 0x20 of SlashEntity::s_ModPowerMask is set by a SlashModifier
// registered in the Arcade-mode wave list. When active, vertical-gravity
// fruits hard-bounce off ±192 X bounds instead of being soft-nudged.
// Historically mislabelled "Zen" in port comments -- binary Fruit::DrawUpdate @0x001da618
// checks gameMode == 2 = ARCADE.
// ASM-spec v1.6.1 Fruit::DrawUpdate @0x001da618 (gate inside DrawUpdate; old 0x00175066 stale v1.5.x)
static bool IsArcadeStrictBounceActive() {
    return (SlashEntity::s_ModPowerMask & 0x20u) != 0;
}

// Matches Fruit::DrawUpdate @0x001da618 — called from Mortar::ActorManager::Update
// immediately after Update (vtable slot 6, +0x18). Also known as
// "DrawUpdate" in per-subclass docs; same slot as Bomb::PostUpdate.
//
// Binary behaviour:
//   m_AccelTerm(0x78) *= 0.9                            // DAT_0017519c damping
//   if (!m_bSliced && m_SpawnDelay(0x74) <= 0) {
//     if (m_Gravity(0xA0).x == 0) {                      // vertical-gravity fruit
//       if (arcade && (s_ModPowerMask & 0x20)) { hard bounce x on +-192 }
//       else                                   { soft nudge x toward centre }
//     } else if (m_Gravity(0xA0).y == 0) {               // horizontal-gravity fruit
//       soft nudge y toward centre on +-128
//     }
//   }
//
// Bounds resolved from binary: X = +/-192 (DAT_001751a0 / 751a4),
// Y = +/-128 (DAT_001751a8 / 751ac). Push / accelTerm magnitudes from
// the disassembly: vel += +/-16*dt, accelTerm += +/-20 (NO dt scaling on
// accelTerm -- accumulates per-frame, equilibrium ~200 against the
// 0.9 damping factor).
//
// ASM-spec v1.6.1 Fruit::DrawUpdate @0x001da618 (old 0x0017501c..0x00175198 stale v1.5.x;
//   the cited DAT_0017519c/751a0..751ac bound/damping slots are likewise stale -- re-verify
//   the v1.6.1 DAT addresses against Fruit::DrawUpdate before re-marking ASM-verified)
void Fruit::PostUpdate(float dt) {
    static const float ROT_AXIS_DAMPING = 0.9f;    // DAT_0017519c
    static const float BOUND_X_LO = -192.0f;       // DAT_001751a0
    static const float BOUND_X_HI =  192.0f;       // DAT_001751a4
    static const float BOUND_Y_LO = -128.0f;       // DAT_001751a8
    static const float BOUND_Y_HI =  128.0f;       // DAT_001751ac
    static const float PUSH_VEL   = 16.0f;
    static const float PUSH_ROT   = 20.0f;

    m_AccelTerm *= ROT_AXIS_DAMPING;

    if (m_bSliced) return;
    if (m_SpawnDelay > 0.0f) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    if (m_Gravity.x == 0.0f) {
        // Vertical-gravity fruit — nudge or hard-bounce on X bounds.
        // ASM-spec v1.6.1 Fruit::DrawUpdate @0x001da618 — gate is
        // gameMode == ARCADE (literal cmp #0x2) plus s_ModPowerMask bit 0x20.
        const bool arcade = (game_work.gameMode == GAME_MODE_ARCADE);
        const bool strictBounce = arcade && IsArcadeStrictBounceActive();
        if (strictBounce) {
            if (pos.x < BOUND_X_LO) { pos.x = BOUND_X_LO; vel.x = -vel.x; }
            if (pos.x > BOUND_X_HI) { pos.x = BOUND_X_HI; vel.x = -vel.x; }
        } else {
            if (pos.x < BOUND_X_LO) {
                vel.x         += dt * PUSH_VEL;
                m_AccelTerm.x += PUSH_ROT;
            }
            if (pos.x > BOUND_X_HI) {
                vel.x         += dt * -PUSH_VEL;
                m_AccelTerm.x -= PUSH_ROT;
            }
        }
    } else if (m_Gravity.y == 0.0f) {
        // Horizontal-gravity fruit — soft nudge on Y bounds.
        if (pos.y < BOUND_Y_LO) {
            vel.y         += dt * PUSH_VEL;
            m_AccelTerm.y += PUSH_ROT;
        }
        if (pos.y > BOUND_Y_HI) {
            vel.y         += dt * -PUSH_VEL;
            m_AccelTerm.y -= PUSH_ROT;
        }
    }
}

// Internal helper: draw the model once at (drawPos, drawRot, drawScale).
static void DrawOneModel(Mortar::Model* model,
                         const _Vector3<float>& drawPos,
                         const Quaternion& drawRot,
                         float s)
{
    Matrix44 mat = Matrix44::MakeScale(s, s, s);

    // No pre-quat mesh alignment. Per RE of iOS + Bada Fruit::Draw, neither
    // binary applies a coordinate fixup between Scale and Quat. The raw
    // mesh orientation (.mmd: +Z = long-axis / up) is intentional.
    Matrix44 qmat = drawRot.ToMatrix44();
    float rotMat[16];
    memcpy(rotMat, qmat.ptr(), sizeof(rotMat));
    float temp[16];
    mat4_multiply(temp, rotMat, mat.ptr());
    memcpy(mat.ptr(), temp, sizeof(temp));

    mat.GlobalTranslate44(drawPos);

    // Depth-test state is owned by the 3D actor pass in GameDraw
    // (SetDepthBuffer(1) before Mortar::ActorManager::Draw, off after) -- binary
    // @ 0x0016ba10. Fruit::Draw in the binary does NOT touch GL state.
    model->Draw(mat);
}

void Fruit::Draw(Renderer& r) {
    // ASM-spec v1.6.1 Fruit::Draw @0x001e0524
    // Binary resets the throw-fruit SFX per-frame flag at Draw entry,
    // not per-launch in Chuck. This means only one fruit per frame can
    // play the SFX, but the flag re-arms on the next frame.
    s_FruitThrowSfxFiredThisFrame = false;

    if (!IsActive() || m_SpawnDelay > 0.0f) return;

    const FruitModelInfo* fmi = GetFruitModelInfo(m_FruitType);
    if (!fmi || !fmi->m_Whole.IsValid()) return;

    // ASM-spec v1.6.1 Fruit::Draw @0x001e0524.
    // Binary Fruit::Draw uses m_VisualScale (+0x28) only for the model scale;
    // m_MenuGrowFade is NOT a model-scale multiplier. It is consumed exclusively
    // by Fruit::AddShadow for the whole<->halves shadow crossfade during slicing.
    float s = scale.x;
    if (s <= 0.0f) return;

    // Position in binary-centred ortho space.
    // See docs/engine/coordinate-system.md and FruitCamera::SetupPerspective.
    // Binary @ 0x1e0524: "if (p_pad[0x7c]==0 || p_pad[0x14c]!=0)" — draw the whole mesh.
    // p_pad[0x7c] = +0xB8 = m_bSliced; p_pad[0x14c] = +0x188 = m_bDrawWhole.
    // m_bDrawWhole is set by MenuButton when halves come to rest (respawn) OR by
    // ClearMenuItems when releasing menu fruit during dojo transition.
    if (!m_bSliced || m_bDrawWhole) {
        // Whole fruit — single draw at pos with m_Rot1.
        _Vector3<float> drawPos(pos.x, pos.y, m_ZPosition);
        DrawOneModel(fmi->m_Whole.Get(), drawPos, m_Rot1, s);
    } else {
        // Sliced fruit — draw two halves. Matches v1.6.1 Fruit::Draw
        // @0x001e0524 sliced branch which loops over
        // m_pFruitModels[type]->m_HalfA / m_HalfB.
        //
        // If a half mesh is missing, fall back to the whole-fruit mesh.
        Mortar::Model* halfA = fmi->m_HalfA.IsValid()
                             ? fmi->m_HalfA.Get() : fmi->m_Whole.Get();
        Mortar::Model* halfB = fmi->m_HalfB.IsValid()
                             ? fmi->m_HalfB.Get() : fmi->m_Whole.Get();

        _Vector3<float> drawPosA(pos.x, pos.y, m_ZPosition);
        _Vector3<float> drawPosB(m_SecondPos.x, m_SecondPos.y, m_ZPosition);
        DrawOneModel(halfA, drawPosA, m_Rot1, s);
        DrawOneModel(halfB, drawPosB, m_Rot2, s);
    }
}

// Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
void Fruit::Deactivate() {
    // No Fruit-specific emitter cleanup needed here; emitters are cleared
    // by KillFruit before the entity is deactivated.
}

// Matches v1.6.1 Fruit::KillFruit @0x001deba8.
void Fruit::KillFruit(bool doMissPenalty) {
    RemoveTrailParticles();

    if (doMissPenalty) {
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        if (!m_bNoPowerUp && !m_bSliced && info->m_Score < 5) {
            Game* g = Game::GetInstance();
            if (g) {
                // v1.6.1 Fruit::KillFruit @0x001deba8 (miss path):
                //   if (gameMode == ARCADE)            -> AddToTotal tracking only
                //   else if (FailureEnabled())          -> miss penalty (Classic/Combo)
                //   else (Zen) -> nothing
                // FailureEnabled() = ((gameMode-2u) > 1u) → true only for Classic/Combo.
                if (game_work.gameMode == GAME_MODE_ARCADE) {
                    // Arcade: tracking only, no life loss, no MissControl spawn.
                    // ASM-spec v1.6.1 Fruit::KillFruit @0x001deba8
                    // Dropped-fruit tracking (NOT a life loss). "dropped" is the global counter;
                    // m_DropsKey is the same per-fruit key used by the score path (line ~904), but
                    // here trackSession=false. (There is no info != null gate: v1.6.1
                    // Fruit::FruitInfo @0x001da5c0 is an unconditional array index.)
                    if (game_work.m_SaveData) {
                        static const uint32_t hDropped = StringHash("dropped");
                        game_work.m_SaveData->AddToTotal("dropped", hDropped, 1, false, false);
                        game_work.m_SaveData->AddToTotal(info->m_DropsKey, info->m_DropsHash, 1, false, false);
                    }
                } else if (Mortar::FailureEnabled(game_work.gameMode)) {
                    // Classic / Combo miss penalty (Zen falls through to no-op).
                    // bM_bPaused gate: binary GameUpdate @0x1cf9c4 `ldrb r3,[gctx,#0x5]; cmp; bne`
                    // suppresses miss penalty+gank+GameOver on the menu (bM_bPaused=1).
                    // ASM-spec v1.6.1 GameUpdate @ 0x001cf534 / 0x001cf9c4 (asm-inspector)
                    if (game_work.bM_bPaused == 0) {
                        if (MissControl* mc = MissControl::GetFree()) {
                            Mortar::SmartPtr<Mortar::Texture> defTex;
                            mc->MakeDisappear(pos, 0, defTex);
                        }
                        if (game_work.mGameSound) game_work.mGameSound->SFXPlay("gank", 1.0f, 1.0f);
                        game_work.missCount++;
                        if (game_work.missCount > 2) {
                            // ASM-spec v1.6.1 Fruit::KillFruit @0x001deba8 -- combo reset only inside game-over branch
                            g_ComboCount  = 0;
                            g_ComboFruitType = -1;  // binary writes 0xFFFFFFFF (v1.6.1 Fruit::KillFruit @0x001deba8)
                            GameOver(-1, -1.0f, -1);
                        }
                    }
                }
            }
        }
    }

    // Matches v1.6.1 Fruit::KillFruit @0x001deba8 cleanup tail.
    // 1. Clear owner's back-pointer (owner+0x14C = m_pTrackedFruit) if it still points at us.
    //    Binary: "puVar7=*(this+0x160); if(puVar7 && puVar7[0x53]==this){ puVar7[0x53]=0; }"
    //    owner+0x14C == MenuButton::m_pTrackedFruit (MenuButton.p_pad+0x110 = 0x14C in MenuButton).
    // ASM-spec v1.6.1 Fruit::KillFruit @0x001deba8
    if (m_pOwner) {
        // owner is always MenuButton (set by MenuButton::CreateFruit via m_pOwner=this).
        // Use named member so x64 host uses the correct pointer-width offset.
        MenuButton* owner = reinterpret_cast<MenuButton*>(m_pOwner);
        if (owner->m_pTrackedFruit == this) {
            owner->m_pTrackedFruit = nullptr;
        }
        m_pOwner = nullptr;
    }
    // 2. Decrement g_PowerFruitCount on natural-expiry path (flag 0x10 not yet set)
    //    AND for power-fruits (info->m_pPowers != nullptr).
    //    v1.6.1 Fruit::KillFruit @0x001deba8: unconditional store of 0 when count<=1
    //    else (count-1). Port previously used conditional decrement which pinned
    //    the counter at 1 across multiple natural expirations.
    if (!(flags & ENT_KILLED)) {
        const FruitInfoData* killInfo = FruitInfo_Get(m_FruitType);
        if (killInfo->m_pPowers) {
            int v = g_PowerFruitCount;
            int newv = 0;
            if (v > 1) newv = v - 1;
            g_PowerFruitCount = newv;
        }
    }
    // 3. Untrack from EntityTracker tree 0.
    if (m_TrackerID != 0) {
        ET_RemoveEntity(0, m_TrackerID);
    }

    // Fire global then per-fruit killed events — binary @ 0x1def10 (global) + 0x1def28 (per-fruit).
    // Both fire before the flags |= 0x10 mark so subscribers see the pre-killed state.
    g_FruitKilled(this);
    m_OnKilled(this);

    flags |= ENT_KILLED;
}

// Matches v1.6.1 Fruit::CheckHasGoneOffsceen @0x001df304.
// Returns true when BOTH halves are confirmed offscreen.
// Exact constants resolved from binary via read_memory.
//
// Coordinate system: X ∈ [-240, +240] (horizontal),
//                    Y ∈ [-160, +160] (vertical, +up).
//
// The "clamp" values are NOT bounces — they TELEPORT the half to the
// far side of the screen so it counts as "gone" for the kill check.
static const float OFFSCREEN_BASE      =  160.0f; // DAT_00175548
static const float WARP_CLAMP_TOP      = -320.0f; // DAT_0017554c
static const float WARP_THRESH_BOT     = -240.0f; // DAT_00175550
static const float WARP_CLAMP_BOT      =  320.0f; // DAT_00175554
static const float WARP_CLAMP_RIGHT    = -480.0f; // DAT_00175558
static const float WARP_CLAMP_LEFT     =  480.0f; // DAT_0017555c
static const float WARP_THRESH_TOP     =  240.0f; // DAT_00175560
static const float SCALE_MARGIN_MULT   =   50.0f; // DAT_00175564
static const float WARP_THRESH_RIGHT   =  360.0f; // DAT_00175568
static const float WARP_THRESH_LEFT    = -360.0f; // DAT_0017556c

// ASM-spec v1.6.1 Fruit::CheckHasGoneOffsceen @0x001df304: each off-screen gate is (<grav test>) || gZero;
// zero-gravity flung menu entities reap only via gZero. Block-5 (+X) return-true restored.
bool Fruit::CheckHasGoneOffscreen() {
    const float margin = SCALE_MARGIN_MULT * scale.y;
    const bool gZero = (m_Gravity.x == 0.0f && m_Gravity.y == 0.0f && m_Gravity.z == 0.0f);

    // === Horizontal gravity early exit (sliced + |m_Gravity.x| > 0) ===
    if (m_bSliced && (fabsf(m_Gravity.x) > 0.0f || gZero)) {
        float yBound = OFFSCREEN_BASE + margin;
        if (pos.y <= -yBound || pos.y >= yBound) {
            if (m_SecondPos.y <= -yBound || m_SecondPos.y >= yBound)
                return true;
        }
    }

    // === Downward gravity (m_Gravity.y < 0) ===
    bool halfA_gone = false;
    if (m_Gravity.y < 0.0f || gZero) {
        // Warp: sliced half that drifts above +240 gets teleported to
        // -320 (far below screen) so it counts as "gone" immediately.
        if (m_bSliced && pos.y > WARP_THRESH_TOP) {
            pos.y = WARP_CLAMP_TOP;
            vel.y = -1.0f;
        }
        if (m_bSliced && m_SecondPos.y > WARP_THRESH_TOP) {
            m_SecondPos.y = WARP_CLAMP_TOP;
            m_SecondVel.y = -1.0f;
        }

        float bottomBound = -(margin + OFFSCREEN_BASE);
        if (pos.y <= bottomBound && vel.y < 0.0f) {
            if (m_SliceTimer <= 0.0f &&
                m_SecondPos.y <= bottomBound && m_SecondVel.y < 0.0f)
                return true;
            halfA_gone = true;
        }

        if (m_bSliced) {
            float xBound = margin + WARP_THRESH_TOP;
            if (pos.x <= -xBound || pos.x >= xBound) {
                if (m_SecondPos.x <= -xBound || m_SecondPos.x >= xBound)
                    return true;
            }
        }
    }

    // === Upward gravity (m_Gravity.y > 0) ===
    if (m_Gravity.y > 0.0f || gZero) {
        if (m_bSliced && pos.y < WARP_THRESH_BOT) {
            pos.y = WARP_CLAMP_BOT;
            vel.y = 1.0f;
        }
        if (m_bSliced && m_SecondPos.y < WARP_THRESH_BOT) {
            m_SecondPos.y = WARP_CLAMP_BOT;
            m_SecondVel.y = 1.0f;
        }

        float topBound = margin + OFFSCREEN_BASE;
        if ((pos.y >= topBound && vel.y > 0.0f) || halfA_gone) {
            if (m_SliceTimer <= 0.0f &&
                m_SecondPos.y >= topBound && m_SecondVel.y > 0.0f)
                return true;
        }

        if (m_bSliced) {
            float xBound = margin + WARP_THRESH_TOP;
            if (pos.x <= -xBound || pos.x >= xBound) {
                if (m_SecondPos.x <= -xBound || m_SecondPos.x >= xBound)
                    return true;
            }
        }
    }

    // === Negative horizontal gravity (m_Gravity.x < 0) ===
    if (m_Gravity.x < 0.0f || gZero) {
        if (m_bSliced) {
            if (pos.x > WARP_THRESH_RIGHT) {
                pos.x = WARP_CLAMP_RIGHT;
                vel.x = -1.0f;
            }
            if (m_SecondPos.x > WARP_THRESH_RIGHT) {
                m_SecondPos.x = WARP_CLAMP_RIGHT;
                m_SecondVel.x = -1.0f;
            }
        }
        float leftBound = -(WARP_THRESH_TOP + margin);
        if ((pos.x <= leftBound && vel.x < 0.0f) || halfA_gone) {
            if (m_SliceTimer <= 0.0f &&
                m_SecondPos.x <= leftBound && m_SecondVel.x < 0.0f)
                return true;
        }
    }

    // === Positive horizontal gravity (m_Gravity.x > 0) ===
    if (m_Gravity.x > 0.0f || gZero) {
        if (m_Gravity.x > 0.0f && m_bSliced) {
            if (pos.x < WARP_THRESH_LEFT) {
                pos.x = WARP_CLAMP_LEFT;
                vel.x = 1.0f;
            }
            if (m_SecondPos.x < WARP_THRESH_LEFT) {
                m_SecondPos.x = WARP_CLAMP_LEFT;
                m_SecondVel.x = 1.0f;
            }
        }
        float rightBound = margin + WARP_THRESH_TOP; // scale.y * 50 + 240
        if (((rightBound <= pos.x && vel.x > 0.0f) || halfA_gone) && m_SliceTimer <= 0.0f && rightBound <= m_SecondPos.x)
            return m_SecondVel.x > 0.0f;
    }

    return false;
}

// v1.6.1 Fruit::CollisionResponse @0x001dd500 — vtable slot 9. Returns 1 if already sliced (early-out), else 0.
// Visual-only pipeline:
//   - guard (already sliced / timer positive → return 1)
//   - critical-hit eligibility ladder (v1.6.1 Fruit::CollisionResponse @0x001dd500)
//   - critical / special-fruit branch selection for impulse clamp + timer
//   - slice angle/impulse/pos capture from bladeVel
//   - one-shot impact particle emitter rotated by blade angle
//   - persistent juice emitters from FRUIT_INFO.m_SlicedHash
//   - AddSlice visual (SliceEffect_Add)
//   - CriticalFlash full-screen tint for critical + special-fruit paths
// Skipped: SFX, achievements, score, power-ups, coins, MissControl.
int Fruit::CollisionResponse(Mortar::Entity* hitter,
                              unsigned long /*flagsA*/,
                              unsigned long /*flagsB*/,
                              _Vector3<float>* bladeVelPtr) {
    // Guard: already sliced or slice timer is positive -> double-hit.
    if (m_bSliced || m_SliceTimer > -1.0f) {
        return 1;
    }
    const _Vector3<float>& bladeVel = bladeVelPtr ? *bladeVelPtr : _Vector3<float>(0, 0, 0);

    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    const bool isSpecial  = (info->m_Score == 0x32);

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500
    // Critical-hit eligibility ladder.
    // All gates must pass; on success roll Rand32(reroll) -- 0 == hit.
    m_bCritical = 0;

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500.
    // kCritScoreBound = CRITICAL_CHANCE (@0x002d8d4c, "chance" attr, fruitlist.xml = 50).
    // kCritResetBase  = CRITICAL_CHANCE_START_INC ("chance_inc" attr, fruitlist.xml = 30).
    // Used as: bound = min(m_ScoreThreshold, CRITICAL_CHANCE);
    //          on crit hit: m_ScoreThreshold = CRITICAL_CHANCE_START_INC + CRITICAL_CHANCE.
    // Previously hardcoded 5/30 -- the wrong bound (5 instead of 50) made crits
    // fire far more often than the original.
    const int kCritScoreBound = CRITICAL_CHANCE;
    const int kCritResetBase  = CRITICAL_CHANCE_START_INC;

    // FruitInfo +0x318 is m_bScorable: 1 = can receive critical hit.
    const bool canCritFruit = info->m_bScorable;

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500.
    // Critical-hit ladder gates on game_work fields at +0x05 (bM_bPaused)
    // and +0x10 (m_BombHitTimer) -- the same "non-interactive cinematic" pair used
    // by GameOver, bomb-hit, level-transition. Previously mislabelled as "frenzy"
    // gating, but it's just the existing transition-gate + bomb-hit-timer pair.
    const int score = game_work.currentScore;

    if (score >= 2
        && canCritFruit
        && game_work.bM_bPaused == 0   // +0x05
        && game_work.m_BombHitTimer       <= 0.0f // +0x10
       ) {
        int& thresh = game_work.m_ScoreThreshold;
        thresh = (thresh < 3) ? 2 : (thresh - 1);

        const float chance = WaveManager::GetInstance()->GetCriticalChance(0);
        if (chance > 0.0f) {
            const int   bound  = (thresh < kCritScoreBound) ? thresh : kCritScoreBound;
            const float ratio  = (float)bound / chance;
            const uint32_t reroll = (ratio <= 1.0f) ? 1u : (uint32_t)ratio;

            const uint32_t roll = WaveManager::GetInstance()->GetRandom().Rand32(reroll);
            if (roll == 0) {
                m_bCritical = 1;
                thresh = kCritResetBase + kCritScoreBound;
            }
        }
    }

    const bool isCritical = (m_bCritical != 0);

    // Blade speed clamp. Critical / special -> [6, 8]; normal -> [4, 8].
    float bladeSpeed = bladeVel.Magnitude() * SLICE_BLADE_SCALE;
    const float clampMin = (isCritical || isSpecial)
                           ? 6.0f : SLICE_CLAMP_MIN_NRM;
    if (bladeSpeed < clampMin)          bladeSpeed = clampMin;
    if (bladeSpeed > SLICE_CLAMP_MAX)   bladeSpeed = SLICE_CLAMP_MAX;

    // Slice timer — base 0.03, critical × 2.5 (slow), special × 0.5 (fast).
    float sliceTimer = SLICE_TIMER_BASE;
    if (isCritical)      sliceTimer *= 2.5f;
    else if (isSpecial)  sliceTimer *= 0.5f;

    m_SliceTimer      = sliceTimer;
    m_SliceArcImpulse = bladeSpeed;
    m_SlicePos        = pos;
    // Atan2Idx: 16-bit angle index (65536 = 360 deg). Port uses std atan2
    // + the same scale factor that Atan2Idx produces.
    const float rad = atan2f(bladeVel.x, bladeVel.y);
    m_SliceArcAngle   = (uint16_t)((int)(rad * (65536.0f / 6.2831853f)) & 0xFFFF);

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500.
    // Clear any prior trail/juice emitters before allocating the slice-
    // burst + persistent juice emitters below. Mirrors the pattern used
    // by Release/KillFruit so special-fruit trails (from
    // SetTrailParticles) don't leak when the fruit gets sliced.
    RemoveTrailParticles();

    // Impact particle emitter — one-shot, rotated by the blade direction.
    // Uses FRUIT_INFO.m_NameHash (e.g. "apple") as the template lookup. The
    // m_DirCos/m_DirSin (+0x30/+0x34) encode (cos, sin) of the rotation
    // applied to each spawned particle's initial velocity -- matches binary
    // AddParticle 0x00115644. Negative-angle sign flip mirrors the binary:
    //   e->m_DirCos =  CosIdx(-sliceAngle);
    //   e->m_DirSin = -SinIdx(-sliceAngle);  = SinIdx(sliceAngle)
    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        const float sliceRad = (float)(int16_t)m_SliceArcAngle *
                               (6.2831853f / 65536.0f);

        // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500.
        // Both emitter hashes are overridden to the "crit_hit"/"crit_hit_stars"
        // star-burst templates when the slice is a critical hit -- the port
        // previously always used the fruit's own splat/trail hashes even on crit.
        static const uint32_t kHashCritHit      = StringHash("crit_hit");
        static const uint32_t kHashCritHitStars = StringHash("crit_hit_stars");
        const uint32_t hitHash   = isCritical ? kHashCritHit      : info->m_NameHash;
        const uint32_t trailHash = isCritical ? kHashCritHitStars : info->m_SlicedHash;

        // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001ddbd4: updateWhenPaused =
        //   (game_work.m_PauseAmount(+0xc) < 1.0f) -- same idiom as SuperFruitControl::Sliced.
        PSPParticleEmitter* eHit = pm.AddEmitter(
            hitHash, nullptr, /*updateWhenPaused=*/game_work.m_PauseAmount < 1.0f);
        if (eHit) {
            eHit->m_Pos     = pos;
            eHit->m_DirCos  =  cosf(sliceRad);   // cos theta
            eHit->m_DirSin  =  sinf(sliceRad);   // sin theta
        }

        // Persistent juice emitters — one per future half. m_SlicedHash
        // resolves to "<name>_sliced" (e.g. "apple_sliced").
        // Note: ppRef=nullptr is safe (matches binary). All *_sliced templates that
        // exist in the XML have <life>0</life> and at least one set with stop="0" and
        // perSec>0, making them naturally-infinite: maxLifetime<=0 && !EmitterTemplateEnds().
        // The reap condition (binary @ 0x00115ed8: keep if timer<maxLifetime || maxLifetime<=0
        // && !Ends()) never frees them while Fruit holds the pointer. For most fruits the
        // template does not exist at all -> AddEmitter returns nullptr on hash-miss, safe.
        // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001ddc70 / @0x001ddc88: r2=NULL, r3=1.
        m_pEmitter1 = pm.AddEmitter(trailHash, nullptr, /*updateWhenPaused=*/true);
        m_pEmitter2 = pm.AddEmitter(trailHash, nullptr, /*updateWhenPaused=*/true);
        if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
        if (m_pEmitter2) m_pEmitter2->m_Pos = m_SecondPos;
    }

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500
    {
        bool altPlayed = ItemManager::GetInstance()->PlayAlternateImpactSound(1.0f, 0.5f);
        if (!altPlayed && info->m_pSounds && info->m_SoundCount > 0) {
            for (int si = 0; si < info->m_SoundCount; ++si) {
                const char* sndName = info->m_pSounds[si].m_SoundName;
                if (sndName && game_work.mGameSound) {
                    game_work.mGameSound->SFXPlay(sndName, 0.5f, 1.0f,
                        Mortar::Delegate1<bool, Mortar::MortarSound*>());
                }
            }
        }
    }

    // Full-screen tint flash. Matches CriticalFlash @ 0x0016a9a4.
    // Critical: v1.6.1 Fruit::CollisionResponse @0x001dd500 passes the GLOBAL
    // CRITICAL_COLOUR (loaded from fruitlist.xml <critical colour="0,140,245,170"/>),
    // NOT the sliced fruit's own per-type FruitTypeColour(). The previous
    // FruitTypeColour(m_FruitType) call here was a fabrication with no binary basis.
    // Special (score==0x32): white half-alpha (v1.6.1 Fruit::CollisionResponse @0x001dd500).
    if (isCritical) {
        CriticalFlash(pos, CRITICAL_COLOUR);
    } else if (isSpecial) {
        CriticalFlash(pos, Colour(255, 255, 255, 128));
    }

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500: CRITICAL_SCORE (@0x002d8d48,
    // fruitlist.xml "score" attr = 10), CRITICAL_CHANCE (@0x002d8d4c, "chance" attr = 50);
    // single-player crit = CriticalFlash only, no MakeCritical popup (that is
    // online-MP only). No MakeCritical/MakeRare here.

    // White slice-line visual — matches AddSlice call in binary
    // v1.6.1 Fruit::CollisionResponse @0x001dd500. Binary builds sliceInfo as:
    //   x = m_SliceArcAngle / -182.0 + 90.0   (degrees-offset)
    //   y = bladeSpeed * 0.4                   (impulse length)
    // v1.6.1 Fruit::CollisionResponse @0x001de53c: AddSlice with 6 args.
    //   modelIdx = 0 (normal) or 1 (crit), fruit = this, rateMul = 1.0
    // v1.6.1 @0x001ddac4: the binary reads the halfword UNSIGNED (ldrh +
    // vcvt.f32.u32), so arc >= 0x8000 must stay positive here. Casting through
    // int16_t makes those angles 360.09 deg low.
    const float sliceAngleDeg = (float)(uint16_t)m_SliceArcAngle / -182.0f + 90.0f;
    const float sliceLength   = bladeSpeed * 0.4f;
    AddSlice(_Vector3<float>(sliceAngleDeg, sliceLength, 1.0f), pos.x, pos.y,
             isCritical ? 1 : 0, this, pos.z);

    // Score, save totals, powerup, combo and achievements are gated exactly as
    // the binary does (v1.6.1 Fruit::CollisionResponse @0x001dd500):
    //   GameTaskState+0x06 (retryFlag) == 0          -- outer "interactive" gate
    //   && slash (hitter) != null                    -- real blade hit, not an
    //                                                   internal re-slice
    //   && m_bNoPowerUp == 0
    //   && ( GameTaskState+0x05 (bM_bPaused) == 0   -- normal play
    //        || bombHitWindow )                       -- or inside the bomb-hit
    //                                                   cinematic window
    // The bomb-hit window: (gameMode - 2u) < 2u && m_PauseAmount < 0.95f && m_PauseAmount > -0.1f.
    // Binary field gameMode (+0x04) for window, bM_bPaused (+0x05) for outer gate.
    // ASM-verified: 2026-06-07 v1.6.1 Fruit::CollisionResponse @0x001dd500 (re-analyst).
    int g_FruitWasSliced_points = 0; // carries score out of the gate for event fire at 0x1de5a0

    // v1.6.1 Fruit::CollisionResponse @0x001dd500:
    // OUTER gate uses bM_bPaused (+0x05); bomb-window uses gameMode (+0x04) and
    // m_PauseAmount (+0x0C = flM_PauseAmount in binary), NOT m_BombHitTimer (+0x10).
    // Hoisted above the scoring block: the same compound term also drives the
    // event-fire tail gate below (gateEarlyReturn), not just the scoring dispatch.
    const bool bombHitWindowGate = (uint8_t)(game_work.gameMode - 2u) < 2u
        && game_work.m_PauseAmount < kBombHitMax
        && game_work.m_PauseAmount > kBombHitMin;

    // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500: two-path tail.
    // TRUE  -> binary returns early WITHOUT firing FruitSliced/m_OnSliced (no
    //          event-subscriber walk); a super fruit still gets a reduced notify.
    // FALSE -> full main-path tail (scoring dispatch below, then the
    //          unconditional event fire further down) runs as today.
    const bool gateEarlyReturn = (hitter == nullptr) || m_bNoPowerUp
        || (game_work.bM_bPaused != 0 && !bombHitWindowGate);

    {
        if (game_work.retryFlag == 0
            && hitter != nullptr
            && !m_bNoPowerUp
            && (game_work.bM_bPaused == 0 || bombHitWindowGate)) {
        // Matches CollisionResponse score+save dispatch (binary @ 0x001de40c, inside 0x001dd500).
        // ASM-verified: 2026-05-10 v1.6.1 binary @ 0x001dd500 (re-analyst).
        // Formula:
        //   score = info->m_Score                               // FRUIT_INFO+0x314
        //   if (critical) score += CRITICAL_SCORE               // @ 0x002d8d48 (fruitlist.xml "score" attr = 10)
        //   if (info->m_CoinsMax > 0 && info->m_CoinsMin < info->m_CoinsMax)
        //       score = info->m_CoinsMin + Rand32(max - min)    // random-score override
        // Note: port's m_CoinsMin/m_CoinsMax slots are the binary's "RandBonusBase/Max"
        // when used in this score path; same fields, dual-purpose semantics.
        // The only x2 in the binary is on COINS, not score -- no score *= 2.
        {
            int score = info->m_Score;
            if (m_bCritical) score += CRITICAL_SCORE;
            // TODO: v1.6.1 -- verify CoinsMin/Max score-override; binary uses these for
            // coin count only, not score
            if (info->m_CoinsMax > 0 && info->m_CoinsMin < info->m_CoinsMax) {
                const uint32_t range = (uint32_t)(info->m_CoinsMax - info->m_CoinsMin);
                score = info->m_CoinsMin
                      + (int)WaveManager::GetInstance()->GetRandom().Rand32(range);
            }
            g_FruitWasSliced_points = score;    // carry score for event fire at 0x1de5a0
            AddToCurrentScore(score, (int)m_PlayerIdx,
                                  /*trackFruit=*/true, /*sendNetPacket=*/false);
            // ASM-spec v1.6.1 Fruit::CollisionResponse coin drop @0x001de778-95c:
            {
                int coinCount = 0;
                if (info->m_CoinsMax > 0) {
                    coinCount = info->m_CoinsMin;
                    if (info->m_CoinsMin < info->m_CoinsMax) {
                        const uint32_t coinRange = (uint32_t)(info->m_CoinsMax - info->m_CoinsMin);
                        coinCount = info->m_CoinsMin + (int)Math::g_Random.Rand32(coinRange);
                    }
                }
                if (m_bCritical) coinCount = (CRITICAL_SCORE / 2) * coinCount;
                if (coinCount > 0) {
                    const uint16_t coinAngleSpread =
                        (uint16_t)Math::Min((coinCount + 1) * 8190, 65520);
                    Coin::MakeCoins(coinCount, 1, pos, m_SliceArcAngle, coinAngleSpread,
                                    /*target=*/nullptr, 0.02f, 0.15f,
                                    /*flyFXName=*/nullptr, /*collectFXName=*/nullptr,
                                    Coin::DefaultArrivedDelegate(), /*silent=*/true);
                }
            }

            // v1.6.1 Fruit::CollisionResponse @0x001dd500: on an unsullied run (no misses
            // yet this game), tests the running score against SCORE_UNSULLIED achievements.
            // AchievementManager::UnlockScoreUnsulliedAchievement had zero call sites in the
            // port before this fix.
            if (game_work.m_bUnsullied == 0) {
                AchievementManager::GetInstance()->UnlockScoreUnsulliedAchievement(GetCurrentScore(0));
            }

            // Per-fruit-name save totals.
            // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500: two AddToTotal --
            // per-name (m_Name) + per-_total; the per-name total feeds the No-Bananas
            // bonus (frenzy/freeze/scorex2) via GetTotal(StringHash(powerName)).
            // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500: successful slice writes
            // ONLY m_Name (false) + m_TotalStatKey/_total (trackSession=true). <name>_drops
            // is written only in KillFruit's drop path (@0x001deba8) -- writing it here
            // over-counted strawberry_drops on every slice, breaking NOTHING BUT BERRY
            // (max-strawberry_drops=0) + polluting lifetime _drops.
            if (game_work.m_SaveData) {
                game_work.m_SaveData->AddToTotal(info->m_Name, info->m_NameHash, 1,
                                         /*trackSession=*/false, false);
                game_work.m_SaveData->AddToTotal(info->m_TotalStatKey, info->m_TotalStatHash, 1,
                                         /*trackSession=*/true, false);

                // On critical hit, record crit totals.
                if (isCritical) {
                    static const uint32_t hCrit      = StringHash("crit");
                    static const uint32_t hCritTotal = StringHash("crits_total");
                    game_work.m_SaveData->AddToTotal("crit",        hCrit,      1, false, false);
                    game_work.m_SaveData->AddToTotal("crits_total", hCritTotal, 1, true,  false);
                    char critBuf[128];
                    snprintf(critBuf, 128, "%scrit", info->m_Name);
                    game_work.m_SaveData->AddToTotal(critBuf, StringHash(critBuf), 1, false, false);
                }
            }
        }
        // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500
        // Arcade-mode-only (NOT Zen as a prior TODO claimed):
        //   AddToSpeedLossTime(0.05f, 0)             -- SpeedControl HUD tick refresh.
        //   first_fruit = sticky write-once          -- records m_FruitType+1 of first
        //                                               slice ever (savefile-wide).
        //   last_fruit  = set to current m_FruitType+1 via delta math (total := newVal).
        if (game_work.gameMode == GAME_MODE_ARCADE) {
            WaveManager::GetInstance()->AddToSpeedLossTime(0.05f, 0);
            if (game_work.m_SaveData) {
                static const uint32_t hFirstFruit = StringHash("first_fruit");
                static const uint32_t hLastFruit  = StringHash("last_fruit");
                const int newVal = (int)m_FruitType + 1;
                if (game_work.m_SaveData->GetTotal(hFirstFruit) <= 0) {
                    game_work.m_SaveData->AddToTotal("first_fruit", hFirstFruit, newVal, false, false);
                }
                const int cur = game_work.m_SaveData->GetTotal(hLastFruit);
                game_work.m_SaveData->AddToTotal("last_fruit", hLastFruit, newVal - cur, false, false);
            }
        }

        // ASM-verified: 2026-05-22 v1.6.1 binary @ 0x001dd500 ~+0x360 (re-analyst).
        // Powerup-fruit slice activates the modifier polymorphism chain. Without
        // this call, no Freeze/Frenzy/x2/Blitz effects ever fire in Arcade.
        // ASM-verified: 2026-05-22 v1.6.1 binary @ 0x001dd500 (re-analyst).
        // Powerup-fruit slice fires either during normal gameplay (bM_bPaused==0) OR
        // inside the bomb-hit cinematic window (gameMode in {2,3} = timed-game modes
        // AND m_PauseAmount in (-0.1f, 0.95f)). Binary field: gameMode (+0x04) for window,
        // bM_bPaused (+0x05) for outer; m_PauseAmount (+0x0C = flM_PauseAmount in binary).
        // v1.6.1 Fruit::CollisionResponse @0x001dd500
        const bool bombHitWindow = (uint8_t)(game_work.gameMode - 2u) < 2u
            && game_work.m_PauseAmount < kBombHitMax
            && game_work.m_PauseAmount > kBombHitMin;
        if (info->m_pPowers && !m_bNoPowerUp
            && (game_work.bM_bPaused == 0 || bombHitWindow)) {
            uint32_t hash = info->m_pPowers->RandomPower();
            _Vector3<float> localPos = pos;
            // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500 (@0x001ddd9c): purchaseExtra
            // arg is NULL (mov r3,#0x0) -- a slice never passes a touch-pos "extra" value. Passing
            // &localPos here made ActivatePower treat the slice as a purchase (isPurchased=true,
            // blocks wave rewind) and GameModifier::OnDeferComplete read the slice's X coord as
            // m_BonusAccum, collapsing freeze duration to ~0.01s (one tick) instead of ~7s.
            PowerUpManager::GetInstance()->ActivatePower(hash, localPos, NULL);
        }

        // Combo counter increment.
        // ASM-verified v1.6.1 Fruit::CollisionResponse @0x001dddec/0x1dde14/0x1dde1c:
        // combo resets when a DIFFERENT fruit TYPE continues the streak, not when a
        // different player slashes. g_ComboFruitType == Fruit::s_consecutiveType.
        if (g_ComboFruitType != (int)m_FruitType) {
            g_ComboCount     = 0;
            g_ComboFruitType = (int)m_FruitType;
        }
        g_ComboCount += 1;
        // ASM-spec v1.6.1 Fruit::CollisionResponse @0x001dd500: unconditional call
        // (not gated on hitter) right after the combo increment. arg1 = post-increment
        // g_ComboCount, arg2 = info->m_NameHash (FruitInfo+0x250, same hash passed to
        // UnlockSpecificOrderAchievement below).
        AchievementManager::GetInstance()->UnlockConsecutiveAchievement(g_ComboCount, info->m_NameHash);
        }

        // Defunct: P2P MP slice-broadcast block intentionally omitted -- v1.6.1 Fruit::CollisionResponse
        //   @0x001ddf1c (IsOnlineMultiplayer()-gated, ~380 instrs). Dead at runtime (IsOnlineMultiplayer
        //   always false); the call graph is already preserved by the SendP2PPacket / FruitSlicedPacket
        //   no-op stubs at their real defunct call sites. Replaying the exact block here would only chase
        //   the asm-verify LCS for zero gameplay value and requires restructuring the merged score path.

        // ASM-verified: 2026-07-04 v1.6.1 Fruit::CollisionResponse @0x001dd500 (call at
        // 0x001ddcf0). Binary control flow converges here regardless of the hitter/paused
        // branch above; the ONLY gate on this specific call is retryFlag==0 (game_work+0x6),
        // NOT the stricter hitter/paused/critical compound gate at line 1344-1347.
        if (game_work.retryFlag == 0) {
            AchievementManager::GetInstance()->UnlockSpecificOrderAchievement(info->m_NameHash);
        }
    }

    // ASM-verified: 2026-07-05T09:06:43Z v1.6.1 Fruit::CollisionResponse gated tail @0x001dea10 (asm-inspector)
    // Gated early-return path (binary two-paths the tail at 0x001ddcf4 -> 0x001dea10 -> 0x001de99c):
    // only a super fruit (FruitInfo+0x330 m_bIsSuperFruit) still notifies, and only when
    // hitter != 0; it fires the real FruitSliced/m_OnSliced Event3s with points=0
    // (Event3::operator() @0x0010c6c8, same fn as the full path). NOT SuperFruitSliced.
    if (gateEarlyReturn) {
        if (info->m_bIsSuperFruit && hitter != nullptr) {
            g_FruitWasSliced(this, 0, hitter);   // 0x1de9b8, points=0
            m_OnSliced(this, 0, hitter);         // 0x1de9cc, points=0
        }
        return 0;
    }

    // Fire g_FruitWasSliced — binary @ 0x1de5a0 (main slice path) and
    // 0x1de9b8 (early-disappear path). Args: (this, points=r7, slasher=param_1).
    // r7 in binary = the score local from the scoring block; port carries it via
    // g_FruitWasSliced_points (0 if gate not entered, matching likely binary r7 value).
    g_FruitWasSliced(this, g_FruitWasSliced_points, hitter);
    // Per-fruit m_OnSliced — binary @ 0x1de5b4 (main path) and 0x1de9cc (early-return path).
    // Both paths fire with identical args (this, points, hitter); port fires once here since
    // the two binary paths are merged into one in the current CollisionResponse port.
    m_OnSliced(this, g_FruitWasSliced_points, hitter);

    // v1.6.1 Fruit::CollisionResponse @0x001dd500: super-fruit slice runs via the
    // FruitSliced event subscription (SuperFruitControl::LoadContent @0x001bda74),
    // not a direct call.

    return 0;
}

// v1.6.1 Fruit::Slice @0x001dcba0 (body 0x001dcba0-0x001dd4ff): flipSide
// logic, special-fruit x1.5 impulse, and spin-boost loop on both halves.
// ASM-spec v1.6.1 Fruit::Slice @ 0x001dcba0..0x001dd4f8: the 2026-07-25T17:45:50Z
// asm-inspector pass over this range PASSED while the splat-count/spread/scale
// globals were still hardcoded to their pre-XML .data initialisers (10/1.2/1.5),
// so it did not cover the .data-vs-XML distinction. Verification claim withdrawn;
// re-earn it with a fresh asm-inspector pass now that the globals are read.
void Fruit::Slice() {
    m_SliceTimer = 0.0f;
    // ASM-spec v1.6.1 Fruit::Slice @0x001dcba0: top-of-function stores
    // @0x001dcbbc-0x001dcbd0 -- m_SliceBounceTimer (+0x88) = 0 and
    // m_SliceVelocity (+0x8C) = vel (snapshot of the member velocity at +0x1C);
    // the reverse-time un-slice in Update (@0x001df9d8) restores both.
    m_SliceBounceTimer = 0.0f;
    m_SliceVelocity = vel;

    // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
    // (was: 0x00176d78..0x00176db2 -- stale v1.5.x) -- two discarded select-pattern draws at
    // the very top of Slice, BEFORE the flipSide computation. Each: roll
    // Rand32(0x5550); if the roll exceeds 0x2aa8, roll once more (discarding
    // the result either way). The binary's RNG source here (GOT+DAT_00177058)
    // is the same singleton Math::g_Random wraps (instance @ 0x0026C8B0), so
    // these draws advance the same stream the half-velocity randA/randB and the
    // splat loop read from. Omitting them desynced the stream by 2-4 draws.
    {
        uint32_t d0 = Math::g_Random.Rand32(0x5550U);
        if (d0 > 0x2aa8U) Math::g_Random.Rand32(0x5550U);
        uint32_t d1 = Math::g_Random.Rand32(0x5550U);
        if (d1 > 0x2aa8U) Math::g_Random.Rand32(0x5550U);
    }

    // --- flipSide determination ---
    // Binary: rotate (0,0,1) by current m_Rot1 (Quaternion::Matrix44Unit +
    // Matrix44::MultVec44 with Vec3(0,0,1)), compare XY direction against
    // m_SliceArcAngle via GetSmallestDelta. If the rotated Z axis points
    // away from the slice direction, flip the halves' angles.
    _Vector3<float> slicePlane(0, 0, 1);
    // m_Rot1.ToMatrix44() * (0,0,1) == third column of the rotation matrix
    // (column-major, mat.m[8..10]).
    Matrix44 rotMat = m_Rot1.ToMatrix44();
    slicePlane.x = rotMat.m[8];
    slicePlane.y = rotMat.m[9];
    slicePlane.z = rotMat.m[10];

    // ASM-spec v1.6.1 Fruit::Slice @0x001dcba0 (inner 0x001dcc70-0x001dcc7c):
    // paused game clears the crit flag -- if (game_work.bM_bPaused)
    // m_bCritical = 0. Sits between the slicePlane computation and the
    // flipSide gate, so every downstream m_bCritical read (isCritical
    // snapshot, crit block, splat-loop bonus) sees the cleared value.
    if (game_work.bM_bPaused != 0) {
        m_bCritical = 0;
    }

    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    bool flipSide = false;
    // v1.6.1 Fruit::Slice @0x001dcc84: super-fruit (ldrb [r0,#0x330]) forces
    // flipSide (`bne 0x001dcd20` -> `mov r11,#1`); the geometric predicate
    // below is not executed at all in that case.
    if (info->m_bIsSuperFruit) {
        flipSide = true;
    } else if (fabsf(slicePlane.x) + fabsf(slicePlane.y) > 0.0f) {
        // ASM-verified: 2026-07-26T02:01Z v1.6.1 Fruit::Slice @ 0x001dcc84..0x001dcd2c (asm-inspector)
        // Literal binary math -- offsets 0x3FFC / 0x7FF8 and divisor 182.0 are
        // exact instruction-level constants, NOT 0x4000/0x8000/65536-per-360.
        // The (uint16_t) cast around Atan2Idx restores the binary's mod-65536
        // domain (port's Atan2Idx returns signed short). flipSide is
        // load-bearing visually: it selects which half gets the + vs - angle
        // offset AND is passed as sliceDirFlag into Fruit::SetupSliceRotations
        // @0x001da968 which sets the halves' spin -- do not "simplify" this
        // back to a plain atan2 delta (that reintroduces a handedness flip
        // plus a 90-degree offset in the sliced halves' rotation).
        uint16_t atanIdx = (uint16_t)Math::Atan2Idx(slicePlane.y, slicePlane.x);
        float a = (float)(uint16_t)(atanIdx - 0x3FFCu) / -182.0f + 360.0f;
        float b = (float)(uint16_t)(m_SliceArcAngle - 0x7FF8u) / 182.0f;
        if (GetSmallestDelta(a, b) < 0.0f) flipSide = true;
    }

    // --- Impulse ---
    float impulse = m_SliceArcImpulse;
    // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
    // (was: 0x00176e88 -- stale v1.5.x) -- base splatCount = Rand32(2) + 2 (= 2 or 3). Uses
    // the same Math::g_Random singleton (GOT+DAT_00177058) as every other draw
    // in Slice; the splat-count draw uses Math::g_Random.Rand32(2U) + 2.
    int   splatCount = (int)Math::g_Random.Rand32(2U) + 2;

    // Critical hit gets 1.5× impulse + crit dual-line AddSlice.
    // (`info` is fetched above, before the flipSide gate, matching the binary's
    // single FruitInfo load.)
    const bool isCritical = (m_bCritical != 0);
    // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
    // (was: 0x00176e94 -- stale v1.5.x) -- the ENTIRE critical block (two AddSlice lines,
    // splatCount override, impulse*1.5, MakeCritical) is gated on
    // m_bCritical && m_PlayerIdx < 2. With playerIdx >= 2 a crit slice
    // skips all of it and falls through to the normal splatCount/impulse.
    if (isCritical && m_PlayerIdx < 2) {
        // Binary: two slice lines at +/-60 deg offset from the base angle.
        //   infoA.x = m_SliceArcAngle / -182.0 + 60.0
        //   infoB.x = m_SliceArcAngle / -182.0 - 60.0
        //   infoA/B.y = impulse * 0.4 * 0.7
        // v1.6.1 Fruit::Slice @0x001dcba0: two crit slice lines at +/-60 deg.
        //   rateMul=1.0, modelIdx=0, fruit=(Fruit*)1 (sentinel, not real ptr)
        // v1.6.1 @0x001dcd98: unsigned halfword read (ldrh + vcvt.f32.u32) -- see
        // the note at the sliceAngleDeg site above.
        const float critBase = (float)(uint16_t)m_SliceArcAngle / -182.0f;
        const float critLen  = impulse * 0.4f * 0.7f;
        AddSlice(_Vector3<float>(critBase + 60.0f, critLen, 1.0f), pos.x, pos.y,
                 0, (Fruit*)1, pos.z);
        AddSlice(_Vector3<float>(critBase - 60.0f, critLen, 1.0f), pos.x, pos.y,
                 0, (Fruit*)1, pos.z);
        // v1.6.1 Fruit::Slice @0x001dce14: splatCount = CRITICAL_SPLATS
        // (@0x002d8d38), the XML-configured juice-burst count -- NOT
        // splatCount += 2. impulse *= 1.5.
        impulse *= 1.5f;
        splatCount = CRITICAL_SPLATS;
        // v1.6.1 Fruit::Slice @0x001dcba0: single-player crit popup (MissControl::MakeCritical),
        // gated m_bCritical && m_PlayerIdx<2. (No-op until the MissControl pool lands -- GetFree
        // returns nullptr -- but the call-shape is binary-faithful. #132 wrongly stripped this via
        // the stale 0x176d58 Slice address.)
        if (MissControl* mc = MissControl::GetFree()) {
            mc->MakeCritical(pos, (int)m_PlayerIdx);
        }
    }

    // Special-fruit (baseScore == 0x32 = 50) or super-fruit also gets 1.5x
    // impulse and the configured juice-burst count (v1.6.1 Fruit::Slice
    // @0x001dce54..0x001dce70: ldr +0x314 == 0x32 || ldrb +0x330).
    // @0x001dce80 reads the SAME CRITICAL_SPLATS global as the crit block.
    if (info->m_Score == 0x32 || info->m_bIsSuperFruit) {
        impulse *= 1.5f;
        splatCount = CRITICAL_SPLATS;
    }

    // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
    // (was: 0x00176f76 -- stale v1.5.x) -- offscreen kill of the juice burst: when
    // GameTaskState+0x06 (retryFlag) is set, this fruit is offscreen, and it
    // belongs to a remote/AI player (m_PlayerIdx > 1), suppress all splats.
    if (game_work.retryFlag != 0 && IsOffscreen() && m_PlayerIdx > 1) {
        splatCount = 0;
    }

    // --- Splat spawn ---
    // Per-splat speed = (impulse + RandF(0.5)*impulse) * (i*0.2 + 5).
    // Per-splat angle = Rand32(0xfff0) -- v1.6.1 @0x001dcf14
    // (`movw r1,#0xfff0; bl Rand32`): ONE draw over [0, 0xfff0), not a
    // 16-aligned masked draw.
    //
    // Binary uses raw impulse values directly (4..8 range from
    // CollisionResponse clamp). The port's Update integrates pos
    // with a x60 fudge factor (matching v1.6.1 Fruit::Update @0x001e00b8;
    // was: stale ref 0x00177d00), which means velocities should also stay in
    // the binary's per-frame scale -- no extra x50 multiplier needed here.
    for (int i = 0; i < splatCount; ++i) {
        const uint16_t angle16 = (uint16_t)Math::g_Random.Rand32(0xfff0);
        const float r          = Math::g_Random.RandF(0.5f);
        const float speed      = (impulse + r * impulse) *
                                 ((float)i * 0.2f + 5.0f);
        const float a          = (float)angle16 * (6.2831853f / 65536.0f);
        _Vector3<float> sv(sinf(a) * speed, cosf(a) * speed, 0.0f);

        SplatEntity* s = SplatEntity::GetFree();
        // v1.6.1 Fruit::Slice @0x001dcf5c: a critical slice passes fruitType = m_FruitType + count
        // (out of range) so MakeSplat sets m_ColourPhase=1.5 -- the critical-flash trigger.
        // SplatEntity::Update recovers the real colour via (fruitType % count). The 3rd arg
        // (m_bParam3) is a constant 0 here (binary `mov r3,#0`), not isCritical. GetFree() never
        // returns null (v1.6.1 SplatEntity::GetFree @0x001eb318 -- flat round-robin pool steals
        // the cursor slot when full).
        const long splatFruitType = (long)m_FruitType + (isCritical ? g_FruitInfoCount : 0);
        // ASM-spec v1.6.1 Fruit::Slice @0x001dcfc8: mute arg = (FruitInfo+0x330
        // m_bIsSuperFruit != 0) -- super-fruit splats land silent.
        s->MakeSplat(pos, sv, /*m_bParam3=*/false,
                     /*mute=*/info->m_bIsSuperFruit != 0, splatFruitType);

        // Per-splat post-MakeSplat taper. The later splats (high i) lose Z
        // velocity and, past index 2, gain X/Y velocity and scale so the burst
        // spreads outward as it grows.
        //   factor = clamp(1 - (i-2)/splatCount, 0.3, 1.0)   // 0.3 = DAT_001dcf04
        //   m_Vel.z *= factor
        //   if (i > 2) { m_Vel.x/y *= CRITICAL_SPLAT_SPREAD;
        //                m_Scale   *= CRITICAL_SPLAT_SCALE }
        // v1.6.1 Fruit::Slice: `i > 2` gate @0x001dd028, spread mul @0x001dd044,
        // scale mul @0x001dd064. Both muls read XML-loaded globals
        // (CRITICAL_SPLAT_SPREAD @0x002d8d40, CRITICAL_SPLAT_SCALE @0x002d8d3c).
        if (s) {
            float factor = 1.0f - (float)(i - 2) / (float)splatCount;
            if (factor <= kSplatTaperMin)      factor = kSplatTaperMin;
            else if (factor >= 1.0f)           factor = 1.0f;
            s->m_Vel.z *= factor;
            if (i > 2) {
                const float spread = CRITICAL_SPLAT_SPREAD;
                s->m_Vel.y *= spread;
                s->m_Vel.x *= spread;
                s->m_Scale *= CRITICAL_SPLAT_SCALE;
            }
        }
    }

    // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
    // (was: ASM-verified 0x001770e0 -- stale v1.5.x address, re-verify at v1.6.1 range)
    if (splatCount > 0 && game_work.mGameSound) {
        char cleanSliceBuf[16];
        uint32_t r = Math::g_Random.Rand32(3);
        snprintf(cleanSliceBuf, sizeof(cleanSliceBuf), "Clean-Slice-%u", r + 1);
        game_work.mGameSound->SFXPlay(cleanSliceBuf, 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }

    // --- Half velocities ---
    // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
    // (was: 0x00177186 -- stale v1.5.x) -- sliceFactor = FRUIT_INFO[+0x24c] (m_HitInfluence).
    // Default 0.75 for all shipped fruits; coconut = 0.9 (from hitInfluence XML attr).
    // Used as: halfVel = dir*(impulse*sliceFactor) + fruitVel*(1-sliceFactor)
    // and:     off = rand * (1-sliceFactor) * 4.0
    const float sliceFactor = info->m_HitInfluence;

    // SELECT pattern: use const 10920.0f when randVal <= 0x2aa8 (= 10920),
    // recompute Rand32(0x5550) when > 0x2aa8. Not a retry-if-small loop.
    // Constant from pool @0x001dcf08: bytes 00 A0 2A 46 = 0x462AA000 = 10920.0f.
    uint32_t _ra = Math::g_Random.Rand32(0x5550U);
    float randA = (_ra > 0x2aa8U) ? (float)Math::g_Random.Rand32(0x5550U) : 10920.0f;
    uint32_t _rb = Math::g_Random.Rand32(0x5550U);
    float randB = (_rb > 0x2aa8U) ? (float)Math::g_Random.Rand32(0x5550U) : 10920.0f;

    // Angle offsets for the two halves — bound by `(1-softness)*4`.
    const int16_t offA = (int16_t)(randA * (1.0f - sliceFactor) * 4.0f);
    const int16_t offB = (int16_t)(randB * (1.0f - sliceFactor) * 4.0f);

    // v1.6.1 Fruit::Slice @0x001dd204: flipSide writes arc+0x7ff8 back into
    // m_SliceArcAngle, then the half-angle computations RE-ADD 0x7ff8 before
    // the Sin/CosIdx calls (@0x001dd218 / @0x001dd2dc), cancelling back to
    // ~arc (net -0x10). Draw-to-half mapping (@0x001dd23c): the m_SecondVel
    // half uses the SECOND rand draw (offB, binary r6 @0x001dd1ec) added, the
    // vel half uses the FIRST draw (offA, binary r8 @0x001dd198) subtracted.
    if (flipSide) {
        m_SliceArcAngle = (uint16_t)(m_SliceArcAngle + 0x7ff8);
    }
    uint16_t angSecond, angVel;  // m_SecondVel half, vel half
    if (flipSide) {  // m_SliceArcAngle already == arc + 0x7ff8 from the write-back
        angSecond = (uint16_t)(m_SliceArcAngle - offB + 0x7ff8);  // == arc - offB - 0x10
        angVel    = (uint16_t)(m_SliceArcAngle + offA + 0x7ff8);  // == arc + offA - 0x10
    } else {
        angSecond = (uint16_t)(m_SliceArcAngle + offB);
        angVel    = (uint16_t)(m_SliceArcAngle - offA);
    }

    const float radSecond = (float)(int16_t)angSecond * (6.2831853f / 65536.0f);
    const float radVel    = (float)(int16_t)angVel * (6.2831853f / 65536.0f);
    _Vector3<float> dirSecond(sinf(radSecond), cosf(radSecond), 0.0f);
    _Vector3<float> dirVel(sinf(radVel), cosf(radVel), 0.0f);

    _Vector3<float> halfVelSecond = dirSecond * (impulse * sliceFactor) +
        vel * (1.0f - sliceFactor);
    _Vector3<float> halfVelVel = dirVel * (impulse * sliceFactor) +
        vel * (1.0f - sliceFactor);

    m_SecondPos = pos;

    // v1.6.1 Fruit::Slice @0x001dd380..0x001dd3b0: crit/special velocity
    // override gated on crit (ldrb +0x165) || score == 0x32 (ldr +0x314) ||
    // super-fruit (ldrb +0x330); mutually exclusive (if/else) with the
    // MoveFruitZPositionToBack branch. The override uses raw m_SliceArcAngle
    // (NOT the offset-baked half angles) with +-0x3ffc / 0xc004, int32
    // truncation on each velocity component, and a x1.75 scale.
    if (isCritical || info->m_Score == 0x32 || info->m_bIsSuperFruit) {
        const float critRadA = (float)(int16_t)(uint16_t)(m_SliceArcAngle + 0x3ffc) * (6.2831853f / 65536.0f);
        const float critRadB = (float)(int16_t)(uint16_t)(m_SliceArcAngle + 0xc004) * (6.2831853f / 65536.0f);
        halfVelSecond = _Vector3<float>((float)(int)(sinf(critRadA) * impulse),
                                        (float)(int)(cosf(critRadA) * impulse), 0.0f) * 1.75f;
        halfVelVel = _Vector3<float>((float)(int)(sinf(critRadB) * impulse),
                                     (float)(int)(cosf(critRadB) * impulse), 0.0f) * 1.75f;
    } else if (!m_bMenuFling) {
        // TODO: re-RE inner offset against v1.6.1 Fruit::Slice 0x001dcba0
        // (was: 0x00177444..0x0017744e -- stale v1.5.x) -- only on the plain slice path and
        // only when this fruit was NOT spawned by a critical splash / menu-fling.
        // m_bMenuFling==1 marks menu-context fruits (was m_bSpawnedByCriticalSplash).
        MoveFruitZPositionToBack(this->m_ZPosition);
    }

    m_SecondVel = halfVelSecond;
    vel         = halfVelVel;

        m_bSliced = true;

    // v1.6.1 Fruit::Slice @0x001dcba0: does NOT write m_Gravity (+0xA0) anywhere --
    // no str/vstr to [this,#0xa0] in the whole function. The prior
    // `m_Gravity = Vec3(0,-12,0)` reset was a port-only band-aid and has been
    // removed for binary fidelity.

    // SetupSliceRotations (binary @ 0x1DA968): fills m_SliceAxes, m_RotVel1/2,
    // and m_Rot1/2. sliceDirFlag = flipSide ? 1 : 0 (binary param_2 select).
    {
        const FruitInfoData* sliceInfo = FruitInfo_Get((long)m_FruitType);
        bool isSuperFruit = sliceInfo->m_bIsSuperFruit;
        SetupSliceRotations(isSuperFruit, flipSide ? 1 : 0);
    }

}

// Binary @ 0x001DA968. Fills m_SliceAxes[0..5] and m_RotVel1/2 with per-half
// spin axes and angular velocities, then builds initial m_Rot1/2 from the
// slice arc angle. Called from Fruit::Slice with (FruitInfo->m_bSuperFruit, sliceDirFlag).
//
// NORMAL path (isSuperFruit==0): copies unit-basis kSliceBaseAxis into m_SliceAxes,
//   derives spin magnitudes from m_RotVel magnitudes + sign coins, stores into m_RotVel.
// SUPER path  (isSuperFruit!=0): builds axes from blade direction (m_SliceArcAngle),
//   derives spin magnitudes from the same RandF/Rand32 pattern.
// Both paths: build m_Rot[idx] = (q1*q2)*q3 from fixed 0x3FFC angles + m_SliceArcAngle.
void Fruit::SetupSliceRotations(bool isSuperFruit, bool sliceDirFlag) {
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();

    // super-path: local_e4 starts 0, += 0xB4 per half (0, 180).
    int local_e4 = 0;

    for (int idx = 0; idx < 2; ++idx) {
        _Vector3<float>* rv = (idx == 0) ? &m_RotVel1 : &m_RotVel2;

        // Spin magnitude scale from incoming m_RotVel (per half).
        // DAT_001dae2c (2.0/0.5) keyed on m_bCritical @0x165.
        float mag = fabsf(rv->x) + fabsf(rv->y) + fabsf(rv->z);
        float magScale = (m_bCritical != 0) ? 2.0f : 0.5f;
        float fMag = mag * magScale;

        float angX, angY, angZ;

        if (!isSuperFruit) {
            // ===== NORMAL fruit: random spin axes (unit basis) =====
            m_SliceAxes[idx * 3 + 0] = kSliceBaseAxis[0];   // (1,0,0) @0x118/0x13C
            m_SliceAxes[idx * 3 + 1] = kSliceBaseAxis[1];   // (0,1,0) @0x124/0x148
            m_SliceAxes[idx * 3 + 2] = kSliceBaseAxis[2];   // (0,0,1) @0x130/0x154

            // Two RandF(0.5)+0.75 draws -> spin components a, b.
            float a = fMag * (rng.RandF(0.5f) + 0.75f);   // DAT_001dae30/34 via halfRange
            float b = fMag * (rng.RandF(0.5f) + 0.75f);

            // Sign coins on a (sliceDirFlag selects branch).
            bool flip;
            if (sliceDirFlag == 0) {
                if (rng.Rand32(5) < 2) a = -a;
                flip = (idx == 0);
            } else {
                if (rng.Rand32(5) >= 2) a = -a;
                flip = (idx == 1);
            }
            if (flip) { a = -a; } else { b = -b; }

            // Third-component coin (Rand32(3)):
            //   0: xUnits = |a|*1.5 (sign-masked to negative), yUnits = DAT_001dae24 (=0.0)
            //   else: xUnits = fMag*(RandF(0.3)-0.1), yUnits = a
            // angX=xUnits, angY=yUnits, angZ=-b
            if (rng.Rand32(3) == 0) {
                float v = a * 1.5f;
                if (v > 0.0f) v = -v;   // sign-mask: force negative (binary: fabs then negate)
                angX = v;
                angY = 0.0f;             // DAT_001dae24 = 0.0
            } else {
                float t = rng.RandF(0.3f);   // DAT_001dae30
                angX = fMag * (t - 0.1f);    // DAT_001dae34 = 0.1
                angY = a;
            }
            angZ = -b;
        } else {
            // ===== SUPER fruit: spin axes from blade direction =====
            // axis0: perpendicular to blade.
            uint16_t aBlade = (uint16_t)((0x3FC0 - (int)m_SliceArcAngle + 0x3C) & 0xFFFF);
            m_SliceAxes[idx * 3 + 0] = _Vector3<float>(CosIdx(aBlade), SinIdx(aBlade), 0.0f);

            // axis1: g_BaseAxisC = (0,0,1), flipped for half0.
            {
                _Vector3<float> base = kSliceBaseAxis[2];   // (0,0,1) = g_BaseAxisC @0x3328AC
                if (sliceDirFlag == 0) base = base * -1.0f;
                m_SliceAxes[idx * 3 + 1] = base;
            }

            // axis2: second blade-derived index with per-half offset.
            // local_e4 = 0 for half0, 0xB4 for half1.
            {
                uint16_t bIdx = (uint16_t)((int)((float)local_e4 * 182.0f) & 0xFFFF);
                uint16_t a2   = (uint16_t)(bIdx - m_SliceArcAngle);
                m_SliceAxes[idx * 3 + 2] = _Vector3<float>(CosIdx(a2), SinIdx(a2), 0.0f);
            }

            // For half1: flip this half's three axes (this+0x13c/+0x148/+0x154,
            // _Vector3::operator*=(-1.0) @0x001dab10..0x001dab3c).
            if (idx == 1) {
                m_SliceAxes[3] = m_SliceAxes[3] * -1.0f;
                m_SliceAxes[4] = m_SliceAxes[4] * -1.0f;
                m_SliceAxes[5] = m_SliceAxes[5] * -1.0f;
            }

            // Spin magnitudes (same RandF(0.5)+0.75 pattern, 3 values).
            float a = (float)(int)(fMag * (rng.RandF(0.5f) + 0.75f));
            float b = (float)(int)(fMag * (rng.RandF(0.5f) + 0.75f));
            float c = -(float)(int)(fMag * (rng.RandF(0.5f) + 0.75f));
            if (rng.Rand32(100) < 2) a = -a;
            // 1/3: scale c by 0.2 (DAT_001dae2c); else scale b by 0.2.
            if (rng.Rand32(3) == 0) { c = (float)((int)((float)c * 0.2f)); }
            else                    { b = (float)((int)((float)b * 0.2f)); }
            angX = a; angY = b; angZ = c;

            local_e4 += 0xB4;
        }

        // Store the per-axis spin triple into m_RotVel slot.
        *rv = _Vector3<float>(angX, angY, angZ);

        // Build initial m_Rot from the slice arc angle.
        // Binary @0x001dad40..0x001dadf0: three CreateFromAxisAngle calls with
        // angle 0x3FFC, 0x3FFC, then m_SliceArcAngle; axes (0,0,1)/(0,1,0)/(0,0,1).
        // Product: m_Rot[idx] = (q1*q2)*q3, stored WITHOUT normalize.
        // ASM-verified: 2026-07-26T06:00Z v1.6.1 Fruit::SetupSliceRotations @ 0x001da968..0x001dae54 (asm-inspector)
        Quaternion* q = (idx == 0) ? &m_Rot1 : &m_Rot2;
        Quaternion q1, q2, q3;
        q1.CreateFromAxisAngle(0.0f, 0.0f, 1.0f, 0x3FFCu);
        q2.CreateFromAxisAngle(0.0f, 1.0f, 0.0f, 0x3FFCu);
        q3.CreateFromAxisAngle(0.0f, 0.0f, 1.0f, (uint32_t)m_SliceArcAngle);
        *q = ((q1 * q2) * q3);
    }
}

// Matches v1.6.1 Fruit::RotateFacingUp @0x001db478.
// Sets m_Rot1/m_Rot2 to a fixed starting orientation (facing up) then
// optionally applies an alignment rotation. Sets m_RotVel1/m_RotVel2
// to spinVelAxis * random magnitude.
//
// Spin magnitude: +(2 + RandF(2.0)) or -(2 + RandF(2.0)), sign is a 50/50
// coin flip off Math::g_Random.Rand32(2). Byte-faithful to the binary --
// NOT a substitution. The sign only appears to be "always the same" on the
// port when g_Random's boot seed is near-constant across launches (see
// SystemManager::Init); that is a seed-entropy bug, not a math bug.
void Fruit::RotateFacingUp(bool alignToFacing, _Vector3<float> spinVelAxis) {
    // ASM-spec v1.6.1 Fruit::RotateFacingUp @0x001db478 — uses Math::g_Random
    float r    = Math::g_Random.RandF(2.0f);
    float sign = (Math::g_Random.Rand32(2) == 0) ? 1.0f : -1.0f;
    float magnitude = sign * (2.0f + r);

    for (int i = 0; i < 2; i++) {
        Quaternion* rot    = (i == 0) ? &m_Rot1 : &m_Rot2;
        _Vector3<float>* rotVel = (i == 0) ? &m_RotVel1 : &m_RotVel2;

        // Step 1: RandomStartAngle(rot, use2D=true)
        RandomStartAngle(*rot, true);

        // Step 2: optional facing-up alignment (alignToFacing=false at MP call site)
        if (alignToFacing) {
            // qA: axis=spinVelAxis, angle=0 --> Identity (sin(0)=0)
            Quaternion qA = Quaternion::Identity();

            // qB: axis=(0, 0, zSign), angle = 0x4e34
            // ARM idiom: zSign = -1 when (spinVelAxis.x + spinVelAxis.y >= 0), else +1
            float zSign = (spinVelAxis.x + spinVelAxis.y >= 0.0f) ? -1.0f : 1.0f;
            {
                float halfAngle = (float)(0x4e34 >> 1) / 65536.0f * 6.2831853f;
                float c = cosf(halfAngle), s = sinf(halfAngle);
                Quaternion qB(0.0f, 0.0f, zSign * s, c);
                *rot = (*rot * qA) * qB;
            }
        }

        // Step 3: rotation velocity = spinVelAxis * random magnitude
        *rotVel = spinVelAxis * magnitude;
    }
}

// Matches v1.6.1 Fruit::FruitType @0x001db6c8.
// Searches FRUIT_INFO array by hash of name, matching m_NameHash or
// m_NameHashUpper. Returns index on match. If not found and
// fallbackRandom=true returns a random index in [0, MAX_FRUIT_TYPES-1), else -1.
int Fruit::FruitType(const char* name, bool fallbackRandom) {
    const int count = g_FruitInfoCount;
    if (name && *name) {
        const uint32_t hash = StringHash(name);
        for (int i = 0; i < count; i++) {
            const FruitInfoData* info = FruitInfo_Get(i);
            if (info->m_NameHash == hash || info->m_NameHashUpper == hash) {
                return i;
            }
        }
    }
    if (fallbackRandom && count > 0) {
        // v1.6.1 Fruit::FruitType @0x001db6c8: the fallback draw comes from
        // WaveManager's own Random (WaveManager+0x20), NOT Math::g_random, and
        // the bound is MAX_FRUIT_TYPES - 1 -- the last registered type is never
        // picked by the fallback.
        return (int)WaveManager::GetInstance()->GetRandom().Rand32((uint32_t)(count - 1));
    }
    return -1;
}


// Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24.
// Also creates s_pool (capacity=100) and loads s_sliceModel[0/1/3] when fruitInfo==0
// (first call), matching the lazy-init guard @0x001e10c4.
void Fruit::LoadInfo() {
    FruitInfo_Load("xml/fruitlist.xml");
    // ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084: CRITICAL_COLOUR is the
    // <critical colour="..."/> global, copied here after the XML parse.
    CRITICAL_COLOUR = FruitInfo_GetCriticalColour();
}

// --- FruitGlobalData (v1.6.1 binary static block @ 0x332910) ---------------
//
// Single contiguous static block for all Fruit global state. Layout verified
// against v1.6.1 Fruit::DestroyFruitModels @0x001df1c0:
//   ldrb [base,#0xD4]  -> s_fruitModelsLoaded
//   ldr  [base,#0xD8]  -> s_fruitModels
//
// s_slices and s_pool: HEAP POINTERS (binary: operator new(0x14) each,
// operator delete in CleanupFruit; v1.6.1 CleanupFruit TODO). Port lifecycle:
// allocated in LoadFruitModels, deleted in DestroyFruitModels.
//
// s_fruitModels raw base and count are NOT stored as separate statics; derive:
//   count = *(int*)((char*)s_fruitModels - 4)
//   raw   = (char*)s_fruitModels - 8
//
// Binary allocates FruitModelInfo[] via operator_new(count * 0x24 + 8). Header:
//   [0] uint32_t elemSize (0x24)  [4] uint32_t count  [8] FruitModelInfo[count]
// Port mirrors the raw-allocation + placement-new pattern (v1.6.1 @0x001e08ec).

// Contiguous Fruit global-state block. Mirrors binary layout @ 0x332910.
// [port] s_slices/s_pool are stored as heap pointers (binary embeds the 0x14-byte
// objects inline; port uses new/delete to match the binary's allocation pattern).
// Convenience alias; List<SliceEffect>::Node is the 0x30-byte doubly-linked node
// managed by AddNodeToHead/Remove (v1.6.1 @0x001e3158 / @0x001e36c8).
typedef Mortar::List<SliceEffect>::Node SliceNode;

struct FruitGlobalData {
    Mortar::List<SliceEffect>*              s_slices;              // +0x00 (heap ptr)
    _Vector3<float> s_sliceParams[7];      // +0x04..+0x57 (slice-effect tuning Vec3s; HLE-confirmed floats)
    Mortar::SmartPtr<Mortar::Model>         s_sliceModel[4];       // +0x58..+0x67
    Mortar::MemoryPool<SliceNode>*          s_pool;                // +0x68 (heap ptr)
    // +0x6C..+0x8B: NOT FruitGlobalData members -- in the binary these 8 ints are
    // function-local statics of GetFact (@0x1db7b4: 2 __cxa_guard + cached
    // FruitType("red apple"/"apple")) and RandomFruit (@0x1dc5d8: 4 spawn-chance
    // accumulators) that the linker parked right after the block. The port's
    // GetFact/RandomFruit keep their own locals, so this is layout padding only.
    char                                    _pad6C[0x20];          // +0x6C..+0x8B (GetFact/RandomFruit statics in binary)
    Mortar::SmartPtr<Mortar::Texture2D>     s_globalFruitAtlas[2]; // +0x8C..+0x93
    Mortar::SmartPtr<Mortar::Texture2D>     s_atlas2[2];           // +0x94..+0x9B (opaque)
    Mortar::SmartPtr<Mortar::Texture>       s_texSlots[3];         // +0x9C..+0xA7 (opaque)
    char                                    _padA8[0x20];          // +0xA8..+0xC7 (other statics)
    Mortar::SmartPtr<Mortar::Texture>       s_objC8;               // +0xC8
    char                                    _padCC[8];             // +0xCC..+0xD3
    bool                                    s_fruitModelsLoaded;   // +0xD4
    char                                    _padD5[3];             // +0xD5..+0xD7
    FruitModelInfo*                         s_fruitModels;         // +0xD8
};

#ifdef __bada__
static_assert(__builtin_offsetof(FruitGlobalData, s_sliceModel)        == 0x58,
    "FruitGlobalData::s_sliceModel binary offset wrong");
static_assert(__builtin_offsetof(FruitGlobalData, s_pool)              == 0x68,
    "FruitGlobalData::s_pool binary offset wrong");
static_assert(__builtin_offsetof(FruitGlobalData, s_globalFruitAtlas)  == 0x8C,
    "FruitGlobalData::s_globalFruitAtlas binary offset wrong");
static_assert(__builtin_offsetof(FruitGlobalData, s_fruitModelsLoaded) == 0xD4,
    "FruitGlobalData::s_fruitModelsLoaded binary offset wrong");
static_assert(__builtin_offsetof(FruitGlobalData, s_fruitModels)       == 0xD8,
    "FruitGlobalData::s_fruitModels binary offset wrong");
static_assert(sizeof(FruitGlobalData)                                  == 0xDC,
    "sizeof(FruitGlobalData) wrong");
#endif

static FruitGlobalData g_fruitData;

// AddSlice -- v1.6.1 @0x001dc990
// Mangles: _Z8AddSlice8_Vector3IfEffiP5Fruitf
//   v.x = angleDeg, v.y = impulse, v.z = rateMul
//   posX/posY/posZ: world position components (split across arg slots)
//   modelIdx: 0=slice_fx, 1=slice_fx_crit, 3=slice_fx (super-fruit pass)
//   fruit: dedup/clamp sentinel (real Fruit* or 0/1/3)
void AddSlice(_Vector3<float> v, float posX, float posY, int modelIdx, Fruit* fruit, float posZ)
{
    // ASM-spec v1.6.1 AddSlice @0x001dc990: crit-slice SFX (impulse.y>2.5, 1-in-3),
    //   pick: Rand32(3)==0 -> "Visceral-impact-1"; else Rand32(2)==0 -> "-3"; else "-2".
    //   (Port previously used invented "Air-Whoosh-*" names; corrected #58.)
    if (v.y > 2.5f && Math::g_Random.Rand32(3) == 0) {
        const char* sfxName;
        if (Math::g_Random.Rand32(3) == 0)      sfxName = "Visceral-impact-1";
        else if (Math::g_Random.Rand32(2) == 0) sfxName = "Visceral-impact-3";
        else                                    sfxName = "Visceral-impact-2";
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay(sfxName, 1.0f, 1.0f);
        }
    }

    if (!g_fruitData.s_slices || !g_fruitData.s_pool) {
        return;
    }

    // Dedup: walk s_slices via Iterator; expire (set m_Timer=6.0) any earlier node
    // whose m_pFruit key matches this fruit/ident.
    // v1.6.1 @0x001dc990: +0x1c (m_pFruit) is the dedup key; action = expire.
    {
        Mortar::List<SliceEffect>::Iterator it = g_fruitData.s_slices->Begin();
        while (it.Okay()) {
            Mortar::List<SliceEffect>::Iterator nextIt = it.Next();
            if (it.Get()->value.m_pFruit == fruit) {
                it.Get()->value.m_Timer = 6.0f;
            }
            it = nextIt;
        }
    }

    SliceNode* n = g_fruitData.s_pool->Pop();
    if (!n) {
        return;
    }

    // Initialise the payload fields of the new node.
    n->value.m_Timer    = 0.f;
    n->value.m_Impulse  = v.y;
    n->value.m_AngleDeg = v.x;
    n->value.m_Pos      = _Vector3<float>(posX, posY, posZ);
    n->value.m_ModelIdx = modelIdx;
    n->value.m_pFruit   = fruit;
    n->value.m_RateMul  = v.z;
    n->value.m_Reserved24 = 0;

    g_fruitData.s_slices->AddNodeToHead(n);
}

// DrawSlices -- v1.6.1 @0x001dae7c
// Combined update+draw pass. pass==false draws modelIdx!=3; pass==true draws modelIdx==3.
void DrawSlices(float dt, bool pass)
{
    if (!g_fruitData.s_slices || !g_fruitData.s_pool) return;

    Mortar::List<SliceEffect>::Iterator it = g_fruitData.s_slices->Begin();
    while (it.Okay()) {
        // Capture next before potentially removing this node.
        Mortar::List<SliceEffect>::Iterator nextIt = it.Next();
        SliceNode* n = it.Get();

        float rate = (n->value.m_ModelIdx != 0) ? 0.75f : 1.0f;
        n->value.m_Timer += dt * n->value.m_RateMul * 40.0f * rate;

        // Fruit-link clamp: if the linked fruit has been sliced, clear the link;
        // clamp timer to minimum 3.0 while the link was active.
        if (n->value.m_pFruit) {
            // Sentinel check: only real Fruit* instances have m_bSliced.
            // Sentinels 0/1/3 must not be dereferenced -- check size_t > 3.
            uintptr_t fp = (uintptr_t)n->value.m_pFruit;
            if (fp > 3) {
                if (n->value.m_pFruit->m_bSliced) {
                    n->value.m_pFruit = 0;
                }
            }
            if (n->value.m_Timer < 3.0f) {
                n->value.m_Timer = 3.0f;
            }
        }

        if (n->value.m_Timer < 6.0f) {
            if (n->value.m_Timer >= 0.0f) {
                // pass gate: pass==false draws modelIdx!=3; pass==true draws modelIdx==3
                if ((!pass) == (n->value.m_ModelIdx != 3)) {
                    int f = (int)n->value.m_Timer;
                    if (f < 0) f = 0;
                    if (f > 5) f = 5;
                    float frac = n->value.m_Timer - (float)f;
                    const _Vector3<float>& kA = SLICE_KEYFRAMES[f];
                    const _Vector3<float>& kB = SLICE_KEYFRAMES[f + 1];
                    _Vector3<float> scale(
                        kA.x + (kB.x - kA.x) * frac,
                        kA.y + (kB.y - kA.y) * frac,
                        kA.z + (kB.z - kA.z) * frac
                    );
                    if (n->value.m_pFruit) {
                        scale.y *= 0.9f;
                    }

                    int idx = n->value.m_ModelIdx;
                    if (idx < 0 || idx > 3) idx = 0;
                    Mortar::Model* model = g_fruitData.s_sliceModel[idx].Get();
                    if (model) {
                        uint16_t a = (uint16_t)(int)(n->value.m_AngleDeg * 182.0f);
                        Matrix44 m = Matrix44::MakeScale(scale);
                        m.RotZ44(SinIdx(a), CosIdx(a));
                        m.GlobalTranslate44(n->value.m_Pos);
                        model->Draw(m);
                    }
                }
            }
        } else {
            g_fruitData.s_slices->Remove(n);
            g_fruitData.s_pool->Push(n);
        }

        it = nextIt;
    }
}

// Matches Fruit::LoadFruitModels (v1.6.1 0x001e08ec). Replicates the binary's
// raw-allocation + header pattern, followed by per-fruit model loading with
// SetupLighting, GetProperty extraction, T_2044 attachment, and shared
// slice-effect / atlas-texture extraction.
//
// Binary structure:
//   1. operator_new(count * 0x24 + 8), write header [0]=0x24, [1]=count
//   2. placement-new FruitModelInfo ctor on each slot (i = 0..count-1)
//   3. For each fruit i:
//      a. Load half-pieces 1/2, whole, outline from "models/Fruit/<name>_%c_..."
//      b. File::Exists guard before single.mmd
//      c. Per loaded model: SetupLighting + GetNode(0)->Geometry::GetProperty("DiffuseMap")
//         (0x2843d1 = rodata address of the "DiffuseMap" string literal, not a hash)
//      d. T_2044(effect, model) when whole valid AND FruitInfo[i]+0x330 (m_bIsSuperFruit) != 0
//   4. Load slice_fx.mmd / slice_fx_crit.mmd into static globals
//   5. Extract fruit atlas texture from s_fruitModels[0].m_pWholeEffect
void Fruit::LoadFruitModels() {
    if (g_fruitData.s_fruitModels) return;  // already loaded

    const int count = g_FruitInfoCount;
    if (count <= 0) return;

    // Step 1: Allocate raw array with header (matches binary: operator_new(count*0x24 + 8))
    const size_t elemSize = sizeof(FruitModelInfo);  // 0x24
    const size_t allocSize = 8 + (size_t)count * elemSize;
    uint8_t* raw = static_cast<uint8_t*>(::operator new(allocSize));

    // Write header: [0] = elemSize, [1] = count
    *reinterpret_cast<uint32_t*>(raw + 0) = static_cast<uint32_t>(elemSize);
    *reinterpret_cast<uint32_t*>(raw + 4) = static_cast<uint32_t>(count);

    // Element array starts at raw + 8
    FruitModelInfo* models = reinterpret_cast<FruitModelInfo*>(raw + 8);

    // Step 2: Placement-construct each FruitModelInfo slot
    for (int i = 0; i < count; ++i) {
        new (&models[i]) FruitModelInfo();
    }

    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();

    // Step 3: Per-fruit model loading (ALL indices, no skip for missing names)
    for (int i = 0; i < count; ++i) {
        const FruitInfoData* info = FruitInfo_Get(i);
        const char* name = info->m_ModelName;
        if (!name[0]) continue;

        const char firstChar = name[0];

        // ---- Half-piece 1/2: "<name>_<c>_piece_1.mmd" / "_piece_2.mmd" ----
        for (int piece = 1; piece <= 2; ++piece) {
            char path[256];
            snprintf(path, sizeof(path), "models/Fruit/%s_%c_piece_%d.mmd",
                     name, firstChar, piece);
            Mortar::SmartPtr<Mortar::Model> model = meshMgr->Load(path);
            if (!model.IsValid()) continue;

            // Binary @ 0x001e0a2c: SetupLighting(model)
            SetupLighting(model);

            // ASM-spec v1.6.1 @ 0x001e0a50: Model::GetNode(0)->GetGeometryEntry(0)->GetProperty("DiffuseMap")
            // (0x2843d1 = rodata address of the "DiffuseMap" string literal, not a hash --
            // Geometry::GetProperty does a plain string lookup). Stores the EffectProperty*
            // into the FruitModelInfo EffectProperty slot for shared atlas-texture resolution.
            // Port: Geometry::GetProperty returns nullptr while BuildPropList is
            // defunct (m_PropList always null), so this currently always null.
            Mortar::EffectProperty* prop = nullptr;
            {
                Mortar::Mesh* node0 = model->GetNode(0UL).Get();
                if (node0) {
                    Mortar::Geometry* geom = node0->GetGeometryEntry(0);
                    if (geom) {
                        prop = geom->GetProperty("DiffuseMap");
                    }
                }
            }

            if (piece == 1) {
                models[i].m_HalfA = model;
                models[i].m_pHalfEffectA = prop;
            } else {
                models[i].m_HalfB = model;
                models[i].m_pHalfEffectB = prop;
            }
        }

        // ---- Whole-fruit model: "<name>_single.mmd" (File::Exists guard) ----
        {
            char path[256];
            snprintf(path, sizeof(path), "models/Fruit/%s_single.mmd", name);
            Mortar::SmartPtr<Mortar::Model> wholeModel;
            if (Mortar::File::Exists(path, 0)) {
                wholeModel = meshMgr->Load(path);
            }

            if (wholeModel.IsValid()) {
                // Binary @ 0x001e0a2c: SetupLighting(model)
                SetupLighting(wholeModel);

                Mortar::EffectProperty* prop = nullptr;
                {
                    Mortar::Mesh* node0 = wholeModel->GetNode(0UL).Get();
                    // ASM-spec v1.6.1 @ 0x001e0a50: GetProperty("DiffuseMap") (0x2843d1 = rodata
                    // address of the string literal, not a hash)
                    if (node0) {
                        Mortar::Geometry* geom = node0->GetGeometryEntry(0);
                        if (geom) {
                            prop = geom->GetProperty("DiffuseMap");
                        }
                    }
                }

                models[i].m_Whole = wholeModel;
                models[i].m_pWholeEffect = prop;

                // ASM-spec v1.6.1 T_2044 @ 0x001e050c:
                // EffectProperty::SetValue<Texture2D>(prop, modelSmartPtr).
                // Called when info->m_bIsSuperFruit is set (binary +0x330).
                // Binary path: stores the model's diffuse-map texture as a
                // EffectTexture2D value in the EffectPropertyValues buffer so
                // the shared atlas extraction step can read it back.
                // Port: prop is always null while EffectProperty/BuildPropList
                // is defunct. The texture is already loaded by MeshManager
                // during .mmd parsing into Geometry::m_DiffuseTex, so no additional
                // work is required for rendering.
                if (info->m_bIsSuperFruit) {
                    // TODO: wire prop->SetValue(EffectTexture2D{...}, 0)
                    // when BuildPropList is reactivated. EffectProperty::SetValue
                    // added at SharedEffectProperties.h (v1.6.1 @ 0x0023fc9c).
                    // Port: texture already available via model's mesh materials.
                }
            }
        }

        // ---- Outline/MP model: "<name>_outline.mmd" (File::Exists guard) ----
        // ASM-spec v1.6.1 Fruit::LoadFruitModels @0x001e08ec: "%s_outline.mmd" is
        //   File::Exists-gated (soft) like _single; skip silently when absent.
        {
            char path[256];
            snprintf(path, sizeof(path), "models/Fruit/%s_outline.mmd", name);
            Mortar::SmartPtr<Mortar::Model> outlineModel;
            if (Mortar::File::Exists(path, 0)) {
                outlineModel = meshMgr->Load(path);
            }
            if (outlineModel.IsValid()) {
                SetupLighting(outlineModel);

                Mortar::EffectProperty* prop = nullptr;
                {
                    Mortar::Mesh* node0 = outlineModel->GetNode(0UL).Get();
                    // ASM-spec v1.6.1 @ 0x001e0a50: GetProperty("DiffuseMap") (0x2843d1 = rodata
                    // address of the string literal, not a hash)
                    if (node0) {
                        Mortar::Geometry* geom = node0->GetGeometryEntry(0);
                        if (geom) {
                            prop = geom->GetProperty("DiffuseMap");
                        }
                    }
                }

                models[i].m_pMpModel = outlineModel;
                models[i].m_pMpEffect = prop;
            }
        }
    }

    // Step 4: Load slice-effect models into g_fruitData.s_sliceModel[].
    // v1.6.1 Fruit::LoadFruitModels @0x001e09b4:
    //   [0] = models/fruit/slice_fx.mmd       (normal slice)
    //   [1] = models/fruit/slice_fx_crit.mmd  (critical slice)
    //   [2] = unused
    //   [3] = models/fruit/slice_fx.mmd       (super-fruit second pass)
    g_fruitData.s_sliceModel[0] = meshMgr->Load("models/fruit/slice_fx.mmd");
    g_fruitData.s_sliceModel[1] = meshMgr->Load("models/fruit/slice_fx_crit.mmd");
    g_fruitData.s_sliceModel[3] = meshMgr->Load("models/fruit/slice_fx.mmd");

    // Allocate slice list and pool (binary: new(0x14) each in LoadInfo @0x001e10c4).
    // Port: allocate here as equivalent lazy-init. Pool created with capacity 100.
    // The list's m_pPool field points to s_pool (list owns/points its pool per binary).
    if (!g_fruitData.s_slices) {
        g_fruitData.s_slices = new Mortar::List<SliceEffect>();
    }
    if (!g_fruitData.s_pool) {
        g_fruitData.s_pool = new Mortar::MemoryPool<SliceNode>();
    }
    if (g_fruitData.s_pool->Capacity() == 0) {
        g_fruitData.s_pool->Create(100);
    }

    // Step 5: Extract shared fruit atlas texture from first model's EffectProperty.
    // Binary @ 0x001e0b78: reads models[0].m_pWholeEffect->m_Owner
    //   ->TryGetValue<EffectTexture2D>(Type_Texture2D, 0) and stores the result
    //   into g_fruitData.s_globalFruitAtlas[0/1] (+0x8C/+0x90).
    // EffectProperty path is defunct in the port (BuildPropList not wired); atlas
    // texture is already available via per-geometry m_DiffuseTex from MeshManager.
    // DIFFERS: original reads atlas from EffectProperty after loading all models;
    // using per-geometry m_DiffuseTex from MeshManager because BuildPropList is
    // defunct (port). Fruit atlas texture is the same either way.
    // TODO: v1.6.1 Fruit::LoadFruitModels @0x001e08ec -- write g_fruitData.s_globalFruitAtlas[0/1]
    //   when EffectProperty path is reactivated.
    {
        // g_fruitData.s_globalFruitAtlas[0/1] written here when EffectProperty is live.
        // Field exists at +0x8C/+0x90 for binary layout fidelity (#295).
        if (count > 0 && models[0].m_pWholeEffect && models[0].m_pWholeEffect->m_Owner) {
            Mortar::EffectTexture2D texId;
            models[0].m_pWholeEffect->m_Owner->TryGetValue<Mortar::EffectTexture2D>(
                Mortar::EffectDataTypes::Type_Texture2D, 0, texId);
        }
    }

    // Binary stores element pointer at g_fruitData.s_fruitModels (+0xD8).
    // Raw base and count derived from header when needed (s_fruitModels-8=raw, s_fruitModels-4=count).
    g_fruitData.s_fruitModels = models;
    // Binary sets s_fruitModelsLoaded=1 as the last write in LoadFruitModels @0x1e08ec.
    g_fruitData.s_fruitModelsLoaded = true;
}

#if defined(FN_BLOCK_PRELOAD)
// Boot trim (task #59). Thin forward to FruitInfo_LoadHudTextures
// (FruitInfo.cpp), which owns the deferred hud_%s/zen_%s texture-load seam.
// See FruitInfo.h for the contract.
void Fruit::LoadHudTextures() {
    FruitInfo_LoadHudTextures();
}
#endif

const FruitModelInfo* Fruit::GetFruitModelInfo(int fruitType) {
    if (!g_fruitData.s_fruitModels) return nullptr;
    // Count derived from allocation header: *(s_fruitModels - 4) (v1.6.1 binary pattern).
    int modelCount = *reinterpret_cast<const int*>((const char*)g_fruitData.s_fruitModels - 4);
    if (fruitType < 0 || fruitType >= modelCount) return nullptr;
    return &g_fruitData.s_fruitModels[fruitType];
}

// v1.6.1 Fruit::RandomFruit @0x001dc5d8
// 4-path weighted selector: {crit,normal} x {includeOnSide,avail-only}.
// Lazy-init cumulative tables on first call via m_CumWeight / m_CumCritWeight cache fields.
// Field usage (asm-inspector verified offsets):
//   "available" gate  -> m_CoinsMax < 1  (+0x328, int)
//   "critical"  gate  -> m_bScorable     (+0x318, byte)
//   m_bSpecial (+0x319) is NOT read by this function.
static int s_TotalWeight     = 0;
static int s_TotalAvail      = 0;
static int s_TotalCrit       = 0;
static int s_TotalCritAvail  = 0;

// ASM-spec v1.6.1 Fruit::RandomFruit @ 0x001dc5d8 (decompile)
int Fruit::RandomFruit(bool includeOnSide) {
    const int cnt = g_FruitInfoCount;
    if (s_TotalWeight < 1) {
        s_TotalWeight     = 0;
        s_TotalAvail      = 0;
        s_TotalCrit       = 0;
        s_TotalCritAvail  = 0;
        FruitInfoData* fi = g_pFruitInfo;
        for (int i = 0; i < cnt; ++i, ++fi) {
            s_TotalWeight += fi->m_Chance;
            fi->m_CumWeight = s_TotalWeight;
            if (fi->m_CoinsMax < 1)
                s_TotalAvail += fi->m_Chance;
            if (fi->m_bScorable) {
                s_TotalCrit += fi->m_Chance;
                if (fi->m_CoinsMax < 1)
                    s_TotalCritAvail += fi->m_Chance;
            }
            fi->m_CumCritWeight = s_TotalCrit;
        }
    }
    bool isCrit = WaveManager::GetInstance()->CriticalMode(0);
    Math::Random* rng = &WaveManager::GetInstance()->m_Random;
    FruitInfoData* fruitInfoData = g_pFruitInfo;
    if (!isCrit) {
        if (includeOnSide) {
            uint32_t r = rng->Rand32((uint32_t)s_TotalWeight);
            FruitInfoData* fi = fruitInfoData;
            for (int i = 0; i < cnt; ++i, ++fi)
                if (r < (uint32_t)fi->m_CumWeight) return i;
        } else {
            uint32_t r = rng->Rand32((uint32_t)s_TotalAvail);
            FruitInfoData* fi = fruitInfoData;
            int acc = 0;
            for (int i = 0; i < cnt; ++i, ++fi) {
                if (fi->m_CoinsMax < 1) {
                    acc += fi->m_Chance;
                    if (r < (uint32_t)acc) return i;
                }
            }
        }
    } else {
        if (includeOnSide) {
            uint32_t r = rng->Rand32((uint32_t)s_TotalCrit);
            FruitInfoData* fi = fruitInfoData;
            for (int i = 0; i < cnt; ++i, ++fi)
                if (r < (uint32_t)fi->m_CumCritWeight) return i;
        } else {
            uint32_t r = rng->Rand32((uint32_t)s_TotalCritAvail);
            FruitInfoData* fi = fruitInfoData;
            int acc = 0;
            for (int i = 0; i < cnt; ++i, ++fi) {
                if (fi->m_CoinsMax < 1 && fi->m_bScorable) {
                    acc += fi->m_Chance;
                    if (r < (uint32_t)acc) return i;
                }
            }
        }
    }
    return (int)rng->Rand32((uint32_t)(cnt - 1));
}

// ASM-spec v1.6.1 Fruit::GetNumActiveForPlayer @0x001db104.
// Second param is a MODE-SELECTOR, NOT "checkBombs" as the name suggested.
//   byPlayerMode==false: counts INACTIVE fruits (ignores playerIdx).
//   byPlayerMode==true : counts fruits where m_PlayerIdx == playerIdx
//                        (any active state). Bombs are NEVER iterated.
int Fruit::GetNumActiveForPlayer(int playerIdx, bool byPlayerMode) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return 0;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    int count = 0;
    while (e) {
        Fruit* f = static_cast<Fruit*>(e);
        if (!byPlayerMode) { if (!f->IsActive()) ++count; }
        else               { if (f->m_PlayerIdx == playerIdx) ++count; }
        e = am->GetEntityNext(0, it);
    }
    return count;
}

// ASM-spec v1.6.1 Fruit::ClearUnspawned @0x001def68
void Fruit::ClearUnspawned(bool clearAll) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    while (e) {
        Fruit* f = static_cast<Fruit*>(e);
        Mortar::Entity* next_e = am->GetEntityNext(0, it);
        if (clearAll || f->m_SpawnDelay > 0.0f)
            f->KillFruit(false);
        e = next_e;
    }
}

// Matches v1.6.1 Fruit::Disable @0x0012c9cc: one-byte store of 1 to m_bNoPowerUp.
// ASM-spec v1.6.1 Fruit::Disable @0x0012c9cc
void Fruit::Disable() {
    m_bNoPowerUp = 1;
}

// Matches Fruit::DrawShadows (0x001dea40) + AddShadow (0x001dbbe8).
// Texture: fruit_shadow.tex (loaded by FruitInfo_Load step 0).
// Geometry: up to 3 quads per fruit (1 spawn-fade + 2 half-shadows when active).
// DrawTriStrip with 6*N-1 verts: each quad is 6 vertices (4 distinct + 2 degenerate padding).
// Strip draws 2 triangles per quad (indices 0..3) then the padding (v4=v3, v5=v2)
// creates degenerate triangles separating adjacent quads.
static QUADCUSTOMVERTEX s_ShadowVerts[1152];   // 64 fruits * 3 quads * 6 verts

void Fruit::DrawShadows() {
    Mortar::Texture* shadowTex = FruitInfo_GetShadowTex();
    if (!shadowTex) return;

    QUADCUSTOMVERTEX* w = s_ShadowVerts;
    int count = 0;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    while (e) {
        Fruit* f = static_cast<Fruit*>(e);
        // Binary @ 0x001deaa4: gate on scale.x > 0 AND (flags & 0x01) == 0
        if (f->scale.x > 0.0f && (f->flags & ENT_INACTIVE) == 0) {
            f->AddShadow(&w, &count);
        }
        // Binary calls GetInstance before GetEntityNext (loop @ 0x001dead8)
        am = Mortar::ActorManager::GetInstance();
        e = am->GetEntityNext(0, it);
    }
    if (count == 0) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    shadowTex->Set();
    // Binary @ 0x001deb64: DrawTriStrip(verts, count*6-1, 0, false, null, texPage)
    Mortar::Mesh::DrawTriStrip(s_ShadowVerts, count * 6 - 1, false, NULL);
    shadowTex->UnSet();
}

// Writes up to 3 quads (shadow geometry) for one fruit into the shared buffer.
// Matches Fruit::AddShadow @ 0x001dbbe8.
//
// Binary DAT constants:
//   DAT_00176164 = 230.0f  (spawn-fade alpha mult)
//   DAT_00176168 = 82.0f   (whole-fruit shadow half-size base)
//   DAT_0017616c = -0.65f  (whole-shadow offset mult)
//   DAT_00176170 = 100.0f  (post-spawn alpha mult)
//   DAT_00176174 = 50.0f   (sliced-half shadow half-size base)
//   DAT_00176178 = -0.45f  (sliced-half offset mult)
//   DAT_00176180 = Vec3(0,0,1)  (slice-plane axis BSS singleton, never reassigned)
//   DAT_00175e9c = -5000.0f    (shadow Z, written by AddQuad)
void AddQuad(QUADCUSTOMVERTEX** out, float cx, float cy, float w, float h, Colour col) {
    // Strip-compatible vertex order: TL, BL, BR, TR, TR, BR
    //   v0: (cx-w, cy-h)    v3: (cx+w, cy-h)  -- TR (also degenerate padding v4)
    //   v1: (cx-w, cy+h)    v5: (cx+w, cy+h)  -- BR (also degenerate padding v5)
    //   v2: (cx+w, cy+h)    -- BR
    // First 4 distinct vertices (v0..v3) produce 2 valid triangles covering the quad.
    // v4 (same as v3) and v5 (same as v2) are degenerate to separate adjacent quads.
    // All verts: z = -5000 (DAT_00175e9c), normal (0,0,1), u in {0,1}, v in {0,1}.
    const float z    = -5000.0f;   // DAT_00175e9c
    const uint32_t c = ((uint32_t)col.a << 24)
                     | ((uint32_t)col.b << 16)
                     | ((uint32_t)col.g <<  8)
                     | ((uint32_t)col.r);

    QUADCUSTOMVERTEX* v = *out;

    v[0] = { cx - w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 0.0f };
    v[1] = { cx - w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 1.0f };
    v[2] = { cx + w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 1.0f };
    v[3] = { cx + w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 0.0f };
    v[4] = { cx + w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 0.0f };
    v[5] = { cx + w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 1.0f };

    *out += 6;
}

// ============================================================
// Chunk A: pure-data accessors
// ============================================================

// v1.6.1 Fruit::FruitTypeName @0x001da4a4
const char* Fruit::FruitTypeName(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info->m_Name;
}

// v1.6.1 Fruit::FruitTypeHash @0x001da4cc
unsigned long Fruit::FruitTypeHash(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return (unsigned long)info->m_NameHash;
}

// v1.6.1 Fruit::FruitFactTexture @0x001da4f8
const char* Fruit::FruitFactTexture(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info->m_FactTexture;
}

// v1.6.1 Fruit::FruitTypeColour @0x001da524
Colour Fruit::FruitTypeColour(long type) {
    // DIFFERS: v1.6.1 Fruit::FruitTypeColour @0x001da524 checks the special-fruit
    // index == type (MAX_FRUIT_TYPES sentinel) and returns the special/critical
    // colour (CRITICAL_COLOUR) when matched, set by
    // StarFruit/Gem/Pomegranate spawners as per-frame colour overrides.
    // (old v1.5.x DAT_0x00174fbc/DAT_0x00174fc0 are stale -- re-verify the v1.6.1 DAT slots.)
    // Port has neither global wired (none of those spawners are ported yet);
    // -1 default means "never match", so this branch always falls through
    // to FRUIT_INFO[type]. Re-enable when those spawners port.
    const FruitInfoData* info = FruitInfo_Get((int)type);
    if (!info) return Colour(255, 255, 255, 255);
    // FIX (v1.6.1 Fruit::FruitTypeColour @0x001da524): m_FruitColour bytes are [B,G,R,A]
    // (FruitInfo.cpp:271-274). Passing them positionally into Colour(r,g,b,a) swapped R<->B
    // (orange 255,115,0 came out blue 0,115,255). Map r=[2], b=[0] to un-swap -- mirrors
    // FruitFactColour below. Only caller is the critical-flash lerp (SplatEntity.cpp:553).
    return Colour(info->m_FruitColour[2], info->m_FruitColour[1],
                  info->m_FruitColour[0], info->m_FruitColour[3]);
}

// v1.6.1 Fruit::FruitFactColour @0x001da584
Colour Fruit::FruitFactColour(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    if (!info) return Colour(255, 255, 255, 255);
    // FIX (v1.6.1 Fruit::FruitFactColour @0x001da584): m_FactColour bytes are [B,G,R,A]
    // (FruitInfo.cpp:250-253). Passing them positionally into Colour(r,g,b,a) swapped R<->B
    // (apple 189,238,58 came out cyan). Map r=[2], b=[0] to un-swap.
    return Colour(info->m_FactColour[2], info->m_FactColour[1],
                  info->m_FactColour[0], info->m_FactColour[3]);
}

// v1.6.1 Fruit::FruitInfo @0x001da5c0
const ::FruitInfo* Fruit::FruitInfo(long type) {
    return FruitInfo_Get((int)type);
}

// v1.6.1 GetFruitZPosition @0x001ca61c — return next z-slot and advance counter.
// Counter starts at -500, decrements by 100 per call, resets to -500 when
// it falls below -2499. DATs: step=100, lower=-2499, reset=-500.
// TODO: re-verify v1.6.1 DAT slots for step/lower/reset (old 0x00169108/10c/110 stale v1.5.x).
// ASM-spec v1.6.1 GetFruitZPosition @0x001ca61c
float GetFruitZPosition() {
    s_FruitZCounter -= 100.0f;
    if (s_FruitZCounter < -2499.0f) {
        s_FruitZCounter = -500.0f;
    }
    return s_FruitZCounter;
}

// Binary: _Z24MoveFruitZPositionToBackRf @0x001ca674 (v1.6.1)
// Formula from disassembly (VNMLS): z = (500 + z)*0.5 - 2600
// DATs: addend=500, subtrahend=2600, half=0.5 (vmov literal).
// TODO: re-verify v1.6.1 DAT slots for addend/subtrahend (old 0x0016913c/0x00169140 stale v1.5.x).
// ASM-spec v1.6.1 MoveFruitZPositionToBack @0x001ca674
void MoveFruitZPositionToBack(float& z) {
    z = (500.0f + z) * 0.5f - 2600.0f;
}

// ============================================================
// Chunk B: small helpers
// ============================================================

// ASM-spec v1.6.1 Fruit::CheckFruitDropped @0x001dbf70: reads .LANCHOR1+4/+8
// (@0x002842C0 `_ZL14outOfFruitTime`, .rodata const C.589 = {255,255,255,255}); both > 0
// so the body folds to GameOver(-1, -1.0f, 0); return true. Third arg is 0, not -1.
// Unreachable in v1.6.1: the only call site (GameUpdate @0x001cfa90) is gated on
// IsMultiplayer(), a hard 0. NOT related to miss counting -- that runs through
// Fruit::Update -> CheckHasGoneOffscreen() -> KillFruit(true).
bool Fruit::CheckFruitDropped() {
    GameOver(-1, -1.0f, 0);
    return true;
}

// ASM-spec v1.6.1 Fruit::NumberOfPowerupFruits @0x001db0ac
// Counts type-0 (fruit) entities whose FruitInfo has a power-up table
// (m_pPowers != nullptr). `this` is unused in the binary body -- CanSpawn calls
// it via a WaveManager* cast to Fruit* (thiscall convention artifact), but the
// function never dereferences the receiver.
int Fruit::NumberOfPowerupFruits() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    int count = 0;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    while (e) {
        Fruit* f = static_cast<Fruit*>(e);
        const ::FruitInfo* info = Fruit::FruitInfo((long)f->m_FruitType);
        if (info && info->m_pPowers != NULL) {
            ++count;
        }
        am = Mortar::ActorManager::GetInstance();
        e = am->GetEntityNext(0, it);
    }
    return count;
}

// v1.6.1 Fruit::IsOffscreen @0x001da83c — gravity-axis projection offscreen check.
// Bounds: 160.0f (Y-bound base), 240.0f (X-bound base), 50.0f (margin scale per scale.y).
// m_SecondPos is checked unconditionally (no m_bSliced gate).
// TODO: re-verify v1.6.1 DAT slots for the bound bases (old 0x001756d0/d4/d8 stale v1.5.x).
bool Fruit::IsOffscreen() const {
    const float scaleY = scale.y;
    if (fabsf(m_Gravity.y) > 0.0f) {
        const float bound = 160.0f + 50.0f * scaleY;
        if (pos.y < -bound || pos.y > bound) return true;
        return (m_SecondPos.y < -bound || m_SecondPos.y > bound);
    }
    if (fabsf(m_Gravity.x) > 0.0f) {
        const float bound = 240.0f + 50.0f * scaleY;
        if (pos.x < -bound || pos.x > bound) return true;
        return (m_SecondPos.x < -bound || m_SecondPos.x > bound);
    }
    return false;
}

// v1.6.1 Fruit::EnableCollision @0x001dc1d8
void Fruit::EnableCollision(bool enable) {
    if (enable) {
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        const float fScale   = info->m_Scale;
        const float fColBase = info->m_CollisionScale;
        const float radius   = fColBase + COL_RADIUS_FACTOR * fScale;
        if (!m_Col) m_Col = new ColSphere();
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center() = _Vector3<float>(pos.x, pos.y, 0.0f);
        cs->radius = radius;
    } else {
        delete m_Col;
        m_Col = nullptr;
    }
}

// ASM-spec v1.6.1 Fruit::SetForPlayer @0x001db778: gate is param==2 (P2P partition index), not param==1.
// Binary: IsOnlineMultiplayer(); if (online && param==2) colSphere->radius*=0.66; m_PlayerIdx=param;
// m_PlayerIdx in {0,1,2} is the P2P/EntityTracker partition; the radius shrink targeted partition 2.
void Fruit::SetForPlayer(int playerIdx) {
    // Defunct: online-mp — P2P partition 2 collision radius *= 0.66; v1.6.1 Fruit::SetForPlayer @0x001db778
    // Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer() is always false in port.
    if (Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer()) {
        if (playerIdx == 2 && m_Col) {
            static_cast<ColSphere*>(m_Col)->radius *= 0.66f;
        }
    }
    m_PlayerIdx = (uint32_t)playerIdx;
}

// v1.6.1 Fruit::Release @0x001dbfe4 — virtual Mortar::Entity::Release override.
// Called by Mortar::ActorManager teardown before the destructor.
void Fruit::Release() {
    RemoveTrailParticles();
    if (m_pOwner) {
        MenuButton* owner = reinterpret_cast<MenuButton*>(m_pOwner);
        if (owner->m_pTrackedFruit == this) {
            owner->m_pTrackedFruit = nullptr;
        }
        m_pOwner = nullptr;
    }
    Mortar::Entity::Release();
}

// ============================================================
// Chunk C: GetFact + SetTrailParticles
// ============================================================

// ASM-spec v1.6.1 Fruit::GetFact @0x001db7b4: fruitType < 0 draws a fresh
// Rand32(MAX_FRUIT_TYPES) each call (@0x001db7d4); the type is then clamped to
// [0, count-2] (top 2 special types excluded) and apple_red remaps to apple
// (cached FruitType statics behind __cxa_guard, string @0x00283513). The fact
// WITHIN the chosen fruit is a persisted round-robin: bump the "facts" global
// counter (@0x001db844) and the per-fruit "<name>_facts" counter, then
// factIdx = (newTotal - 1) % m_FactCount (__aeabi_idivmod). If the chosen
// fruit has no facts, re-roll via recursion GetFact(outType, outFactIdx, -1, -1).
// Both AddToTotal calls pass trackSession=true, achievementGate=true.
const char* Fruit::GetFact(int* outType, int* outFactIdx, int fruitType, int factIdx) {
    const int count = g_FruitInfoCount;
    if (count <= 0) return nullptr;

    int ft = fruitType;
    if (ft < 0) ft = (int)Math::g_Random.Rand32((uint32_t)count);
    if (ft < 1) ft = 0;
    else if (ft >= count - 2) ft = count - 2;

    // Binary caches these two lookups as function-local statics (__cxa_guard).
    static const int s_appleRedType = Fruit::FruitType("apple_red", false);
    static const int s_appleType    = Fruit::FruitType("apple", false);
    if (ft == s_appleRedType) ft = s_appleType;

    if (outType) *outType = ft;

    const FruitInfoData* chosen = FruitInfo_Get(ft);

    int fi = factIdx;
    if (fi < 0) {  // round-robin path
        if (chosen->m_FactCount < 1) {
            return GetFact(outType, outFactIdx, -1, -1);  // re-roll
        }
        if (game_work.m_SaveData) {
            static const uint32_t hFactsGlobal = StringHash("facts");
            game_work.m_SaveData->AddToTotal("facts", hFactsGlobal, 1, true, true);

            char buf[64];
            snprintf(buf, sizeof(buf), "%s_facts", chosen->m_Name);
            int newTotal = game_work.m_SaveData->AddToTotal(buf, StringHash(buf), 1, true, true);
            fi = (newTotal - 1) % chosen->m_FactCount;
        } else {
            fi = 0;  // Port specific: unit tests run without save data
        }
    } else {
        if (chosen->m_FactCount <= 0) return nullptr;
        fi = fi % chosen->m_FactCount;
    }

    if (outFactIdx) *outFactIdx = fi;

    // fruitlist.xml stores localisation keys (e.g. "FRUIT_FACT_07") in
    // <fact> elements; resolve via StringTable so the caller gets the
    // translated paragraph, not the raw key.
    const char* key = chosen->m_pFacts ? chosen->m_pFacts[fi] : nullptr;
    if (!key) return nullptr;
    return GETSTRING_CAST_0_STR(key);
}

// v1.6.1 Fruit::GetSliceDir @0x001bff08 — unit direction for a slice index, offset by the
// fruit's blade angle (m_SliceArcAngle, +0xc0). Returns (SinIdx(a), CosIdx(a), 0).
_Vector3<float> Fruit::GetSliceDir(uint16_t sliceIdx)
{
    uint16_t a = (uint16_t)(sliceIdx + m_SliceArcAngle);
    return _Vector3<float>(SinIdx(a), CosIdx(a), 0.0f);
}

// v1.6.1 Fruit::RemoveTrailParticles @0x001db2a8 — release both trail/juice emitters back to
// the particle manager, then null both slots. Matches the binary's structure: GetInstance()
// per live emitter, unconditional null of each slot afterward.
void Fruit::RemoveTrailParticles() {
    if (m_pEmitter1) { PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter1); }
    m_pEmitter1 = nullptr;
    if (m_pEmitter2) { PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter2); }
    m_pEmitter2 = nullptr;
}

// v1.6.1 Fruit::SetTrailParticles @0x001db2f4 — replace m_pEmitter1 with a custom trail emitter.
bool Fruit::SetTrailParticles(unsigned long emitterHash) {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();

    // Binary: EmitterExists check before replace. Port uses FindTemplate
    // as the equivalent gate (AddEmitter returns nullptr for unknown hashes).
    if (!pm.FindTemplate((uint32_t)emitterHash)) return false;

    if (m_pEmitter1) {
        pm.ClearEmitter(m_pEmitter1);
        m_pEmitter1 = nullptr;
    }
    // Note: ppRef=nullptr is safe (matches binary). All trail templates used here
    // (fruit_flight, scorex2_trail, blue_fruit_flight, dragon_trail, etc.) have
    // <life>0</life> with every set having stop="0" and perSec>0 -> naturally-infinite.
    // Reap (binary @ 0x00115ed8) never frees them while Fruit holds the pointer.
    // ASM-spec v1.6.1 Fruit::SetTrailParticles @0x001db33c: r3=1 (mov r3,#0x1).
    m_pEmitter1 = pm.AddEmitter((uint32_t)emitterHash, nullptr, /*updateWhenPaused=*/true);
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    return m_pEmitter1 != nullptr;
}

// ============================================================
// Chunk D: UpdateBombAvoidance + DestroyFruitModels
// ============================================================

// v1.6.1 Fruit::UpdateBombAvoidance @0x001db190 — push bombs away from this fruit on the X axis.
// Constants confirmed: 4900.0f, 56.25f, multiplier=12.0f; dist check is MagnitudeSqr(diff)<4900.
// Binary re-fetches ActorManager::GetInstance() each iteration. No null guard.
// TODO: re-verify v1.6.1 DAT slots for these constants (old 0x00175a5c/0x00175a60 stale v1.5.x).
void Fruit::UpdateBombAvoidance(float dt) {
    if (m_bSliced != 0) return;

    std::list<Mortar::Entity*>::iterator it;
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    while (e) {
        Bomb* bomb = static_cast<Bomb*>(e);
        if (bomb->IsActive() && bomb->m_Col != NULL) {
            _Vector3<float> diff = bomb->pos - pos;
            if (diff.MagnitudeSqr() < 4900.0f) {   // 4900.0 (70^2); v1.6.1 DAT TODO
                float dvx = vel.x - bomb->vel.x;
                float dvy = vel.y - bomb->vel.y;
                if (dvx * dvx + dvy * dvy < 56.25f) {   // 56.25 (7.5^2); v1.6.1 DAT TODO
                    float dir = (diff.x < 0.0f) ? -1.0f : 1.0f;
                    bomb->vel.x += dir * dt * 12.0f;
                }
            }
        }
        am = Mortar::ActorManager::GetInstance();
        e = am->GetEntityNext(1, it);
    }
}

// ASM-spec v1.6.1 Fruit::DestroyFruitModels @0x001df1c0 — releases the FruitModelInfo[] array.
// Binary teardown order: (1) pre-null SmartPtr+EffectProperty* per element if loaded,
// (2) element dtors + delete raw block, (3) pool destroy, (4) null slice models,
// (5) clear loaded flag.
void Fruit::DestroyFruitModels() {
    // Step 1: if loaded, null each element's SmartPtr<Model> and EffectProperty* fields
    // before the dtor loop runs (binary T_2039 SmartPtr::SetPtr(null) per slot @+0x18..+0x4c,
    // EffectProperty* zero writes interleaved).
    if (g_fruitData.s_fruitModelsLoaded) {
        // Count derived from allocation header (v1.6.1 Fruit::DestroyFruitModels @0x001df1c0).
        int count = *reinterpret_cast<int*>((char*)g_fruitData.s_fruitModels - 4);
        for (int j = 0; j < count; ++j) {
            g_fruitData.s_fruitModels[j].m_HalfA        = Mortar::SmartPtr<Mortar::Model>();
            g_fruitData.s_fruitModels[j].m_pHalfEffectA = 0;
            g_fruitData.s_fruitModels[j].m_HalfB        = Mortar::SmartPtr<Mortar::Model>();
            g_fruitData.s_fruitModels[j].m_pHalfEffectB = 0;
            g_fruitData.s_fruitModels[j].m_Whole        = Mortar::SmartPtr<Mortar::Model>();
            g_fruitData.s_fruitModels[j].m_pWholeEffect = 0;
            g_fruitData.s_fruitModels[j].m_pMpModel     = Mortar::SmartPtr<Mortar::Model>();
            g_fruitData.s_fruitModels[j].m_pMpEffect    = 0;
        }
    }
    // Step 2: element dtors + delete the raw block.
    // Raw base and count derived from allocation header (*(s_fruitModels-4)=count, s_fruitModels-8=raw).
    if (g_fruitData.s_fruitModels) {
        int count = *reinterpret_cast<int*>((char*)g_fruitData.s_fruitModels - 4);
        uint8_t* raw = (uint8_t*)g_fruitData.s_fruitModels - 8;
        for (int i = 0; i < count; ++i) {
            g_fruitData.s_fruitModels[i].~FruitModelInfo();
        }
        ::operator delete(raw);
        g_fruitData.s_fruitModels = nullptr;
    }
    // Step 3: pool destroy (binary: two T_2024 pool-destroy calls @+0x8c/+0x90, matching
    // MemoryPool::Destroy()'s delete[]+free() for m_Backing and m_FreeList -- accept-cosmetic).
    // Port: heap-allocated; also delete the object itself (binary stores inline at +0x68).
    // Destroy pool BEFORE the list so any Remove() calls during list dtor don't
    // attempt to push into a freed pool. List<SliceEffect> dtor calls Clear() which
    // gates on m_Active==1; since AddNodeToHead sets m_Active==2, Clear() just
    // zeroes pointers without touching the already-destroyed pool nodes.
    if (g_fruitData.s_pool) {
        g_fruitData.s_pool->Destroy();
        delete g_fruitData.s_pool;
        g_fruitData.s_pool = 0;
    }
    // Port: heap-allocated slice list; delete (binary stores inline at +0x00).
    if (g_fruitData.s_slices) {
        delete g_fruitData.s_slices;
        g_fruitData.s_slices = 0;
    }
    // Step 4: null the 4 slice models (binary T_2039 x4 @+0x58..+0x64).
    for (int i = 0; i < 4; ++i)
        g_fruitData.s_sliceModel[i] = Mortar::SmartPtr<Mortar::Model>();
    // Step 5: clear the loaded flag.
    g_fruitData.s_fruitModelsLoaded = false;
}


// ASM-spec v1.6.1 CleanupFruit @ 0x001defd4.
// Full fruit-subsystem teardown (shutdown path, distinct from Fruit::DestroyFruitModels
// which is the mid-game reload path). Steps match binary order exactly.
void CleanupFruit() {
    // Step 1: null 7 texture SmartPtrs in the binary's non-sequential order.
    g_fruitData.s_globalFruitAtlas[0].SetNull();
    g_fruitData.s_atlas2[0].SetNull();
    g_fruitData.s_globalFruitAtlas[1].SetNull();
    g_fruitData.s_atlas2[1].SetNull();
    g_fruitData.s_texSlots[1].SetNull();
    g_fruitData.s_texSlots[2].SetNull();
    g_fruitData.s_texSlots[0].SetNull();

    // Step 2: if models were loaded, null m_HalfA, m_HalfB, m_Whole for every fruit
    // type (NOT m_pMpModel; binary outer loop is field index 0=HalfA/1=HalfB/2=Whole,
    // inner is fruit type index). m_pMpModel is handled by ~FruitModelInfo in step 5.
    if (g_fruitData.s_fruitModelsLoaded) {
        int count = *reinterpret_cast<int*>((char*)g_fruitData.s_fruitModels - 4);
        for (int j = 0; j < count; j++) {
            g_fruitData.s_fruitModels[j].m_HalfA.SetNull();
        }
        for (int j = 0; j < count; j++) {
            g_fruitData.s_fruitModels[j].m_HalfB.SetNull();
        }
        for (int j = 0; j < count; j++) {
            g_fruitData.s_fruitModels[j].m_Whole.SetNull();
        }
    }

    // Step 3: destroy s_slices list (clear -> dtor -> delete -> null).
    if (g_fruitData.s_slices) {
        g_fruitData.s_slices->Clear();
    }
    if (g_fruitData.s_slices) {
        delete g_fruitData.s_slices;
        g_fruitData.s_slices = 0;
    }

    // Step 4: destroy s_pool (dtor -> delete -> null).
    if (g_fruitData.s_pool) {
        delete g_fruitData.s_pool;
        g_fruitData.s_pool = 0;
    }

    // Step 5: destroy s_fruitModels backing alloc (backward dtor walk + delete raw-8).
    // Raw header: [0]=elemSize, [4]=count, [8]=FruitModelInfo[count] (v1.6.1 @0x001defd4).
    if (g_fruitData.s_fruitModels) {
        int count = *reinterpret_cast<int*>((char*)g_fruitData.s_fruitModels - 4);
        FruitModelInfo* end = g_fruitData.s_fruitModels + count;
        while (end != g_fruitData.s_fruitModels) {
            --end;
            end->~FruitModelInfo();
        }
        ::operator delete((char*)g_fruitData.s_fruitModels - 8);
        g_fruitData.s_fruitModels = 0;
    }

    // Step 6: binary heap-frees Fruit::fruitInfo (count*0x338+8 alloc).
    // DIFFERS: binary heap-frees fruitInfo; port FruitInfo is a static array -> no free needed
    // (v1.6.1 CleanupFruit @0x001defd4 step6)

    // Step 7: mark unloaded.
    g_fruitData.s_fruitModelsLoaded = 0;
}

// ASM-spec v1.6.1 Fruit::AddShadow @0x001dbbe8.
// SSM Player 2 swaps shadow-offset axes; offset rotates 90 deg and the
// sign follows the fruit's screen-half (pos.x < 0 -> negative).
void Fruit::AddShadow(QUADCUSTOMVERTEX** out, int* outCount) {
    float mirrorX = 1.0f;
    float mirrorY = 0.0f;
    if (m_PlayerIdx >= 1 && IsSameScreenMultiplayer()) {
        mirrorX = 0.0f;
        mirrorY = (pos.x < 0.0f) ? 1.0f : -1.0f;
    }

    // Quad 1: spawn-fade whole-fruit shadow (active while m_MenuGrowFade < 1).
    // m_MenuGrowFade replaces m_ScaleAnim as the grow/scale-in animation float.
    if (m_MenuGrowFade < 1.0f) {
        int a = (int)((1.0f - m_MenuGrowFade) * 230.0f);  // DAT_00176164
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = 82.0f * scale.x;                        // DAT_00176168
        AddQuad(out, pos.x + mirrorY * hs * -0.65f, pos.y + mirrorX * hs * -0.65f, hs, hs, Colour(255, 255, 255, al));
        ++(*outCount);
    }

    // Quads 2+3: per-half shadows (active while m_MenuGrowFade > 0).
    if (m_MenuGrowFade > 0.0f) {
        int a = (int)(m_MenuGrowFade * 100.0f);            // DAT_00176170
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = scale.x * 50.0f;                      // DAT_00176174

        // Binary calls Quaternion::Matrix33Unit on each rot, then multiplies axis (0,0,1).
        // Port uses ToMatrix44() and extracts column 2 directly: (m[8], m[9], m[10]).
        {
            Matrix44 mat1 = m_Rot1.ToMatrix44();
            AddQuad(out,
                pos.x + mat1.m[8] * 0.5f + mirrorY * hs * -0.45f,
                pos.y + mat1.m[9] * 0.5f + mirrorX * hs * -0.45f,
                hs, hs, Colour(255, 255, 255, al));
            ++(*outCount);

            Matrix44 mat2 = m_Rot2.ToMatrix44();
            AddQuad(out,
                m_SecondPos.x + mat2.m[8] * 0.5f + mirrorY * hs * -0.45f,
                m_SecondPos.y + mat2.m[9] * 0.5f + mirrorX * hs * -0.45f,
                hs, hs, Colour(255, 255, 255, al));
            ++(*outCount);
        }
    }
}
