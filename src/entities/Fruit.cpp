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
#include "hud/SliceEffect.h"
#include "hud/MissControl.h"
#include "game/BombHit.h"
#include "game/ScoreState.h"
#include "game/WaveManager.h"
#include "game/PowerUpManager.h"
#include "game/ItemManager.h"
#include "game/GameOver.h"
#include "engine/network/NetworkManager.h"
#include "engine/util/StringTable.h"
#include "engine/util/Event.h"
#include "game/FruitSaveData.h"
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
#include <string>
#include <vector>
#include "game/GameWork.h"

// Binary static: critical/charged fruit blend-target colour for blade flash.
// Used by SlashEntity::UpdatePoints colour-blend path (binary @ 0x1e6914).
// TODO: 0x1e6914 -- verify exact RGB from Ghidra (binary blends toward this when m_Scale>0).
const Colour Fruit::CRITICAL_COLOUR(255, 128, 0, 255);

// File-scope global: multicast event fired on every fruit slice.
// Binary: file-static in Fruit.cpp, ctor'd in global.ctors @ 0x1e2404.
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
// Binary @ 0x00176cc8..0x00176cd4: unconditional store, clamped to >= 0 (not >= 1).
static int g_PowerFruitCount = 0;

// Per-frame gate for "Throw-fruit" SFX. Binary stores at *(g_fruitGlobal + 0x48)
// (DAT_00177960 + 0x48). Reset in Fruit::Chuck so each new launch can fire once.
// Prevents multiple fruits chucked in the same frame from each playing the SFX.
static bool s_FruitThrowSfxFiredThisFrame = false;

// Base slice-rotation axes — written by global.constructors.keyed.to.Fruit.cpp
// (binary @ 0x1E206C). Values: world unit basis X/Y/Z.
//   0x2D9EE4 -> kSliceBaseAxis[0] = (1,0,0)   slice axis[0]
//   0x2D9ED8 -> kSliceBaseAxis[1] = (0,1,0)   slice axis[1]
//   0x3328AC -> kSliceBaseAxis[2] = (0,0,1)   slice axis[2]
static const Vec3 kSliceBaseAxis[3] = {
    Vec3(1.0f, 0.0f, 0.0f),
    Vec3(0.0f, 1.0f, 0.0f),
    Vec3(0.0f, 0.0f, 1.0f),
};

// GetFruitZPosition counter (binary @ 0x001690cc).
// Decrements by 100 per call, wraps back to -500 when it falls below -2499.
// Constants from binary DATs: step=100 (DAT_00169108), lower=-2499 (DAT_0016910c),
// reset=-500 (DAT_00169110).
static float s_FruitZCounter = -500.0f;

// Static Colour constants from _GLOBAL__I_Fruit.cpp @ 0x0017a354.
// *DAT_0017a678: Colour(0x80, 0x80, 0xff, 0x80) = RGBA(128, 128, 255, 128)
// Likely g_FruitOutlineTint or g_FruitGlowTint. Exact consumer not yet RE'd -- TODO.
static Colour g_FruitTint1(128, 128, 255, 128);  // DAT_0017a678
// DAT_0017a670: copy-ctor from BLUE singleton (DAT_0017a674 -> GOT off 0x7a5c
// == BLUE created in _GLOBAL__I_Colour_cpp @ 0x00184068).
// ASM-verified: 2026-05-20 binary @ 0x0017a512..0x0017a51c (re-analyst).
static Colour g_FruitTint2(0, 0, 255, 255);      // DAT_0017a670: blue

// Binary constants for fruit slicing.
// Resolved from DATs near CollisionResponse (0x1780b0) and Slice (0x176d58).
static const float SLICE_TIMER_BASE    = 0.03f;   // DAT_001784dc
static const float SLICE_BLADE_SCALE   = 0.1f;    // DAT_001784e0

// Bomb-hit cinematic window: LTF in {2,3} and timer in (-0.1f, 0.95f).
// Shared by both the power-up-activation gate and the score+save gate
// in CollisionResponse. Binary @ 0x001788f4 (same game_work GOT entry).
static const float kBombHitMax = 0.95f;
static const float kBombHitMin = -0.1f;
static const float SLICE_CLAMP_MIN_NRM = 4.0f;    // normal fruit clamp
static const float SLICE_CLAMP_MAX     = 8.0f;
// Fruit::SetFruitType (0x0017621c) collision radius formula — verified
// 2026-04-15 from binary disassembly at 0x0017630e..0x0017631e:
//   vldr s14, [r3, #0x244]   ; s14 = m_Scale            (XML "scale")
//   vldr s15, [r3, #0x248]   ; s15 = m_CollisionScale   (XML "collision")
//   vmla s15, s13, s14       ; s15 += 0.52 * s14
//   vmul s15, s15, scale     ; s15 *= scaleParam (1.0 typical)
// →  radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam
// Defaults from LoadInfo: m_Scale = 25.0 @ 0x41c80000,
//                         m_CollisionScale = 1.0 @ 0x3f800000.
static const float COL_RADIUS_FACTOR = 0.52f;   // DAT_00176340

// Slice juice-burst tuning, resolved from the binary via read_memory.
// Critical / special slices set splatCount to a configured global rather than
// the base Rand32(2)+2. The global *(GOT+DAT_00177060) reads 10 at runtime.
static const int   kSliceJuiceSplatCount = 10;   // *0x001F3E20
// Per-splat taper applied after MakeSplat (binary @ 0x00177070..0x001770f0):
//   factor = clamp(1 - (i-2)/splatCount, kSplatTaperMin, 1.0)
//   m_Vel.z *= factor; and for i > 2 the X/Y velocity and scale get boosted.
static const float kSplatTaperMin    = 0.3f;     // DAT_0017706c
static const float kSplatVelXYBoost  = 1.2f;     // *(GOT+DAT_001774b0) @ 0x001F3E28
static const float kSplatScaleBoost  = 1.5f;     // *(GOT+DAT_001774b4) @ 0x001F3E24

// Matches RandomStartAngle(Quat&, false) @ 0x00175740 — gives the fruit a
// uniformly random orientation on the sphere by picking a random axis in
// the unit cube, normalising, and combining with a random ~16-bit angle.
// The old port used FromAxisAngle(Vec3(1,0,0), RandRange(pi)) which locked
// every fruit's initial spin to the X axis — visible as all fruits starting
// level/upright instead of at varied tilts.
// RNG source: binary uses the WaveManager-owned PRNG (ASM-verified: 2026-05-26 binary @ 0x00176708 (re-analyst)).
static Quaternion RandomStartAngle() {
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();
    float ax = rng.RandF(2.0f) - 1.0f;   // [-1, 1]
    float ay = rng.RandF(2.0f) - 1.0f;
    float az = rng.RandF(2.0f) - 1.0f;
    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len < 1e-6f) { ax = 1.0f; ay = 0.0f; az = 0.0f; len = 1.0f; }
    ax /= len; ay /= len; az /= len;
    // Binary: Rand32(0xff3a) — ~full turn in 16-bit angle units.
    uint32_t angle16 = rng.Rand32(0xff3aU);
    Quaternion q;
    q.CreateFromAxisAngle(ax, ay, az, angle16);
    return q;
}

// SetupLighting @ 0x00175018 — single `bx lr`, a genuine no-op stub in
// the shipped binary. Both Bomb::LoadContent (0x001727d8) and
// Fruit::LoadFruitModels (0x001e08ec) reach it via PLT trampoline.
// No material / mesh / GL state is touched.
// NOTE: this is a separate file-local in Fruit.cpp (Bomb.cpp has its own
// static copy; the binary's single function serves both TUs via PLT).
static Mortar::SmartPtr<Mortar::Model>& SetupLighting(Mortar::SmartPtr<Mortar::Model>& model) {
    return model;
}

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

