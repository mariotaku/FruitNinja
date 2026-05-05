#include "Coin.h"
#include "ActorManager.h"
#include "game/FruitSaveData.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "asset/Mesh.h"
#include "asset/MeshManager.h"
#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "render/MatrixManager.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

// Analysed: 2026-04-12T16:45

// ---------------------------------------------------------------------------
// Key constants from binary (coin.md)
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
static const Vec3  COIN_DEFAULT_GRAVITY(220.0f, -140.0f, 0.0f); // default gravity/target
static const Vec3  COIN_SCALE(0.5f, 0.5f, 0.5f);   // entity visual scale

// Screen bounds for spawn clamping (coin.md "Key Constants")
static const float COIN_BOUND_Y_MIN = -240.0f;
static const float COIN_BOUND_Y_MAX =  240.0f;
static const float COIN_BOUND_X_MIN = -160.0f;
static const float COIN_BOUND_X_MAX =  160.0f;

// Fixed timestep that Update wrapper subdivides by (matches binary DAT_0018ae84)
static const float COIN_FIXED_DT = 1.0f / 60.0f;

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

// 0x00173114 loaded flag.  Model SmartPtr is TODO — asset pipeline not ready.
static bool s_loaded = false;

// TODO: load "models/coin.mmd" via MeshManager when asset pipeline is wired.
// Binary: Coin::LoadContent loads a .mmd into s_coinModel.
static SmartPtr<Mortar::Model> s_coinModel;

// ---------------------------------------------------------------------------
// CoinArrived — static helper @ 0x0017320C.
// Free function in binary; kept as static member of Coin.cpp for greppability.
// Calls FruitSaveData::AddCoins with the coin's value.
// ---------------------------------------------------------------------------
static void CoinArrived(Coin* coin) {
    Game* game = Game::GetInstance();
    if (game && game->pSaveData) {
        game->pSaveData->AddCoins(coin->m_CoinValue);
    }
}

// ---------------------------------------------------------------------------
// Coin constructor (C1) @ 0x00173394
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
    , m_TargetX(COIN_DEFAULT_GRAVITY.x)
    , m_TargetY(COIN_DEFAULT_GRAVITY.y)
    , m_TargetZ(COIN_DEFAULT_GRAVITY.z)
    , m_pFlyEmitter(nullptr)
    , m_pCollectEmitter(nullptr)
{
    entityType = 2;
    LoadContent();
}

// ---------------------------------------------------------------------------
// ~Coin (D1) @ 0x00173218
// ---------------------------------------------------------------------------
Coin::~Coin() {
    Release();
}

