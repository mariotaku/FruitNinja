#include "Coin.h"
#include "ActorManager.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include "asset/MeshManager.h"
#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "render/MatrixManager.h"
#include "game/FruitCamera.h"
#include "game/GameOver.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include "game/GameWork.h"

// Analysed: 2026-04-12T16:45

// ---------------------------------------------------------------------------
// Key constants from binary (v1.6.1 Coin::_Update @0x001d81bc / InitCoin @0x001d7d84)
// ---------------------------------------------------------------------------
static const float COIN_SPEED_BASE      = 500.0f;   // base launch speed
static const float COIN_SPEED_RAND      = 550.0f;   // random addend range
static const float COIN_SPEED_SCALE     = 0.66f;    // launch speed multiplier
static const float COIN_VEL_DAMP        = 0.7f;     // velocity damping per frame (state 2)
static const float COIN_VEL_SQ_THRESH   = 900.0f;   // |vel|^2 threshold: state 2->3
static const float COIN_DECEL_TIME      = 0.05f;    // state 3 hold time (seconds)
static const float COIN_HOMING_CLOSE    = 30.0f;    // arrival distance (state 4->1)
static const float COIN_TURN_RATE       = 0.85f;    // homing steering base rate
static const float COIN_SPIN_RATE       = 32760.0f * 500.0f; // spin = rate * dt (binary: 32760*dt*500)
static const _Vector3<float> COIN_DEFAULT_TARGET(220.0f, -140.0f, 0.0f);  // default homing target (was misnamed "gravity")
static const _Vector3<float> COIN_SCALE(0.5f, 0.5f, 0.5f);   // entity visual scale

// Scatter-acceptance bounds for the spawn angle retry (v1.6.1 Coin::MakeCoins
// @0x001d7ec8). The binary tests the SCATTERED x against +/-240 and the
// scattered y against +/-160 -- i.e. the long axis is x here, opposite to the
// port's usual centered-ortho convention. Values read straight off the
// vcmp chain at 0x001d8018..0x001d8070.
static const float COIN_BOUND_X_MIN = -240.0f;
static const float COIN_BOUND_X_MAX =  240.0f;
static const float COIN_BOUND_Y_MIN = -160.0f;
static const float COIN_BOUND_Y_MAX =  160.0f;

// Fixed timestep that Update wrapper subdivides by (matches binary DAT_0018ae84)
static const float COIN_FIXED_DT = 1.0f / 60.0f;

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

// v1.6.1 Coin::LoadContent @0x001d7920 loaded flag (s_isContentLoaded).
static bool s_loaded = false;

// v1.6.1 AddToScoreOnArrival @0x162ab8: file-static bonus-mode firework counter.
// Cycles 1..8 (reset to 0 when >8), triggering camera shake + particle bursts at
// counts 3, 6, and >8 (0).
static int g_oneInThree = 0;

// The ASSET FruitNinjaBada/Data/models/Fruit/coin.mmd (+ coin.mad) does exist,
// following the same "models/Fruit/<name>.mmd" convention as Bomb::LoadContent's
// "models/Fruit/bomb.mmd". The BINARY, however, never loads it: s_coinModel's
// only writers in v1.6.1 are the static-init zero and UnLoadContent's NULL, and
// the string "coin.mmd" is absent from the image. See LoadContent's TODO.
static Mortar::SmartPtr<Mortar::Model> s_coinModel;

// ---------------------------------------------------------------------------
// CoinArrived — free helper; v1.6.1 CoinArrived @0x001d7a88.
// Free function in binary; kept as static member of Coin.cpp for greppability.
// Coin balance lives in game_work (+0x20/+0x24), not FruitSaveData.
// ---------------------------------------------------------------------------
void CoinArrived(Coin* coin) {
    AddCoins(coin->m_CoinValue);
}

