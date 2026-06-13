//
// SplatEntity -- juice-splat pool, 1:1 binary port.
// Binary @ 0x0017ed58..0x00180344. sizeof = 0x78 (120 bytes), no base class.
//
// Analysed: 2026-05-04T00:00
//

#include "SplatEntity.h"
#include "ActorManager.h"
#include "Fruit.h"
#include "FruitInfo.h"
#include "Game.h"
#include "hud/HUD.h"
#include "math/Colour.h"
#include "audio/GameSound.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"

// ---------------------------------------------------------------------
// Binary constants (resolved from Ghidra; all addresses in ARM32 .text/.rodata)
// ---------------------------------------------------------------------

// ASM-verified: 2026-04-29T03:09Z binary @ 0x001bd08c (asm-inspector)
// Per-splat-type slide-rate table. Indexed by m_SplatType (0..5).
static const float kSlideRate[6] = { 2.5f, 2.5f, 2.5f, 2.9f, 0.0f, 0.0f };

// ASM-verified: 2026-04-29T03:09Z binary @ 0x001bd074 (asm-inspector)
// Per-type post-landing scale multiplier. Applied as: m_Scale *= kLandScale[type] * 2.5f.
static const float kLandScale[6] = { 1.6f, 1.6f, 1.6f, 1.6f, 2.9f, 2.9f };

// MakeSplat @ 0x0017f2f0
static const float MS_Z_ZERO          = 0.0f;     // DAT_0017f564 = 0x00000000
static const float MS_Z_BIAS          = 150.0f;   // DAT_0017f568 = 0x43160000
static const float MS_COL_PHASE_DEF   = 0.75f;    //           0x3fc00000
static const float MS_VEL_FINAL_MULT  = 6.0f;
static const float MS_VEL_Y_STRETCH   = 1.5f;
static const float MS_Z_RAND_RANGE    = 10.0f;

static const float MS_SCALE_BASE      = 10.0f;    // sc = Rand(10) + 10
static const float MS_SCALE_RAND      = 10.0f;

// Update @ 0x0017f774
static const float UP_LAND_Z          = -50.0f;   // DAT_0017faa8 (landing threshold)

// ASM-verified: binary @ 0x001803c0 DrawActiveSplats draws the splat batch
// through a world matrix translated to z = DAT_00180404 = -5500, far behind
// all fruits (fruit mesh z = -500..-2499). The landed m_Pos.z (-50) is the
// entity's logical land plane; the DRAW plane is -5500. Without this, splats
// at -50 are nearer than fruits and win GL_LESS, painting over them.
static const float SPLAT_DRAW_Z       = -5500.0f; // DAT_00180404

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

// PlaySplat size-bucket thresholds (binary @ 0x0017f9ce-f9f8)
static const float SPLAT_SZ_LARGE_THR  = 50.0f;   // > 50 -> bucket 3 (no SFX / mute)
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

// Binary: SplatEntity::Update @ 0x0017fa56 + UpdateActiveSplats @ 0x0017fd68
// Pulp-drip ambient SFX gate.
//   > 0   : armed, counting down to fire edge
//   0..-0.5: cooldown soak (no re-arm allowed)
//   < -0.5 : inert (re-arm allowed)
// BSS-zero-initialised.
static float s_PulpDripGate = 0.0f;

// Cached Moose fruit type index. Binary loads via __cxa_guard at 0x0017fadc.
// -1 until first resolved; set once via Fruit::FruitType("Moose", false).
static int s_MooseFruitType = -2;  // -2 = uncached; use -2 so -1 (not found) can cache

// DrawActiveSplats @ 0x00180344 -- UV atlas table @ 0x001bd014 (6 x 4 floats)
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

static Mortar::MemoryPool<SplatEntity> s_Pool;
static Mortar::SmartPtr<Mortar::Texture>       s_SplatTex;

static const int MAX_SPLATS_PER_FRAME = 128;
static QUADCUSTOMVERTEX s_SplatVerts[MAX_SPLATS_PER_FRAME * 6];

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

// Uniform [0, r)
static float RandRange(float r) {
    return ((float)rand() / (float)RAND_MAX) * r;
}

// Uniform integer [0, n)
static int RandInt(int n) {
    if (n <= 0) return 0;
    return rand() % n;
}

