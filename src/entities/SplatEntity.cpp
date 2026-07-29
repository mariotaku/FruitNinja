//
// SplatEntity -- juice-splat pool, 1:1 binary port.
// Binary @ 0x0017ed58..0x001ece34. sizeof = 0x78 (120 bytes), no base class.
//
// Analysed: 2026-05-04T00:00
//

#include "SplatEntity.h"
#include "ActorManager.h"
#include "Fruit.h"
#include "FruitInfo.h"
#include "hud/HUD.h"
#include "math/Colour.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "audio/GameSound.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"

// ---------------------------------------------------------------------
// Binary constants (resolved from Ghidra; all addresses in ARM32 .text/.rodata)
// ---------------------------------------------------------------------

// ASM-verified: 2026-04-29T03:09Z v1.6.1 binary @ 0x001bd08c (asm-inspector)
// Per-splat-type slide-rate table. Indexed by m_SplatType (0..5).
static const float kSlideRate[6] = { 2.5f, 2.5f, 2.5f, 2.9f, 0.0f, 0.0f };

// ASM-verified: 2026-04-29T03:09Z v1.6.1 binary @ 0x001bd074 (asm-inspector)
// Per-type post-landing scale multiplier. Applied as: m_Scale *= kLandScale[type] * 2.5f.
static const float kLandScale[6] = { 1.6f, 1.6f, 1.6f, 1.6f, 2.9f, 2.9f };

// MakeSplat -- v1.6.1 SplatEntity::MakeSplat @0x001eb910
static const float MS_Z_ZERO          = 0.0f;     // DAT_0017f564 = 0x00000000
static const float MS_Z_BIAS          = 150.0f;   // DAT_0017f568 = 0x43160000
// ASM-verified: 2026-07-07T00:00Z v1.6.1 SplatEntity::MakeSplat @0x001eb910 (implementer)
static const float MS_COL_PHASE_DEF   = 1.5f;     // fruitType >= MAX_FRUIT_TYPES path
static const float MS_VEL_FINAL_MULT  = 6.0f;
static const float MS_VEL_Y_STRETCH   = 1.5f;
static const float MS_Z_RAND_RANGE    = 10.0f;

static const float MS_SCALE_BASE      = 10.0f;    // sc = Rand(10) + 10
static const float MS_SCALE_RAND      = 10.0f;

// Update -- v1.6.1 SplatEntity::Update @0x001ebee0
static const float UP_LAND_Z          = -50.0f;   // DAT_0017faa8 (landing threshold)

// Verified 2026-04-15 from instruction at 0x0017fa90:
//   vmov.f32 s13, 0xc1200000   ; -10.0f
//   vmla.f32 s15, s13, s14     ; vel.y += -10.0 * dt
static const float UP_GRAVITY         = -10.0f;

static const float UP_VEL_CLAMP_LO    = -50.0f;   // DAT_0017fd40
static const float UP_SCALE_CLAMP_LO  = -200.0f;  // DAT_0017fd48 (ASM-verified
                                                   // 2026-04-30: was incorrectly
                                                   // -50; binary value is -200)

static const float UP_LIFE_SLIDE_THR  = 1.25f;    // slide-phase threshold

// Life/decay randomisation constants (written on landing, binary @ 0x0017fa1c-fa36)
static const float UP_LIFE_BASE       = 3.75f;    // Rand(2.5) + 3.75
static const float UP_LIFE_RAND       = 2.5f;
static const float UP_DECAY_BASE      = 0.375f;   // Rand(0.25) + 0.375
static const float UP_DECAY_RAND      = 0.25f;

// PlaySplat size-bucket thresholds.
// ASM-spec v1.6.1 SplatEntity::Update @0x001ebee0: no-SFX bucket threshold is 100.0.
static const float SPLAT_SZ_LARGE_THR  = 100.0f;  // > 100 -> bucket 3 (no SFX / mute)
static const float SPLAT_SZ_MEDIUM_THR = 30.0f;   // > 30 -> bucket 2 (medium)
                                                   // else -> bucket 1 (small)

// Per-frame "spring rate" (binary GameTaskData+0x2c). Recomputed in
// UpdateActive each frame from the active entity counts; UpdateSplat
// reads it during the slide-decay phase. See UpdateActive() for the
// full derivation.
static float s_SpringRate = 1.25f;

// Binary: PlaySplat @ 0x0017f5ec -- per-size SFX cooldown gates.
// Three independent gates indexed by splat size (0..2). Each starts at 0,
// is set to 0.5 on fire, and ticks down by dt/frame in UpdateActive.
// While > 0, PlaySplat suppresses re-fire for that size.
static float s_SplatSfxGate[3] = { 0.0f, 0.0f, 0.0f };

// Binary: v1.6.1 SplatEntity::Update @0x001ebee0 + v1.6.1 SplatEntity::UpdateActiveSplats @0x001ec5d8
// Pulp-drip ambient SFX gate.
//   > 0   : armed, counting down to fire edge
//   0..-0.5: cooldown soak (no re-arm allowed)
//   < -0.5 : inert (re-arm allowed)
// BSS-zero-initialised.
static float s_PulpDripGate = 0.0f;

// Cached coconut fruit type index. Binary loads via __cxa_guard; resolved once
// via Fruit::FruitType("coconut", false).
// ASM-spec v1.6.1 SplatEntity::Update @0x001ebee0: SFX-suppress/size-0 fruit is "coconut" (DAT_0028077a), not "Moose".
static int s_CoconutFruitType = -2;  // -2 = uncached; use -2 so -1 (not found) can cache

// DrawActiveSplats @ 0x001ece34 -- UV atlas table @ 0x001bd014 (6 x 4 floats)
// Each entry is {u0, u1, v0, v1} -- verified from raw little-endian dump.
struct SplatAtlasEntry { float u0, u1, v0, v1; };
static const SplatAtlasEntry SPLAT_ATLAS[6] = {
    { 0.0f, 0.5f, 0.0f,  0.25f },  // type 0: upper-left small cell
    { 0.5f, 1.0f, 0.0f,  0.25f },  // type 1: upper-right small cell
    { 0.0f, 0.5f, 0.25f, 0.5f  },  // type 2: middle-left small cell
    { 0.5f, 1.0f, 0.25f, 0.5f  },  // type 3: middle-right small cell
    { 0.0f, 1.0f, 0.5f,  0.75f },  // type 4: full-width middle row
    { 0.0f, 1.0f, 0.75f, 1.0f  },  // type 5: full-width bottom row
};