// ---------------------------------------------------------------------------
// Coin constructor — v1.6.1 Coin::Coin @0x001d7b94 / @0x001d7c8c (two emitted
// variants, identical default-ctor signature; verified via Ghidra symbol lookup)
// ---------------------------------------------------------------------------
Coin::Coin()
    : m_CoinValue(0)
    , m_State(0)
    , m_Timer(0.0f)
    , m_Silent(0)
    , m_Speed(0.0f)
    , m_SpinAngle(0)
    , m_FlyFXHash(0)
    , m_CollectFXHash(0)
    , m_TargetX(COIN_DEFAULT_TARGET.x)
    , m_TargetY(COIN_DEFAULT_TARGET.y)
    , m_TargetZ(COIN_DEFAULT_TARGET.z)
    , m_pFlyEmitter(nullptr)
    , m_pCollectEmitter(nullptr)
{
    entityType = 2;
    LoadContent();
}

// ---------------------------------------------------------------------------
// ~Coin (D1) @ 0x001d7a90
// ASM-spec v1.6.1 Coin::~Coin (D1) @ 0x001d7a90:
//  - dtor only tears down m_OnArrived (Delegate1) + base Mortar::Entity;
//  - does NOT call Release() -- Release is vtable-dispatch-only, invoked by
//    ActorManager deactivation paths, never from the destructor.
// ---------------------------------------------------------------------------
Coin::~Coin() {
}

