#ifndef FN_COIN_H
#define FN_COIN_H

// Coin : Mortar::Entity — entity type 2.
// Binary struct: 0x94 / 148 bytes. Pool-based via Mortar::ActorManager(2).
//
// Binary references:
//   Coin::Coin   (C1)  0x00173394
//   Coin::Coin   (C2)  0x001732D4
//   ~Coin        (D1)  v1.6.1 @0x001d7a90
//   ~Coin        (D2)  v1.6.1 @0x001d7ae4  (base object dtor, identical body)
//   ~Coin        (D0)  v1.6.1 @0x001d7b38
//   Coin::Init        0x0019D5FC  (stub/empty)
//   Coin::Release     v1.6.1 @0x001d7a5c  (vtable-dispatch only; NOT called from dtor)
//   Coin::Update      0x0017312C  (fixed-timestep wrapper)
//   Coin::_Update     v1.6.1 @0x001d81bc  (5-state machine; v1.5.1 was 0x00173790)
//   Coin::Draw        0x00173CC4
//   Coin::DrawUpdate  0x0017318C  (empty)
//   Coin::InitCoin    v1.6.1 @0x001d7d84  (v1.5.1 was 0x00173454)
//   Coin::Arrived     0x00173190
//   Coin::MakeCoins   v1.6.1 @0x001d7ec8  (v1.5.1 was 0x00173568)
//   Coin::ClearCoins  v1.6.1 @0x001d7a00  (thunk 0x00106eb8)
//   Coin::LoadContent   0x00173114
//   Coin::UnLoadContent 0x00173CA8
//   CoinArrived (free) 0x0017320C
//
// Analysed: 2026-04-12T16:45

#include "Entity.h"
#include "util/Delegate.h"
#include <cstdint>

// Forward declarations
struct Renderer;
struct PSPParticleEmitter;

class Coin : public Mortar::Entity {
public:
    // --- Binary fields beyond Mortar::Entity base (Mortar::Entity = 0x3C bytes) --------
    // m_Angle at +0x36 is now in the Mortar::Entity base (added 2026-05-04).
    // Coin's own angle field mapped to Mortar::Entity::m_Angle; callers already use
    // this->m_Angle via the base class.

    int      m_CoinValue;        // +0x3C  coin value credited on arrival
    int      m_State;            // +0x40  0=waiting,1=arrived,2=flying,3=decel,4=homing
    float    m_Timer;            // +0x44  delay countdown (state 0) / elapsed (states 3,4);
                                  //        state 4 elapsed also drives the HOMING rampFactor
                                  //        curve (v1.6.1 Coin::_Update @0x001d81bc)
    uint8_t  m_Silent;           // +0x48  if nonzero, skip SFX on launch
    float    m_Speed;            // +0x4C  launch speed (computed in InitCoin)
    uint16_t m_SpinAngle;        // +0x50  Y-axis visual spin accumulator
    uint32_t m_FlyFXHash;        // +0x54  StringHash of fly particle effect name
    uint32_t m_CollectFXHash;    // +0x58  StringHash of collect particle effect name
    // states 3/4 only -- NOT a gravity accel (v1.6.1 Coin::_Update @0x001d81bc)
    float    m_TargetX;          // +0x5C  homing target X
    float    m_TargetY;          // +0x60  homing target Y
    float    m_TargetZ;          // +0x64  homing target Z
    PSPParticleEmitter* m_pFlyEmitter;      // +0x68
    PSPParticleEmitter* m_pCollectEmitter;  // +0x6C
    Mortar::Delegate1<void, Coin*>  m_OnArrived;        // +0x70  36 bytes (Mortar::Delegate1<void,Coin*>)

    // --- Constructor / destructor ----------------------------------------

    Coin();
    ~Coin() override;

    // --- Vtable overrides ------------------------------------------------

    // 0x0019D5FC — Coin uses the base no-op Init (vtable slot 2 = base 0x0019d5fc).
    // Coin is fully initialised by its ctor + MakeCoins/InitCoin; this override
    // is kept so the compiler knows the virtual is satisfied (same function ptr).
    void Init(void* p1, long p2, Vec3* p3) override;

    // v1.6.1 @0x001d7a5c — clear fly emitter; vtable-dispatch only, NOT called
    // from the destructor (D1 body has no bl to Release; confirmed via xref check).
    void Release() override;

    // 0x0017312C — fixed-timestep wrapper; subdivides dt by 1/60
    void Update(float dt) override;

    // 0x0017318C — empty in binary
    void PostUpdate(float dt) override;

    // 0x00173CC4 — scale × RotY(spin) × RotZ(heading) × Translate
    void Draw(Renderer& r) override;

    // Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
    void Deactivate();

    // --- Public API ------------------------------------------------------