// Base colour used as the "pre-fruit" tint during the colour lerp.
// Binary reads from GOT+0x7A44 (resolved 0x001F3B74), initialized in
// _GLOBAL__I_Fruit_cpp @ 0x0017a514 as a copy of the engine Blue constant
// from _GLOBAL__I_Colour_cpp @ 0x0018406a:
//   MakeColour_Engine(ptr, 0, 0, 0xff)  -> R=0, G=0, B=255, A=255
// Verified 2026-04-15.
static const uint8_t BASE_R = 0;
static const uint8_t BASE_G = 0;
static const uint8_t BASE_B = 255;
static const uint8_t BASE_A = 255;

// ---------------------------------------------------------------------
// Pool + shared content
// ---------------------------------------------------------------------

// Binary flat pool (v1.6.1 SplatEntity::CreatePool @0x001eb490 / GetFree @0x001eb318).
// Round-robin scan over m_bAlive; GetFree() never returns null once the pool
// exists -- it steals the cursor slot (overwriting a live splat) when full.
// Replaces the earlier Mortar::MemoryPool<SplatEntity> LIFO free-list, whose
// Pop() returned nullptr on exhaustion and caused MakeSplat call sites to
// silently drop splats under heavy slicing load.
static SplatEntity* s_PoolBase   = nullptr;
static int          s_PoolCount  = 0;
static int          s_CurrentFree = 0;

static Mortar::SmartPtr<Mortar::Texture>       s_SplatTex;

static const int MAX_SPLATS_PER_FRAME = 128;
static QUADCUSTOMVERTEX s_SplatVerts[MAX_SPLATS_PER_FRAME * 6];

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

// Every random draw in this TU comes from the shared seeded stream
// Math::g_Random -- NOT libc rand(). The binary's SplatEntity TU inlines
// Math::Random::Rand32 / Math::Random::RandF with `this` hard-wired to
// Math::g_random (.bss @0x00351db0, reached via GOT 0x002D8670): the
// file-local outlined copies are T.936 @0x001eb874 (Rand32) and
// T.937 @0x001eb8d8 (RandF = Rand32(0x7FFFF) / 524287.0f * range).
// Because the stream is shared game-wide, the NUMBER and ORDER of draws
// in MakeSplat/UpdateSplat is observable by every other consumer -- do not
// add, remove, or reorder draws.

static int ClampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Returns cached coconut fruit type, resolving on first call.
// ASM-spec v1.6.1 SplatEntity::Update @0x001ebee0: SFX-suppress/size-0 fruit is "coconut" (DAT_0028077a), not "Moose".
static int GetCoconutFruitType() {
    if (s_CoconutFruitType == -2) {
        s_CoconutFruitType = Fruit::FruitType("coconut", false);
    }
    return s_CoconutFruitType;
}

// ---------------------------------------------------------------------
// SplatEntity
// ---------------------------------------------------------------------

// Binary @ 0x0017ed58
SplatEntity::SplatEntity()
    : m_ColourPhase(0.0f)
    , m_ColB(255), m_ColG(255), m_ColR(255), m_ColA(255)
    , m_AlphaBase(0.0f)
    , m_Angle(0.0f)
    , m_FruitType(0)
    , m_bParam3(0)
    , m_bSpecial(0)
    , m_AxisA(0, 0, 0)
    , m_AxisB(0, 0, 0)
    , m_bFlipV(0)
    , m_Pos(0, 0, 0)
    , m_Scale(0, 0, 0)
    , m_ScaleSpawn(0, 0, 0)
    , m_Vel(0, 0, 0)
    , m_Life(0.0f)
    , m_DecayRate(0.0f)
    , m_SplatType(-1)
    , m_bSSMPHorizGravity(0)
    , m_bAlive(0)
{
    // SplatEntity is NOT an Mortar::Entity subclass (binary @ 0x0017ed58 ctor).
    // Pool managed by s_PoolBase/s_PoolCount; m_bAlive is the live/dead flag.
    // Binary ctor only sets vptr, default-inits Colour, m_SplatType=-1, m_bAlive=0.
    // The pad fields are left uninitialised (not zeroed by the binary).
    pad1A[0] = 0; pad1A[1] = 0;
    pad35[0] = 0; pad35[1] = 0; pad35[2] = 0;
    m_bMuteSfx = 0;
    pad77[0] = 0;
}

SplatEntity::~SplatEntity() {}

// --- Vtable slot 2: Init (binary @ 0x001eb264) ---
// Binary signature: (SplatEntity*, void*, long, _Vector3*). All args ignored.
// Binary body: `this[0x70]=-1; this[0x75]=1; bx lr`
void SplatEntity::Init(void* /*param1*/, long /*param2*/, _Vector3<float>* /*param3*/) {
    m_SplatType = -1;
    m_bAlive    = 1;
}

// --- Vtable slot 3: Release (binary @ 0x0017edd0) ---
// Binary body: `bx lr` (no-op)
void SplatEntity::Release() {
    // Defunct: no-op stub; v1.6.1 binary @ 0x0017edd0
}

// --- Vtable slot 6: Draw (binary @ 0x0017ee30) ---
// Binary body: `bx lr` (no-op -- splats are batched, never per-instance Draw())
void SplatEntity::Draw() {
    // Defunct: no-op stub; v1.6.1 binary @ 0x0017ee30
}

// --- Vtable slot 7: DrawUpdate (binary @ 0x0017ee2c) ---
// Binary body: `bx lr` (no-op)
void SplatEntity::DrawUpdate(float /*dt*/) {
    // Defunct: no-op stub; v1.6.1 binary @ 0x0017ee2c
}

// MakeSplat suppression denominators (v1.6.1 SplatEntity::MakeSplat @0x001eb910,
// block @0x001ebae0-0x001ebb38).
// FN_SPLAT_RAND_DENOM=4: main unconditional 25% suppression (Rand32(4)==0, @0x001ebae4).
// FN_SPLAT_SPRINKLE_RAND_DENOM=3: secondary kill for on-side fruit (Rand32(3)==0, @0x001ebb28).
#ifndef FN_SPLAT_RAND_DENOM
#  define FN_SPLAT_RAND_DENOM 4
#endif
#ifndef FN_SPLAT_SPRINKLE_RAND_DENOM
#  define FN_SPLAT_SPRINKLE_RAND_DENOM 3
#endif

// Test seam: s_RandKillEnabled defaults true (binary-faithful suppression rolls).
// Tests set false to force deterministic splat spawn. Production never modifies it;
// default behaviour is byte-identical. NOTE: disabling it also removes the two
// suppression DRAWS from Math::g_Random, so a test that sets it false diverges
// from the binary's RNG sequence for everything downstream.
bool SplatEntity::s_RandKillEnabled = true;