static int ClampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Returns cached moose fruit type, resolving on first call.
// Binary @ 0x0017fadc: loaded via __cxa_guard from Fruit::FruitType("Moose", false).
static int GetMooseFruitType() {
    if (s_MooseFruitType == -2) {
        s_MooseFruitType = Fruit::FruitType("Moose", false);
    }
    return s_MooseFruitType;
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
    // Pool managed by s_Pool; m_bAlive is the live/dead flag.
    // Binary ctor only sets vptr, default-inits Colour, m_SplatType=-1, m_bAlive=0.
    // The pad fields are left uninitialised (not zeroed by the binary).
    pad1A[0] = 0; pad1A[1] = 0;
    pad35[0] = 0; pad35[1] = 0; pad35[2] = 0;
    pad76[0] = 0; pad76[1] = 0;
}

SplatEntity::~SplatEntity() {}

// --- Vtable slot 2: Init (binary @ 0x0017edc0) ---
// Resets m_SplatType and activates the slot.
// Binary body: `this[0x70]=-1; this[0x75]=1; bx lr`
void SplatEntity::Init() {
    m_SplatType = -1;
    m_bAlive    = 1;
}

// --- Vtable slot 3: Release (binary @ 0x0017edd0) ---
// Binary body: `bx lr` (no-op)
void SplatEntity::Release() {
    // Defunct: no-op stub; binary @ 0x0017edd0
}

// --- Vtable slot 6: Draw (binary @ 0x0017ee30) ---
// Binary body: `bx lr` (no-op -- splats are batched, never per-instance Draw())
void SplatEntity::Draw() {
    // Defunct: no-op stub; binary @ 0x0017ee30
}

// --- Vtable slot 7: DrawUpdate (binary @ 0x0017ee2c) ---
// Binary body: `bx lr` (no-op)
void SplatEntity::DrawUpdate(float /*dt*/) {
    // Defunct: no-op stub; binary @ 0x0017ee2c
}