    // v1.6.1 Coin::InitCoin @0x001d7d84 — set up all coin fields for a launch.
    //
    // Binary signature (HFA): InitCoin(_Vector3<float> gravity /*s0-s2*/,
    //   _Vector3<float>* pos /*r1*/, _Vector3<float>* target /*r2*/,
    //   ushort baseAngle /*r3*/, ushort launchAngle /*sp+0x.. -> +0x36 angle*/,
    //   int coinValue /*sp -> +0x3c*/, ulong flyFXHash /*sp -> +0x54*/,
    //   ulong collectFXHash /*sp -> +0x58*/, Delegate1<void,Coin*> onArrived
    //   /*sp -> +0x70*/, bool silent /*sp -> +0x48*/).
    //
    // Field writes proven from disassembly:
    //   +0x44 m_Timer   = -gravity.x           (vneg s16; vstr [r4,#0x44])
    //   +0x36 angle     = (ushort)launchAngle  (strh r10,[r4,#0x36])
    //   +0x40 m_State   = 0                     (str r6,[r4,#0x40])
    //   +0x3c m_CoinValue                       (str [r4,#0x3c])
    //   +0x4c m_Speed   = (500 + rand/524287*550) * 0.66
    //   +0x10 pos       = *r1 ; +0x5c target = *r2 ; +0x1c vel = DAT vec
    //   +0x54 m_FlyFXHash     = flyFXHash raw (uint32, stored as-is -- NOT hashed here)
    //   +0x58 m_CollectFXHash = collectFXHash raw (uint32, stored as-is -- NOT hashed here)
    //   +0x68/+0x6c emitters = 0 ; +0x70 m_OnArrived = onArrived ; +0x50 = 0
    //
    // The null-name -> "coin_fly"/"coin_collect" default substitution and the
    // StringHash() call both happen in the CALLER (MakeCoins @0x001d7ec8), not here.
    // InitCoin takes pre-hashed StringHashes and stores them raw.
    void InitCoin(Vec3 pos, Vec3 gravity, uint16_t angle, int coinValue,
                  unsigned long flyFXHash, unsigned long collectFXHash,
                  Mortar::Delegate1<void, Coin*> onArrived, float delay, bool silent);

    // 0x00173190 — invoke m_OnArrived, cleanup emitters, mark dead
    void Arrived();

    // v1.6.1 Coin::MakeCoins @0x001d7ec8 — spawn N coins via Mortar::ActorManager::Add(2).
    // Binary sig (12 params, this excluded):
    //   (int totalCoins, int coinsPerCoin, Vec3 delay, ushort baseAngle, ushort angleSpread,
    //    Vec3* spawnPos, float delayStep, float delayCap, char* flyFXName,
    //    char* collectFXName, Delegate1<void,Coin*>, bool silent)
    //   delayStep/delayCap exact semantics unconfirmed (labeled s0/s1 in Ghidra decompile);
    //   port maps them to per-coin delay step and max delay cap respectively.
    // TODO: v1.6.1 Coin::MakeCoins @0x001d7ec8 -- confirm delayStep/delayCap semantic
    //   against full binary decompile (currently inferred from per-coin stagger logic).
    //
    // Behaviour: default FX names ("coin_fly"/"coin_collect") filled from DAT_00173784/88
    //   when null; both StringHash'd here and the hashes passed to InitCoin.
    //   Per-coin loop: ActorManager::Add(2,true), random angle in baseAngle+/-spread,
    //   up to 10 retries against screen bounds, then InitCoin.
    static void MakeCoins(int totalCoins, int coinsPerCoin, Vec3 delay,
                          uint16_t baseAngle, uint16_t angleSpread,
                          Vec3* spawnPos,
                          float delayStep, float delayCap,
                          const char* flyFXName, const char* collectFXName,
                          Mortar::Delegate1<void, Coin*> onArrived, bool silent);

    // v1.6.1 @0x001d7a00 (thunk 0x00106eb8) — unconditional sweep of all type-2
    // entities (no IsActive() gate); arrive=true credits via Arrived(), else
    // ORs ENT_INACTIVE|ENT_KILLED (0x11) directly.
    static void ClearCoins(bool arrive);

    // Returns a Delegate1<void,Coin*> bound to the file-static CoinArrived helper
    // (binary @ 0x0017320C). Callers outside Coin.cpp use this to obtain the
    // standard arrived callback without needing to know CoinArrived's linkage.
    static Mortar::Delegate1<void, Coin*> DefaultArrivedDelegate();

    // 0x00173114 — set loaded flag; model loaded elsewhere
    static void LoadContent();

    // 0x00173CA8 — null out model SmartPtr
    static void UnLoadContent();

private:
    // v1.6.1 @0x001d81bc — 5-state machine, called by Update wrapper
    void _Update(float dt);
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(Coin) == 0x94, "Coin size mismatch"); // v1.6.1 Coin @0x1d90b8
#endif

// v1.6.1 AddToScoreOnArrival @0x162ab8
// Bonus-mode coin arrival handler. Cycles a firework counter (g_oneInThree) and at
// counts 3, 6, and >8 fires camera shake + "Bonus-Firework-Explode" SFX + two
// particle emitters ("bonus_mode_fx_red" and "arcade_confetti"), then credits
// coin->m_CoinValue via AddToCurrentScore.
void AddToScoreOnArrival(Coin* coin);

#endif  // FN_COIN_H