// v1.6.1 SplatEntity::MakeSplat @0x001eb910
// Initialises a free pool slot. Called from Fruit::Slice (0x00177028) and
// Fruit::Update juice-trail (0x0017e342).
//
// Bugfix #2 (@0x001ebae4): 25% of all splats are suppressed unconditionally
// (Rand32(4)==0).
//
// Bugfix #1: m_Life and m_DecayRate are NOT set here -- the binary sets them
// in Update's landing branch (binary @ 0x0017fa1c). Removed from MakeSplat.
//
// Bugfix #6: m_ScaleSpawn snapshot added after m_Scale is set.
//
// ASM-verified: 2026-07-15T00:00Z v1.6.1 SplatEntity::MakeSplat @ 0x001eb910 (asm-inspector)
// 5-param binary signature; always spawns airborne (m_SplatType=-1) with the
// real launch velocity. There is no immediate-landing branch in the binary --
// splats land via normal Update physics. mute maps to the binary's 4th bool arg.
//
// Draw order off Math::g_Random -- FIXED and observable game-wide (see the
// helper note at the top of this file). The binary's sequence is:
//   1. @0x001eb9a0  Rand32(2)    -> m_bFlipV
//   2. @0x001eb9f0  RandF(10)    -> m_Vel.z bias
//   3. @0x001eba28  Rand32(360)  -> m_Angle
//   4. @0x001eba64  RandF(10)    -> scale
//   5. @0x001ebae4  Rand32(4)    -> suppression roll 1
//   6. @0x001ebb28  Rand32(3)    -> suppression roll 2 (CONDITIONAL -- only
//                                   reached for an in-range on-side fruit)
// Draws 1-4 ALWAYS happen, before any suppression roll: minimum 5 draws per
// call, 6 on the on-side path. Suppression is a LATE m_bAlive clear, never an
// early return -- the slot is fully initialised (pos/vel/angle/scale/axes/
// m_SplatType/Init) even when the splat is killed.
void SplatEntity::MakeSplat(_Vector3<float> p, _Vector3<float> v, bool param3, bool mute, long fruitType) {
    m_bParam3 = param3 ? 1 : 0;

    // m_bMuteSfx = caller mute arg = (FruitInfo::m_bIsSuperFruit @+0x330 != 0).
    // Super-fruit splats land silent.
    m_bMuteSfx = mute ? 1 : 0;

    // Colour selection. Binary reads FruitTypeColour(fruitType) when in
    // range, else falls back to a default colour + ColourPhase = 0.75.
    // The range test is explicit here because Fruit::Slice passes the
    // critical-flash sentinel `m_FruitType + g_FruitInfoCount`, which is
    // exactly the out-of-range case this branch exists for -- FruitInfo_Get
    // (v1.6.1 Fruit::FruitInfo @0x001da5c0) does not bounds-check.
    const bool inRange = (fruitType >= 0 && fruitType < (long)g_FruitInfoCount);
    const FruitInfo* info = inRange ? FruitInfo_Get(fruitType) : nullptr;
    if (inRange) {
        m_ColourPhase = 0.0f;
        m_ColB = info->m_FruitColour[0];
        m_ColG = info->m_FruitColour[1];
        m_ColR = info->m_FruitColour[2];
        // ASM-verified: 2026-05-16 v1.6.1 binary @ 0x0016f342 — FruitTypeColour
        // helper fills all 4 bytes (B, G, R, A) from FruitInfo, including
        // the alpha. The next suppression block (binary @ 0x0016f45e:
        // `ldrb r3,[r4,#11]; cbz r3, exit`) then exits if m_ColA == 0 so
        // transparent fruits never spawn a splat. Earlier port hardcoded
        // m_ColA = 255, producing solid-black splats for any fruit with
        // alpha=0 in the XML (banana's `colour="0,0,0,0"` is the visible
        // case -- yellow juice particles still render via a separate code
        // path, but the persistent background splat-mark was rendering
        // opaque-black instead of being suppressed).
        m_ColA = info->m_FruitColour[3];
    } else {
        m_ColourPhase = MS_COL_PHASE_DEF;
        m_ColB = BASE_B;
        m_ColG = BASE_G;
        m_ColR = BASE_R;
        m_ColA = BASE_A;
    }

    // Draw 1 @0x001eb9a0.
    m_bFlipV    = (Math::g_Random.Rand32(2) != 0) ? 1 : 0;
    m_AlphaBase = (float)m_ColA;

    // Position -- Z forced to 0.
    m_Pos   = p;
    m_Pos.z = MS_Z_ZERO;

    // Velocity transform (binary @ 0x0017f3??):
    //   m_Vel   = vel
    //   speed   = |m_Vel|
    //   m_Vel.y *= 1.5
    //   m_Vel.z = speed * -0.5 - 150.0 - RandF(10)
    //   m_Vel  *= 6.0
    m_Vel = v;
    const float speed = m_Vel.Magnitude();
    m_Vel.y *= MS_VEL_Y_STRETCH;
    // Draw 2 @0x001eb9f0.
    m_Vel.z = speed * -0.5f - MS_Z_BIAS - Math::g_Random.RandF(MS_Z_RAND_RANGE);
    m_Vel   = m_Vel * MS_VEL_FINAL_MULT;

    // Draw 3 @0x001eba28. Angle: uniform [0, 360).
    m_Angle = (float)Math::g_Random.Rand32(360);

    // m_bSpecial source (@0x001eba4c, __aeabi_idivmod): the binary wraps the
    // raw fruitType into the table with a MODULO, so the out-of-range
    // critical-splat sentinel (fruitType + MAX_FRUIT_TYPES) still resolves to
    // its real fruit. Distinct from the suppression lookup below, which uses
    // the RAW type behind a range guard.
    {
        const int fruitCount = g_FruitInfoCount;
        const long wrapped = (fruitCount > 0) ? (fruitType % fruitCount) : 0;
        m_bSpecial = (fruitCount > 0) ? Fruit::FruitInfo(wrapped)->m_bSpecial : 0;
    }

    m_FruitType = fruitType;

    // Draw 4 @0x001eba64. Scale triple: sc random [10, 20), stored as (sc, -sc, sc).
    const float sc = MS_SCALE_BASE + Math::g_Random.RandF(MS_SCALE_RAND);
    m_Scale = _Vector3<float>(sc, -sc, sc);

    // Bugfix #6: snapshot of scale at spawn (binary @ 0x0017f428: stm r3,{r0,r1,r2}).
    m_ScaleSpawn = m_Scale;

    // Start airborne -- Update will pick m_SplatType on landing.
    m_SplatType = -1;
    // Binary @0x001ebab0 dispatches the slot-2 Init through the vtable
    // (`blx vptr[+0x8]`) rather than writing m_bAlive inline.
    Init(0, 0, 0);

    // Defunct: SSMP horizontal-gravity flag -- stubbed to 0; v1.6.1 binary @ 0x0017f438.
    // Binary: m_bSSMPHorizGravity = IsSameScreenMultiplayer() && game->field_0xc == 0
    m_bSSMPHorizGravity = 0;

    // Suppression block @0x001ebae0-0x001ebb38. Runs AFTER every field write;
    // "suppress" clears m_bAlive only -- there is no early return, execution
    // falls through to the axis build below. The Rand32(3) roll is
    // short-circuited (NOT drawn) when the fruit type is out of range or the
    // fruit is not on-side; that short-circuit is part of the draw count.
    {
        bool suppress;
        if (s_RandKillEnabled && Math::g_Random.Rand32(FN_SPLAT_RAND_DENOM) == 0) {
            suppress = true;                       // draw 5 @0x001ebae4
        } else if (m_ColA == 0) {
            suppress = true;                       // transparent fruit (e.g. banana)
        } else if (m_FruitType >= g_FruitInfoCount) {
            suppress = false;                      // out of range -> no draw 6
        } else if (Fruit::FruitInfo(m_FruitType)->m_bOnSide == 0) {
            suppress = false;                      // not on-side -> no draw 6
        } else if (!s_RandKillEnabled) {
            suppress = false;
        } else {
            suppress = (Math::g_Random.Rand32(FN_SPLAT_SPRINKLE_RAND_DENOM) == 0);
        }                                          // draw 6 @0x001ebb28
        if (suppress) m_bAlive = 0;
    }

    // Axis vectors @0x001ebb3c -- computed even for a suppressed splat.
    // Binary @ 0x0017f1cc: axisA = (cos, sin) * 0.5 and
    // axisB = perp * 0.5 (local_44 = 0x3f000000 = 0.5f).
    // ASM-verified: 2026-04-29T03:09Z v1.6.1 binary @ 0x0017f1cc (asm-inspector)
    const float angleRad  = m_Angle * (3.1415926f / 180.0f);
    const float axPerpRad = angleRad + 1.5707963f;  // +90 deg
    m_AxisA = _Vector3<float>(cosf(angleRad),  sinf(angleRad),  0.0f) * 0.5f;
    m_AxisB = _Vector3<float>(cosf(axPerpRad), sinf(axPerpRad), 0.0f) * 0.5f;

    // NOTE: m_Life and m_DecayRate are NOT initialised here.
    // Binary @ 0x0017fa1c sets them in Update's landing branch only.
    // Values are stale until landing -- safe because the decay consumer
    // only runs after m_SplatType >= 0 (landed).
}