// ---------------------------------------------------------------------------
// Release v1.6.1 @ 0x001d7a5c — clear fly emitter
// ---------------------------------------------------------------------------
void Coin::Release() {
    if (m_pFlyEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
        m_pFlyEmitter = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Init — base no-op; Coin uses the base body (vtable slot 2 points at
// v1.6.1 Mortar::Entity::Init @0x0025623c).
// Coin is initialised via ctor + MakeCoins/InitCoin, never through factory-Init.
// ---------------------------------------------------------------------------
void Coin::Init(void* /*p1*/, long /*p2*/, _Vector3<float>* /*p3*/) {}

// ---------------------------------------------------------------------------
// PostUpdate — v1.6.1 Coin::DrawUpdate @0x001d79b8; body is `return this;` only
// ---------------------------------------------------------------------------
void Coin::PostUpdate(float /*dt*/) {}

// ---------------------------------------------------------------------------
// Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
// ---------------------------------------------------------------------------
void Coin::Deactivate() {
    if (m_pFlyEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
        m_pFlyEmitter = nullptr;
    }
    if (m_pCollectEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pCollectEmitter);
        m_pCollectEmitter = nullptr;
    }
}

// ---------------------------------------------------------------------------
// ASM-verified: 2026-07-04T00:00:00Z v1.6.1 Coin::Arrived @ 0x001d79bc
// Only m_pFlyEmitter is torn down here; m_pCollectEmitter is left running so
// the collect/sparkle burst plays to completion (InitCoin re-nulls it on
// next reuse, so there's no leak). flags |= 0x11, but ENT_INACTIVE is dead
// for Coin (force-cleared on recycle) -- keep ENT_KILLED-only.
// ---------------------------------------------------------------------------
void Coin::Arrived() {
    if (m_OnArrived) {
        m_OnArrived(this);
    }
    if (m_pFlyEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
        m_pFlyEmitter = nullptr;
    }
    flags |= ENT_KILLED;
}

// ---------------------------------------------------------------------------
// v1.6.1 Coin::InitCoin @ 0x001d7d84
// Stores flyFXHash/collectFXHash raw -- the null-name -> default substitution
// and the StringHash() call both happen in the caller (MakeCoins).
// ---------------------------------------------------------------------------
void Coin::InitCoin(_Vector3<float> pos_in, _Vector3<float> target, uint16_t angle, int coinValue,
                    unsigned long flyFXHash, unsigned long collectFXHash,
                    Mortar::Delegate1<void, Coin*> onArrived, float delay, bool silent)
{
    // flags &= 0xEE — clear active+dead bits (bits 0 and 4: ENT_INACTIVE | ENT_KILLED)
    flags &= 0xEE;
    m_Angle      = angle;
    m_State      = 0;
    m_CoinValue  = coinValue;
    // Speed formula: (500 + rand(524287)/524287 * 550) * 0.66
    // ASM-spec v1.6.1 Coin::InitCoin @0x001d7d84: Math::g_random.Rand32(0x7FFFF) x1
    float randFrac = (float)Math::g_Random.Rand32(0x7FFFF) / 524287.0f;
    m_Speed      = (COIN_SPEED_BASE + randFrac * COIN_SPEED_RAND) * COIN_SPEED_SCALE;
    pos          = pos_in;
    m_Timer      = -delay;
    m_TargetX    = target.x;
    m_TargetY    = target.y;
    m_TargetZ    = target.z;
    vel          = _Vector3<float>(0.0f, 0.0f, 0.0f);
    scale        = COIN_SCALE;
    m_FlyFXHash      = flyFXHash;
    m_Silent         = silent ? 1 : 0;
    m_CollectFXHash  = collectFXHash;
    m_pFlyEmitter      = nullptr;
    m_pCollectEmitter  = nullptr;
    m_OnArrived  = onArrived;
    m_SpinAngle  = 0;
}

// ---------------------------------------------------------------------------
// _Update v1.6.1 @0x001d81bc — 5-state machine
// (header's old "0x00173790" marker was stale v1.5.1 -- no "v1.6.1" tag, see
// project convention: address-only markers are presumed outdated.)
//
// Five REAL-GAP fixes applied here vs the previous port (batch1-realgap-specs.json,
// _ZN4Coin7_UpdateEf):
//   1. Launch velocity (state 0->2) was missing the binary's *1.5 scalar
//      (local_2c = 0x3fc00000) -- coins launched ~33% slower than the binary.
//   2. FLYING (state 2) fabricated a "gravity" pull from m_TargetX/Y/Z*dt that
//      does not exist in the binary -- m_TargetX/Y/Z (+0x5c) is the HOMING
//      destination, used only in states 3/4, never a per-frame accel term.
//   3. The spin/wobble accumulator (m_SpinAngle, +0x50) advanced unconditionally
//      every call at a flat rate; the binary only advances it in HOMING (state 4),
//      scaled by the same ramp factor that drives homing speed.
//   4. HOMING (state 4) velocity magnitude used a distance-based blend/normalize
//      instead of the binary's rampFactor*(m_Speed*2) formula, and direction was
//      taken from the raw float turn-blend instead of re-quantizing through
//      SinIdx/CosIdx(m_Angle) after the angle update (matches the binary's
//      16-bit-angle-only storage).
//   5. The sparkle/collect emitter (m_pCollectEmitter, +0x6c) is unconditionally
//      torn down at the TOP of every _Update call in the binary, then
//      conditionally re-spawned in states 3 and 4 behind a particle-budget
//      throttle (GetNumEntities(2)<20 || Rand32(3)==0); the port previously
//      treated it as a single persistent spawn with no reap/re-roll.
//   Also (part of the shared case 0/2 fallthrough tail): the fly/trail emitter's
//   m_DirSin/m_DirCos were never written (only m_Pos) -- now set from the current
//   heading angle every tick, matching the pattern already ported for
//   SlashEntity/Fruit/Bomb emitters.
// ---------------------------------------------------------------------------
void Coin::_Update(float dt) {
    // Bug (5): the sparkle/collect emitter is torn down unconditionally at the
    // TOP of every call (even while WAITING/FLYING, where it's already null) --
    // NOT just once on a state transition. It's conditionally re-spawned inside
    // cases 3/4 below, behind a particle-budget throttle.
    if (m_pCollectEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pCollectEmitter);
        m_pCollectEmitter = nullptr;
    }

    switch (m_State) {
    case 0: // WAITING — timer countdown
        // ASM-verified: v1.6.1 Coin::_Update @0x001d81bc case 0:
        //   m_Timer -= dt; if (m_Timer > 0.0f) return;  // still WAITING
        // Sign is call-site dependent (NOT always negative):
        //   - BonusScreen (-0.05f) -> m_Timer starts positive -> real multi-frame stagger.
        //   - Fruit/SlashEntity combo-coin (+0.02f) -> m_Timer starts negative -> WAITING
        //     never holds -> launches on frame 1 (binary-faithful instant burst).
        // The port previously had += dt / <0 (inverted): the -0.05 bonus count-up
        // coins then launched all on frame 1, bursting ~11 overlapping "achievement" SFX.
        m_Timer -= dt;
        if (m_Timer > 0.0f) {
            // Still in delay period
            return;
        }
        // Timer expired: play "achievement" SFX if not silent
        if (m_Silent == 0) {
            Game* game = Game::GetInstance();
            if (game && game_work.mGameSound) {
                game_work.mGameSound->SFXPlay("achievement", 1.0f, 1.0f);
            }
        }
        // Bug (1): binary applies a missing *1.5 scalar to the launch velocity
        // (v1.6.1 Coin::_Update @0x001d81bc, local_2c=0x3fc00000).
        vel.x = SinIdx(m_Angle) * m_Speed * 1.5f;
        vel.y = CosIdx(m_Angle) * m_Speed * 1.5f;
        vel.z = 0.0f;
        // ASM-spec v1.6.1 Coin::_Update @0x001d81bc: non-silent launch (m_Silent==0) plays the
        // "achievement" SFX and jumps straight to HOMING (state 4), resetting m_Timer=0; only silent
        // launches (m_Silent!=0) pass through FLYING (state 2).
        if (m_Silent == 0) {
            m_Timer = 0.0f;
            m_State = 4;
            return; // skip FLYING processing this tick -- binary jumps straight to HOMING
        }
        m_State = 2; // immediate transition to FLYING
        // fall through to process FLYING on same tick
        /* FALLTHROUGH */

    case 2: // FLYING — damp velocity; wait for speed drop
        // Bug (2): binary case 2 is pure damping, no gravity term -- the previous
        // `vel += m_TargetX/Y/Z*dt` "gravity" block here was fabricated and has
        // been removed. m_TargetX/Y/Z is the homing destination (states 3/4 only).
        vel.x *= COIN_VEL_DAMP;
        vel.y *= COIN_VEL_DAMP;
        vel.z *= COIN_VEL_DAMP;
        // Advance position
        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;
        // TODO: v1.6.1 Coin::_Update @0x001d81bc LAB_001d839c -- trail-emitter respawn guard
        // differs from binary; needs RE.
        // Spawn/refresh fly (trail) emitter -- shared case 0/2 fallthrough tail.
        if (m_FlyFXHash != 0 && !m_pFlyEmitter &&
            PSPParticleManager::GetInstance().EmitterExists(m_FlyFXHash)) {
            m_pFlyEmitter = PSPParticleManager::GetInstance().AddEmitter(m_FlyFXHash, &m_pFlyEmitter);
        }
        if (m_pFlyEmitter) {
            m_pFlyEmitter->m_Pos    = pos;
            m_pFlyEmitter->m_DirSin = SinIdx(m_Angle);
            m_pFlyEmitter->m_DirCos = CosIdx(m_Angle);
        }
        {
            float velSq = vel.x*vel.x + vel.y*vel.y + vel.z*vel.z;
            if (velSq < COIN_VEL_SQ_THRESH) {
                m_Timer = 0.0f;
                m_State = 3; // transition to DECEL
            }
        }
        break;

    case 3: // DECEL — wait 0.05s, spawn sparkle, compute homing angle
        m_Timer += dt;
        if (m_Timer >= COIN_DECEL_TIME) {
            // Bug (5): sparkle/collect emitter re-spawn behind a particle-budget
            // throttle (GetNumEntities(2)<20 || Rand32(3)==0), gated on EmitterExists.
            if (m_CollectFXHash != 0 &&
                (Mortar::ActorManager::GetInstance()->GetNumEntities(2) < 20 ||
                 Math::g_Random.Rand32(3) == 0) &&
                PSPParticleManager::GetInstance().EmitterExists(m_CollectFXHash)) {
                m_pCollectEmitter = PSPParticleManager::GetInstance().AddEmitter(m_CollectFXHash, &m_pCollectEmitter);
                if (m_pCollectEmitter) {
                    m_pCollectEmitter->m_Pos = pos;
                }
            }
            // Clear fly emitter on transition to homing
            if (m_pFlyEmitter) {
                PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
                m_pFlyEmitter = nullptr;
            }
            // Compute homing angle from current pos toward target
            float dx = m_TargetX - pos.x;
            float dy = m_TargetY - pos.y;
            float len = sqrtf(dx*dx + dy*dy);
            if (len > 0.0f) {
                // Convert direction to 16-bit angle index via atan2
                // SinIdx/CosIdx use (idx/65536)*2pi, so angle = atan2(dx,dy)/(2pi) * 65536
                float a = atan2f(dx, dy);
                if (a < 0.0f) a += 6.2831853f;
                m_Angle = (uint16_t)(int)(a / 6.2831853f * 65536.0f);
            }
            m_Timer = 0.0f;
            m_State = 4; // transition to HOMING
        }
        break;

    case 4: // HOMING — steer toward target, arrive when close
    {
        float dx = m_TargetX - pos.x;
        float dy = m_TargetY - pos.y;
        float dist = sqrtf(dx*dx + dy*dy);

        if (dist < COIN_HOMING_CLOSE) {
            m_State = 1; // ARRIVED — handled next tick
            break;
        }

        // Bug (4): rampFactor curve driven by m_Timer (state-entry elapsed time),
        // 0..1, ramping from ~0.5-0.7 up to 1.0 over ~0.45s. First term reads the
        // PRE-increment timer value; m_Timer is then advanced before the second term.
        float preTimer = m_Timer;
        float rampT = 0.25f - preTimer;
        if (rampT >= 0.25f)      rampT = 0.5f;
        else if (rampT > 0.0f)   rampT = rampT * 2.0f;
        else                     rampT = 0.0f;

        m_Timer += dt;
        rampT = (m_Timer + 0.1f) * 2.0f + rampT;
        if (rampT > 1.0f) rampT = 1.0f;
        float rampFactor = rampT;

        // Turn rate: base 0.85, boosted when close (existing blend kept -- close
        // to the binary's GetSmallestDeltaIdx-based turn step per re-analyst
        // spec; exact LUT-domain turn math needs its own follow-up RE pass).
        // TODO: v1.6.1 0x001d8d74 (GetSmallestDeltaIdx) -- 16-bit angle-index turn
        // helper not yet RE'd; this float-domain blend is an approximation.
        float turnRate = COIN_TURN_RATE;
        if (dist < 80.0f) {
            turnRate = 1.0f; // steer directly when very close
        }

        // Desired direction
        float desiredSinA = (dist > 0.0f) ? dx / dist : 0.0f;
        float desiredCosA = (dist > 0.0f) ? dy / dist : 1.0f;

        // Current heading from m_Angle
        float curSin = SinIdx(m_Angle);
        float curCos = CosIdx(m_Angle);

        // Blend toward desired heading
        float newSin = curSin + (desiredSinA - curSin) * turnRate;
        float newCos = curCos + (desiredCosA - curCos) * turnRate;
        float newLen = sqrtf(newSin*newSin + newCos*newCos);
        if (newLen > 0.0f) {
            newSin /= newLen;
            newCos /= newLen;
        }

        // Convert back to angle index
        float a = atan2f(newSin, newCos);
        if (a < 0.0f) a += 6.2831853f;
        m_Angle = (uint16_t)(int)(a / 6.2831853f * 65536.0f);

        // Bug (4): velocity is direction=(SinIdx,CosIdx)(m_Angle) re-quantized
        // through the 16-bit angle -- NOT the raw float blend (newSin,newCos) --
        // times magnitude=rampFactor*(m_Speed*2). Previous port used newSin/newCos
        // directly and a distance-based speed formula.
        float speedMag = rampFactor * (m_Speed * 2.0f);
        vel.x = SinIdx(m_Angle) * speedMag;
        vel.y = CosIdx(m_Angle) * speedMag;
        vel.z = 0.0f;

        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;

        // Bug (3): spin/wobble accumulator only advances in HOMING, scaled by
        // rampFactor (previous port advanced it unconditionally every _Update
        // call at a flat rate). spin rate = 32760*500 (COIN_SPIN_RATE).
        float spinInc = rampFactor * dt * COIN_SPIN_RATE;
        if (spinInc < 0.0f) spinInc = 0.0f;
        m_SpinAngle = (uint16_t)(m_SpinAngle + (uint16_t)(int)spinInc);

        // Bug (5): sparkle/collect emitter re-spawn behind the same particle-budget
        // throttle as case 3 (it was torn down unconditionally at the top of
        // this call).
        if (m_CollectFXHash != 0 &&
            (Mortar::ActorManager::GetInstance()->GetNumEntities(2) < 20 ||
             Math::g_Random.Rand32(3) == 0) &&
            PSPParticleManager::GetInstance().EmitterExists(m_CollectFXHash)) {
            m_pCollectEmitter = PSPParticleManager::GetInstance().AddEmitter(m_CollectFXHash, &m_pCollectEmitter);
        }
        if (m_pCollectEmitter) {
            m_pCollectEmitter->m_Pos = pos;
        }
        break;
    }

    case 1: // ARRIVED — call Arrived() and retire
        Arrived();
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Update — v1.6.1 Coin::Update @0x001d7940 — fixed-timestep wrapper
// Binary ignores its own dt param and drives the substep loop from the
// global game_work.dt (+0x38) instead; some call sites zero/alter the
// passed dt for freeze effects (GameInit.cpp bomb-hit freeze, WaveManager.cpp
// settle-pass) while game_work.dt stays nonzero, so using the parameter here
// wrongly froze coins in those scenarios. Break-gate is flags & ENT_SKIP_MASK
// (0x11), not just ENT_KILLED.
// ---------------------------------------------------------------------------
void Coin::Update(float /*dt*/) {
    float acc = game_work.dt;
    while (acc > 0.0f) {
        float step = (acc <= COIN_FIXED_DT) ? acc : COIN_FIXED_DT;
        _Update(step);
        if (flags & ENT_SKIP_MASK) return;
        acc -= COIN_FIXED_DT;
    }
}

// ---------------------------------------------------------------------------
// Draw — v1.6.1 Coin::Draw @0x001d8810
// Verified structure: m_Silent gate -> model gate -> m_State<=1 early-out ->
// Scale44 -> RotY44(spin) -> RotZ44(heading) -> GlobalTranslate44 -> Model::Draw.
//
// Binary check: `if (m_Silent == 0) return;`
// This looks inverted but is replicated exactly as the binary has it.
// Result: coins with m_Silent==0 (non-silent) do NOT render their model;
// only coins launched with silent=true render. This may be a binary quirk
// (possibly the model draw was intentionally gated on a different condition
// later, or the check is for something we haven't decoded yet).
// We replicate it faithfully per the spec.
// ---------------------------------------------------------------------------
void Coin::Draw(Renderer& /*r*/) {
    // Binary: if (m_Silent == 0) return;  — replicate exactly
    if (m_Silent == 0) return;

    if (!s_coinModel.IsValid()) return;

    // Don't draw if waiting (state 0) or arrived (state 1)
    if (m_State <= 1) return;

    // Matrix: Scale × RotY(spin) × RotZ(heading) × Translate
    // Binary: mat = Scale(scale); mat *= RotY(SinIdx(spin), CosIdx(spin));
    //         mat *= RotZ(SinIdx(angle), CosIdx(angle)); mat *= Translate(pos)
    Matrix44 mat = Matrix44::MakeScale(scale);
    mat.RotY44(SinIdx(m_SpinAngle), CosIdx(m_SpinAngle));
    mat.RotZ44(SinIdx(m_Angle), CosIdx(m_Angle));
    mat.GlobalTranslate44(pos);

    s_coinModel->Draw(mat);
}

// ---------------------------------------------------------------------------
// LoadContent — v1.6.1 Coin::LoadContent @0x001d7920
// The whole binary body is `s_isContentLoaded = 1; return;` (11 bytes): no
// MeshManager load and no `if (s_loaded) return` guard -- the guard lives in
// the caller, Coin::Coin @0x001d7b94 (`if (s_isContentLoaded == 0) LoadContent();`).
//
// TODO: v1.6.1 Coin::LoadContent @0x001d7920 — port loads models/Fruit/coin.mmd
//   with no binary basis; s_coinModel is never assigned in v1.6.1 (its only two
//   writers are the static-init zero and UnLoadContent's NULL, and the string
//   "coin.mmd" does not exist in the binary), which implies Coin::Draw's
//   `s_coinModel != NULL` gate is never true. Pending HLE confirmation before
//   the load is removed -- leaving the port drawing the coin for now.
// ---------------------------------------------------------------------------
void Coin::LoadContent() {
    if (s_loaded) return;
    s_loaded = true;
    Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();
    if (mm) s_coinModel = mm->Load("models/Fruit/coin.mmd");
}

// ---------------------------------------------------------------------------
// UnLoadContent — v1.6.1 Coin::UnLoadContent @0x001d87f0
// The binary body is ONLY SmartPtr<Model>::SetPtr(&s_coinModel, NULL); it does
// NOT reset the loaded flag. The `s_loaded = false` below has no binary basis
// and is paired with the port-only model load in LoadContent (see its TODO).
// ---------------------------------------------------------------------------
void Coin::UnLoadContent() {
    s_coinModel.SetNull();
    s_loaded = false;
}

// ---------------------------------------------------------------------------
// ClearCoins v1.6.1 @ 0x001d7a00 (thunk 0x00106eb8)
// Iterates all type-2 entities via Mortar::ActorManager iterator pair,
// unconditionally (no IsActive() gate in the binary loop).
// If arrive=true, calls Arrived() on each (credits coins, clears emitters);
// otherwise the binary ORs ENT_INACTIVE|ENT_KILLED (0x11) directly, not
// solely ENT_KILLED -- same 0x11-vs-0x10 pattern as BombHit.h/GameInit.cpp
// DeactivateAllEntities.
// ---------------------------------------------------------------------------
void Coin::ClearCoins(bool arrive) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(2, it);
    while (e) {
        // Advance iterator before potentially modifying the entity
        Mortar::Entity* next_e = am->GetEntityNext(2, it);
        Coin* coin = static_cast<Coin*>(e);
        if (arrive) {
            coin->Arrived();
        } else {
            coin->flags |= ENT_INACTIVE | ENT_KILLED;
        }
        e = next_e;
    }
}

// ---------------------------------------------------------------------------
// v1.6.1 Coin::MakeCoins @ 0x001d7ec8
// Spawn N coins via Mortar::ActorManager::Add(2).
// Retry spawn position up to 10x if out of screen bounds.
// ---------------------------------------------------------------------------
void Coin::MakeCoins(int totalCoins, int coinsPerCoin, _Vector3<float> spawnPos,
                     uint16_t baseAngle, uint16_t angleSpread,
                     _Vector3<float>* target, float delayStep, float delayCap,
                     const char* flyFXName, const char* collectFXName,
                     Mortar::Delegate1<void, Coin*> onArrived, bool silent)
{
    if (totalCoins <= 0) return;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Null -> default FX name substitution (DAT_00173784/88), then hash once.
    if (!flyFXName)     flyFXName     = "coin_fly";
    if (!collectFXName) collectFXName = "coin_collect";
    unsigned long flyHash     = StringHash(flyFXName);
    unsigned long collectHash = StringHash(collectFXName);

    // Number of coins spawned (each represents coinsPerCoin value)
    int numCoinEntities = (totalCoins + coinsPerCoin - 1) / coinsPerCoin;
    (void)numCoinEntities;
    // delayStep = per-coin delay increment (negative); delayCap = max total delay cap (negative).
    // Each coin i gets delayStep * (i+1), clamped so it never exceeds delayCap in magnitude.
    float perStep = delayStep;
    float maxDelay = delayCap;

    _Vector3<float> resolvedTarget = target ? *target : COIN_DEFAULT_TARGET;

    int idx = 0;
    int remaining = totalCoins;
    while (remaining > 0) {
        Coin* coin = static_cast<Coin*>(am->Add(2, true));
        if (!coin) break;

        // Random launch angle: baseAngle + Rand32(angleSpread) - angleSpread/2.
        // The draw is UNCONDITIONAL -- Math::Random::Rand32 advances the shared
        // LCG for every argument, so a `angleSpread > 0` guard would silently
        // remove a draw from the global stream and desync everything after it.
        uint16_t randAngle = (uint16_t)((uint32_t)baseAngle +
            (Math::g_Random.Rand32((uint32_t)angleSpread) - ((uint32_t)angleSpread >> 1)));

        // Scatter is used ONLY to decide whether to re-roll the angle: the
        // coin itself is always spawned at the unscattered spawnPos. Counter
        // runs 1 -> 10, so at most 9 re-rolls == 10 draws per coin.
        float tryX = spawnPos.x + SinIdx(randAngle) * 100.0f;
        float tryY = spawnPos.y + CosIdx(randAngle) * 100.0f;
        for (int attempt = 1; attempt != 10; ++attempt) {
            if (tryX >= COIN_BOUND_X_MIN && tryX <= COIN_BOUND_X_MAX &&
                tryY >= COIN_BOUND_Y_MIN && tryY <= COIN_BOUND_Y_MAX) {
                break;
            }
            randAngle = (uint16_t)((uint32_t)baseAngle +
                (Math::g_Random.Rand32((uint32_t)angleSpread) - ((uint32_t)angleSpread >> 1)));
            tryX = spawnPos.x + SinIdx(randAngle) * 100.0f;
            tryY = spawnPos.y + CosIdx(randAngle) * 100.0f;
        }

        _Vector3<float> coinPos(spawnPos);
        // TODO: v1.6.1 Coin::MakeCoins @0x001d7ec8 -- per-coin delay stagger differs from
        // binary; needs RE.
        // Stagger delay: perStep * (idx+1), but never more negative than maxDelay.
        float coinDelay = perStep * (float)(idx + 1);
        if (maxDelay < 0.0f && coinDelay < maxDelay) coinDelay = maxDelay;
        int coinValue = remaining < coinsPerCoin ? remaining : coinsPerCoin;

        coin->InitCoin(coinPos, resolvedTarget, randAngle, coinValue,
                       flyHash, collectHash, onArrived, coinDelay, silent);

        remaining -= coinsPerCoin;
        idx++;
    }
}

// ---------------------------------------------------------------------------
// DefaultArrivedDelegate — factory returning a Delegate1 bound to CoinArrived.
// Exposes the file-static CoinArrived helper (v1.6.1 CoinArrived @0x001d7a88) to callers
// in other translation units (e.g. SlashEntity::Update combo-coin spawn).
// ---------------------------------------------------------------------------
Mortar::Delegate1<void, Coin*> Coin::DefaultArrivedDelegate() {
    return Mortar::Delegate1<void, Coin*>::MakeFree(&CoinArrived);
}

// ASM-spec v1.6.1 AddToScoreOnArrival @0x162ab8
// Bonus-mode coin arrival handler: cycles g_oneInThree and at counts 3, 6, and >8
// fires a camera shake, "Bonus-Firework-Explode" SFX, and two particle bursts
// before crediting the coin's value to the score.
void AddToScoreOnArrival(Coin* coin) {
    g_oneInThree++;
    if (g_oneInThree == 3 || g_oneInThree == 6 || g_oneInThree > 8) {
        FruitCamera* cam = game_work.m_FruitCamera;
        if (cam) {
            cam->CreateCameraShake(_Vector3<float>(-230.0f, 150.0f, 0.0f), 0.15f, 0.75f);
        }
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Bonus-Firework-Explode", 1.0f, 1.0f);
        }
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        PSPParticleEmitter* em = pm.AddEmitter(StringHash("bonus_mode_fx_red"), 0, false);
        if (em) {
            em->m_Pos = _Vector3<float>(-230.0f, 150.0f, 0.0f);
        }
        // x-offset varies by count: 3=0, 6=57.6, >8=-57.6
        float xoff = (g_oneInThree == 3) ? 0.0f :
                     (g_oneInThree == 6) ? 57.6f : -57.6f;
        float x = (Math::g_Random.RandF(24.0f) - 12.0f) + xoff;
        float y = Math::g_Random.RandF(10.0f) + 160.0f + 3.0f;
        PSPParticleEmitter* em2 = pm.AddEmitter(StringHash("arcade_confetti"), 0, false);
        if (em2) {
            em2->m_Pos = _Vector3<float>(x, y, 0.0f);
        }
        if (g_oneInThree > 8) {
            g_oneInThree = 0;
        }
    }
    AddToCurrentScore(coin->m_CoinValue, 0, false, false);
}

// ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: exposes the file-static
// g_oneInThree counter (declared above) so AwardScores can prime it to 3 right
// before its own CreateCameraShake -- identical storage AddToScoreOnArrival uses.
void Coin_PrimeOneInThree(int value) {
    g_oneInThree = value;
}