// Binary @ 0x0017f2f0 -- MakeSplat
// Initialises a free pool slot. Called from Fruit::Slice (0x00177028) and
// Fruit::Update juice-trail (0x0017e342).
//
// Bugfix #2 (binary @ 0x0017f456-f482): 25% of all splats are suppressed
// unconditionally (Rand(4)==0). Port previously spawned 100%.
//
// Bugfix #1: m_Life and m_DecayRate are NOT set here -- the binary sets them
// in Update's landing branch (binary @ 0x0017fa1c). Removed from MakeSplat.
//
// Bugfix #6: m_ScaleSpawn snapshot added after m_Scale is set.
//
// landImmediately (5th param, binary byte arg 2): if true, skip the airborne
// phase and land the splat instantly. Binary 6-arg ExplodeSuperFruit path
// passes (0, 1) for (param3, landImmediately).
void SplatEntity::MakeSplat(Vec3 p, Vec3 v, bool param3, int fruitType, bool landImmediately) {
    // Bugfix #2 -- binary @ 0x0017f456-f482: 25% spawn-suppression.
    // Also suppresses when m_ColA would be 0 (transparent fruit, rare) and
    // when special-fruit + Rand(3)==0. The dominant effect is the 25% kill.
    if (RandInt(4) == 0) return;

    m_bParam3 = param3 ? 1 : 0;

    // Colour selection. Binary reads FruitTypeColour(fruitType) when in
    // range, else falls back to a default colour + ColourPhase = 0.75.
    const FruitInfo* info = FruitInfo_Get(fruitType);
    if (info) {
        m_ColourPhase = 0.0f;
        m_ColB = info->m_FruitColour[0];
        m_ColG = info->m_FruitColour[1];
        m_ColR = info->m_FruitColour[2];
        // ASM-verified: 2026-05-16 binary @ 0x0016f342 — FruitTypeColour
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

    // Suppression block (binary @ 0x0016f456-f482 tail):
    //   if (m_ColA == 0) suppress      -- transparent fruit (e.g. banana)
    //   if (info->m_bSpecial && Rand(3) == 0) suppress
    if (m_ColA == 0) {
        m_bAlive = 0;
        return;
    }
    if (info && info->m_bSpecial && RandInt(3) == 0) {
        m_bAlive = 0;
        return;
    }

    m_bFlipV    = (RandInt(2) != 0) ? 1 : 0;
    m_AlphaBase = (float)m_ColA;

    // Position -- Z forced to 0.
    m_Pos   = p;
    m_Pos.z = MS_Z_ZERO;

    // Velocity transform (binary @ 0x0017f3??):
    //   m_Vel   = vel
    //   speed   = |m_Vel|
    //   m_Vel.y *= 1.5
    //   m_Vel.z = speed * -0.5 - 150.0 - rand(10)
    //   m_Vel  *= 6.0
    m_Vel = v;
    const float speed = m_Vel.Magnitude();
    m_Vel.y *= MS_VEL_Y_STRETCH;
    m_Vel.z = speed * -0.5f - MS_Z_BIAS - RandRange(MS_Z_RAND_RANGE);
    m_Vel   = m_Vel * MS_VEL_FINAL_MULT;

    // Angle: uniform [0, 360).
    m_Angle = (float)RandInt(360);

    // bSpecial from FruitInfo.
    m_bSpecial = info ? info->m_bSpecial : 0;

    m_FruitType = fruitType;

    // Scale triple: sc random [10, 20), stored as (sc, -sc, sc).
    const float sc = MS_SCALE_BASE + RandRange(MS_SCALE_RAND);
    m_Scale = Vec3(sc, -sc, sc);

    // Bugfix #6: snapshot of scale at spawn (binary @ 0x0017f428: stm r3,{r0,r1,r2}).
    m_ScaleSpawn = m_Scale;

    // Axis vectors. Binary @ 0x0017f1cc: axisA = (cos, sin) * 0.5 and
    // axisB = perp * 0.5 (local_44 = 0x3f000000 = 0.5f).
    // ASM-verified: 2026-04-29T03:09Z binary @ 0x0017f1cc (asm-inspector)
    const float angleRad  = m_Angle * (3.1415926f / 180.0f);
    const float axPerpRad = angleRad + 1.5707963f;  // +90 deg
    m_AxisA = Vec3(cosf(angleRad),  sinf(angleRad),  0.0f) * 0.5f;
    m_AxisB = Vec3(cosf(axPerpRad), sinf(axPerpRad), 0.0f) * 0.5f;

    // Defunct: SSMP horizontal-gravity flag -- stubbed to 0; binary @ 0x0017f438.
    // Binary: m_bSSMPHorizGravity = IsSameScreenMultiplayer() && game->field_0xc == 0
    m_bSSMPHorizGravity = 0;

    // Start airborne -- Update will pick m_SplatType on landing.
    m_SplatType = -1;
    m_bAlive    = 1;

    // NOTE: m_Life and m_DecayRate are NOT initialised here.
    // Binary @ 0x0017fa1c sets them in Update's landing branch only.
    // Values are stale until landing -- safe because the decay consumer
    // only runs after m_SplatType >= 0 (landed).

    if (landImmediately) {
        // Binary 6-arg ExplodeSuperFruit path: flag2=1 forces immediate landing.
        // Reuse the same landing logic from UpdateSplat without a physics tick.
        int type;
        if (RandInt(4) == 0) {
            type = (RandInt(2) == 0) ? 2 : 3;
        } else {
            type = (RandInt(6) != 0) ? 0 : 1;
        }
        if (m_bParam3 && RandInt(2) == 0) {
            type = (RandInt(2) == 0) ? 4 : 5;
        }
        {
            const FruitInfo* linfo = FruitInfo_Get(m_FruitType);
            if (linfo && linfo->m_bOnSide != 0) {
                type = (RandInt(2) == 0) ? 2 : 3;
            }
        }
        m_SplatType = type;
        const int idx = (type >= 0 && type < 6) ? type : 0;
        m_Scale = m_Scale * (kLandScale[idx] * 2.5f);
        m_Pos.z = UP_LAND_Z;
        m_Vel   = Vec3(0.0f, 0.0f, 0.0f);
        m_Life      = UP_LIFE_BASE  + RandRange(UP_LIFE_RAND);
        m_DecayRate = UP_DECAY_BASE + RandRange(UP_DECAY_RAND);
    }
}

// Binary @ 0x0017f774 -- SplatEntity::Update (vtable slot 5)
// Two phases: airborne (m_SplatType < 0) does physics + z check;
// landed (m_SplatType >= 0) does slide + decay.
//
// Bugfix #4: pos integration now unconditional (binary @ 0x0017f78c-f7a4:
// add happens BEFORE the airborne/landed branch check).
//
// Bugfix #1: m_Life/m_DecayRate set on landing (binary @ 0x0017fa1c).
//
// Bugfix #3: PlaySplat bucket by m_Scale.x (binary @ 0x0017f9ce-f9f8).
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
        m_Vel.y += UP_GRAVITY * dt;

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
            if (RandInt(4) == 0) {
                type = (RandInt(2) == 0) ? 2 : 3;
            } else {
                type = (RandInt(6) != 0) ? 0 : 1;
            }
            if (m_bParam3 && RandInt(2) == 0) {
                type = (RandInt(2) == 0) ? 4 : 5;
            }

            // Bugfix #5 (binary @ 0x0017f806-f82a): special-fruit (m_bOnSide /
            // field_0x2fc != 0) forces splat-type to 2 or 3 (the large-round pair).
            // In FruitInfo the field at +0x2fc is m_bOnSide.
            {
                const FruitInfo* info = FruitInfo_Get(m_FruitType);
                if (info && info->m_bOnSide != 0) {
                    type = (RandInt(2) == 0) ? 2 : 3;
                }
            }

            m_SplatType = type;

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
            m_Vel   = Vec3(0.0f, 0.0f, 0.0f);

            // Bugfix #1 (binary @ 0x0017fa1c-fa36): m_Life and m_DecayRate
            // are initialised HERE (on landing), not in MakeSplat.
            m_Life      = UP_LIFE_BASE  + RandRange(UP_LIFE_RAND);
            m_DecayRate = UP_DECAY_BASE + RandRange(UP_DECAY_RAND);

            // Bugfix #3 (binary @ 0x0017f9ce-f9f8): PlaySplat size bucket is
            // determined by m_Scale.x (after the landing scale multiply above),
            // NOT by m_SplatType/2. Moose fruit type suppresses SFX entirely.
            {
                int splatSize;
                if (m_FruitType == GetMooseFruitType()) {
                    splatSize = 0;   // Moose: bucket 0 -- suppress / no SFX
                } else if (m_Scale.x > SPLAT_SZ_LARGE_THR) {
                    splatSize = 3;   // large (> 50): no SFX (PlaySplat clamps to [0,2])
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
            // ASM-verified: 2026-05-06T17:00 binary @ 0x0017fa56 (asm-inspector)
            // (Earlier port had `>= -0.5f` -- inverted comparator; rearmed
            //  while still in cooldown soak instead of after it.)
            if (RandInt(10) == 0 && s_PulpDripGate < -0.5f) {
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

    // Colour lerp -- binary @ 0x0017f774 ticks m_ColourPhase down each
    // Update and writes the resulting RGBA back into m_ColR/G/B/A.
    // DrawSplat reads the stored fields directly (no re-computation).
    if (m_ColourPhase > 0.0f) {
        m_ColourPhase -= dt;
        if (m_ColourPhase < 0.0f) m_ColourPhase = 0.0f;
    }
    {
        float fade = m_ColourPhase * 2.0f;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        const int rMix = (int)m_ColR + (int)(((int)BASE_R - (int)m_ColR) * fade);
        const int gMix = (int)m_ColG + (int)(((int)BASE_G - (int)m_ColG) * fade);
        const int bMix = (int)m_ColB + (int)(((int)BASE_B - (int)m_ColB) * fade);
        m_ColR = (uint8_t)ClampInt(rMix, 0, 255);
        m_ColG = (uint8_t)ClampInt(gMix, 0, 255);
        m_ColB = (uint8_t)ClampInt(bMix, 0, 255);
    }
}

// ---------------------------------------------------------------------
// Binary: PlaySplat @ 0x0017f5ec
// Plays one of 6 splat-impact SFX. Caller passes a size index (0..2);
// PlaySplat clamps to [0,2] then picks one of two pair entries via
// RandInt(2). Strings (binary capitalisation, no extension):
//   size 0: "Pulp-drip-2",        "Pulp-drip-1"        (pair 0/1)
//   size 1: "Splatter-Small-2",   "Splatter-Small-1"
//   size 2: "Splatter-Medium-2",  "Splatter-Medium-1"
// Note pair order: RandInt(2)==0 selects suffix -2, ==1 selects -1.
// Per-size cooldown: gate ticks down by dt/frame in Update; when
// <= 0 here, fires + resets to 0.5. Three independent gates by size.
// ASM-verified: 2026-04-29 binary @ 0x0017f5ec..0x0017f74b (asm-inspector)
// ---------------------------------------------------------------------
void SplatEntity::PlaySplat(int splatSize) {
    int sz = splatSize;
    if (sz > 2) sz = 2;
    if (sz < 0) sz = 0;

    if (s_SplatSfxGate[sz] > 0.0f) return;

    static const char* kPairs[3][2] = {
        { "Pulp-drip-2",        "Pulp-drip-1"        },  // size 0
        { "Splatter-Small-2",   "Splatter-Small-1"   },  // size 1
        { "Splatter-Medium-2",  "Splatter-Medium-1"  },  // size 2
    };
    const char* name = kPairs[sz][RandInt(2)];

    Game* game = Game::GetInstance();
    if (game && game_work.mGameSound) {
        game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);
    }

    s_SplatSfxGate[sz] = 0.5f;
}

// ---------------------------------------------------------------------
// Pool / static ops
// ---------------------------------------------------------------------

void SplatEntity::CreatePool(int capacity) {
    s_Pool.Create(capacity);
}

void SplatEntity::DestroyPool() {
    s_Pool.Destroy();
    s_SplatTex.SetNull();
}

void SplatEntity::LoadContent() {
    if (!s_SplatTex.IsValid()) {
        s_SplatTex = Mortar::TextureManager::LoadLocalisedTexture("white_splash.tex");
    }
}

void SplatEntity::CleanUp() {
    s_SplatTex.SetNull();
}

SplatEntity* SplatEntity::GetFree() {
    SplatEntity* s = s_Pool.Pop();
    if (!s) return 0;
    s->m_bAlive = 0;
    return s;
}

// Cache populated by UpdateActiveSplats; NumActiveSplats returns this.
// Also serves as the per-call vertex write cursor inside DrawSplat (GOT +0x72c4).
static int s_NumActiveSplats = 0;

// Set by DrawActiveSplats before each DrawSplat call.
// Points into HUD::scales[3..5] (world tint) for the duration of the batch pass.
static const float* s_CurrentTintRGB = 0;

// ASM-verified: 2026-05-06T17:00 binary @ 0x0017ee34 (asm-inspector)
// Returns the cached counter; binary does NOT iterate the pool here.
// The cache is refreshed at the end of UpdateActiveSplats's pool loop.
int SplatEntity::NumActiveSplats() {
    return s_NumActiveSplats;
}

// ASM-verified: 2026-05-06T18:00 binary @ 0x0017fd68 (asm-inspector)
// Body order matches binary 0x0017fd7c..0x0017ff38:
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
            const char* name = (RandInt(2) == 0) ? "Pulp-drip-2" : "Pulp-drip-1";
            Game* game = Game::GetInstance();
            if (game && game_work.mGameSound) {
                game_work.mGameSound->SFXPlay(name, 1.0f, 1.0f);
            }
        }
    }

    // (c) Per-frame spring rate compute -- binary 0x0017fe46..0x0017feda.
    //   N_total  = Mortar::ActorManager::GetNumEntities()
    //   N_active = s_NumActiveSplats   // PRIOR-frame cached count
    //   raw      = (N_total + N_active) / 15.0 - 0.15
    //   if raw <= 0:   spring = 1.25
    //   elif raw >= 3: spring = 4.25
    //   else:          spring = raw + 1.25  (linear ramp 1.25..4.25)
    int totalEntities = 0;
    if (Mortar::ActorManager* am = Mortar::ActorManager::GetInstance()) {
        totalEntities = am->GetNumEntities();
    }
    {
        const float raw = (float)(totalEntities + s_NumActiveSplats) / 15.0f - 0.15f;
        if (raw <= 0.0f)      s_SpringRate = 1.25f;
        else if (raw >= 3.0f) s_SpringRate = 4.25f;
        else                  s_SpringRate = raw + 1.25f;
    }

    // (d) Pool loop -- update each alive splat, push dead ones back,
    //     write the new active count to s_NumActiveSplats LAST.
    const int N = s_Pool.Capacity();
    int activeCount = 0;
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive) continue;

        s->UpdateSplat(dt);

        if (!s->m_bAlive) {
            s_Pool.Push(s);
        } else {
            ++activeCount;
        }
    }
    s_NumActiveSplats = activeCount;
}