// v1.6.1 SplatEntity::Update @0x001ebee0 (vtable slot 5)
// Two phases: airborne (m_SplatType < 0) does physics + z check;
// landed (m_SplatType >= 0) does slide + decay.
//
// Bugfix #4: pos integration now unconditional (binary @ 0x0017f78c-f7a4:
// add happens BEFORE the airborne/landed branch check).
//
// Bugfix #1: m_Life/m_DecayRate set on landing (binary @ 0x0017fa1c).
//
// Bugfix #3: PlaySplat bucket by m_Scale.x (v1.6.1 SplatEntity::Update @0x001ebee0).
//
// Bugfix #5: special-fruit splat-type override on landing (binary @ 0x0017f806-f82a).
void SplatEntity::UpdateSplat(float dt) {
    if (!m_bAlive) return;

    // Bugfix #4 -- binary @ 0x0017f78c-f7a4: pos integration is unconditional.
    // The binary integrates pos for ALL splats each frame, then branches.
    // For non-SSMP landed splats vel is (0,0,0) at landing so drift is zero;
    // the effect is only visible in SSMP horizontal-grav (m_bSSMPHorizGravity).
    m_Pos = m_Pos + m_Vel * dt;

    if (m_SplatType < 0) {
        // --- Airborne phase ---
        // ASM-verified: 2026-07-07T00:00Z v1.6.1 SplatEntity::Update @0x001ebee0 (implementer)
        // Binary reads the scaled per-frame dt (game_work.flM_Dt), NOT the
        // dt parameter passed into this function (`this->m_Vel.y = this->m_Vel.y
        // + game_work.flM_Dt * -10.0`). Only the m_bSSMPHorizGravity==0 arm is
        // reachable -- the SSMP branch is defunct (m_bSSMPHorizGravity is
        // always stubbed to 0 in MakeSplat).
        m_Vel.y += UP_GRAVITY * game_work.dt;

        // Binary clamps velocity ONLY in the slide-decay phase (below); the
        // airborne phase does not floor-clamp. (asm-inspector 2026-05-06)

        // Check landing threshold.
        if (m_Pos.z < UP_LAND_Z) {
            // Landing -- pick splat variant.
            //   Normal path:
            //     1/4 chance  -> type = (Rand(2)==0) ? 2 : 3 (large round)
            //     else (3/4)  -> type = (Rand(6)!=0) ? 0 : 1 (small round)
            //   param3 override: 1/2 chance swap to type 4 or 5
            int type;
            if (Math::g_Random.Rand32(4) == 0) {
                type = (Math::g_Random.Rand32(2) == 0) ? 2 : 3;
            } else {
                type = (Math::g_Random.Rand32(6) != 0) ? 0 : 1;
            }
            if (m_bParam3 && Math::g_Random.Rand32(2) == 0) {
                type = (Math::g_Random.Rand32(2) == 0) ? 4 : 5;
            }

            // Bugfix #5 (binary @ 0x0017f806-f82a): special-fruit (m_bOnSide /
            // field_0x2fc != 0) forces splat-type to 2 or 3 (the large-round pair).
            // In FruitInfo the field at +0x2fc is m_bOnSide.
            {
                // m_FruitType may still hold MakeSplat's out-of-range
                // critical-flash sentinel, so range-check before indexing --
                // FruitInfo_Get (v1.6.1 Fruit::FruitInfo @0x001da5c0) does not.
                if (m_FruitType >= 0 && m_FruitType < g_FruitInfoCount
                        && FruitInfo_Get(m_FruitType)->m_bOnSide != 0) {
                    type = (Math::g_Random.Rand32(2) == 0) ? 2 : 3;
                }
            }

            m_SplatType = type;

            // ASM-spec v1.6.1 SplatEntity::Update @0x001ebff4: directional streak
            // orientation for types 4/5 (bottom half of white_splash.tex). Angle
            // from the landing velocity: Atan2Idx(vel.y, vel.x), degrees =
            // (uint16)idx / -182.0 (NOTE the negation, vdiv @0x001ec02c), then
            // jittered by RandF(45)-22 (@0x001ec034..48). Axes rebuilt from the
            // angle via the deg->idx factor +182.0: axis A (cos,sin)*0.5
            // (0x3f000000 @0x001ec0a0), axis B at angle+90 scaled *0.25
            // (0x3e800000 @0x001ec118) -- the 0.25 half-height is what makes the
            // streak elongated (generic MakeSplat uses 0.5 for both axes).
            // Must run BEFORE m_Vel is zeroed below.
            if (m_SplatType - 4U < 2U) {
                const uint16_t velIdx = (uint16_t)Math::Atan2Idx(m_Vel.y, m_Vel.x);
                m_Angle = (float)velIdx / -182.0f;
                m_Angle += Math::g_Random.RandF(45.0f) - 22.0f;
                const uint16_t iA = (uint16_t)(int32_t)(m_Angle * 182.0f);
                const uint16_t iB = (uint16_t)(int32_t)((m_Angle + 90.0f) * 182.0f);
                m_AxisA = _Vector3<float>(CosIdx(iA), SinIdx(iA), 0.0f) * 0.5f;
                m_AxisB = _Vector3<float>(CosIdx(iB), SinIdx(iB), 0.0f) * 0.25f;
            }

            // Per-type size multiplier -- binary at landing branch:
            //   m_Scale *= (kLandScale[type] * 2.5)
            // Table @ 0x001bd074. See kLandScale[] above.
            const int idx = (type >= 0 && type < 6) ? type : 0;
            m_Scale = m_Scale * (kLandScale[idx] * 2.5f);

            // Stick to the background plane. Binary at 0x0017f968 copies a
            // static global vec3 (DAT_0017fad0) into m_Vel; the pattern
            // strongly indicates (0,0,0). Port matches.
            // Splats land at z = -50 (UP_LAND_Z). Farther from camera than fruits
            // (spawn z = (i+1)*32, positive) under ortho near=2000/far=-6000 (larger z
            // = nearer). Drawn after fruits but with depth-TEST ON / depth-WRITE OFF
            // (binary GameDraw @ 0x0016b888), so depth test rejects splats behind fruits.
            // Do NOT nudge z empirically -- keep -50 and the depth-test state.
            m_Pos.z = UP_LAND_Z;
            m_Vel   = _Vector3<float>(0.0f, 0.0f, 0.0f);

            // Bugfix #1 (binary @ 0x0017fa1c-fa36): m_Life and m_DecayRate
            // are initialised HERE (on landing), not in MakeSplat.
            m_Life      = UP_LIFE_BASE  + Math::g_Random.RandF(UP_LIFE_RAND);
            m_DecayRate = UP_DECAY_BASE + Math::g_Random.RandF(UP_DECAY_RAND);

            // Bugfix #3 (v1.6.1 SplatEntity::Update @0x001ebee0): PlaySplat size bucket is
            // determined by m_Scale.x (after the landing scale multiply above),
            // NOT by m_SplatType/2. Coconut fruit type suppresses SFX entirely.
            // ASM-spec v1.6.1 SplatEntity::Update @0x001ebee0: PlaySplat gated on m_bMuteSfx==0 (super-fruit splats land silent).
            if (m_bMuteSfx == 0) {
                int splatSize;
                if (m_FruitType == GetCoconutFruitType()) {
                    splatSize = 0;   // coconut: bucket 0 -- suppress / no SFX
                } else if (m_Scale.x > SPLAT_SZ_LARGE_THR) {
                    splatSize = 3;   // large (> 100): no SFX (PlaySplat clamps to [0,2])
                } else if (m_Scale.x > SPLAT_SZ_MEDIUM_THR) {
                    splatSize = 2;   // medium (> 30)
                } else {
                    splatSize = 1;   // small
                }
                PlaySplat(splatSize);
            }

            // Per-splat ambient pulp-drip arm: 1-in-10 chance, only when the
            // gate has soaked past -0.5 (i.e. the post-fire cooldown is over
            // and we're free to rearm). Binary uses `vcmpe gate, -0.5; it mi`
            // -> N flag set when gate + 0.5 < 0, i.e., gate < -0.5.
            // ASM-verified: 2026-05-06T17:00 v1.6.1 binary @ 0x0017fa56 (asm-inspector)
            // (Earlier port had `>= -0.5f` -- inverted comparator; rearmed
            //  while still in cooldown soak instead of after it.)
            if (Math::g_Random.Rand32(10) == 0 && s_PulpDripGate < -0.5f) {
                s_PulpDripGate = 0.25f;
            }
        }
        return;
    }

    // --- Landed phase ---
    // Slide / scale decay -- binary runs this only while m_Life <= 1.25
    // (the tail slide phase). Above that threshold the splat sits still.
    // Rate is per-splat-type from kSlideRate[] @ 0x001bd08c.
    if (m_Life <= UP_LIFE_SLIDE_THR) {
        const int slideIdx = (m_SplatType >= 0 && m_SplatType < 6) ? (int)m_SplatType : 0;
        // Binary @ 0x0017fb9e: dy = dt * s_SpringRate * kSlideRate[type].
        const float dy = dt * s_SpringRate * kSlideRate[slideIdx];
        float ny = m_Vel.y - dy;
        if (ny < UP_VEL_CLAMP_LO) ny = UP_VEL_CLAMP_LO;
        m_Vel.y = ny;
        float nsy = m_Scale.y - dy;
        if (nsy < UP_SCALE_CLAMP_LO) nsy = UP_SCALE_CLAMP_LO;
        m_Scale.y = nsy;
    }

    // Critical-flash colour lerp -- v1.6.1 SplatEntity::Update @0x001ebee0,
    // gated block @0x001ec448-0x001ec570. Runs ONLY while m_ColourPhase > 0,
    // and BEFORE the life-decay/m_ColA computation below (binary order:
    // colour -> life -> m_ColA), so the alpha calc reads the freshly
    // updated m_AlphaBase.
    // ASM-verified: 2026-07-08T00:00Z v1.6.1 SplatEntity::Update critical-flash @ 0x001ebee0 (asm-inspector)
    // Block @0x001ec448-0x001ec570: lerp CRITICAL_COLOUR->fresh FruitTypeColour,
    // w=clamp(1-2*m_ColourPhase,0,1); updates m_AlphaBase.
    if (m_ColourPhase > 0.0f) {
        // Binary reads the scaled per-frame dt (game_work.flM_Dt), matching
        // the airborne-gravity precedent above -- NOT the dt parameter.
        m_ColourPhase -= game_work.dt;
        if (m_ColourPhase < 0.0f) m_ColourPhase = 0.0f;

        float w = 1.0f - 2.0f * m_ColourPhase;
        if (w < 0.0f) w = 0.0f;
        else if (w > 1.0f) w = 1.0f;

        // m_FruitType may be an out-of-range placeholder (MakeSplat's
        // "no FruitInfo" fallback); wrap into the valid table range before
        // re-fetching the fruit's true colour (binary's module-by-count guard).
        const int fruitCount = g_FruitInfoCount;
        const long freshType = (fruitCount > 0) ? (m_FruitType % fruitCount) : 0;
        const Colour fresh = Fruit::FruitTypeColour(freshType);
        const Colour& crit = Fruit::CRITICAL_COLOUR;

        m_ColR = (uint8_t)ClampInt((int)(crit.r + ((int)fresh.r - (int)crit.r) * w), 0, 255);
        m_ColG = (uint8_t)ClampInt((int)(crit.g + ((int)fresh.g - (int)crit.g) * w), 0, 255);
        m_ColB = (uint8_t)ClampInt((int)(crit.b + ((int)fresh.b - (int)crit.b) * w), 0, 255);
        m_AlphaBase = (float)crit.a + ((int)fresh.a - (int)crit.a) * w;  // float, unclamped
    }

    // Life decay.
    // Binary @ 0x0017fcea: m_Life -= dt * s_SpringRate * m_DecayRate.
    m_Life -= dt * s_SpringRate * m_DecayRate;
    if (m_Life <= 0.0f) {
        m_Life   = 0.0f;
        m_bAlive = 0;
        return;
    }

    // Alpha = min(base, base * life). Binary clamp to uint8.
    const float rawAlpha = m_AlphaBase * m_Life;
    const float aF = rawAlpha < m_AlphaBase ? rawAlpha : m_AlphaBase;
    m_ColA = (uint8_t)Clampf(aF, 0.0f, 255.0f);
}