// ASM-verified: 2026-04-28T00:00 binary @ 0x00176708 (asm-inspector)
// ASM-verified: 2026-05-20 binary @ 0x00176708 (re-analyst) -- bActive and bCriticalEligible default to 1, not 0.
// ASM-verified: 2026-05-26 binary @ 0x00176708 (re-analyst) -- RNG source, field writes, flags bit-op.
// ASM-verified-partial: 2026-05-27 binary @ 0x00176708 (asm-inspector) --
//   power-fruit counter increment + non-arcade path verified;
//   arcade pineapple-blitz dedup + power-fruit gate left as TODO.
// Binary @ 0x00176708 — vtable slot 2. p2=fruitType; p3=scale (nullable).
void Fruit::Init(void* /*p1*/, long fruitType, Vec3* /*scaleOrNull*/) {
    // Binary @ 0x00176708: range-check fruitType; out-of-range falls back to RandomFruit(true).
    if (fruitType >= 0 && fruitType < (long)FruitInfo_GetCount()) {
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
    m_CollisionSize = 75;         // binary @ 0x00176708: str r3, [r0, #0x4b] = 0x4B
    m_VestigialInitFour = 4;      // binary @ 0x00176708: write-only dead field
    // ASM-verified: 2026-05-26 binary @ 0x00176708 (re-analyst)
    // ASM-verified: 2026-05-27 binary @ 0x0017690a (re-analyst)
    // orr r1,r1,#0x2 ; bfc r1,#0x4,#0x1
    flags = (flags & ~ENT_KILLED) | ENT_HAS_COLLISION;

    m_ZPosition = GetFruitZPosition();

    // Reset slice state (binary Fruit::Init — m_SliceTimer = -1).
    m_SliceTimer      = -1.0f;
    m_SliceArcAngle   = 0;
    m_SliceArcImpulse = 0.0f;
    m_SlicePos        = Vec3(0, 0, 0);
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

    // Random rotation velocity (matches binary Fruit::Init @ 0x00176708):
    // one triple of random values, stored IDENTICALLY into both m_RotVel1
    // and m_RotVel2 — the two halves tumble in sync.
    // RNG source: binary uses WaveManager-owned PRNG (ASM-verified: 2026-05-26 binary @ 0x00176708 (re-analyst)).
    {
        Math::Random& rng = WaveManager::GetInstance()->GetRandom();
        m_RotVel1 = Vec3(rng.RandF(11.0f) - 5.5f,
                         rng.RandF(11.0f) - 5.5f,
                         rng.RandF(11.0f) - 5.5f);
    }
    m_RotVel2 = m_RotVel1;

    // Random start rotation — random axis + random angle (binary
    // RandomStartAngle @ 0x00175740, called with false from Fruit::Init).
    m_Rot1 = RandomStartAngle();
    m_Rot2 = m_Rot1;

    // Default gravity — confirmed from Fruit::Init 0x00176708: literal -12.0, DAT_00176a18=0.0
    m_Gravity = Vec3(0.0f, -12.0f, 0.0f);

    // Extra accel/jerk term — init to zero.
    // Binary Fruit::Init @ 0x00176708 reads *globalConfigVec3 (GOT 0x001f4328);
    // BSS Vec3 initialised by _GLOBAL__I_Fruit.cpp to (0,0,0).
    m_AccelTerm = Vec3(0.0f, 0.0f, 0.0f);

    // Matches SetFruitType (0x17621c):
    // visualScale = globalScaleVec * FruitInfo[type].scale * VISUAL_SCALE_MULT (0.01)
    // globalScaleVec is at BSS 0x1F4334, initialized to (0,0,0) by static init
    // but overwritten at runtime before fruit creation.
    // Per-fruit scale from Data/xml/fruitlist.xml (e.g. watermelon=75)
    {
        // Vec3::One at BSS 0x1F4334 — a constant singleton for (1,1,1), not a
        // mutable scale variable. Matches binary: _Vector3::operator*(Vec3*, float*)
        // in SetFruitType (0x17621c) multiplies Vec3::One by m_Scale then 0.01.
        const FruitInfoData* info = FruitInfo_Get(fruitType);
        float fruitScale = info ? info->m_Scale * 0.01f : 1.0f;
        scale = Vec3::One() * fruitScale;
        m_VisualScale = scale;  // ASM-verified: 2026-05-18 binary @ 0x00176290 (re-analyst); SetFruitType writes 0xAC/B0/B4

        // Collision sphere (SetFruitType @ 0x0017621c, verified
        // 2026-04-15 from disassembly).
        //   radius = (m_CollisionScale + 0.52 * m_Scale) * scaleParam
        // where scaleParam is the SetFruitType arg (1.0 at the common
        // call site). m_Scale is the XML "scale" attr (e.g. watermelon
        // = 75); m_CollisionScale is the XML "collision" attr (5 for
        // every fruit in fruitlist.xml). Defaults if FRUIT_INFO is
        // missing: m_Scale = 25.0, m_CollisionScale = 1.0.
        const float fScale  = info ? info->m_Scale          : 25.0f;
        const float fColBase = info ? info->m_CollisionScale : 1.0f;
        const float radius   = fColBase + COL_RADIUS_FACTOR * fScale;
        if (!m_Col) m_Col = new ColSphere();
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center() = Vec3(pos.x, pos.y, 0.0f);
        cs->radius = radius;
    }

    // ASM-verified: 2026-05-27 binary @ 0x00176754..0x0017683e (re-analyst).
    // Arcade-only (gameMode==2, m_GameDt<1.0) duplicate-pineapple + power-fruit
    // spam gate. Runs BEFORE the g_PowerFruitCount increment so the kill branch
    // doesn't need an undo-decrement.
    if (game_work.gameMode == 2 && game_work.m_GameDt < 1.0f) {
        // (1) Re-roll while we'd spawn another black_pineapple this frame.
        //     BOMB_PINEAPPLE binary literal -> port "black_pineapple" per fruitlist.xml.
        static const int kBlackPineappleType = Fruit::FruitType("black_pineapple", false);
        while ((int)m_FruitType == kBlackPineappleType) {
            m_FruitType = (uint8_t)Fruit::RandomFruit(true);
        }

        // (2) Power-fruit spam gate. Pre-increment test.
        const FruitInfoData* gateInfo = FruitInfo_Get(m_FruitType);
        if (gateInfo && gateInfo->m_pPowers) {
            static const uint32_t kScoreMultHash = StringHash("score_mult");
            bool kill = false;
            if (g_PowerFruitCount > 0) {
                kill = true;
            } else {
                const float tRem = (game_work.m_SaveData
                                    ? game_work.m_SaveData->m_TimeRemainingSave
                                    : 0.0f);
                if (tRem < 8.0f
                        && gateInfo->m_pPowers->m_pArray
                        && gateInfo->m_pPowers->m_pArray[0].m_PowerHash != kScoreMultHash) {
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

    // ASM-verified: 2026-05-27 binary @ 0x001768a8..0x001768b8 (asm-inspector).
    // Increment global active-power-fruit counter for power-fruits. Pairs with
    // KillFruit's natural-expiry decrement.
    const FruitInfoData* spawnInfo = FruitInfo_Get(m_FruitType);
    if (spawnInfo && spawnInfo->m_pPowers) {
        ++g_PowerFruitCount;
    }

    // Defunct: online-MP -- no-op stub; binary @ 0x00176708 +0x1b8
    // Binary: BOMB_PINEAPPLE count decrement for online multiplayer sync packet.
    // Dead on this platform -- P2P online MP was removed.

}

// ASM-verified: 2026-05-27 binary @ 0x00175a64 (re-analyst)
// Binary semantics: cache pos into m_SecondPos, clamp negative delay to
// 0.125, set m_SpawnDelay. NO flags write, NO m_ScaleAnim write,
// NO s_FruitThrowSfxFired reset -- those belong in Init.
void Fruit::Chuck(float delay) {
    m_SecondPos = pos;
    if (delay < 0.0f) delay = 0.125f;
    m_SpawnDelay = delay;
    // ASM-verified: 2026-05-27 binary @ 0x00175ab2..0x00175af2 (re-analyst)
    // Power-fruit cancel-if-thrown-too-late: when this fruit carries a non-freeze
    // power-up and the wave will end within 8s of when its delay elapses, abort
    // (mark dead, decrement g_PowerFruitCount).
    // "freeze" string @ binary 0x001BA2BF. Live wave-time mirror is
    // game_work.m_SaveData->m_TimeRemainingSave -- TimeControl::Update writes it
    // every frame (binary @ 0x00162830).
    const FruitInfoData* chuckInfo = FruitInfo_Get(m_FruitType);
    if (chuckInfo && chuckInfo->m_pPowers && chuckInfo->m_pPowers->m_pArray
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

// ASM-verified: 2026-06-15T00:00Z binary @ 0x001df828 (asm-inspector)
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
                && (   (game_work.gameMode == 2 && game_work.m_GameDt < 1.0f)
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
                if (info && info->m_TrailHash) {
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
                // Per-tick m_pEmitter1 follow-position (binary's GOT offset Vec3 is
                // BSS-zero, so the addend collapses to just pos).
                if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
            }

            // binary @0x001dfd80 -- cascade fruit-spawn fires ONCE on the transition
            // frame when m_SpawnDelay crosses from positive to non-positive. Binary
            // nests this inside the if (m_SpawnDelay > 0.0f) block, after trail re-arm.
            // WaveManager::field_0x6c is the per-frame fruit multiplier set by
            // PowerUp::FruitMultiplyer. For value N: spawn (N-1) extras from a
            // random side-template. For value < 1: warp this fruit off-screen so
            // CheckHasGoneOffscreen kills it next frame.
            {
                WaveManager* wm = WaveManager::GetInstance();
                float countF = wm->field_0x6c;
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
                    vel          = Vec3(0.0f, -1.0f, 0.0f);
                } else if (cnt != 1) {
                    // Spawn (cnt-1) extra fruits via a random side-template.
                    // Binary @ 0x001dfdf4. Three stack-built SPAWNER_INFOs;
                    // each starts from SPAWNER_INFO ctor defaults then overrides.
                    SPAWNER_INFO templates[3];
                    templates[0].m_SpawnType  = PLACEMENT_BOTTOM_SLOW;
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
                Vec3 step = (vel * dtScaled + m_Gravity * (0.5f * dtScaled * dtScaled)) * POS_INTEGRATION_SCALE;
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
            m_SliceTimer -= dtScaled;
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
        if (m_Gravity != Vec3(0.0f, 0.0f, 0.0f)) {
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
        const bool isSuperFruit = (spinInfo && spinInfo->m_bIsSuperFruit);
        Quaternion* rotSlots[2] = { &m_Rot1, &m_Rot2 };
        Vec3* velSlots[2] = { &m_RotVel1, &m_RotVel2 };
        for (int idx = 0; idx < 2; ++idx) {
            if (m_bFrozen != 0) break;
            // Super-fruit: recompute axis0 from current m_Rot1 each frame.
            if (isSuperFruit && m_bSliced) {
                Matrix44 mat = m_Rot1.ToMatrix44();
                // Matrix44 col-major: col0 = (m[0],m[1],m[2]) = mat * (1,0,0)
                m_SliceAxes[idx * 3 + 0] = Vec3(mat.m[0], mat.m[1], mat.m[2]);
            }
            Vec3& rv = *velSlots[idx];
            Quaternion& rot = *rotSlots[idx];
            Vec3& ax0 = m_SliceAxes[idx * 3 + 0];
            Vec3& ax1 = m_SliceAxes[idx * 3 + 1];
            Vec3& ax2 = m_SliceAxes[idx * 3 + 2];
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
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        if (m_pEmitter1) { pm.ClearEmitter(m_pEmitter1); m_pEmitter1 = nullptr; }
        if (m_pEmitter2) { pm.ClearEmitter(m_pEmitter2); m_pEmitter2 = nullptr; }
    }

    // binary @0x001e034e -- per-frame emitter position/rotation tracking.
    // The "direction" Vec3 at GOT[DAT_00177d0c]/DAT_001e0424/DAT_001e0428 is BSS-zero,
    // so the matrix-rotate -> Atan2Idx pipeline collapses to (sin=0, cos=1). We
    // still write those slots to clear any stale orientation from a previous trail.
    // Binary forces emitter1.z to -5000.0f (DAT_001e0420) off-camera depth marker.
    if (m_pEmitter1) {
        m_pEmitter1->m_Pos     = pos;
        m_pEmitter1->m_Pos.z   = -5000.0f;  // DAT_001e0420
        m_pEmitter1->m_DirCos  = 1.0f;      // binary +0x30 = CosIdx(0)
        m_pEmitter1->m_DirSin  = 0.0f;      // binary +0x34 = SinIdx(0)
        // TODO: 0x1e03c0 emitter rotation via m_Rot1.Matrix33 x gEmitVec -> Atan2Idx; port collapses to (1,0)
    }
    if (m_pEmitter2) {
        m_pEmitter2->m_Pos     = m_SecondPos;  // binary calls this m_HalfB_pos; slot +0xC8
        m_pEmitter2->m_DirCos  = 1.0f;
        m_pEmitter2->m_DirSin  = 0.0f;
        // NOTE: binary does NOT force emitter2.z to -5000.0f (only emitter1).
    }

    if (CheckHasGoneOffscreen()) {
        KillFruit(true);
    }
}

// Zen-mode "mirror bounce at X limits" flag. Reads bit 0x20 of
// SlashEntity::s_ModPowerMask (binary BSS 0x0024d8cc) — a uint bitmask
// that active SlashModifier instances OR their bits into each frame.
// Bit 0x20 of SlashEntity::s_ModPowerMask is set by a SlashModifier
// registered in the Arcade-mode wave list. When active, vertical-gravity
// fruits hard-bounce off ±192 X bounds instead of being soft-nudged.
// Historically mislabelled "Zen" in port comments -- binary @ 0x00175066
// checks gameMode == 2 = ARCADE.
// ASM-verified: 2026-05-20 binary @ 0x00175066 (re-analyst)
static bool IsArcadeStrictBounceActive() {
    return (SlashEntity::s_ModPowerMask & 0x20u) != 0;
}

// Matches Fruit::DrawUpdate (0x0017501c) — called from Mortar::ActorManager::Update
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
// ASM-verified: 2026-05-09 binary @ 0x0017501c..0x00175198 (asm-inspector)
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
        // ASM-verified: 2026-05-20 binary @ 0x00175066 (re-analyst) — gate is
        // gameMode == ARCADE (literal cmp #0x2) plus s_ModPowerMask bit 0x20.
        const bool arcade = (game_work.gameMode == Mortar::GAME_MODE_ARCADE);
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
                         const Vec3& drawPos,
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
    // ASM-verified: 2026-05-27 binary @ 0x00179216 (re-analyst)
    // Binary resets the throw-fruit SFX per-frame flag at Draw entry,
    // not per-launch in Chuck. This means only one fruit per frame can
    // play the SFX, but the flag re-arms on the next frame.
    s_FruitThrowSfxFiredThisFrame = false;

    if (!IsActive() || m_SpawnDelay > 0.0f) return;

    const FruitModelInfo* fmi = GetFruitModelInfo(m_FruitType);
    if (!fmi || !fmi->m_Whole.IsValid()) return;

    // ASM-verified: 2026-05-27 binary @ 0x001791f4 (re-analyst).
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
        Vec3 drawPos(pos.x, pos.y, m_ZPosition);
        DrawOneModel(fmi->m_Whole.Get(), drawPos, m_Rot1, s);
    } else {
        // Sliced fruit — draw two halves. Matches Fruit::Draw
        // (0x1791f4) sliced branch which loops over
        // m_pFruitModels[type]->m_HalfA / m_HalfB.
        //
        // If a half mesh is missing, fall back to the whole-fruit mesh.
        Mortar::Model* halfA = fmi->m_HalfA.IsValid()
                             ? fmi->m_HalfA.Get() : fmi->m_Whole.Get();
        Mortar::Model* halfB = fmi->m_HalfB.IsValid()
                             ? fmi->m_HalfB.Get() : fmi->m_Whole.Get();

        Vec3 drawPosA(pos.x,         pos.y,         m_ZPosition);
        Vec3 drawPosB(m_SecondPos.x, m_SecondPos.y, m_ZPosition);
        DrawOneModel(halfA, drawPosA, m_Rot1, s);
        DrawOneModel(halfB, drawPosB, m_Rot2, s);
    }
}

// Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
void Fruit::Deactivate() {
    // No Fruit-specific emitter cleanup needed here; emitters are cleared
    // by KillFruit before the entity is deactivated.
}

// Matches Fruit::KillFruit (0x00176abc).
void Fruit::KillFruit(bool doMissPenalty) {
    if (m_pEmitter1) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter1);
        m_pEmitter1 = nullptr;
    }
    if (m_pEmitter2) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter2);
        m_pEmitter2 = nullptr;
    }

    if (doMissPenalty) {
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        if (!m_bNoPowerUp && !m_bSliced && info && info->m_Score < 5) {
            Game* g = Game::GetInstance();
            if (g) {
                // Binary @ 0x00176b14..0x00176c00 (Fruit::KillFruit miss path):
                //   if (gameMode == ARCADE)            -> AddToTotal tracking only
                //   else if (FailureEnabled())          -> miss penalty (Classic/Combo)
                //   else (Zen) -> nothing
                // FailureEnabled() = ((gameMode-2u) > 1u) → true only for Classic/Combo.
                if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
                    // Arcade: tracking only, no life loss, no MissControl spawn.
                    // ASM-verified: 2026-05-20 binary @ 0x00176bbe..0x00176c00 (re-analyst)
                    // Dropped-fruit tracking (NOT a life loss). "dropped" is the global counter;
                    // m_DropsKey is the same per-fruit key used by the score path (line ~904), but
                    // here trackSession=false. Per-fruit AddToTotal is gated on info != null.
                    if (game_work.m_SaveData) {
                        static const uint32_t hDropped = StringHash("dropped");
                        game_work.m_SaveData->AddToTotal("dropped", hDropped, 1, false, false);
                        if (info) {
                            game_work.m_SaveData->AddToTotal(info->m_DropsKey, info->m_DropsHash, 1, false, false);
                        }
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
                            // ASM-verified: 2026-05-02 binary @ 0x00176c84 -- combo reset only inside game-over branch
                            g_ComboCount  = 0;
                            g_LastSlasher = -1;  // binary writes 0xFFFFFFFF @ 0x00176c8c
                            FN::GameOver(-1, -1.0f, -1);
                        }
                    }
                }
            }
        }
    }

    // Matches Fruit::KillFruit cleanup tail (binary @ 0x00176c8e..0x00176cea).
    // 1. Clear owner's back-pointer (owner+0x14C = m_pTrackedFruit) if it still points at us.
    //    Binary: "puVar7=*(this+0x160); if(puVar7 && puVar7[0x53]==this){ puVar7[0x53]=0; }"
    //    owner+0x14C == MenuButton::m_pTrackedFruit (MenuButton.p_pad+0x110 = 0x14C in MenuButton).
    // ASM-verified: 2026-05-03 binary @ 0x00176c8e..0x00176cea (asm-inspector)
    if (m_pOwner) {
        // owner+0x14C stores the tracked-fruit pointer. On ARM32 puVar7[0x53] = *(puVar7 + 0x14C).
        Fruit** ownerSlot = reinterpret_cast<Fruit**>(
            reinterpret_cast<char*>(m_pOwner) + 0x14C);
        if (*ownerSlot == this) {
            *ownerSlot = nullptr;
        }
        m_pOwner = nullptr;
    }
    // 2. Decrement g_PowerFruitCount on natural-expiry path (flag 0x10 not yet set)
    //    AND for power-fruits (info->m_pPowers != nullptr).
    //    Binary @ 0x00176cc8..0x00176cd4: unconditional store of 0 when count<=1
    //    else (count-1). Port previously used conditional decrement which pinned
    //    the counter at 1 across multiple natural expirations.
    if (!(flags & ENT_KILLED)) {
        const FruitInfoData* killInfo = FruitInfo_Get(m_FruitType);
        if (killInfo && killInfo->m_pPowers) {
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

// Matches Fruit::CheckHasGoneOffscreen (0x00175218).
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

// ASM-verified: 2026-04-28T00:00 binary @ 0x00175218 (asm-inspector)
bool Fruit::CheckHasGoneOffscreen() {
    const float margin = SCALE_MARGIN_MULT * scale.y;

    // === Horizontal gravity early exit (sliced + |m_Gravity.x| > 0) ===
    if (m_bSliced && fabsf(m_Gravity.x) > 0.0f) {
        float yBound = OFFSCREEN_BASE + margin;
        if (pos.y <= -yBound || pos.y >= yBound) {
            if (m_SecondPos.y <= -yBound || m_SecondPos.y >= yBound)
                return true;
        }
    }

    // === Downward gravity (m_Gravity.y < 0) ===
    bool halfA_gone = false;
    if (m_Gravity.y < 0.0f) {
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
    if (m_Gravity.y > 0.0f) {
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
    if (m_Gravity.x < 0.0f) {
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

    return false;
}

// Binary @ 0x001780b0 — vtable slot 9. Returns 1 if already sliced (early-out), else 0.
// Visual-only pipeline:
//   - guard (already sliced / timer positive → return 1)
//   - critical-hit eligibility ladder (binary @ 0x001780f0..0x001781e8)
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
                              Vec3* bladeVelPtr) {
    // Guard: already sliced or slice timer is positive -> double-hit.
    if (m_bSliced || m_SliceTimer > -1.0f) return 1;
    const Vec3& bladeVel = bladeVelPtr ? *bladeVelPtr : Vec3(0, 0, 0);

    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    const bool isSpecial  = (info->m_Score == 0x32);

    // ASM-verified: 2026-04-29T00:00Z binary @ 0x001780f0 (asm-inspector)
    // Critical-hit eligibility ladder (binary @ 0x001780f0..0x001781e8).
    // All gates must pass; on success roll Rand32(reroll) -- 0 == hit.
    m_bCritical = 0;

    // ASM-verified: 2026-05-20 binary @ 0x00178154/0x001781d4 (re-analyst).
    // kCritScoreBound and kCritResetBase are GOT-indirect int32 globals.
    // DAT_001784fc -> GOT[0x7674] -> *0x001f3e34 = 5
    // DAT_00178504 -> GOT[0x77c8] -> *0x001f3e38 = 30
    // Used as: bound = min(m_ScoreThreshold, 5); on crit hit: m_ScoreThreshold = 30 + 5 = 35.
    static const int kCritScoreBound = 5;   // DAT_001784fc
    static const int kCritResetBase  = 30;  // DAT_00178504

    // FruitInfo +0x318 is m_bScorable: 1 = can receive critical hit.
    const bool canCritFruit = info->m_bScorable;

    // ASM-verified: 2026-05-20 binary @ 0x001780f0 (re-analyst).
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

    // ASM-verified: 2026-05-18 binary @ 0x00178454..0x00178466 (re-analyst).
    // Clear any prior trail/juice emitters before allocating the slice-
    // burst + persistent juice emitters below. Mirrors the pattern used
    // by Release/KillFruit so special-fruit trails (from
    // SetTrailParticles) don't leak when the fruit gets sliced.
    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        if (m_pEmitter1) { pm.ClearEmitter(m_pEmitter1); m_pEmitter1 = nullptr; }
        if (m_pEmitter2) { pm.ClearEmitter(m_pEmitter2); m_pEmitter2 = nullptr; }
    }

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
        PSPParticleEmitter* eHit = pm.AddEmitter(
            info->m_NameHash, nullptr, /*persistent=*/false);
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
        m_pEmitter1 = pm.AddEmitter(info->m_SlicedHash, nullptr, /*persistent=*/true);
        m_pEmitter2 = pm.AddEmitter(info->m_SlicedHash, nullptr, /*persistent=*/true);
        if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
        if (m_pEmitter2) m_pEmitter2->m_Pos = pos;
    }

    // ASM-verified: 2026-05-23 binary @ 0x001781e8..0x00178218 (re-analyst)
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
    // Critical: binary @ 0x00178380 passes the sliced fruit's OWN per-type
    // colour (FRUIT_INFO m_FruitColour, same object FruitTypeColour returns),
    // NOT a gold literal. The previous Colour(255,215,0,192) was a fabrication
    // with no binary basis. DrawCritHit applies the time fade; alpha comes from
    // m_FruitColour[3]. Special (score==0x32): white half-alpha (binary @ 0x001783fa).
    if (isCritical) {
        FN::CriticalFlash(pos, FruitTypeColour((long)m_FruitType));
    } else if (isSpecial) {
        FN::CriticalFlash(pos, Colour(255, 255, 255, 128));
    }

    // Overlay label on critical / rare slices. Pool is stubbed (GetFree
    // returns nullptr until the 9-slot MissControl pool lands in
    // GameInitialise), so this is currently a no-op — the call is wired
    // so it'll light up for free once the pool exists. See
    // docs/entities/miss-control.md and src/hud/MissControl.h.
    if (isCritical) {
        if (MissControl* mc = MissControl::GetFree())
            mc->MakeCritical(pos, 0 /* playerIdx */);
    } else if (isSpecial) {
        if (MissControl* mc = MissControl::GetFree())
            mc->MakeRare(pos);
    }

    // White slice-line visual — matches AddSlice call in binary
    // v1.6.1 Fruit::CollisionResponse @0x001dd500. Binary builds sliceInfo as:
    //   x = m_SliceArcAngle / -182.0 + 90.0   (degrees-offset)
    //   y = bladeSpeed * 0.4                   (impulse length)
    const float sliceAngleDeg = (float)(int16_t)m_SliceArcAngle / -182.0f + 90.0f;
    const float sliceLength   = bladeSpeed * 0.4f;
    FN::SliceEffect_Add(pos, sliceAngleDeg, sliceLength, isCritical);

    // Score, save totals, powerup, combo and achievements are gated exactly as
    // the binary does (v1.6.1 Fruit::CollisionResponse @0x001dd500):
    //   GameTaskState+0x06 (retryFlag) == 0          -- outer "interactive" gate
    //   && slash (hitter) != null                    -- real blade hit, not an
    //                                                   internal re-slice
    //   && m_bNoPowerUp == 0
    //   && ( GameTaskState+0x05 (bM_bPaused) == 0   -- normal play
    //        || bombHitWindow )                       -- or inside the bomb-hit
    //                                                   cinematic window
    // The bomb-hit window: (gameMode - 2u) < 2u && m_GameDt < 0.95f && m_GameDt > -0.1f.
    // Binary field gameMode (+0x04) for window, bM_bPaused (+0x05) for outer gate.
    // ASM-verified: 2026-06-07 v1.6.1 Fruit::CollisionResponse @0x001dd500 (re-analyst).
    int g_FruitWasSliced_points = 0; // carries score out of the gate for event fire at 0x1de5a0
    {
        // v1.6.1 Fruit::CollisionResponse @0x001dd500:
        // OUTER gate uses bM_bPaused (+0x05); bomb-window uses gameMode (+0x04) and
        // m_GameDt (+0x0C = flM_PauseAmount in binary), NOT m_BombHitTimer (+0x10).
        const bool bombHitWindowGate = (uint8_t)(game_work.gameMode - 2u) < 2u
            && game_work.m_GameDt < kBombHitMax
            && game_work.m_GameDt > kBombHitMin;
        if (game_work.retryFlag == 0
            && hitter != nullptr
            && !m_bNoPowerUp
            && (game_work.bM_bPaused == 0 || bombHitWindowGate)) {
        // Matches CollisionResponse score+save dispatch (binary @ 0x001de40c, inside 0x001dd500).
        // ASM-verified: 2026-05-10 binary @ 0x001dd500 (re-analyst).
        // Formula:
        //   score = info->m_Score                               // FRUIT_INFO+0x314
        //   if (critical) score += 5                            // g_CritScoreBonus @ 0x001f3e30
        //   if (info->m_CoinsMax > 0 && info->m_CoinsMin < info->m_CoinsMax)
        //       score = info->m_CoinsMin + Rand32(max - min)    // random-score override
        //   if (critical) score *= 2                            // g_CritScoreMul / 2 = 2 (int div)
        // Earlier port had `score * 2` for critical, missing the +5 bonus.
        // For a normal scorable fruit (m_Score=1, no random override), this gives
        // critical = (1+5)*2 = 12 vs port's old 1*2 = 2 -- the +10 difference user reports.
        // Note: port's m_CoinsMin/m_CoinsMax slots are the binary's "RandBonusBase/Max"
        // when used in this score path; same fields, dual-purpose semantics.
        {
            int score = info->m_Score;
            if (m_bCritical) score += 5;
            if (info->m_CoinsMax > 0 && info->m_CoinsMin < info->m_CoinsMax) {
                const uint32_t range = (uint32_t)(info->m_CoinsMax - info->m_CoinsMin);
                score = info->m_CoinsMin
                      + (int)WaveManager::GetInstance()->GetRandom().Rand32(range);
            }
            if (m_bCritical) score *= 2;  // g_CritScoreMul / 2 = 2
            g_FruitWasSliced_points = score;    // carry score for event fire at 0x1de5a0
            FN::AddToCurrentScore(score, (int)m_PlayerIdx,
                                  /*trackFruit=*/true, /*sendNetPacket=*/false);

            // Per-fruit-name save totals.
            if (game_work.m_SaveData) {
                game_work.m_SaveData->AddToTotal(info->m_TotalStatKey, info->m_TotalStatHash, 1,
                                         /*trackSession=*/false, false);
                game_work.m_SaveData->AddToTotal(info->m_DropsKey, info->m_DropsHash, 1,
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
        // ASM-verified: 2026-05-20 binary @ 0x00178b40..0x00178c34 (re-analyst)
        // Arcade-mode-only (NOT Zen as a prior TODO claimed):
        //   AddToSpeedLossTime(0.05f, 0)             -- SpeedControl HUD tick refresh.
        //   first_fruit = sticky write-once          -- records m_FruitType+1 of first
        //                                               slice ever (savefile-wide).
        //   last_fruit  = set to current m_FruitType+1 via delta math (total := newVal).
        if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
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

        // ASM-verified: 2026-05-22 binary @ 0x001dd500 ~+0x360 (re-analyst).
        // Powerup-fruit slice activates the modifier polymorphism chain. Without
        // this call, no Freeze/Frenzy/x2/Blitz effects ever fire in Arcade.
        // ASM-verified: 2026-05-22 binary @ 0x001dd500 (re-analyst).
        // Powerup-fruit slice fires either during normal gameplay (bM_bPaused==0) OR
        // inside the bomb-hit cinematic window (gameMode in {2,3} = timed-game modes
        // AND m_GameDt in (-0.1f, 0.95f)). Binary field: gameMode (+0x04) for window,
        // bM_bPaused (+0x05) for outer; m_GameDt (+0x0C = flM_PauseAmount in binary).
        // v1.6.1 Fruit::CollisionResponse @0x001dd500
        const bool bombHitWindow = (uint8_t)(game_work.gameMode - 2u) < 2u
            && game_work.m_GameDt < kBombHitMax
            && game_work.m_GameDt > kBombHitMin;
        if (info->m_pPowers && !m_bNoPowerUp
            && (game_work.bM_bPaused == 0 || bombHitWindow)) {
            uint32_t hash = info->m_pPowers->RandomPower();
            Vec3 localPos = pos;
            PowerUpManager::GetInstance()->ActivatePower(hash, localPos, reinterpret_cast<float*>(&localPos));
        }

        // Combo counter increment.
        // TODO: re-verify combo addr -- v1.5.x @ 0x001787a8..0x001787b0 are stale;
        // combo counter moved into SlashEntity::Update @0x001e867c in v1.6.1.
        int slasher = (int)m_PlayerIdx;
        if (g_LastSlasher != slasher) {
            g_ComboCount  = 0;
            g_LastSlasher = slasher;
        }
        g_ComboCount += 1;
        }
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

    // v1.6.1 super-fruit: notify slice path.
    // Binary @ 0x001be630: SuperFruitControl::SuperFruitSliced gates on FruitInfo[+0x330].
    SuperFruitControl::SuperFruitSliced(this, 0, hitter);

    return 0;
}

// Matches Fruit::Slice (0x176d58), now with the binary's flipSide
// logic, special-fruit ×1.5 impulse, and spin-boost loop on both
// halves.
void Fruit::Slice() {
    m_SliceTimer = 0.0f;

    // Binary @ 0x00176d78..0x00176db2 — two discarded select-pattern draws at
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
    // Binary: rotate (0,0,1) by current m_Rot1, compare XY direction
    // against m_SliceArcAngle via GetSmallestDelta. If the rotated Z axis
    // points away from the slice direction, flip the halves' angles.
    Vec3 slicePlane(0, 0, 1);
    // Approximate: m_Rot1.ToMatrix44() * (0,0,1) -- just extract the
    // third column of the rotation matrix.
    Matrix44 rotMat = m_Rot1.ToMatrix44();
    // Third column of a column-major 4x4 is mat.m[8..10].
    slicePlane.x = rotMat.m[8];
    slicePlane.y = rotMat.m[9];
    slicePlane.z = rotMat.m[10];

    bool flipSide = false;
    if (fabsf(slicePlane.x) + fabsf(slicePlane.y) > 0.0f) {
        // 16-bit angle of the rotated-Z XY projection.
        float rotAngleRad = atan2f(slicePlane.y, slicePlane.x);
        float sliceAngleRad = (float)(int16_t)m_SliceArcAngle *
                              (6.2831853f / 65536.0f);
        // Wrap both into [-pi, pi] and take signed delta.
        float delta = rotAngleRad - sliceAngleRad;
        while (delta >  3.1415926f) delta -= 6.2831853f;
        while (delta < -3.1415926f) delta += 6.2831853f;
        if (delta < 0.0f) flipSide = true;
    }

    // --- Impulse ---
    float impulse = m_SliceArcImpulse;
    // Binary @ 0x00176e88 — base splatCount = Rand32(2) + 2 (= 2 or 3). Uses
    // the same Math::g_Random singleton (GOT+DAT_00177058) as every other draw
    // in Slice; the splat-count draw uses Math::g_Random.Rand32(2U) + 2.
    int   splatCount = (int)Math::g_Random.Rand32(2U) + 2;

    // Critical hit gets 1.5× impulse + crit dual-line AddSlice.
    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    const bool isCritical = (m_bCritical != 0);
    // Binary @ 0x00176e94 -- the ENTIRE critical block (two AddSlice lines,
    // splatCount override, impulse*1.5, MakeCritical) is gated on
    // m_bCritical && m_PlayerIdx < 2. With playerIdx >= 2 a crit slice
    // skips all of it and falls through to the normal splatCount/impulse.
    if (isCritical && m_PlayerIdx < 2) {
        // Binary: two slice lines at +/-60 deg offset from the base angle.
        //   infoA.x = m_SliceArcAngle / -182.0 + 60.0
        //   infoB.x = m_SliceArcAngle / -182.0 - 60.0
        //   infoA/B.y = impulse * 0.4 * 0.7
        const float critBase = (float)(int16_t)m_SliceArcAngle / -182.0f;
        const float critLen  = impulse * 0.4f * 0.7f;
        FN::SliceEffect_Add(pos, critBase + 60.0f, critLen, true);
        FN::SliceEffect_Add(pos, critBase - 60.0f, critLen, true);
        // Binary @ 0x00176f1e — splatCount = *(int*)(*(GOT+DAT_00177060)),
        // the configured juice-burst count global (read_memory @ 0x001F3E20 = 10),
        // NOT splatCount += 2. impulse *= 1.5 then a MissControl::MakeCritical.
        impulse *= 1.5f;
        splatCount = kSliceJuiceSplatCount;  // = 10 (binary DAT @ 0x001F3E20)
        // Binary @ 0x00176f2c..0x00176f46 — MissControl::MakeCritical(GetFree(), pos).
        if (MissControl* mc = MissControl::GetFree()) {
            mc->MakeCritical(pos, (int)m_PlayerIdx);
        }
    }

    // Special-fruit (baseScore == 0x32 = 50) also gets 1.5× impulse and the
    // configured juice-burst count. Binary @ 0x00176f4e..0x00176f72.
    if (info->m_Score == 0x32) {
        impulse *= 1.5f;
        splatCount = kSliceJuiceSplatCount;  // = 10 (binary DAT @ 0x001F3E20)
    }

    // Binary @ 0x00176f76 — offscreen kill of the juice burst: when
    // GameTaskState+0x06 (retryFlag) is set, this fruit is offscreen, and it
    // belongs to a remote/AI player (m_PlayerIdx > 1), suppress all splats.
    if (game_work.retryFlag != 0 && IsOffscreen() && m_PlayerIdx > 1) {
        splatCount = 0;
    }

    // --- Splat spawn ---
    // Per-splat speed = (impulse + RandF(0.5)*impulse) * (i*0.2 + 5).
    // Per-splat angle = Rand32(0x10000) & 0xFFF0.
    //
    // Binary uses raw impulse values directly (4..8 range from
    // CollisionResponse clamp). The port's Update integrates pos
    // with a ×60 fudge factor (matching binary 0x00177d00), which
    // means velocities should also stay in the binary's per-frame
    // scale — no extra ×50 multiplier needed here.
    for (int i = 0; i < splatCount; ++i) {
        const uint16_t angle16 = (uint16_t)(Math::g_Random.Rand32(0x10000) & 0xFFF0);
        const float r          = Math::g_Random.RandF(0.5f);
        const float speed      = (impulse + r * impulse) *
                                 ((float)i * 0.2f + 5.0f);
        const float a          = (float)angle16 * (6.2831853f / 65536.0f);
        Vec3 sv(sinf(a) * speed, cosf(a) * speed, 0.0f);

        SplatEntity* s = SplatEntity::GetFree();
        // Binary passes param3 = isCritical for crit splats (biases
        // MakeSplat's landing-type RNG toward types 4/5, the larger
        // variants).
        if (s) s->MakeSplat(pos, sv, isCritical, m_FruitType);

        // Binary @ 0x00177070..0x001770f0 — per-splat post-MakeSplat taper.
        // The later splats (high i) lose Z velocity and, past index 2, gain
        // X/Y velocity and scale so the burst spreads outward as it grows.
        //   factor = clamp(1 - (i-2)/splatCount, 0.3, 1.0)   // 0.3 = DAT_0017706c
        //   m_Vel.z *= factor
        //   if (i > 2) { m_Vel.x *= 1.2; m_Vel.y *= 1.2; m_Scale *= 1.5 }
        if (s) {
            float factor = 1.0f - (float)(i - 2) / (float)splatCount;
            if (factor <= kSplatTaperMin)      factor = kSplatTaperMin;
            else if (factor >= 1.0f)           factor = 1.0f;
            s->m_Vel.z *= factor;
            if (i > 2) {
                s->m_Vel.y *= kSplatVelXYBoost;  // *(GOT+DAT_001774b0) = 1.2
                s->m_Vel.x *= kSplatVelXYBoost;
                s->m_Scale *= kSplatScaleBoost;  // *(GOT+DAT_001774b4) = 1.5
            }
        }
    }

    // ASM-verified: 2026-05-23 binary @ 0x001770e0 (re-analyst)
    if (splatCount > 0 && game_work.mGameSound) {
        char cleanSliceBuf[16];
        uint32_t r = Math::g_Random.Rand32(3);
        snprintf(cleanSliceBuf, sizeof(cleanSliceBuf), "Clean-Slice-%u", r + 1);
        game_work.mGameSound->SFXPlay(cleanSliceBuf, 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }

    // --- Half velocities ---
    // Binary @ 0x00177186: sliceFactor = FRUIT_INFO[+0x24c] (m_HitInfluence).
    // Default 0.75 for all shipped fruits; coconut = 0.9 (from hitInfluence XML attr).
    // Used as: halfVel = dir*(impulse*sliceFactor) + fruitVel*(1-sliceFactor)
    // and:     off = rand * (1-sliceFactor) * 4.0
    const float sliceFactor = info->m_HitInfluence;

    // Binary @ 0x001771b6: SELECT pattern — use const 10912.0f when randVal<=0x2aa8,
    // recompute Rand32(0x5550) when >0x2aa8. Not a retry-if-small loop.
    uint32_t _ra = Math::g_Random.Rand32(0x5550U);
    float randA = (_ra > 0x2aa8U) ? (float)Math::g_Random.Rand32(0x5550U) : 10912.0f;
    uint32_t _rb = Math::g_Random.Rand32(0x5550U);
    float randB = (_rb > 0x2aa8U) ? (float)Math::g_Random.Rand32(0x5550U) : 10912.0f;

    // Angle offsets for the two halves — bound by `(1-softness)*4`.
    const int16_t offA = (int16_t)(randA * (1.0f - sliceFactor) * 4.0f);
    const int16_t offB = (int16_t)(randB * (1.0f - sliceFactor) * 4.0f);

    // Binary @ 0x00177236 also writes back into m_SliceArcAngle when flipSide is set.
    if (flipSide) {
        m_SliceArcAngle = (uint16_t)(m_SliceArcAngle + 0x7ff8);
    }
    uint16_t base = m_SliceArcAngle;
    uint16_t angA = (uint16_t)(base + offA);  // binary 0x0017725e: base + offA
    uint16_t angB = (uint16_t)(base - offB);  // binary 0x0017727e: base - offB

    const float radA = (float)(int16_t)angA * (6.2831853f / 65536.0f);
    const float radB = (float)(int16_t)angB * (6.2831853f / 65536.0f);
    Vec3 dirA(sinf(radA), cosf(radA), 0.0f);
    Vec3 dirB(sinf(radB), cosf(radB), 0.0f);

    Vec3 halfVelA = dirA * (impulse * sliceFactor) +
                    vel  * (1.0f - sliceFactor);
    Vec3 halfVelB = dirB * (impulse * sliceFactor) +
                    vel  * (1.0f - sliceFactor);

    m_SecondPos = pos;

    // Binary @ 0x0017735e — the crit/special branch and the
    // MoveFruitZPositionToBack branch are mutually exclusive (if/else):
    //   if (m_bCriticalEligible || FRUIT_INFO[type].score == 0x32) {
    //       // critical / special velocity override
    //   } else if (m_bSpawnedByCriticalSplash == 0) {
    //       MoveFruitZPositionToBack(this);  // modifies f->m_ZPosition(0x9C)
    //   }
    // The crit override uses raw m_SliceArcAngle (NOT the offset-baked radA) with
    // ±0x3ffc / 0xc004, int32 truncation on each velocity component, and a
    // ×1.75 scale (DAT 0x3fe00000 @ 0x001773c6 / 0x0017742a).
    if (isCritical || info->m_Score == 0x32) {
        const float critRadA = (float)(int16_t)(uint16_t)(m_SliceArcAngle + 0x3ffc) * (6.2831853f / 65536.0f);
        const float critRadB = (float)(int16_t)(uint16_t)(m_SliceArcAngle + 0xc004) * (6.2831853f / 65536.0f);
        halfVelA = Vec3((float)(int)(sinf(critRadA) * impulse),
                        (float)(int)(cosf(critRadA) * impulse), 0.0f) * 1.75f;
        halfVelB = Vec3((float)(int)(sinf(critRadB) * impulse),
                        (float)(int)(cosf(critRadB) * impulse), 0.0f) * 1.75f;
    } else if (!m_bMenuFling) {
        // Binary @ 0x00177444..0x0017744e — only on the plain slice path and
        // only when this fruit was NOT spawned by a critical splash / menu-fling.
        // m_bMenuFling==1 marks menu-context fruits (was m_bSpawnedByCriticalSplash).
        MoveFruitZPositionToBack(this);
    }

    m_SecondVel = halfVelA;
    vel         = halfVelB;

        m_bSliced = true;

    // NOTE: binary Fruit::Slice (0x176d58..0x0017766f) does NOT write
    // m_Gravity (+0xA0) anywhere -- no str/vstr to [this,#0xa0] in the
    // whole function. The prior `m_Gravity = Vec3(0,-12,0)` reset was a
    // port-only band-aid and has been removed for binary fidelity.

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
// Both paths: build m_Rot[idx] = (qx*qy)*qz from fixed 0x3FC0 angles + m_SliceArcAngle.
void Fruit::SetupSliceRotations(bool isSuperFruit, int sliceDirFlag) {
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();

    // super-path: local_e4 starts 0, += 0xB4 per half (0, 180).
    int local_e4 = 0;

    for (int idx = 0; idx < 2; ++idx) {
        Vec3* rv = (idx == 0) ? &m_RotVel1 : &m_RotVel2;

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
            m_SliceAxes[idx * 3 + 0] = Vec3(CosIdx(aBlade), SinIdx(aBlade), 0.0f);

            // axis1: g_BaseAxisC = (0,0,1), flipped for half0.
            {
                Vec3 base = kSliceBaseAxis[2];   // (0,0,1) = g_BaseAxisC @0x3328AC
                if (sliceDirFlag == 0) base = base * -1.0f;
                m_SliceAxes[idx * 3 + 1] = base;
            }

            // axis2: second blade-derived index with per-half offset.
            // local_e4 = 0 for half0, 0xB4 for half1.
            {
                uint16_t bIdx = (uint16_t)((int)((float)local_e4 * 182.0f) & 0xFFFF);
                uint16_t a2   = (uint16_t)(bIdx - m_SliceArcAngle);
                m_SliceAxes[idx * 3 + 2] = Vec3(CosIdx(a2), SinIdx(a2), 0.0f);
            }

            // For half1: flip the m_RotVel components.
            if (idx == 1) {
                m_RotVel1 = m_RotVel1 * -1.0f;
                m_RotVel2 = m_RotVel2 * -1.0f;
                m_SliceAxes[0] = m_SliceAxes[0] * -1.0f;
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
        *rv = Vec3(angX, angY, angZ);

        // Build initial m_Rot from the slice arc angle.
        // Binary: three CreateFromAxisAngle calls with angle 0x3FC0 then m_SliceArcAngle.
        // Product: m_Rot[idx] = (qx*qy)*qz; axis layout: (1,0,0)/(0,1,0)/(0,0,1).
        Quaternion* q = (idx == 0) ? &m_Rot1 : &m_Rot2;
        Quaternion qx, qy, qz;
        qx.CreateFromAxisAngle(1.0f, 0.0f, 0.0f, 0x3FC0u);
        qy.CreateFromAxisAngle(0.0f, 1.0f, 0.0f, 0x3FC0u);
        qz.CreateFromAxisAngle(0.0f, 0.0f, 1.0f, (uint32_t)m_SliceArcAngle);
        *q = ((qx * qy) * qz);
        *q = q->normalized();
    }
}

// Matches Fruit::RotateFacingUp (0x001757f4).
// Sets m_Rot1/m_Rot2 to a fixed starting orientation (facing up) then
// optionally applies an alignment rotation. Sets m_RotVel1/m_RotVel2
// to spinVelAxis * random magnitude.
//
// RandomStartAngle (0x00175740): sets rot to axis=(-1,0,0),
//   angle16=0xce2c via CreateFromAxisAngle (0x0017ac68), then resets to
//   Identity if w==0.
//
// Spin magnitude: +(2 + RandF(2.0)) or -(2 + RandF(2.0)). Binary uses
//   WaveManager's Random instance; port substitutes rand() since this
//   only affects display orientation, not gameplay.
void Fruit::RotateFacingUp(bool alignToFacing, Vec3 spinVelAxis) {
    // ASM-verified: 2026-05-20 binary @ 0x001757f4 — RotateFacingUp uses Math::g_Random
    float r    = Math::g_Random.RandF(2.0f);
    float sign = (Math::g_Random.Rand32(2) == 0) ? 1.0f : -1.0f;
    float magnitude = sign * (2.0f + r);

    for (int i = 0; i < 2; i++) {
        Quaternion* rot    = (i == 0) ? &m_Rot1 : &m_Rot2;
        Vec3*       rotVel = (i == 0) ? &m_RotVel1 : &m_RotVel2;

        // Step 1: RandomStartAngle(rot, fixedAxis=true)
        // Binary: Identity, then CreateFromAxisAngle(-1, 0, 0, 0xce2c).
        // halfAngle = (0xce2c >> 1) / 65536.0 * 2pi
        {
            float halfAngle = (float)(0xce2c >> 1) / 65536.0f * 6.2831853f;
            float c = cosf(halfAngle), s = sinf(halfAngle);
            *rot = Quaternion(-1.0f * s, 0.0f, 0.0f, c);
            if (rot->w == 0.0f) *rot = Quaternion::Identity();
        }

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

// Matches Fruit::FruitType (0x00175b10).
// Searches FRUIT_INFO array by hash of name, matching m_NameHash or
// m_NameHashUpper. Returns index on match. If not found and
// fallbackRandom=true returns a random valid index, else -1.
int Fruit::FruitType(const char* name, bool fallbackRandom) {
    const int count = FruitInfo_GetCount();
    if (name && *name) {
        const uint32_t hash = StringHash(name);
        for (int i = 0; i < count; i++) {
            const FruitInfoData* info = FruitInfo_Get(i);
            if (info && (info->m_NameHash == hash || info->m_NameHashUpper == hash)) {
                return i;
            }
        }
    }
    if (fallbackRandom && count > 0) {
        // Binary uses WaveManager's RNG; port uses rand() — behaviorally
        // equivalent since the result is just a fallback fruit index.
        return rand() % count;
    }
    return -1;
}


// Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
void Fruit::LoadInfo() {
    Game* game = Game::GetInstance();
    if (!game) return;

    std::string xmlPath = game->data_dir + "/xml/fruitlist.xml";
    FruitInfo_Load(xmlPath.c_str());
}

// --- FruitModelInfo global array ---------------------------------------
//
// Binary allocates a flat FruitModelInfo[fruitCount] array at
// LoadFruitModels time using operator_new(count * 0x24 + 8). Layout:
//   [0] uint32_t elemSize (0x24)
//   [4] uint32_t count
//   [8] FruitModelInfo[count]  -- 0x24 per entry
//
// The header stores [elemSize, count] so the "next element" stride is
// runtime-configurable. Port mirrors this raw-allocation + placement-new
// pattern to match v1.6.1 binary @ 0x001e08ec.
// The element pointer is stored alongside the raw base for accessor use.
//
// Static slice-effect models (loaded after per-fruit loop).
// v1.6.1 binary loads slice_fx.mmd / slice_fx_crit.mmd into file-scope globals.

static uint8_t* s_FruitModelsRaw = nullptr;          // raw allocation base (header + elements)
static FruitModelInfo* s_FruitModels = nullptr;      // -> raw + 8 (first element)
static int s_FruitModelCount = 0;                    // number of elements

static Mortar::SmartPtr<Mortar::Model> s_SliceFxModel;
static Mortar::SmartPtr<Mortar::Model> s_SliceFxCritModel;

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
//      c. Per loaded model: SetupLighting + GetNode(0)->Geometry::GetProperty(0x2843d1)
//      d. T_2044(effect, model) when whole valid AND FruitInfo[i]+0x330 (m_bIsSuperFruit) != 0
//   4. Load slice_fx.mmd / slice_fx_crit.mmd into static globals
//   5. Extract fruit atlas texture from s_fruitModels[0].m_pWholeEffect
void Fruit::LoadFruitModels() {
    if (s_FruitModels) return;  // already loaded

    const int count = FruitInfo_GetCount();
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
        if (!info) continue;

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

            // ASM-spec v1.6.1 @ 0x001e0a50: Model::GetNode(0)->GetGeometryEntry(0)->GetProperty(0x2843d1)
            // 0x2843d1 = StringHash("DiffuseMap"). Stores the EffectProperty* into the
            // FruitModelInfo EffectProperty slot for shared atlas-texture resolution.
            // Port: Geometry::GetProperty returns nullptr while BuildPropList is
            // defunct (m_PropList always null), so this currently always null.
            Mortar::EffectProperty* prop = nullptr;
            {
                Mortar::Mesh* node0 = model->GetNode(0UL).Get();
                if (node0) {
                    Mortar::Geometry* geom = node0->GetGeometryEntry(0);
                    if (geom) {
                        prop = geom->GetProperty(0x2843d1);
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
                    // ASM-spec v1.6.1 @ 0x001e0a50: GetProperty(0x2843d1) for DiffuseMap
                    if (node0) {
                        Mortar::Geometry* geom = node0->GetGeometryEntry(0);
                        if (geom) {
                            prop = geom->GetProperty(0x2843d1);
                        }
                    }
                }

                models[i].m_Whole = wholeModel;
                models[i].m_pWholeEffect = prop;

                // ASM-spec v1.6.1 T_2044 @ 0x001e0b3c:
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

        // ---- Outline/MP model: "<name>_outline.mmd" ----
        {
            char path[256];
            snprintf(path, sizeof(path), "models/Fruit/%s_outline.mmd", name);
            Mortar::SmartPtr<Mortar::Model> outlineModel = meshMgr->Load(path);
            if (outlineModel.IsValid()) {
                SetupLighting(outlineModel);

                Mortar::EffectProperty* prop = nullptr;
                {
                    Mortar::Mesh* node0 = outlineModel->GetNode(0UL).Get();
                    // ASM-spec v1.6.1 @ 0x001e0a50: GetProperty(0x2843d1) for DiffuseMap
                    if (node0) {
                        Mortar::Geometry* geom = node0->GetGeometryEntry(0);
                        if (geom) {
                            prop = geom->GetProperty(0x2843d1);
                        }
                    }
                }

                models[i].m_pMpModel = outlineModel;
                models[i].m_pMpEffect = prop;
            }
        }
    }

    // Step 4: Load slice-effect models into static globals
    // Binary @ 0x001e0b50+: load slice_fx.mmd and slice_fx_crit.mmd
    s_SliceFxModel = meshMgr->Load("models/Fruit/slice_fx.mmd");
    s_SliceFxCritModel = meshMgr->Load("models/Fruit/slice_fx_crit.mmd");

    // Step 5: Extract shared fruit atlas texture from first model's EffectProperty.
    // Binary @ 0x001e0b78: s_fruitModels[0].m_pWholeEffect->m_Owner
    //   ->TryGetValue<EffectTexture2D>(Type_Texture2D, 0).
    // EffectProperty path is defunct in the port (BuildPropList not wired), so this
    // is a no-op here. Each geometry's m_DiffuseTex is already set by MeshManager
    // during model loading from the .mmd material table (shared atlas fruit_atlas.tex
    // -> FruitNinja.png).  No fallback or assignment loop needed.
    // DIFFERS: original reads atlas from EffectProperty after loading all models;
    // using per-geometry m_DiffuseTex from MeshManager because BuildPropList is
    // defunct (port).  Fruit atlas is the same texture either way.
    {
        if (count > 0 && models[0].m_pWholeEffect && models[0].m_pWholeEffect->m_Owner) {
            Mortar::EffectTexture2D texId;
            models[0].m_pWholeEffect->m_Owner->TryGetValue<Mortar::EffectTexture2D>(
                Mortar::EffectDataTypes::Type_Texture2D, 0, texId);
        }
    }

    s_FruitModelsRaw = raw;
    s_FruitModels = models;
    s_FruitModelCount = count;
}

const FruitModelInfo* Fruit::GetFruitModelInfo(int fruitType) {
    if (!s_FruitModels) return nullptr;
    if (fruitType < 0 || fruitType >= s_FruitModelCount) return nullptr;
    return &s_FruitModels[fruitType];
}

// Binary @ 0x00176564
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
    const int cnt = FruitInfo_GetCount();
    if (s_TotalWeight < 1) {
        s_TotalWeight     = 0;
        s_TotalAvail      = 0;
        s_TotalCrit       = 0;
        s_TotalCritAvail  = 0;
        FruitInfoData* fi = FruitInfo_GetArray();
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
    FruitInfoData* fruitInfoData = FruitInfo_GetArray();
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

// ASM-verified: 2026-05-20 binary @ 0x00175928 (re-analyst).
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

// ASM-verified: 2026-05-18 binary @ 0x00176d14 (re-analyst)
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

// Matches Fruit::Disable (binary @ 0x00126374): one-byte store of 1 to +0x3D.
// ASM-verified: 2026-05-03 binary @ 0x00126374 (asm-inspector)
void Fruit::Disable(Fruit* f) {
    f->m_bNoPowerUp = 1;
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
static void AddQuad(QUADCUSTOMVERTEX** out, float cx, float cy, float w, float h, Colour col) {
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

// Binary @ 0x00174f18
const char* Fruit::FruitTypeName(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info ? info->m_Name : nullptr;
}

// Binary @ 0x00174f38
unsigned long Fruit::FruitTypeHash(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info ? (unsigned long)info->m_NameHash : 0UL;
}

// Binary @ 0x00174f5c
const char* Fruit::FruitFactTexture(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    return info ? info->m_FactTexture : nullptr;
}

// Binary @ 0x00174f80
Colour Fruit::FruitTypeColour(long type) {
    // DIFFERS: binary checks g_SpecialFruitIdx == type (DAT_0x00174fbc) and
    // returns g_SpecialFruitColour (DAT_0x00174fc0) when matched, set by
    // StarFruit/Gem/Pomegranate spawners as per-frame colour overrides.
    // Port has neither global wired (none of those spawners are ported yet);
    // -1 default means "never match", so this branch always falls through
    // to FRUIT_INFO[type]. Re-enable when those spawners port.
    const FruitInfoData* info = FruitInfo_Get((int)type);
    if (!info) return Colour(255, 255, 255, 255);
    return Colour(info->m_FruitColour[0], info->m_FruitColour[1],
                  info->m_FruitColour[2], info->m_FruitColour[3]);
}

// Binary @ 0x00174fc8
Colour Fruit::FruitFactColour(long type) {
    const FruitInfoData* info = FruitInfo_Get((int)type);
    if (!info) return Colour(255, 255, 255, 255);
    return Colour(info->m_FactColour[0], info->m_FactColour[1],
                  info->m_FactColour[2], info->m_FactColour[3]);
}

// Binary @ 0x00174ff8
const ::FruitInfo* Fruit::FruitInfo(long type) {
    return FruitInfo_Get((int)type);
}

// Binary @ 0x001690cc — return next z-slot and advance counter.
// Counter starts at -500, decrements by 100 per call, resets to -500 when
// it falls below -2499. DATs: step=100 (0x00169108), lower=-2499 (0x0016910c),
// reset=-500 (0x00169110).
// ASM-verified: 2026-05-23 binary @ 0x001690cc (re-analyst)
float Fruit::GetFruitZPosition() {
    s_FruitZCounter -= 100.0f;
    if (s_FruitZCounter < -2499.0f) {
        s_FruitZCounter = -500.0f;
    }
    return s_FruitZCounter;
}

// Binary @ 0x0016911c — move sliced fruit to back of z-order.
// Formula from disassembly (VNMLS): *m_ZPosition = (500 + *m_ZPosition)*0.5 - 2600
// DATs: addend=500 (0x0016913c), subtrahend=2600 (0x00169140), half=0.5 (vmov literal).
// ASM-verified: 2026-05-23 binary @ 0x0016911c (re-analyst)
void Fruit::MoveFruitZPositionToBack(Fruit* f) {
    f->m_ZPosition = (500.0f + f->m_ZPosition) * 0.5f - 2600.0f;
}

// ============================================================
// Chunk B: small helpers
// ============================================================

// Binary @ 0x00176184 — local-MP "did a player drop their last life" check.
// Port: FN::GameOver is wired; the multi-player player-count gate is not yet
// ported (GetNumActiveForPlayer doesn't filter by player yet). Stub the
// per-player gating and call GameOver when count hits zero for player 0.
void Fruit::CheckFruitDropped() {
    // Binary @ 0x00176184: per-player live-count check uses MissControl/HeartControl
    // lives counters at iVar1+4/+8, NOT per-fruit active counts. The per-fruit count
    // is computed on-demand via GetNumActiveForPlayer(idx, true). Single-player path:
    // count INACTIVE fruits == 0 means the wave is clear, which maps to byPlayerMode=false.
    if (GetNumActiveForPlayer(0, false) == 0) {
        FN::GameOver(-1, -1.0f, -1);
    }
}

// Binary @ 0x00175624 — gravity-axis projection offscreen check.
// DAT_001756d4=160.0f (Y-bound base), DAT_001756d8=240.0f (X-bound base),
// DAT_001756d0=50.0f (margin scale per scale.y).
// m_SecondPos is checked unconditionally (no m_bSliced gate).
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

// Binary @ 0x00176354
void Fruit::EnableCollision(bool enable) {
    if (enable) {
        const FruitInfoData* info = FruitInfo_Get(m_FruitType);
        const float fScale   = info ? info->m_Scale          : 25.0f;
        const float fColBase = info ? info->m_CollisionScale : 1.0f;
        const float radius   = fColBase + COL_RADIUS_FACTOR * fScale;
        if (!m_Col) m_Col = new ColSphere();
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center() = Vec3(pos.x, pos.y, 0.0f);
        cs->radius = radius;
    } else {
        delete m_Col;
        m_Col = nullptr;
    }
}

// Binary @ 0x00175b78
void Fruit::SetForPlayer(int playerIdx) {
    m_PlayerIdx = (uint32_t)playerIdx;
    // Defunct: online-mp — P2 collision radius *= 0.66; binary @ 0x00175b78
    // Mortar::NetworkManager::GetInstance().IsOnlineMultiplayer() is always false in port.
    if (Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer()) {
        if (playerIdx == 1 && m_Col) {
            static_cast<ColSphere*>(m_Col)->radius *= 0.66f;
        }
    }
}

// Binary @ 0x001761d8 — virtual Mortar::Entity::Release override.
// Called by Mortar::ActorManager teardown before the destructor.
void Fruit::Release() {
    if (m_pEmitter1) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter1);
        m_pEmitter1 = nullptr;
    }
    if (m_pEmitter2) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter2);
        m_pEmitter2 = nullptr;
    }
    if (m_pOwner) {
        Fruit** ownerSlot = reinterpret_cast<Fruit**>(
            reinterpret_cast<char*>(m_pOwner) + 0x14C);
        if (*ownerSlot == this) {
            *ownerSlot = nullptr;
        }
        m_pOwner = nullptr;
    }
    Mortar::Entity::Release();
}

// ============================================================
// Chunk C: GetFact + SetTrailParticles
// ============================================================

// Binary @ 0x00175ba4 — fact-of-the-day picker with save-data round-robin
// and exclude-special-fruits remap.
const char* Fruit::GetFact(int* outType, int* outFactIdx, int fruitType, int factIdx) {
    const int count = FruitInfo_GetCount();
    if (count <= 0) return nullptr;

    // Remap fruitType: skip special-fruit entries (m_bSpecial != 0).
    // Binary walks the array and builds a non-special subset for indexing.
    int ft = fruitType;
    if (ft < 0 || ft >= count) ft = 0;

    // Advance to a fruit that has facts.
    int attempts = 0;
    while (attempts < count) {
        const FruitInfoData* info = FruitInfo_Get(ft);
        if (info && !info->m_bSpecial && info->m_FactCount > 0) break;
        ft = (ft + 1) % count;
        ++attempts;
    }

    const FruitInfoData* chosen = FruitInfo_Get(ft);
    if (!chosen || chosen->m_FactCount <= 0) return nullptr;

    // ASM-verified: 2026-05-20 binary @ 0x00175c8c..0x00175cd4 (re-analyst)
    // Fact-tracking AddToTotal pair drives deterministic fact rotation (NOT Rand32):
    // per-fruit `<Name>_facts` count modulo m_FactCount picks the index.
    // Both calls use trackSession=true, unlockAchievement=true.
    int fi = factIdx;
    if (fi < 0) {  // pick-random path
        if (game_work.m_SaveData) {
            static const uint32_t hFactsGlobal = StringHash("_facts");
            game_work.m_SaveData->AddToTotal("_facts", hFactsGlobal, 1, true, true);

            char buf[64];
            snprintf(buf, sizeof(buf), "%s_facts", chosen->m_Name);
            int newTotal = game_work.m_SaveData->AddToTotal(buf, StringHash(buf), 1, true, true);
            fi = (newTotal - 1) % chosen->m_FactCount;
        } else {
            fi = 0;
        }
    } else {
        fi = fi % chosen->m_FactCount;
    }

    if (outType)    *outType    = ft;
    if (outFactIdx) *outFactIdx = fi;

    // fruitlist.xml stores localisation keys (e.g. "FRUIT_FACT_07") in
    // <fact> elements; resolve via StringTable so the caller gets the
    // translated paragraph, not the raw key.
    const char* key = chosen->m_pFacts ? chosen->m_pFacts[fi] : nullptr;
    if (!key) return nullptr;
    return Mortar::GETSTRING_CAST_0_STR(key);
}

// Binary @ 0x001756dc — replace m_pEmitter1 with a custom trail emitter.
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
    m_pEmitter1 = pm.AddEmitter((uint32_t)emitterHash, nullptr, /*persistent=*/true);
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    return m_pEmitter1 != nullptr;
}

// ============================================================
// Chunk D: UpdateBombAvoidance + DestroyFruitModels
// ============================================================

// Binary @ 0x001db190 — push bombs away from this fruit on the X axis.
// ASM-verified: re-analyst 2026-05-16 confirmed DAT_00175a5c=4900.0f,
// DAT_00175a60=56.25f, multiplier=12.0f; dist check is MagnitudeSqr(diff)<4900.
// Binary re-fetches ActorManager::GetInstance() each iteration. No null guard.
void Fruit::UpdateBombAvoidance(float dt) {
    if (m_bSliced != 0) return;

    std::list<Mortar::Entity*>::iterator it;
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    while (e) {
        Bomb* bomb = static_cast<Bomb*>(e);
        if (bomb->IsActive() && bomb->m_Col != NULL) {
            Vec3 diff = bomb->pos - pos;
            if (diff.MagnitudeSqr() < 4900.0f) {   // DAT_00175a5c = 4900.0 (70^2)
                float dvx = vel.x - bomb->vel.x;
                float dvy = vel.y - bomb->vel.y;
                if (dvx * dvx + dvy * dvy < 56.25f) {   // DAT_00175a60 = 56.25 (7.5^2)
                    float dir = (diff.x < 0.0f) ? -1.0f : 1.0f;
                    bomb->vel.x += dir * dt * 12.0f;
                }
            }
        }
        am = Mortar::ActorManager::GetInstance();
        e = am->GetEntityNext(1, it);
    }
}

// Binary @ 0x0017911c — releases the FruitModelInfo[] array.
void Fruit::DestroyFruitModels() {
    // Release slice-effect globals
    s_SliceFxModel = Mortar::SmartPtr<Mortar::Model>();
    s_SliceFxCritModel = Mortar::SmartPtr<Mortar::Model>();

    // Release raw allocation (header + elements) with proper destructor calls
    if (s_FruitModelsRaw) {
        const int count = s_FruitModelCount;
        for (int i = 0; i < count; ++i) {
            s_FruitModels[i].~FruitModelInfo();
        }
        ::operator delete(s_FruitModelsRaw);
        s_FruitModelsRaw = nullptr;
        s_FruitModels = nullptr;
        s_FruitModelCount = 0;
    }
}

// ============================================================
// Port specific: binary reads g_Game+0x4 for SSM flag; port re-derives.
// TODO: implement full IsSameScreenMultiplayer when gameMode bitmask is further RE'd.
static bool Fruit_IsSameScreenMultiplayer() {
    return false;
}

// ASM-verified: 2026-05-20 binary @ 0x00175ea0 (re-analyst).
// SSM Player 2 swaps shadow-offset axes; offset rotates 90 deg and the
// sign follows the fruit's screen-half (pos.x < 0 -> negative).
void Fruit::AddShadow(QUADCUSTOMVERTEX** out, int* outCount) {
    float mirrorX = 1.0f;
    float mirrorY = 0.0f;   // DAT_00176160
    if (m_PlayerIdx >= 1 && Fruit_IsSameScreenMultiplayer()) {
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