void SplatEntity::RemoveAllSplats() {
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive) continue;
        s->m_bAlive = 0;
        s_Pool.Push(s);
    }
}

void SplatEntity::ForEachInPool(PoolVisitor fn, void* user) {
    if (!fn) return;
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (s) fn(s, user);
    }
}

// ---------------------------------------------------------------------
// SplatEntity::DrawSplat (0x0017f008) -- virtual per-instance render
// Vtable slot 4.
// ASM-verified: 2026-05-18 binary @ 0x0017f008 (re-analyst)
// ---------------------------------------------------------------------
// Writes 6 QUADCUSTOMVERTEX entries into s_SplatVerts at the cursor
// position given by s_NumActiveSplats. Tint read from s_CurrentTintRGB.
// Called indirectly via vtable from DrawActiveSplats (pure thiscall).
void SplatEntity::DrawSplat() {
    QUADCUSTOMVERTEX* outVerts = &s_SplatVerts[s_NumActiveSplats * 6];
    const float* tintRGB = s_CurrentTintRGB;
    // Quad corner construction -- matches binary DrawSplat (0x0017f008).
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
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0017f1ec (asm-inspector)
    Colour splatColour(m_ColR, m_ColG, m_ColB, m_ColA);
    Colour tinted = Colour::TintColour(splatColour, tintRGB);
    const uint32_t col =
        ((uint32_t)tinted.a << 24) |
        ((uint32_t)tinted.b << 16) |
        ((uint32_t)tinted.g <<  8) |
        ((uint32_t)tinted.r);

    // Atlas UV -- verified via SplatEntity::DrawSplat @ 0x0017f008.
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
    const float px = m_Pos.x, py = m_Pos.y, pz = SPLAT_DRAW_Z;

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
// Batched draw -- matches DrawActiveSplats (0x00180344)
// ---------------------------------------------------------------------
//
// Zeros s_NumActiveSplats at entry (binary @ 0x00180356), sets
// s_CurrentTintRGB from HUD::scales[3..5], then for each alive landed
// splat calls DrawSplat() (pure thiscall, vtable slot 4) and increments
// s_NumActiveSplats (binary @ 0x0018039c). Submits the completed batch.
// ASM-verified: 2026-05-18 binary @ 0x00180344 (re-analyst)
// Depth state owned by GameDraw (binary @ 0x0016b888): no per-call
// glEnable/glDisable(GL_DEPTH_TEST) or glDepthMask in the binary's body.
void SplatEntity::DrawActiveSplats() {
    if (!s_SplatTex.IsValid()) return;

    // Binary @ 0x00180356: *s_NumActiveSplats = 0 before the loop.
    s_NumActiveSplats = 0;

    // Fetch world tint from HUD::scales[3..5] and store in s_CurrentTintRGB.
    // DrawSplat reads this directly rather than receiving it as an argument.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0017f1ec (asm-inspector)
    s_CurrentTintRGB = Colour::IdentityTint();
    if (Game* game = Game::GetInstance()) {
        if (game_work.mHud) {
            s_CurrentTintRGB = &game_work.mHud->scales[3];
        }
    }

    const int N = s_Pool.Capacity();

    for (int i = 0; i < N && s_NumActiveSplats < MAX_SPLATS_PER_FRAME; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive)     continue;
        if (s->m_SplatType < 0)     continue;  // still airborne

        s->DrawSplat();

        // Binary @ 0x0018039c: cursor++ after each DrawSplat call.
        ++s_NumActiveSplats;
    }

    const int count = s_NumActiveSplats;

    if (count == 0) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    s_SplatTex->Set();

    // Depth state is owned by GameDraw at the pass level (binary @ 0x0016b888):
    // SetDepthBuffer(1) + SetDepthBufferWrite(0) is set BEFORE the splat pass and
    // stays in effect. SplatEntity::Draw must NOT mutate it -- doing so leaves
    // depth-test OFF for subsequent same-bucket draws and breaks the fruit
    // -occludes-backdrop sort order on the menu screen.
    Mortar::Mesh::DrawTriList(s_SplatVerts, count * 6, false, NULL);

    s_SplatTex->UnSet();
}