// ---------------------------------------------------------------------
// Binary: PlaySplat @ 0x0017f5ec
// Plays one of 6 splat-impact SFX. Caller passes a size index (0..2);
// PlaySplat clamps to [0,2] then picks one of two pair entries via
// Rand32(2). Strings (binary capitalisation, no extension):
//   size 0: "Pulp-drip-2",        "Pulp-drip-1"        (pair 0/1)
//   size 1: "Splatter-Small-2",   "Splatter-Small-1"
//   size 2: "Splatter-Medium-2",  "Splatter-Medium-1"
// Note pair order: Rand32(2)==0 selects suffix -2, ==1 selects -1.
// Per-size cooldown: gate ticks down by dt/frame in Update; when
// <= 0 here, fires + resets to 0.5. Three independent gates by size.
// ASM-verified: 2026-04-29 v1.6.1 binary @ 0x0017f5ec..0x0017f74b (asm-inspector)
// ---------------------------------------------------------------------
void PlaySplat(int splatSize) {
    int sz = splatSize;
    if (sz > 2) sz = 2;
    if (sz < 0) sz = 0;

    if (s_SplatSfxGate[sz] > 0.0f) return;

    static const char* kPairs[3][2] = {
        { "Pulp-drip-2",        "Pulp-drip-1"        },  // size 0
        { "Splatter-Small-2",   "Splatter-Small-1"   },  // size 1
        { "Splatter-Medium-2",  "Splatter-Medium-1"  },  // size 2
    };
    const char* name = kPairs[sz][Math::g_Random.Rand32(2)];

    game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);

    s_SplatSfxGate[sz] = 0.5f;
}