// ---------------------------------------------------------------------------
// Release @ 0x001731F4 — clear fly emitter
// ---------------------------------------------------------------------------
void Coin::Release() {
    if (m_pFlyEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
        m_pFlyEmitter = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Init @ 0x0019D5FC — base no-op; Coin uses base (vtable slot 2 = 0x0019d5fc).
// Coin is initialised via ctor + MakeCoins/InitCoin, never through factory-Init.
// ---------------------------------------------------------------------------
void Coin::Init(void* /*p1*/, long /*p2*/, Vec3* /*p3*/) {}

// ---------------------------------------------------------------------------
// PostUpdate (DrawUpdate) @ 0x0017318C — empty in binary
// ---------------------------------------------------------------------------
void Coin::PostUpdate(float /*dt*/) {}

// ---------------------------------------------------------------------------
// Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
// ---------------------------------------------------------------------------
void Coin::Deactivate() {
    if (m_pFlyEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
        m_pFlyEmitter = nullptr;
    }
    if (m_pCollectEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pCollectEmitter);
        m_pCollectEmitter = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Arrived @ 0x00173190
// Invoke the OnArrived delegate, clear emitters, mark entity dead.
// ---------------------------------------------------------------------------
void Coin::Arrived() {
    if (m_OnArrived) {
        m_OnArrived(this);
    }
    // Clear both emitters on arrival
    if (m_pFlyEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
        m_pFlyEmitter = nullptr;
    }
    if (m_pCollectEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pCollectEmitter);
        m_pCollectEmitter = nullptr;
    }
    flags |= ENT_KILLED;
}

// ---------------------------------------------------------------------------
// InitCoin @ 0x00173454
// ---------------------------------------------------------------------------
void Coin::InitCoin(const Vec3& pos_in, const Vec3& gravity, uint16_t /*baseAngle*/,
                    int /*playerIdx*/, uint16_t launchAngle, int coinValue,
                    const char* flyFXName, const char* collectFXName,
                    Mortar::Delegate1<void, Coin*> onArrived, float delay, bool silent)
{
    // flags &= 0xEE — clear active+dead bits (bits 0 and 4: ENT_INACTIVE | ENT_KILLED)
    flags &= 0xEE;
    m_Angle      = launchAngle;
    m_State      = 0;
    m_CoinValue  = coinValue;
    // Speed formula: (500 + rand(524287)/524287 * 550) * 0.66
    // Binary uses Rand32(0x7FFFF) — same as rand() % 524288.
    float randFrac = (float)(rand() % 524288) / 524287.0f;
    m_Speed      = (COIN_SPEED_BASE + randFrac * COIN_SPEED_RAND) * COIN_SPEED_SCALE;
    pos          = pos_in;
    m_Timer      = -delay;
    m_TargetX    = gravity.x;
    m_TargetY    = gravity.y;
    m_TargetZ    = gravity.z;
    vel          = Vec3(0.0f, 0.0f, 0.0f);
    scale        = COIN_SCALE;
    m_FlyFXHash      = StringHash(flyFXName ? flyFXName : "");
    m_Silent         = silent ? 1 : 0;
    m_CollectFXHash  = StringHash(collectFXName ? collectFXName : "");
    m_pFlyEmitter      = nullptr;
    m_pCollectEmitter  = nullptr;
    m_OnArrived  = onArrived;
    m_SpinAngle  = 0;
}

// ---------------------------------------------------------------------------
// _Update @ 0x00173790 — 5-state machine
// ---------------------------------------------------------------------------
void Coin::_Update(float dt) {
    // Advance spin regardless of state
    // spin speed = 32760 * dt * 500  (coin.md "Spin speed")
    m_SpinAngle = (uint16_t)(m_SpinAngle + (uint16_t)(int)(COIN_SPIN_RATE * dt));

    switch (m_State) {
    case 0: // WAITING — timer countdown
        m_Timer += dt;
        if (m_Timer < 0.0f) {
            // Still in delay period
            return;
        }
        // Timer expired: play "achievement" SFX if not silent
        if (m_Silent == 0) {
            Game* game = Game::GetInstance();
            if (game && game->pGameSound) {
                game->pGameSound->SFXPlay("achievement", 1.0f, 1.0f);
            }
        }
        // Compute initial velocity from launch angle
        vel.x = SinIdx(m_Angle) * m_Speed;
        vel.y = CosIdx(m_Angle) * m_Speed;
        vel.z = 0.0f;
        // Spawn fly emitter
        if (m_FlyFXHash != 0) {
            m_pFlyEmitter = Mortar::PSPParticleManager::GetInstance().AddEmitter(m_FlyFXHash, &m_pFlyEmitter);
            if (m_pFlyEmitter) {
                m_pFlyEmitter->m_Pos = pos;
            }
        }
        m_State = 2; // immediate transition to FLYING
        // fall through to process FLYING on same tick
        /* FALLTHROUGH */

    case 2: // FLYING — damp velocity; wait for speed drop
        // Apply gravity (target carries gravity in states 0-3)
        vel.x += m_TargetX * dt;  // gravity.x
        vel.y += m_TargetY * dt;  // gravity.y
        vel.z += m_TargetZ * dt;  // gravity.z (usually 0)
        // Damp velocity
        vel.x *= COIN_VEL_DAMP;
        vel.y *= COIN_VEL_DAMP;
        vel.z *= COIN_VEL_DAMP;
        // Advance position
        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;
        // Update fly emitter position
        if (m_pFlyEmitter) {
            m_pFlyEmitter->m_Pos = pos;
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
            // Spawn collect/sparkle emitter
            if (m_CollectFXHash != 0) {
                m_pCollectEmitter = Mortar::PSPParticleManager::GetInstance().AddEmitter(m_CollectFXHash, &m_pCollectEmitter);
                if (m_pCollectEmitter) {
                    m_pCollectEmitter->m_Pos = pos;
                }
            }
            // Clear fly emitter on transition to homing
            if (m_pFlyEmitter) {
                Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pFlyEmitter);
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

        // Accelerate toward target
        m_Timer += dt;
        float spd = m_Speed * (1.0f + m_Timer * 2.0f);

        // Turn rate: base 0.85, boosted when close
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

        vel.x = newSin * spd;
        vel.y = newCos * spd;
        vel.z = 0.0f;

        pos.x += vel.x * dt;
        pos.y += vel.y * dt;
        pos.z += vel.z * dt;

        // Update collect emitter position
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
// Update @ 0x0017312C — fixed-timestep wrapper
// Subdivides incoming dt into 1/60 steps; calls _Update each step.
// ---------------------------------------------------------------------------
void Coin::Update(float dt) {
    float acc = dt;
    while (acc >= COIN_FIXED_DT) {
        _Update(COIN_FIXED_DT);
        acc -= COIN_FIXED_DT;
        // Stop early if entity was killed (Arrived() set ENT_KILLED)
        if (flags & ENT_KILLED) return;
    }
    // Any fractional remainder is discarded — matches binary fixed-step pattern.
}

// ---------------------------------------------------------------------------
// Draw @ 0x00173CC4
//
// Binary check: `if (m_Silent == 0) return;`
// This looks inverted but is replicated exactly as documented in coin.md.
// Result: coins with m_Silent==0 (non-silent) do NOT render their model;
// only coins launched with silent=true render. This may be a binary quirk
// (possibly the model draw was intentionally gated on a different condition
// later, or the check is for something we haven't decoded yet).
// We replicate it faithfully per the spec.
// ---------------------------------------------------------------------------
void Coin::Draw(Renderer& /*r*/) {
    // Binary: if (m_Silent == 0) return;  — replicate exactly
    if (m_Silent == 0) return;

    // TODO: load "models/coin.mmd" — s_coinModel is null until asset pipeline is wired
    if (!s_coinModel.IsValid()) return;

    // Don't draw if waiting (state 0) or arrived (state 1)
    if (m_State <= 1) return;

    // Matrix: Scale × RotY(spin) × RotZ(heading) × Translate
    // Binary: mat = Scale(scale); mat *= RotY(SinIdx(spin), CosIdx(spin));
    //         mat *= RotZ(SinIdx(angle), CosIdx(angle)); mat *= Translate(pos)
    Matrix44 mat = Matrix44::Scale44(scale);
    mat.RotY44(SinIdx(m_SpinAngle), CosIdx(m_SpinAngle));
    mat.RotZ44(SinIdx(m_Angle), CosIdx(m_Angle));
    mat.GlobalTranslate44(pos);

    s_coinModel->Draw(mat);
}

// ---------------------------------------------------------------------------
// LoadContent @ 0x00173114
// ---------------------------------------------------------------------------
void Coin::LoadContent() {
    if (s_loaded) return;
    s_loaded = true;
    // TODO: load coin model when asset pipeline is wired:
    //   Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();
    //   if (mm) s_coinModel = mm->Load("models/coin.mmd");
}

// ---------------------------------------------------------------------------
// UnLoadContent @ 0x00173CA8
// ---------------------------------------------------------------------------
void Coin::UnLoadContent() {
    s_coinModel.SetNull();
    s_loaded = false;
}

// ---------------------------------------------------------------------------
// ClearCoins @ 0x001731B8
// Iterates all type-2 entities via Mortar::ActorManager iterator pair.
// If arrive=true, calls Arrived() on each (credits coins); otherwise kills.
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
        if (coin->IsActive()) {
            if (arrive) {
                coin->Arrived();
            } else {
                coin->flags |= ENT_KILLED;
            }
        }
        e = next_e;
    }
}

// ---------------------------------------------------------------------------
// MakeCoins @ 0x00173568
// Spawn N coins via Mortar::ActorManager::Add(2).
// delayStep = delayRange / (totalCoins/coinsPerCoin + 1)
// Retry spawn position up to 10x if out of screen bounds.
// ---------------------------------------------------------------------------
void Coin::MakeCoins(int totalCoins, int coinsPerCoin, float delayRange,
                     uint16_t baseAngle, uint16_t angleSpread,
                     const Vec3& spawnPos,
                     const char* flyFXName, const char* collectFXName,
                     Mortar::Delegate1<void, Coin*> onArrived, bool silent)
{
    if (totalCoins <= 0) return;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Number of coins spawned (each represents coinsPerCoin value)
    int numCoinEntities = (totalCoins + coinsPerCoin - 1) / coinsPerCoin;
    float delayStep = delayRange / (float)(numCoinEntities + 1);

    Vec3 gravity = COIN_DEFAULT_GRAVITY;

    int idx = 0;
    int remaining = totalCoins;
    while (remaining > 0) {
        Coin* coin = static_cast<Coin*>(am->Add(2, true));
        if (!coin) break;

        // Random launch angle: baseAngle +/- angleSpread/2
        uint16_t randAngle;
        if (angleSpread > 0) {
            int spread = (int)(rand() % (unsigned)angleSpread) - (int)(angleSpread / 2);
            randAngle = (uint16_t)(baseAngle + (uint16_t)(int16_t)spread);
        } else {
            randAngle = baseAngle;
        }

        // Compute spawn position with scatter — retry up to 10x if out of bounds
        float spawnX = spawnPos.x;
        float spawnY = spawnPos.y;
        for (int attempt = 0; attempt < 10; attempt++) {
            float tryX = spawnPos.x + SinIdx(randAngle) * 100.0f;
            float tryY = spawnPos.y + CosIdx(randAngle) * 100.0f;
            if (tryX >= COIN_BOUND_X_MIN && tryX <= COIN_BOUND_X_MAX &&
                tryY >= COIN_BOUND_Y_MIN && tryY <= COIN_BOUND_Y_MAX) {
                spawnX = tryX;
                spawnY = tryY;
                break;
            }
            // Re-randomise angle on retry
            if (angleSpread > 0) {
                int spread = (int)(rand() % (unsigned)angleSpread) - (int)(angleSpread / 2);
                randAngle = (uint16_t)(baseAngle + (uint16_t)(int16_t)spread);
            }
        }

        Vec3 coinPos(spawnX, spawnY, spawnPos.z);
        float delay = delayStep * (float)(idx + 1);
        int coinValue = remaining < coinsPerCoin ? remaining : coinsPerCoin;

        coin->InitCoin(coinPos, gravity, baseAngle, 0, randAngle, coinValue,
                       flyFXName, collectFXName, onArrived, delay, silent);

        remaining -= coinsPerCoin;
        idx++;
    }
}
