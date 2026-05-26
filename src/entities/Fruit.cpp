#include "Fruit.h"
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
#include "game/FruitSaveData.h"
#include "util/StringHash.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "math/math3d.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "game/GameWork.h"

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

Fruit::Fruit()
    : m_FruitType(0)
    , m_bNoPowerUp(0)
    , m_pEmitter1(nullptr)
    , m_pEmitter2(nullptr)
    , m_SlicePos(0, 0, 0)
    , m_LifetimeCounter(0)
    , m_CollisionSize(0)
    , m_VestigialInitFour(0)
    , m_SliceTimer(-1.0f)
    , m_SliceAngle(0)
    , m_SliceImpulse(0.0f)
    , m_SliceState(0)
    , m_bActive(0)
    , m_ChuckDelay(0.0f)
    , m_RotAxis(0, 0, 0)
    , m_PlayerIdx(0)
    , m_TimeScale(1.0f)
    , m_ZPosition(0.0f)
    , m_Gravity(0, -12.0f, 0)
    , m_bSliced(0)
    , m_SecondPos(0, 0, 0)
    , m_SecondVel(0, 0, 0)
    , m_pSlasher(nullptr)
    , m_bSpawnedByCriticalSplash(0)
    , m_bCriticalEligible(0)
    , m_ScaleAnim(0.0f)
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
// Binary @ 0x00176708 — vtable slot 2. p2=fruitType; p3=scale (nullable).
void Fruit::Init(void* /*p1*/, long fruitType, Vec3* /*scaleOrNull*/) {
    // Binary @ 0x00176708: range-check fruitType; out-of-range falls back to RandomFruit(true).
    if (fruitType >= 0 && fruitType < (long)FruitInfo_GetCount()) {
        m_FruitType = (uint8_t)fruitType;
    } else {
        m_FruitType = (uint8_t)RandomFruit(true);
    }
    m_LifetimeCounter = 0;
    m_bActive = 1;
    LOG_INFO("FRUIT", "m_bSliced=0 set on entity=%p pos=(%.1f,%.1f) type=%d (in Init)",
             static_cast<void*>(this), pos.x, pos.y, (int)m_FruitType);
    m_bSliced = 0;
    m_bDrawWhole = 0;
    m_bCriticalEligible = 1;
    m_bSpawnedByCriticalSplash = 0;
    m_bNoPowerUp = 0;
    m_pSlasher = nullptr;
    m_TrackerID = 0;
    m_ScaleAnim = 0.0f;
    m_ChuckDelay = 0.0f;
    m_PlayerIdx = 0;
    m_TimeScale = 1.0f;
    m_CollisionSize = 75;         // binary @ 0x00176708: str r3, [r0, #0x4b] = 0x4B
    m_SliceState = 0;             // binary @ 0x00176708
    m_VestigialInitFour = 4;      // binary @ 0x00176708: write-only dead field
    // ASM-verified: 2026-05-26 binary @ 0x00176708 (re-analyst)
    // Clears ENT_KILLED (0x10) and sets bit 0x02. Bit 0x02 is currently
    // mis-labelled in EntityFlagBits (declared 0x04); leave as literal until
    // EntityFlagBits is re-audited.
    // TODO: re-RE EntityFlagBits to name bit 0x02 correctly.
    flags = (flags & ~0x10) | 0x02;

    m_ZPosition = GetFruitZPosition();

    // Reset slice state (binary Fruit::Init — m_SliceTimer = -1).
    m_SliceTimer   = -1.0f;
    m_SliceAngle   = 0;
    m_SliceImpulse = 0.0f;
    m_SlicePos     = Vec3(0, 0, 0);
    m_pEmitter1    = nullptr;
    m_pEmitter2    = nullptr;

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

    // Rotation axis offset.
    // Binary Fruit::Init @ 0x00176708 reads *globalConfigVec3 (GOT 0x001f4328);
    // BSS Vec3 initialised by _GLOBAL__I_Fruit.cpp to (0,0,0).
    m_RotAxis = Vec3(0.0f, 0.0f, 0.0f);

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
        m_VisualScale = scale;  // ASM-verified: 2026-05-18 binary @ 0x00176290 (re-analyst)

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
        cs->center = Vec3(pos.x, pos.y, 0.0f);
        cs->radius = radius;
    }

    // Defunct: arcade-blitz duplicate-guard — no-op stub; binary @ 0x0017685c
    // Binary: __cxa_guard-protected FruitType("BOMB_PINEAPPLE", false) lookup +
    // collision loop to prevent duplicate pineapple bombs on screen at once.
    // Dead on this platform — pineapple blitz mode was removed.

    // Defunct: online-MP — no-op stub; binary @ 0x00176708 +0x1b8
    // Binary: BOMB_PINEAPPLE count decrement for online multiplayer sync packet.
    // Dead on this platform — P2P online MP was removed.

}

// ASM-verified: 2026-05-27 binary @ 0x00175a64 (re-analyst)
// Binary semantics: cache pos into m_SecondPos, clamp negative delay to
// 0.125, set m_ChuckDelay. NO flags write, NO m_ScaleAnim write,
// NO s_FruitThrowSfxFired reset -- those belong in Init.
void Fruit::Chuck(float delay) {
    m_SecondPos = pos;
    if (delay < 0.0f) delay = 0.125f;
    m_ChuckDelay = delay;
    // TODO: relocate s_FruitThrowSfxFired reset -- binary resets it per-frame
    //   at global+0x48, not inside Chuck. Kept here temporarily to preserve
    //   per-launch SFX gating until the per-frame reset site is RE'd.
    s_FruitThrowSfxFiredThisFrame = false;
    // TODO: 0x00175a64 +abort -- power-fruit no-power-up abort path
    //   (decrement g_PowerFruitCount + set flags |= 0x10) when
    //   info->m_pPowers != null && (waveTimer - delay < 8.0f) &&
    //   info->m_pPowers->m_NameHash != StringHash("Throw-fruit").
    //   Needs WaveManager+0x10c (waveTimer) + FruitInfo+0x32c (m_pPowers)
    //   + StringHash of "Throw-fruit" resolved first.
}