// ---------------------------------------------------------------------
// Pool / static ops
// ---------------------------------------------------------------------

// ASM-spec v1.6.1 SplatEntity::CreatePool @0x001eb490:
//   if (s_PoolBase) delete[] s_PoolBase;   // runs dtors on the old array
//   s_PoolBase = new SplatEntity[capacity];
//   s_PoolCount = capacity;
//   s_CurrentFree = 0;
void SplatEntity::CreatePool(int capacity) {
    if (s_PoolBase) delete[] s_PoolBase;
    s_PoolBase = new SplatEntity[capacity];
    s_PoolCount = capacity;
    s_CurrentFree = 0;
}

// bool flag matching BSS+0x24 in the SplatEntity global block (v1.6.1 CleanUpSplat @0x001ec88c).
static bool s_loadedSplat = false;

void SplatEntity::DestroyPool() {
    delete[] s_PoolBase;
    s_PoolBase = nullptr;
    s_PoolCount = 0;
    s_CurrentFree = 0;
    s_SplatTex.SetNull();
}

void SplatEntity::LoadContent() {
    if (!s_SplatTex.IsValid()) {
        s_SplatTex = Mortar::TextureManager::LoadLocalisedTexture("white_splash.tex");
    }
    s_loadedSplat = true;
}

// ASM-spec v1.6.1 SplatEntity::CleanUp @ 0x001eb404 (note: stale 0x0017eee0 in header = v1.5.x).
// Destroys the flat pool: dtors on all slots via delete[], frees the backing
// allocation, nulls s_PoolBase, zeroes s_PoolCount/s_CurrentFree.
// PORT BUG FIX: prior body incorrectly did s_SplatTex.SetNull() here;
// texture nulling belongs in CleanUpSplat() (the binary never touches
// textures inside SplatEntity::CleanUp).
void SplatEntity::CleanUp() {
    delete[] s_PoolBase;
    s_PoolBase = nullptr;
    s_PoolCount = 0;
    s_CurrentFree = 0;
}

// ASM-spec v1.6.1 CleanUpSplat @ 0x001ec88c (capital U — DISTINCT from CleanupSplat).
// 1. SplatEntity::CleanUp() -- destroys pool
// 2. s_loadedSplat = false
// 3. null s_SplatTex (BSS+0x28)
void CleanUpSplat() {
    SplatEntity::CleanUp();
    s_loadedSplat = false;
    s_SplatTex.SetNull();
    // TODO: v1.6.1 CleanUpSplat @0x001ec88c -- binary nulls a 2nd SmartPtr<Texture> at +0x2c;
    // SplatEntity::LoadContent not yet RE'd to identify it.
}

// Defunct: dead code in v1.6.1 -- in export table but never called; v1.6.1 CleanupSplat @0x001ed0ec
void CleanupSplat() {
}

// ASM-spec v1.6.1 SplatEntity::GetFree @0x001eb318:
// Round-robin scan for a dead slot starting at s_CurrentFree. Never returns
// null once CreatePool has run -- if every slot is alive (pool exhausted),
// the scan lands back on its starting cursor and that slot is stolen
// (overwritten) rather than returning nullptr.
SplatEntity* SplatEntity::GetFree() {
    int idx = s_CurrentFree;
    int tries = 0;
    for (;;) {
        if (!s_PoolBase[idx].m_bAlive) {
            s_CurrentFree = idx;
            return &s_PoolBase[idx];
        }
        if (tries >= s_PoolCount) break;
        idx = (idx + 1) % s_PoolCount;
        ++tries;
    }
    s_CurrentFree = idx;
    return &s_PoolBase[idx];
}

// Cache populated by UpdateActiveSplats; NumActiveSplats returns this.
// Also serves as the per-call vertex write cursor inside DrawSplat (GOT +0x72c4).
static int s_NumActiveSplats = 0;

// Set by DrawActiveSplats before each DrawSplat call.
// Points into HUD::scales[3..5] (world tint) for the duration of the batch pass.
static const float* s_CurrentTintRGB = 0;

// ASM-verified: 2026-05-06T17:00 v1.6.1 binary @ 0x0017ee34 (asm-inspector)
// Returns the cached counter; binary does NOT iterate the pool here.
// The cache is refreshed at the end of UpdateActiveSplats's pool loop.
int SplatEntity::NumActiveSplats() {
    return s_NumActiveSplats;
}

// ASM-verified: 2026-05-06T18:00 v1.6.1 SplatEntity::UpdateActiveSplats @0x001ec5d8 (asm-inspector)
// Body order:
//   (a) Per-impact splat-SFX gate ticks (3-iter loop).
//   (b) Pulp-drip-gate timer + positive->non-positive fire edge.
//   (c) Spring-rate compute -- consumes the PRIOR-frame cached count
//       (s_NumActiveSplats), introducing an intentional 1-frame lag
//       that's part of the original feel.
//   (d) Pool-loop -- updates each alive splat; new active count written
//       to s_NumActiveSplats at the very end.
void SplatEntity::UpdateActiveSplats(float dt) {
    // (a) Per-impact splat SFX gates -- three independent cooldowns by size.
    for (int i = 0; i < 3; ++i) {
        if (s_SplatSfxGate[i] > 0.0f) {
            s_SplatSfxGate[i] -= dt;
            if (s_SplatSfxGate[i] < 0.0f) s_SplatSfxGate[i] = 0.0f;
        }
    }

    // (b) Pulp-drip ambient SFX gate tick + fire on positive->non-positive edge.
    if (s_PulpDripGate <= 0.0f) {
        if (s_PulpDripGate >= -0.5f) s_PulpDripGate -= dt;
    } else {
        s_PulpDripGate -= dt;
        if (s_PulpDripGate <= 0.0f) {
            const char* name = (Math::g_Random.Rand32(2) == 0) ? "Pulp-drip-2" : "Pulp-drip-1";
            game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);
        }
    }

    // (c) Per-frame spring rate compute -- binary 0x0017fe46..0x0017feda.
    //   N_total  = Mortar::ActorManager::GetNumEntities()
    //   N_active = s_NumActiveSplats   // PRIOR-frame cached count
    //   raw      = (N_total + N_active) / 15.0 - 0.15
    //   if raw <= 0:   spring = 1.25
    //   elif raw >= 3: spring = 4.25
    //   else:          spring = raw + 1.25  (linear ramp 1.25..4.25)
    int totalEntities = Mortar::ActorManager::GetInstance()->GetNumEntities();
    {
        const float raw = (float)(totalEntities + s_NumActiveSplats) / 15.0f - 0.15f;
        if (raw <= 0.0f)      s_SpringRate = 1.25f;
        else if (raw >= 3.0f) s_SpringRate = 4.25f;
        else                  s_SpringRate = raw + 1.25f;
    }

    // (d) Pool loop -- update each alive splat; the flat pool has no
    //     separate free list, so a dead slot just sits with m_bAlive=0
    //     until GetFree's round-robin scan reuses it.
    //     Write the new active count to s_NumActiveSplats LAST.
    int activeCount = 0;
    for (int i = 0; i < s_PoolCount; ++i) {
        SplatEntity* s = &s_PoolBase[i];
        if (!s->m_bAlive) continue;

        s->UpdateSplat(dt);

        if (s->m_bAlive) {
            ++activeCount;
        }
    }
    s_NumActiveSplats = activeCount;
}

void SplatEntity::RemoveAllSplats() {
    // Binary @0x0017eea4: iterates ALL pool slots by raw stride 0x78,
    // calls D1 dtor (no-op) on each. No m_bAlive check needed -- the flat
    // pool has no separate free list to reset.
    for (int i = 0; i < s_PoolCount; ++i) {
        s_PoolBase[i].m_bAlive = 0;
    }
}

void SplatEntity::ForEachInPool(PoolVisitor fn, void* user) {
    if (!fn) return;
    for (int i = 0; i < s_PoolCount; ++i) {
        fn(&s_PoolBase[i], user);
    }
}