// ASM-verified: 2026-05-27 binary @ 0x00177680 (re-analyst) -- m_TimeScale applied to integration dt
void Fruit::Update(float dt) {
    // Binary @ 0x00177680: dtScaled = dt * m_TimeScale; all integration uses dtScaled.
    const float dtScaled = dt * m_TimeScale;
    const float dtNorm   = dtScaled * 60.0f;  // equivalent to dtScaled / (1/60)

    // Binary @ 0x00177680: outer gate is !m_bSliced vs sliced — no early
    // returns before the branch. IsActive() and m_ChuckDelay checks live
    // INSIDE the unsliced path only.
    if (!m_bSliced) {
        // === UNSLICED FRUIT ===
        if (!IsActive()) return;

        // Launch delay (unsliced path only)
        // ASM-verified: 2026-05-23 binary @ 0x00177866 (re-analyst) -- "Throw-fruit" SFX
        // fires on negative-going edge of m_ChuckDelay crossing 0.2f, gated by
        // s_FruitThrowSfxFiredThisFrame (mirrors *(g_fruitGlobal+0x48) in binary).
        if (m_ChuckDelay > 0.0f) {
            static const float THROW_FRUIT_SFX_THRESHOLD = 0.2f;  // DAT_00177960+0x48 threshold
            const float prevChuckDelay = m_ChuckDelay;
            m_ChuckDelay -= dtScaled;
            if (prevChuckDelay >= THROW_FRUIT_SFX_THRESHOLD && m_ChuckDelay < THROW_FRUIT_SFX_THRESHOLD) {
                if (!s_FruitThrowSfxFiredThisFrame) {
                    s_FruitThrowSfxFiredThisFrame = true;
                    if (game_work.mGameSound)
                        game_work.mGameSound->SFXPlay("Throw-fruit", 1.0f, 1.0f);
                }
            }
            if (m_ChuckDelay > 0.0f) return;
            m_ChuckDelay = 0.0f;
        }

        // Scale animation (0 → 1 over ~0.3s)
        if (m_ScaleAnim < 1.0f) {
            m_ScaleAnim += dtScaled * 3.0f;
            if (m_ScaleAnim > 1.0f) m_ScaleAnim = 1.0f;
        }

        // ASM-verified: 2026-05-09 binary @ 0x00177bb8..0x00177c1e (asm-inspector)
        // Binary integration (Fruit::Update 0x00177680):
        //   pos += (vel*dtScaled + 0.5*g*dtScaled²) * 60.0    (DAT_00177d00 = 60.0)
        //   vel += gravity * dtScaled
        //   pos += m_RotAxis * dtScaled                 (NO ×60 here — binary uses
        //                                                dt @ sp+0x1c scaled by m_TimeScale)
        const float POS_INTEGRATION_SCALE = 60.0f;  // DAT_00177d00
        Vec3 step = (vel * dtScaled + m_Gravity * (0.5f * dtScaled * dtScaled)) * POS_INTEGRATION_SCALE;
        pos += step;
        vel += m_Gravity * dtScaled;

        // Rotation axis drift — dtScaled, no ×60.
        pos += m_RotAxis * dtScaled;

        // Backup for future split
        m_SecondPos = pos;
        m_SecondVel = vel;

        // Slice-timer countdown — set positive by OnSliced, triggers
        // the actual split when it hits 0. Matches binary Fruit::Update
        // @ 0x177680 phase 4.
        if (m_SliceTimer > 0.0f) {
            m_SliceTimer -= dtScaled;
            if (m_SliceTimer <= 0.0f) {
                m_SliceTimer = 0.0f;
                Slice();
            }
        }
    } else {
        // ASM-verified: 2026-05-09 binary @ 0x00177736..0x001777a0 (asm-inspector)
        // === SLICED (two halves) ===
        // Binary integration order:
        //   normalize(m_Gravity); gravLen += DAT_00177950(=0.2) * dtNorm * 4.5
        //   = gravLen += 0.9 * dtNorm   (per-frame growth at 60fps)
        //   m_Gravity *= new_gravLen / old_gravLen (rescale unit vec)
        //   vel        += m_Gravity * dtScaled   (gravity uses dtScaled)
        //   m_SecondVel += m_Gravity * dtScaled
        //   pos        += vel        * dtScaled * 60  (position uses dtNorm)
        //   m_SecondPos += m_SecondVel * dtScaled * 60
        // Both halves get the same ×60 position scale as the unsliced branch.
        // ASM-verified: 2026-05-20 binary @ 0x001777a8..0x001777ee (re-analyst) -- gravity grow gated.
        if (m_bCriticalEligible == 0) {
            float len = m_Gravity.Normalise();   // unit-izes, returns old magnitude
            m_Gravity *= len + 0.9f * dtNorm;
        }
        if (m_bSpawnedByCriticalSplash != 0) {
            float len = m_Gravity.Normalise();
            m_Gravity *= len + 1.3f * dtNorm;
        }

        // Two-body physics — same ×60 position scale as unsliced.
        const float POS_INTEGRATION_SCALE = 60.0f;
        vel        += m_Gravity * dtScaled;
        m_SecondVel += m_Gravity * dtScaled;
        pos        += vel        * dtScaled * POS_INTEGRATION_SCALE;
        m_SecondPos += m_SecondVel * dtScaled * POS_INTEGRATION_SCALE;

        // Scale grow only when not drawing whole (binary gates this on !m_bDrawWhole).
        if (!m_bDrawWhole) {
            if (m_ScaleAnim < 1.0f) {
                m_ScaleAnim += dtScaled * 3.0f;
                if (m_ScaleAnim > 1.0f) m_ScaleAnim = 1.0f;
            }
        }
    }

    // Quaternion rotation update (both halves). Matches binary Fruit::Update
    // @ 0x00177680: each axis is a CreateFromAxisAngle with 16-bit angle
    //   idx = (ushort)(int)(rotVel * dtNorm * 182.0)   // DAT_00177ff0 = 182
    // then m_Rot = m_Rot * qx * qy * qz.
    // In radians that is rotVel * dtNorm * (182 * 2pi / 65536) ≈
    // rotVel * dtNorm * (pi / 180) — i.e. one degree per unit of rotVel per
    // 60fps frame. The old port used 0.01 here, rotating fruits ~57% as
    // fast as the binary.
    const float ANGLE_PER_UNIT = 182.0f * 6.2831853f / 65536.0f;  // ~pi/180
    const float rotScale = dtNorm;  // dtScaled * 60.0 (DAT_0017794c = 1/60)
    {
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0),
            m_RotVel1.x * rotScale * ANGLE_PER_UNIT);
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0),
            m_RotVel1.y * rotScale * ANGLE_PER_UNIT);
        Quaternion qz = Quaternion::FromAxisAngle(Vec3(0, 0, 1),
            m_RotVel1.z * rotScale * ANGLE_PER_UNIT);
        m_Rot1 = (m_Rot1 * qx * qy * qz).normalized();
    }
    {
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1, 0, 0),
            m_RotVel2.x * rotScale * ANGLE_PER_UNIT);
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0, 1, 0),
            m_RotVel2.y * rotScale * ANGLE_PER_UNIT);
        Quaternion qz = Quaternion::FromAxisAngle(Vec3(0, 0, 1),
            m_RotVel2.z * rotScale * ANGLE_PER_UNIT);
        m_Rot2 = (m_Rot2 * qx * qy * qz).normalized();
    }

    // Update collision sphere center (z forced to -0.5f per binary DAT_00177fec).
    // Binary @ 0x00177f12: writes pos.x, pos.y, then overwrites z with -0.5f.
    if (m_Col) {
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center.x = pos.x;
        cs->center.y = pos.y;
        cs->center.z = -0.5f;  // DAT_00177fec (binary @ 0x00177f12)
    }

    // ASM-verified: 2026-05-18 binary @ 0x00177f30..0x00177f42 (re-analyst).
    // Pause-detach: when scaled dt is zero (paused or m_TimeScale==0),
    // release the juice emitters so they stop tracking the fruit's
    // position while frozen. Re-armed on next slice/SetTrailParticles.
    if (dtScaled == 0.0f) {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        if (m_pEmitter1) { pm.ClearEmitter(m_pEmitter1); m_pEmitter1 = nullptr; }
        if (m_pEmitter2) { pm.ClearEmitter(m_pEmitter2); m_pEmitter2 = nullptr; }
    }

    // Track juice emitters with the two halves so particles follow the
    // pieces instead of spraying from the original slice point. Matches
    // binary Fruit::Update @ 0x177680 tail section.
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    if (m_pEmitter2) m_pEmitter2->m_Pos = m_SecondPos;

    if (CheckHasGoneOffscreen()) {
        KillFruit(true);
    }

    // Lifetime counter tick — Update tail (binary @ 0x17a16+).
    m_LifetimeCounter += (int)(1000.0f * dtScaled);
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
//   m_RotAxis *= 0.9                                    // DAT_0017519c damping
//   if (!m_bSliced && m_ChuckDelay <= 0) {
//     if (m_Gravity.x == 0) {                            // vertical-gravity fruit
//       if (arcade && (s_ModPowerMask & 0x20)) { hard bounce x on +-192 }
//       else                                   { soft nudge x toward centre }
//     } else if (m_Gravity.y == 0) {                     // horizontal-gravity fruit
//       soft nudge y toward centre on +-128
//     }
//   }
//
// Bounds resolved from binary: X = ±192 (DAT_001751a0 / 751a4),
// Y = ±128 (DAT_001751a8 / 751ac). Push / rotAxis magnitudes from
// the disassembly: vel += ±16*dt, rotAxis += ±20 (NO dt scaling on
// rotAxis — accumulates per-frame, equilibrium ~200 against the
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

    m_RotAxis *= ROT_AXIS_DAMPING;

    if (m_bSliced) return;
    if (m_ChuckDelay > 0.0f) return;

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
                vel.x       += dt * PUSH_VEL;
                m_RotAxis.x += PUSH_ROT;
            }
            if (pos.x > BOUND_X_HI) {
                vel.x       += dt * -PUSH_VEL;
                m_RotAxis.x -= PUSH_ROT;
            }
        }
    } else if (m_Gravity.y == 0.0f) {
        // Horizontal-gravity fruit — soft nudge on Y bounds.
        if (pos.y < BOUND_Y_LO) {
            vel.y       += dt * PUSH_VEL;
            m_RotAxis.y += PUSH_ROT;
        }
        if (pos.y > BOUND_Y_HI) {
            vel.y       += dt * -PUSH_VEL;
            m_RotAxis.y -= PUSH_ROT;
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
    (void)r;
#ifndef __bada__
    if (!IsActive() || m_ChuckDelay > 0.0f) return;

    const FruitModelInfo* fmi = GetFruitModelInfo(m_FruitType);
    if (!fmi || !fmi->m_Whole.IsValid()) return;

    float s = scale.x * m_ScaleAnim;
    if (s <= 0.0f) return;

    // Position in binary-centred ortho space.
    // See docs/engine/coordinate-system.md and FruitCamera::SetupPerspective.
    // m_bDrawWhole forces the whole-fruit branch even when m_bSliced is
    // set — used by ClearMenuItems @ 0x0016ac7c when releasing menu
    // fruits during the dojo transition (the fruit flies off as a
    // single object rather than splitting in two).
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
#endif
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

    // Matches Fruit::KillFruit cleanup tail (binary @ 0x00176c8e..0x00176cea).
    // 1. Clear slasher's back-pointer if it still points at us.
    // ASM-verified: 2026-05-03 binary @ 0x00176c8e..0x00176cea (asm-inspector)
    if (m_pSlasher && m_pSlasher->m_pCurrentTarget == this) {
        m_pSlasher->m_pCurrentTarget = nullptr;
    }
    // 2. Decrement g_PowerFruitCount on natural-expiry path (flag 0x10 not yet set)
    //    AND for power-fruits (info->m_pPowers != nullptr).
    //    Binary @ 0x00176cc8..0x00176cd4: unconditional store of 0 when count<=1
    //    else (count-1). Port previously used conditional decrement which pinned
    //    the counter at 1 across multiple natural expirations.
    if (!(flags & 0x10)) {
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

    flags |= 0x10;
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
int Fruit::CollisionResponse(Mortar::Entity* /*hitter*/,
                              unsigned long /*flagsA*/,
                              unsigned long /*flagsB*/,
                              Vec3* bladeVelPtr) {
    LOG_INFO("FRUIT", "CollisionResponse entry: entity=%p pos=(%.1f,%.1f) type=%d bSliced=%d",
             static_cast<void*>(this), pos.x, pos.y, (int)m_FruitType, (int)m_bSliced);
    // Guard: already sliced or slice timer is positive -> double-hit.
    if (m_bSliced || m_SliceTimer > -1.0f) return 1;
    const Vec3& bladeVel = bladeVelPtr ? *bladeVelPtr : Vec3(0, 0, 0);

    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    const bool isSpecial  = (info && info->m_Score == 0x32);

    // ASM-verified: 2026-04-29T00:00Z binary @ 0x001780f0 (asm-inspector)
    // Critical-hit eligibility ladder (binary @ 0x001780f0..0x001781e8).
    // All gates must pass; on success roll Rand32(reroll) -- 0 == hit.
    m_bCriticalEligible = false;

    // ASM-verified: 2026-05-20 binary @ 0x00178154/0x001781d4 (re-analyst).
    // kCritScoreBound and kCritResetBase are GOT-indirect int32 globals.
    // DAT_001784fc -> GOT[0x7674] -> *0x001f3e34 = 5
    // DAT_00178504 -> GOT[0x77c8] -> *0x001f3e38 = 30
    // Used as: bound = min(m_ScoreThreshold, 5); on crit hit: m_ScoreThreshold = 30 + 5 = 35.
    static const int kCritScoreBound = 5;   // DAT_001784fc
    static const int kCritResetBase  = 30;  // DAT_00178504

    // FruitInfo +0x318 is m_bScorable: 1 = can receive critical hit.
    const bool canCritFruit = info && info->m_bScorable;

    // ASM-verified: 2026-05-20 binary @ 0x001780f0 (re-analyst).
    // Critical-hit ladder gates on game_work fields at +0x05 (m_LevelTransitionFlag)
    // and +0x10 (m_BombHitTimer) -- the same "non-interactive cinematic" pair used
    // by GameOver, bomb-hit, level-transition. Previously mislabelled as "frenzy"
    // gating, but it's just the existing transition-gate + bomb-hit-timer pair.
    const int score = game_work.currentScore;

    if (score >= 2
        && canCritFruit
        && game_work.m_LevelTransitionFlag == 0   // +0x05
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
                m_bCriticalEligible = true;
                thresh = kCritResetBase + kCritScoreBound;
            }
        }
    }

    const bool isCritical = m_bCriticalEligible;

    // Blade speed clamp. Critical / special → [6, 8]; normal → [4, 8].
    float bladeSpeed = bladeVel.Magnitude() * SLICE_BLADE_SCALE;
    const float clampMin = (isCritical || isSpecial)
                           ? 6.0f : SLICE_CLAMP_MIN_NRM;
    if (bladeSpeed < clampMin)          bladeSpeed = clampMin;
    if (bladeSpeed > SLICE_CLAMP_MAX)   bladeSpeed = SLICE_CLAMP_MAX;

    // Slice timer — base 0.03, critical × 2.5 (slow), special × 0.5 (fast).
    float sliceTimer = SLICE_TIMER_BASE;
    if (isCritical)      sliceTimer *= 2.5f;
    else if (isSpecial)  sliceTimer *= 0.5f;

    m_SliceTimer   = sliceTimer;
    m_SliceImpulse = bladeSpeed;
    m_SlicePos     = pos;
    // Atan2Idx: 16-bit angle index (65536 = 360°). Port uses std atan2
    // + the same scale factor that Atan2Idx produces.
    const float rad = atan2f(bladeVel.x, bladeVel.y);
    m_SliceAngle   = (uint16_t)((int)(rad * (65536.0f / 6.2831853f)) & 0xFFFF);

    // Menu-fruit fast-path: skip emitters / particles / SliceEffect / score /
    // save totals. Slice-state writes above are sufficient for the next
    // Fruit::Update tick to call Slice() -> m_bSliced=1 -> MenuButton::Update
    // detects the transition and fires the click callback (Retry/Quit/etc).
    //
    // Binary @ 0x001780b0 suppresses score via GameTaskState+0x05 byte gate
    // (set during menu-mode runtime) -- port's GameTaskState layout has
    // pPauseScreen at +0x04 swallowing the +0x05 byte, so we can't replicate
    // that gate cleanly today. This fast-path matches the net binary outcome
    // for menu-fruit slices: slice state set, score NOT added.
    // TODO: 0x001780b0 -- replicate the GameTaskState+0x05 gate properly
    // once the GameTaskState struct layout is fixed.
    if (m_bSpawnedByCriticalSplash != 0) {
        return 0;
    }

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
    // emitter's m_ScaleY / m_field30 pair encodes (cos, sin) of the rotation
    // applied to each spawned particle's initial velocity — matches binary
    // AddParticle 0x00115644. Negative-angle sign flip mirrors the binary:
    //   e->m_CosAngle =  CosIdx(-sliceAngle);
    //   e->m_SinAngle = -SinIdx(-sliceAngle);  = SinIdx(sliceAngle)
    if (info) {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        const float sliceRad = (float)(int16_t)m_SliceAngle *
                               (6.2831853f / 65536.0f);
        PSPParticleEmitter* eHit = pm.AddEmitter(
            info->m_NameHash, nullptr, /*persistent=*/false);
        if (eHit) {
            eHit->m_Pos      = pos;
            eHit->m_ScaleY   =  cosf(sliceRad);   // cos θ
            eHit->m_field30  =  sinf(sliceRad);   // sin θ
        }

        // Persistent juice emitters — one per future half. m_SlicedHash
        // resolves to "<name>_sliced" (e.g. "apple_sliced").
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

    // Full-screen tint flash. Critical uses the configured crit colour
    // (gold/yellow); special-fruit uses half-alpha white. Matches
    // CriticalFlash @ 0x0016a9a4.
    if (isCritical) {
        FN::CriticalFlash(pos, Colour(255, 215, 0, 192));
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
    // CollisionResponse at 0x17821c. Binary builds sliceInfo as:
    //   x = m_SliceAngle / -182.0 + 90.0   (degrees-offset)
    //   y = bladeSpeed * 0.4                (impulse length)
    const float sliceAngleDeg = (float)(int16_t)m_SliceAngle / -182.0f + 90.0f;
    const float sliceLength   = bladeSpeed * 0.4f;
    FN::SliceEffect_Add(pos, sliceAngleDeg, sliceLength, isCritical);

    // Matches CollisionResponse score+save dispatch (binary @ 0x00178c3c).
    // ASM-verified: 2026-05-10 binary @ 0x00178bc8..0x00178e30 (re-analyst).
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
    if (info) {
        Game* g = Game::GetInstance();
        if (g) {
            int score = info->m_Score;
            if (m_bCriticalEligible) score += 5;
            if (info->m_CoinsMax > 0 && info->m_CoinsMin < info->m_CoinsMax) {
                const uint32_t range = (uint32_t)(info->m_CoinsMax - info->m_CoinsMin);
                score = info->m_CoinsMin
                      + (int)WaveManager::GetInstance()->GetRandom().Rand32(range);
            }
            if (m_bCriticalEligible) score *= 2;  // g_CritScoreMul / 2 = 2
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
    }
    // ASM-verified: 2026-05-20 binary @ 0x00178b40..0x00178c34 (re-analyst)
    // Arcade-mode-only (NOT Zen as a prior TODO claimed):
    //   AddToSpeedLossTime(0.05f, 0)             -- SpeedControl HUD tick refresh.
    //   first_fruit = sticky write-once          -- records m_FruitType+1 of first
    //                                               slice ever (savefile-wide).
    //   last_fruit  = set to current m_FruitType+1 via delta math (total := newVal).
    if (Game::GetInstance() && game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
        WaveManager::GetInstance()->AddToSpeedLossTime(0.05f, 0);
        if (game_work.m_SaveData && info) {
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

    // ASM-verified: 2026-05-22 binary @ 0x001780b0 ~+0x360 (re-analyst).
    // Powerup-fruit slice activates the modifier polymorphism chain. Without
    // this call, no Freeze/Frenzy/x2/Blitz effects ever fire in Arcade.
    // ASM-verified: 2026-05-22 binary @ 0x00178c30 (re-analyst).
    // Powerup-fruit slice fires either during normal gameplay (LTF==0) OR
    // inside the bomb-hit cinematic window (LTF in {2,3} = HitBomb-set state
    // AND the timer is in (-0.1f, 0.95f)). The binary's compound check is
    // `LTF == 0 || ((LTF - 2u) < 2u && timer < 0.95f && timer > -0.1f)`.
    static const float kBombHitMax = 0.95f;
    static const float kBombHitMin = -0.1f;
    const uint8_t ltf = (uint8_t)game_work.m_LevelTransitionFlag;
    const bool bombHitWindow = (uint8_t)(ltf - 2u) < 2u
        && game_work.m_BombHitTimer < kBombHitMax
        && game_work.m_BombHitTimer > kBombHitMin;
    if (info && info->m_pPowers && !m_bNoPowerUp
        && (ltf == 0 || bombHitWindow)) {
        uint32_t hash = info->m_pPowers->RandomPower();
        Vec3 localPos = pos;
        PowerUpManager::GetInstance()->ActivatePower(hash, &localPos, reinterpret_cast<float*>(&localPos));
    }

    // Combo counter increment — binary @ 0x001787a8..0x001787b0.
    // ASM-verified: 2026-05-02 binary @ 0x00178708 reads m_PlayerIdx (+0x90).
    int slasher = (int)m_PlayerIdx;
    if (g_LastSlasher != slasher) {
        g_ComboCount  = 0;   // binary @ 0x0017873a: different-player branch
        g_LastSlasher = slasher;
    }
    g_ComboCount += 1;       // binary @ 0x001787b0
    return 0;
}

// Matches Fruit::Slice (0x176d58), now with the binary's flipSide
// logic, special-fruit ×1.5 impulse, and spin-boost loop on both
// halves.
void Fruit::Slice() {
    m_SliceTimer = 0.0f;

    // --- flipSide determination ---
    // Binary: rotate (0,0,1) by current m_Rot1, compare XY direction
    // against m_SliceAngle via GetSmallestDelta. If the rotated Z axis
    // points away from the slice direction, flip the halves' angles.
    Vec3 slicePlane(0, 0, 1);
    // Approximate: m_Rot1.ToMatrix44() * (0,0,1) — just extract the
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
        float sliceAngleRad = (float)(int16_t)m_SliceAngle *
                              (6.2831853f / 65536.0f);
        // Wrap both into [-pi, pi] and take signed delta.
        float delta = rotAngleRad - sliceAngleRad;
        while (delta >  3.1415926f) delta -= 6.2831853f;
        while (delta < -3.1415926f) delta += 6.2831853f;
        if (delta < 0.0f) flipSide = true;
    }

    // --- Impulse ---
    float impulse = m_SliceImpulse;
    int   splatCount = (rand() % 2) + 2;   // Rand(2)+2 → 2 or 3

    // Critical hit gets 1.5× impulse + crit dual-line AddSlice.
    const bool isCritical = m_bCriticalEligible;
    if (isCritical) {
        // Binary: two slice lines at ±60° offset from the base angle.
        //   infoA.x = m_SliceAngle / -182.0 + 60.0
        //   infoB.x = m_SliceAngle / -182.0 - 60.0
        //   infoA/B.y = impulse * 0.4 * 0.7
        const float critBase = (float)(int16_t)m_SliceAngle / -182.0f;
        const float critLen  = impulse * 0.4f * 0.7f;
        FN::SliceEffect_Add(pos, critBase + 60.0f, critLen, true);
        FN::SliceEffect_Add(pos, critBase - 60.0f, critLen, true);
        impulse *= 1.5f;
        splatCount += 2;
    }

    // Special-fruit (baseScore == 0x32 = 50) also gets 1.5× impulse.
    const FruitInfoData* info = FruitInfo_Get(m_FruitType);
    if (info && info->m_Score == 0x32) {
        impulse *= 1.5f;
        splatCount += 2;
    }

    // --- Splat spawn ---
    // Per-splat speed = (impulse + rand(0.5)*impulse) * (i*0.2 + 5).
    // Per-splat angle = Rand16(0xFFF0).
    //
    // Binary uses raw impulse values directly (4..8 range from
    // CollisionResponse clamp). The port's Update integrates pos
    // with a ×60 fudge factor (matching binary 0x00177d00), which
    // means velocities should also stay in the binary's per-frame
    // scale — no extra ×50 multiplier needed here.
    const float imp_screen = impulse;
    for (int i = 0; i < splatCount; ++i) {
        const uint16_t angle16 = (uint16_t)(rand() & 0xFFF0);
        const float r          = ((float)rand() / (float)RAND_MAX) * 0.5f;
        const float speed      = (impulse + r * impulse) *
                                 ((float)i * 0.2f + 5.0f);
        const float a          = (float)angle16 * (6.2831853f / 65536.0f);
        Vec3 sv(sinf(a) * speed, cosf(a) * speed, 0.0f);

        SplatEntity* s = SplatEntity::GetFree();
        // Binary passes param3 = isCritical for crit splats (biases
        // MakeSplat's landing-type RNG toward types 4/5, the larger
        // variants).
        if (s) s->MakeSplat(pos, sv, isCritical, m_FruitType);
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
    // Binary uses sliceFactor = 1 - FRUIT_INFO[+0x24c]. That field
    // isn't in the port's FruitInfo struct yet — hardcode to 0.7
    // which maps to a per-fruit slice softness of 0.3.
    const float sliceFactor = 0.7f;

    // Port the biased "rand(0x5550) retry if < 0x2aa8" pattern.
    int _ra = rand() & 0x5550; if (_ra < 0x2aa8) _ra = rand() & 0x5550;
    int _rb = rand() & 0x5550; if (_rb < 0x2aa8) _rb = rand() & 0x5550;

    // Angle offsets for the two halves — bound by `(1-softness)*4`.
    const float randA = (float)_ra * (1.0f - 0.3f) * 4.0f;
    const float randB = (float)_rb * (1.0f - 0.3f) * 4.0f;
    const int16_t offA = (int16_t)randA;
    const int16_t offB = (int16_t)randB;

    // Binary @ 0x00177236 also writes back into m_SliceAngle when flipSide is set.
    if (flipSide) {
        m_SliceAngle = (uint16_t)(m_SliceAngle + 0x7ff8);
    }
    uint16_t base = m_SliceAngle;
    uint16_t angA = (uint16_t)(base - offB + 0x7ff8);  // halfA direction always +0x7ff8 from base
    uint16_t angB = (uint16_t)(base + offA);            // halfB direction == base + offA

    const float radA = (float)(int16_t)angA * (6.2831853f / 65536.0f);
    const float radB = (float)(int16_t)angB * (6.2831853f / 65536.0f);
    Vec3 dirA(sinf(radA), cosf(radA), 0.0f);
    Vec3 dirB(sinf(radB), cosf(radB), 0.0f);

    Vec3 halfVelA = dirA * (imp_screen * sliceFactor) +
                    vel  * (1.0f - sliceFactor);
    Vec3 halfVelB = dirB * (imp_screen * sliceFactor) +
                    vel  * (1.0f - sliceFactor);

    // Critical / special override — binary @ 0x0017737a..0x00177442 uses raw
    // m_SliceAngle (NOT the offset-baked radA) with ±0x3ffc, plus int32
    // truncation on each velocity component.
    if (isCritical || (info && info->m_Score == 0x32)) {
        const float critRadA = (float)(int16_t)(uint16_t)(m_SliceAngle + 0x3ffc) * (6.2831853f / 65536.0f);
        const float critRadB = (float)(int16_t)(uint16_t)(m_SliceAngle + 0xc004) * (6.2831853f / 65536.0f);
        halfVelA = Vec3((float)(int)(sinf(critRadA) * imp_screen),
                        (float)(int)(cosf(critRadA) * imp_screen), 0.0f) * 0.5f;
        halfVelB = Vec3((float)(int)(sinf(critRadB) * imp_screen),
                        (float)(int)(cosf(critRadB) * imp_screen), 0.0f) * 0.5f;
    }

    m_SecondPos = pos;
    m_SecondVel = halfVelA;
    vel         = halfVelB;

    MoveFruitZPositionToBack(this);

    LOG_INFO("FRUIT", "m_bSliced=1 set on entity=%p pos=(%.1f,%.1f) type=%d (in Fruit::Slice)",
             static_cast<void*>(this), pos.x, pos.y, (int)m_FruitType);
    m_bSliced = true;

    // Reset gravity so the ramp-up in Update starts fresh.
    m_Gravity = Vec3(0.0f, -12.0f, 0.0f);

    // --- Spin boost loop (matches Fruit::Slice 0x176d58 tail) ---
    //
    // For each half i in {0, 1}:
    //   sum = |rv[i].x| + |rv[i].y| + |rv[i].z|
    //   sum *= isCritical ? 2.0 : 0.5
    //   compA = sum * (rand(0.5) + 0.75)
    //   compB = sum * (rand(0.5) + 0.75)
    //   1/4 chance: oneBig = ±compA * 1.5
    //   else:       mix    = sum * (rand(0.3) - 0.1)
    //   sign-flip compA / compB by flipSide + iteration index
    //   new m_RotVel[i] = (picked x, picked y, -compB)
    //   m_Rot[i] reset to axis-angle composition aligned with slice angle
    for (int i = 0; i < 2; ++i) {
        Vec3* rv    = (i == 0) ? &m_RotVel1 : &m_RotVel2;
        Quaternion* q = (i == 0) ? &m_Rot1 : &m_Rot2;

        float mag = fabsf(rv->x) + fabsf(rv->y) + fabsf(rv->z);
        mag *= isCritical ? 2.0f : 0.5f;

        const float r1 = ((float)rand() / (float)RAND_MAX) * 0.5f + 0.75f;
        const float r2 = ((float)rand() / (float)RAND_MAX) * 0.5f + 0.75f;
        float compA = mag * r1;
        float compB = mag * r2;

        // Sign flip — binary uses two different dice rolls per branch.
        if (flipSide) {
            if ((rand() % 5) > 1) compA = -compA;
            if (i == 1)           compA = -compA;
            if (i == 0)           compB = -compB;
        } else {
            if ((rand() % 5) < 2) compA = -compA;
            if (i != 0)           compB = -compB;
            if (i == 0)           compA = -compA;
        }

        Vec3 newRotVel;
        if ((rand() % 3) == 0) {
            // 1/4 chance (rand() % 3 == 0 is actually 1/3): oneBig
            float big = compA * 1.5f;
            if (big < 0.0f) big = -big;
            newRotVel = Vec3(big, 0.0f, -compB);
        } else {
            const float mix = ((float)rand() / (float)RAND_MAX) * 0.3f - 0.1f;
            newRotVel = Vec3(mag * mix, compA, -compB);
        }
        *rv = newRotVel;

        // Reset m_Rot[i] to a composition:
        //   Qx(axis=(1,0,0), 90°) * Qy(axis=(0,1,0), 90°) * Qz(axis=(0,0,1), sliceAngle)
        // (Binary also passes impulse=0 literals; the port uses fixed
        // 90° for the first two and m_SliceAngle in radians for Z.)
        const float half = 1.5707963f * 0.5f;
        const float sliceRad = (float)(int16_t)m_SliceAngle *
                               (6.2831853f / 65536.0f) * 0.5f;
        Quaternion qx(sinf(half), 0.0f, 0.0f, cosf(half));
        Quaternion qy(0.0f, sinf(half), 0.0f, cosf(half));
        Quaternion qz(0.0f, 0.0f, sinf(sliceRad), cosf(sliceRad));
        *q = (qx * qy * qz).normalized();
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
// LoadFruitModels time and stores the pointer in the FRUIT_INFO
// header block at +0xc8. Port keeps it as a module-local vector
// sized once by LoadFruitModels.

static std::vector<FruitModelInfo> s_FruitModels;
static bool s_FruitModelsLoaded = false;

// Matches Fruit::LoadFruitModels (0x1794e0). Walks every FRUIT_INFO
// entry and loads `<name>_<c>_piece_1.mmd` + `_piece_2.mmd` via
// MeshManager. The format string was resolved from DAT_0017986c at
// 0x001bcd43: "models/Fruit/%s_%c_piece_%d.mmd" where %c is the first
// letter of the fruit name.
void Fruit::LoadFruitModels() {
    if (s_FruitModelsLoaded) return;

    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (!meshMgr) return;

    const int n = FruitInfo_GetCount();
    s_FruitModels.clear();
    s_FruitModels.resize((size_t)n);

    static Mortar::SmartPtr<Mortar::Texture> s_fruitAtlas;
    if (!s_fruitAtlas.IsValid()) {
        // logical path; FileSystem_Direct prepends data_dir
        s_fruitAtlas = Mortar::TextureManager::GetInstance().Load(
            "models/fruit/textures/fruit_atlas.tex");
    }

    int loaded = 0;
    for (int i = 0; i < n; ++i) {
        const FruitInfoData* info = FruitInfo_Get(i);
        if (!info || !info->m_ModelName[0]) continue;

        const char* name = info->m_ModelName;
        const char  c    = name[0];
        char path[256];

        // Whole-fruit model (<name>_single.mmd)
        snprintf(path, sizeof(path), "models/Fruit/%s_single.mmd", name);
        s_FruitModels[i].m_Whole = meshMgr->Load(path);

        // Half-piece models (<name>_<c>_piece_1.mmd + _piece_2.mmd)
        for (int piece = 1; piece <= 2; ++piece) {
            // logical path; FileSystem_Direct prepends data_dir
            snprintf(path, sizeof(path),
                     "models/Fruit/%s_%c_piece_%d.mmd",
                     name, c, piece);
            Mortar::SmartPtr<Mortar::Model> m = meshMgr->Load(path);
            if (piece == 1) s_FruitModels[i].m_HalfA = m;
            else            s_FruitModels[i].m_HalfB = m;
        }

        if (s_FruitModels[i].m_HalfA.IsValid()) {
            ++loaded;
        }

        // Assign fruit_atlas texture to all three model slots
        if (s_fruitAtlas.IsValid()) {
            Mortar::Model* slots[3] = {
                s_FruitModels[i].m_Whole.Get(),
                s_FruitModels[i].m_HalfA.Get(),
                s_FruitModels[i].m_HalfB.Get()
            };
            for (int s = 0; s < 3; ++s) {
                Mortar::Model* mod = slots[s];
                if (!mod) continue;
                for (size_t m = 0; m < mod->m_Meshes.size(); ++m) {
                    if (mod->m_Meshes[m].IsValid() &&
                        !mod->m_Meshes[m]->HasDiffuseTexture())
                    {
                        mod->m_Meshes[m]->SetDiffuseTexture(s_fruitAtlas);
                    }
                }
            }
        }
    }

    s_FruitModelsLoaded = true;
    (void)loaded;
}

const FruitModelInfo* Fruit::GetFruitModelInfo(int fruitType) {
    if (!s_FruitModelsLoaded) return nullptr;
    if (fruitType < 0 || fruitType >= (int)s_FruitModels.size()) return nullptr;
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

// ASM-verified: 2026-05-03T09:42 binary @ 0x00176564..0x001766f4 (asm-inspector)
int Fruit::RandomFruit(bool includeOnSide) {
    if (s_TotalWeight < 1) {
        int wT = 0, wA = 0, wC = 0, wCA = 0;
        const int count = FruitInfo_GetCount();
        FruitInfoData* arr = FruitInfo_GetArray();
        for (int i = 0; i < count; ++i) {
            FruitInfoData* fi = &arr[i];
            wT += fi->m_Chance;
            fi->m_CumWeight = wT;
            if (fi->m_CoinsMax < 1) wA += fi->m_Chance;       // binary +0x328
            if (fi->m_bScorable) {                             // binary +0x318
                wC += fi->m_Chance;
                if (fi->m_CoinsMax < 1) wCA += fi->m_Chance;  // binary +0x328
            }
            fi->m_CumCritWeight = wC;
        }
        s_TotalWeight    = wT;
        s_TotalAvail     = wA;
        s_TotalCrit      = wC;
        s_TotalCritAvail = wCA;
    }

    bool isCrit = WaveManager::GetInstance()->CriticalMode(0);
    Math::Random* rng = &WaveManager::GetInstance()->m_Random;
    const int count = FruitInfo_GetCount();

    if (!isCrit) {
        if (includeOnSide) {
            uint32_t r = rng->Rand32((uint32_t)s_TotalWeight);
            for (int i = 0; i < count; ++i)
                if (r < (uint32_t)FruitInfo_Get(i)->m_CumWeight) return i;
        } else {
            uint32_t r = rng->Rand32((uint32_t)s_TotalAvail);
            int acc = 0;
            for (int i = 0; i < count; ++i) {
                const FruitInfoData* fi = FruitInfo_Get(i);
                if (fi->m_CoinsMax < 1) {                      // binary +0x328
                    acc += fi->m_Chance;
                    if (r < (uint32_t)acc) return i;
                }
            }
        }
    } else {
        if (includeOnSide) {
            uint32_t r = rng->Rand32((uint32_t)s_TotalCrit);
            for (int i = 0; i < count; ++i)
                if (r < (uint32_t)FruitInfo_Get(i)->m_CumCritWeight) return i;
        } else {
            uint32_t r = rng->Rand32((uint32_t)s_TotalCritAvail);
            int acc = 0;
            for (int i = 0; i < count; ++i) {
                const FruitInfoData* fi = FruitInfo_Get(i);
                if (fi->m_CoinsMax < 1 && fi->m_bScorable) {   // binary +0x328, +0x318
                    acc += fi->m_Chance;
                    if (r < (uint32_t)acc) return i;
                }
            }
        }
    }
    return (int)rng->Rand32((uint32_t)(count - 1));
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
        if (clearAll || f->m_ChuckDelay > 0.0f)
            f->KillFruit(false);
        e = next_e;
    }
}

// Matches Fruit::Disable (binary @ 0x00126374): one-byte store of 1 to +0x3D.
// ASM-verified: 2026-05-03 binary @ 0x00126374 (asm-inspector)
void Fruit::Disable(Fruit* f) {
    f->m_bNoPowerUp = 1;
}

// Matches Fruit::DrawShadows (0x00178f28) + AddShadow (0x00175ea0).
// Texture: fruit_shadow.tex (loaded by FruitInfo_Load step 0).
// Geometry: 1 fade-out quad while spawning, 2 half-quads when active.
// Buffer: 64 fruits * 3 quads max * 6 verts = 1152 verts (binary uses 18432 stack).
static const int SHADOW_MAX_FRUITS = 64;
static QUADCUSTOMVERTEX s_ShadowVerts[SHADOW_MAX_FRUITS * 3 * 6];

void Fruit::DrawShadows() {
    Mortar::Texture* shadowTex = FruitInfo_GetShadowTex();
    if (!shadowTex) return;

    QUADCUSTOMVERTEX* w = s_ShadowVerts;
    int count = 0;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    while (e && count + 3 <= SHADOW_MAX_FRUITS * 3) {
        Fruit* f = static_cast<Fruit*>(e);
        if (f->scale.x > 0.0f) f->AddShadow(&w, &count);
        e = am->GetEntityNext(0, it);
    }
    if (count == 0) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    shadowTex->Set();
    Mortar::Mesh::DrawTriList(s_ShadowVerts, count * 6, false, NULL);
    shadowTex->UnSet();
}

// Writes up to 3 quads (shadow geometry) for one fruit into the shared buffer.
// Matches Fruit::AddShadow @ 0x00175ea0.
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
    // Corner layout:
    //   v0: (cx-w, cy-h)    v3: (cx+w, cy-h)
    //   v1: (cx-w, cy+h)    v4: (cx-w, cy+h)
    //   v2: (cx+w, cy-h)    v5: (cx+w, cy+h)
    // All verts: z = -5000 (DAT_00175e9c), normal (0,0,1), u in {0,1}, v in {0,1}.
    const float z    = -5000.0f;   // DAT_00175e9c
    const uint32_t c = ((uint32_t)col.a << 24)
                     | ((uint32_t)col.b << 16)
                     | ((uint32_t)col.g <<  8)
                     | ((uint32_t)col.r);

    QUADCUSTOMVERTEX* v = *out;

    v[0] = { cx - w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 0.0f };
    v[1] = { cx - w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 1.0f };
    v[2] = { cx + w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 0.0f };
    v[3] = { cx + w, cy - h, z,   0.0f, 0.0f, 1.0f,   c,   1.0f, 0.0f };
    v[4] = { cx - w, cy + h, z,   0.0f, 0.0f, 1.0f,   c,   0.0f, 1.0f };
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
        cs->center = Vec3(pos.x, pos.y, 0.0f);
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
    if (m_pSlasher && m_pSlasher->m_pCurrentTarget == this) {
        m_pSlasher->m_pCurrentTarget = nullptr;
    }
    m_pSlasher = nullptr;
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
    m_pEmitter1 = pm.AddEmitter((uint32_t)emitterHash, nullptr, /*persistent=*/true);
    if (m_pEmitter1) m_pEmitter1->m_Pos = pos;
    return m_pEmitter1 != nullptr;
}

// ============================================================
// Chunk D: UpdateBombAvoidance + DestroyFruitModels
// ============================================================

// Binary @ 0x00175988 — push bombs away from this fruit on the X axis.
// ASM-verified: re-analyst 2026-05-16 confirmed DAT_00175a5c=4900.0f,
// DAT_00175a60=56.25f, multiplier=12.0f; dist check is MagnitudeSqr(diff)<4900.
void Fruit::UpdateBombAvoidance(float dt) {
    if (m_bSliced != 0) return;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(1, it);   // entity type 1 = bomb
    while (e) {
        Bomb* bomb = static_cast<Bomb*>(e);
        if (bomb->IsActive() && bomb->m_Col != NULL) {
            Vec3 diff = bomb->pos - pos;
            float distSqr = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
            if (distSqr < 4900.0f) {   // DAT_00175a5c = 4900.0 (70^2)
                float dvx = vel.x - bomb->vel.x;
                float dvy = vel.y - bomb->vel.y;
                if (dvx * dvx + dvy * dvy < 56.25f) {   // DAT_00175a60 = 56.25 (7.5^2)
                    float dir = (diff.x < 0.0f) ? -1.0f : 1.0f;
                    bomb->vel.x += dir * dt * 12.0f;
                }
            }
        }
        e = am->GetEntityNext(1, it);
    }
}

// Binary @ 0x0017911c — releases the FruitModelInfo[] array.
void Fruit::DestroyFruitModels() {
    s_FruitModels.clear();
    s_FruitModelsLoaded = false;
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
        mirrorY = (pos.x < 0.0f) ? -1.0f : 1.0f;
    }

    // Quad 1: spawn-fade whole-fruit shadow (active while m_ScaleAnim < 1).
    if (m_ScaleAnim < 1.0f) {
        int a = (int)((1.0f - m_ScaleAnim) * 230.0f);   // DAT_00176164
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = 82.0f * scale.x;                      // DAT_00176168
        float ox = mirrorY * hs * -0.65f;                // DAT_0017616c
        float oy = mirrorX * hs * -0.65f;
        AddQuad(out, pos.x + ox, pos.y + oy, hs, hs, Colour(255, 255, 255, al));
        ++(*outCount);
    }

    // Quads 2+3: per-half shadows (active while m_ScaleAnim > 0).
    if (m_ScaleAnim > 0.0f) {
        int a = (int)(m_ScaleAnim * 100.0f);             // DAT_00176170
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = scale.x * 50.0f;                      // DAT_00176174
        Vec3 axis(0.0f, 0.0f, 1.0f);                    // DAT_00176180 BSS singleton (0,0,1)

        float ox = mirrorY * hs * -0.45f;                // DAT_00176178
        float oy = mirrorX * hs * -0.45f;

        // Binary calls Quaternion::Matrix33Unit on each rot, then multiplies axis.
        // Port uses ToMatrix44() and extracts the 3x3 rotation applied to axis.
        // For axis=(0,0,1): rotated = col2 of the rotation matrix = (m[8], m[9], m[10]).
        {
            Matrix44 mat1 = m_Rot1.ToMatrix44();
            Vec3 anchorA = pos + Vec3(
                mat1.m[0]*axis.x + mat1.m[4]*axis.y + mat1.m[8]*axis.z,
                mat1.m[1]*axis.x + mat1.m[5]*axis.y + mat1.m[9]*axis.z,
                mat1.m[2]*axis.x + mat1.m[6]*axis.y + mat1.m[10]*axis.z
            ) * 0.5f;
            AddQuad(out, anchorA.x + ox, anchorA.y + oy, hs, hs, Colour(255, 255, 255, al));
            ++(*outCount);

            Matrix44 mat2 = m_Rot2.ToMatrix44();
            Vec3 anchorB = m_SecondPos + Vec3(
                mat2.m[0]*axis.x + mat2.m[4]*axis.y + mat2.m[8]*axis.z,
                mat2.m[1]*axis.x + mat2.m[5]*axis.y + mat2.m[9]*axis.z,
                mat2.m[2]*axis.x + mat2.m[6]*axis.y + mat2.m[10]*axis.z
            ) * 0.5f;
            AddQuad(out, anchorB.x + ox, anchorB.y + oy, hs, hs, Colour(255, 255, 255, al));
            ++(*outCount);
        }
    }
}