// ---------------------------------------------------------------------
// SplatEntity::DrawSplat (0x001eb5d8) -- virtual per-instance render
// Vtable slot 4.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017f008 (re-analyst) [addr updated: 0x001eb5d8]
// ---------------------------------------------------------------------
// Writes 6 QUADCUSTOMVERTEX entries into s_SplatVerts at the cursor
// position given by s_NumActiveSplats. Tint read from s_CurrentTintRGB.
// Called indirectly via vtable from DrawActiveSplats (pure thiscall).
// Binary writes vertex z = 0; the z=-5500 depth placement comes from the
// modelview Translate in DrawActiveSplats (v1.6.1 DrawActiveSplats @0x001ece34).
void SplatEntity::DrawSplat() {
    QUADCUSTOMVERTEX* outVerts = &s_SplatVerts[s_NumActiveSplats * 6];
    const float* tintRGB = s_CurrentTintRGB;
    // Quad corner construction -- matches binary DrawSplat (0x001eb5d8).
    // The four corners are built from the sum/diff of the two axis
    // vectors, scaled by m_Scale.x for X and m_Scale.y for Y. Note
    // that m_Scale.y is the negative `-sc` from the spawn triple,
    // which the binary keeps negative on purpose (it's the value the
    // slide-decay phase mutates).
    //
    //   v0 (TL): pos + (+ax + bx) * scX, (+ay + by) * scY
    //   v1 (TR): pos + (-ax + bx) * scX, (-ay + by) * scY
    //   v2 (BL): pos + (+ax - bx) * scX, (+ay - by) * scY
    //   v5 (BR): pos + (-ax - bx) * scX, (-ay - by) * scY
    const float scX = m_Scale.x;
    const float scY = m_Scale.y;
    const float ax = m_AxisA.x, ay = m_AxisA.y;
    const float bx = m_AxisB.x, by = m_AxisB.y;

    // Colour pre-computed in UpdateSplat each frame.
    // Apply per-channel tint from &pHUD->scales[3] -- world tint window.
    // ASM-verified: 2026-04-29T03:29Z v1.6.1 binary @ 0x0017f1ec (asm-inspector)
    Colour splatColour(m_ColR, m_ColG, m_ColB, m_ColA);
    Colour tinted = Colour::TintColour(splatColour, tintRGB);
    const uint32_t col =
        ((uint32_t)tinted.a << 24) |
        ((uint32_t)tinted.b << 16) |
        ((uint32_t)tinted.g <<  8) |
        ((uint32_t)tinted.r);

    // Atlas UV -- verified via SplatEntity::DrawSplat @ 0x001eb5d8.
    // The binary halves the table-stored U coords (* 0.5) before
    // applying them, then optionally shifts both by +0.5 when
    // m_bSpecial (binary field_0x19) is set -- selecting the right
    // half of the texture.
    //
    //   u0 = table.u_start * 0.5
    //   u1 = table.u_end   * 0.5
    //   if (m_bSpecial)  { u0 += 0.5; u1 += 0.5; }
    //   if (m_bFlipV)    swap(v0, v1);
    const int type = ClampInt(m_SplatType, 0, 5);
    const SplatAtlasEntry& e = SPLAT_ATLAS[type];
    float u0 = e.u0 * 0.5f;
    float u1 = e.u1 * 0.5f;
    if (m_bSpecial) { u0 += 0.5f; u1 += 0.5f; }
    float v0 = e.v0, v1 = e.v1;
    if (m_bFlipV) { float t = v0; v0 = v1; v1 = t; }

    QUADCUSTOMVERTEX* v = outVerts;
    const float px = m_Pos.x, py = m_Pos.y, pz = 0.0f;

    // Vertex layout matches binary DrawSplat (vertices 0..5 written in
    // order, then v3=v2 and v4=v1 via memcpy):
    //   v[0] = TL
    //   v[1] = TR
    //   v[2] = BL
    //   v[3] = v[2]
    //   v[4] = v[1]
    //   v[5] = BR
    v[0].x = px + ( ax + bx) * scX;
    v[0].y = py + ( ay + by) * scY;
    v[0].z = pz;
    v[0].u = u0; v[0].v = v0;

    v[1].x = px + (-ax + bx) * scX;
    v[1].y = py + (-ay + by) * scY;
    v[1].z = pz;
    v[1].u = u1; v[1].v = v0;

    v[2].x = px + ( ax - bx) * scX;
    v[2].y = py + ( ay - by) * scY;
    v[2].z = pz;
    v[2].u = u0; v[2].v = v1;

    v[3] = v[2];
    v[4] = v[1];

    v[5].x = px + (-ax - bx) * scX;
    v[5].y = py + (-ay - by) * scY;
    v[5].z = pz;
    v[5].u = u1; v[5].v = v1;

    for (int k = 0; k < 6; ++k) {
        v[k].nx = 0.0f;
        v[k].ny = 0.0f;
        v[k].nz = 1.0f;
        v[k].colour = col;
    }
}

// ---------------------------------------------------------------------
// Batched draw -- matches DrawActiveSplats (0x001ece34)
// ---------------------------------------------------------------------
//
// Zeros s_NumActiveSplats at entry, sets s_CurrentTintRGB from
// HUD::scales[3..5], then for each alive landed splat calls DrawSplat()
// (pure thiscall, vtable slot 4) and increments s_NumActiveSplats.
// Submits the completed batch.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00180344 (re-analyst) [addr updated: 0x001ece34]
// Depth state owned by GameDraw (binary @ 0x0016b888): no per-call
// glEnable/glDisable(GL_DEPTH_TEST) or glDepthMask in the binary's body.
//
// Binary call order (v1.6.1 DrawActiveSplats @0x001ece34, tail block):
//   1. splatTexture->Set()                        -- vtable +0xc, BEFORE matrix ops
//   2. world.Reset()
//   3. world.Translate(Vec3(0,0,-5500))            -- DAT 0xc5abe000 = -5500.0f
//   4. UploadModelViewOnly()
//   5. Mesh::DrawTriList(m_points, count*6, ...)
//   6. splatTexture->UnSet(true)                  -- vtable +0x10, flag=1
void SplatEntity::DrawActiveSplats() {
    if (!s_SplatTex.IsValid()) return;

    s_NumActiveSplats = 0;

    s_CurrentTintRGB = Colour::IdentityTint();
    if (game_work.mHud) {
        s_CurrentTintRGB = &game_work.mHud->scales[3];
    }

    for (int i = 0; i < s_PoolCount && s_NumActiveSplats < MAX_SPLATS_PER_FRAME; ++i) {
        SplatEntity* s = &s_PoolBase[i];
        if (!s->m_bAlive)           continue;
        if (s->m_SplatType < 0)     continue;  // still airborne

        s->DrawSplat();

        ++s_NumActiveSplats;
    }

    if (s_NumActiveSplats == 0) return;

    // Binary call order: Set() first, then matrix block, then draw, then UnSet(true).
    s_SplatTex->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().Translate(_Vector3<float>(0.0f, 0.0f, -5500.0f)); // binary: UnitZ * DAT(0xc5abe000); splats live at modelview z=-5500
    mm.UploadModelViewOnly();

    // Depth state is owned by GameDraw at the pass level (binary @ 0x0016b888):
    // SetDepthBuffer(1) + SetDepthBufferWrite(0) is set BEFORE the splat pass and
    // stays in effect. SplatEntity::Draw must NOT mutate it.
    Mortar::Mesh::DrawTriList(s_SplatVerts, s_NumActiveSplats * 6, false, NULL);

    s_SplatTex->UnSet(true);
}
