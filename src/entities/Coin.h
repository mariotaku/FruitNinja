#ifndef FN_COIN_H
#define FN_COIN_H

// Coin : Mortar::Entity — entity type 2.
// Binary struct: 0x94 / 148 bytes. Pool-based via Mortar::ActorManager(2).
//
// Binary references:
//   Coin::Coin   (C1)  0x00173394
//   Coin::Coin   (C2)  0x001732D4
//   ~Coin        (D1)  0x00173218
//   ~Coin        (D0)  0x00173290
//   Coin::Init        0x0019D5FC  (stub/empty)
//   Coin::Release     0x001731F4
//   Coin::Update      0x0017312C  (fixed-timestep wrapper)
//   Coin::_Update     0x00173790  (5-state machine)
//   Coin::Draw        0x00173CC4
//   Coin::DrawUpdate  0x0017318C  (empty)
//   Coin::InitCoin    0x00173454
//   Coin::Arrived     0x00173190
//   Coin::MakeCoins   0x00173568
//   Coin::ClearCoins  0x001731B8
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
    float    m_Timer;            // +0x44  delay countdown (state 0) / elapsed (states 3,4)
    uint8_t  m_Silent;           // +0x48  if nonzero, skip SFX on launch
    float    m_Speed;            // +0x4C  launch speed (computed in InitCoin)
    uint16_t m_SpinAngle;        // +0x50  Y-axis visual spin accumulator
    uint32_t m_FlyFXHash;        // +0x54  StringHash of fly particle effect name
    uint32_t m_CollectFXHash;    // +0x58  StringHash of collect particle effect name
    float    m_TargetX;          // +0x5C  homing target X (also carries gravity.x in state 0-3)
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

    // 0x001731F4 — clear fly emitter; called from dtor
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

    // 0x00173454 — set up all coin fields for a launch.
    // ASM-verified: 2026-06-07 v1.6.1 binary @ 0x00173454 (disassembly inspected).
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
    //   +0x54 m_FlyFXHash     = (raw uint32, NOT a name)
    //   +0x58 m_CollectFXHash = (raw uint32, NOT a name)
    //   +0x68/+0x6c emitters = 0 ; +0x70 m_OnArrived = onArrived ; +0x50 = 0
    //
    // DIFFERS: original InitCoin takes pre-hashed flyFXHash / collectFXHash
    //   (uint32, hashed by MakeCoins via StringHash before the call); the port
    //   passes the FX names through and StringHashes them inside InitCoin
    //   (Coin.cpp). Net effect is identical (StringHash(name) lands in the same
    //   field). The earlier "port over-ported names the binary lacks" note was
    //   wrong: the binary's FX-name args are real, with "coin_fly"/"coin_collect"
    //   defaults built in MakeCoins (DAT_00173784/DAT_00173788). The 9-vs-11
    //   "param count" gap was an HFA decompiler artifact (Vec3-by-value counts
    //   as one logical arg but three VFP regs; hidden `this`).
    void InitCoin(Vec3 pos, Vec3 gravity, uint16_t baseAngle,
                  int playerIdx, uint16_t launchAngle, int coinValue,
                  const char* flyFXName, const char* collectFXName,
                  Mortar::Delegate1<void, Coin*> onArrived, float delay, bool silent);

    // 0x00173190 — invoke m_OnArrived, cleanup emitters, mark dead
    void Arrived();

    // 0x00173568 — spawn N coins via Mortar::ActorManager::Add(2).
    // ASM-verified: 2026-06-07 v1.6.1 binary @ 0x00173568 (disassembly inspected).
    //
    // Binary signature (HFA): MakeCoins(int totalCoins /*r0*/,
    //   int coinsPerCoin /*r1*/, _Vector3<float> delay /*s0-s2*/,
    //   ushort baseAngle /*sp+0xc0 -> r11*/, ushort angleSpread,
    //   _Vector3<float>* spawnPos /*r2 -> r7*/, char* flyFXName /*sp+0xc4*/,
    //   char* collectFXName /*sp+0xc8*/, Delegate1<void,Coin*> onArrived
    //   /*sp+0xd0*/, bool silent /*sp+0xd4*/).
    //
    // Behaviour proven from disassembly:
    //   - flyFXName  default = DAT_00173784 (-> r6 base) when null  (0x1735e0)
    //   - collectFXName default = DAT_00173788 when null            (0x1735ee)
    //   - both names StringHash'd here (0x1735e8/0x1735f6) and the resulting
    //     uint32 hashes passed to InitCoin (NOT the names themselves).
    //   - per-coin loop: ActorManager::GetInstance()->Add(2,true), random
    //     launch angle baseAngle +/- spread (Rand32 + SinIdx/CosIdx scatter),
    //     up to 10 retries against screen bounds, then InitCoin(...).
    //
    // DIFFERS: the "extra float between spawnPos and flyFXName" in the earlier
    //   note was an HFA mis-read of the `delay` Vec3 (s0/s1/s2). There is no
    //   stray scalar: the binary has exactly the args listed above. Port keeps
    //   `Vec3 delay` (delay.x = per-coin step, delay.y = total cap) and passes
    //   names to InitCoin (hashing deferred into InitCoin) — same net effect.
    static void MakeCoins(int totalCoins, int coinsPerCoin, Vec3 delay,
                          uint16_t baseAngle, uint16_t angleSpread,
                          Vec3* spawnPos,
                          const char* flyFXName, const char* collectFXName,
                          Mortar::Delegate1<void, Coin*> onArrived, bool silent);

    // 0x001731B8 — mark all active coins dead (arrive=true: credit them first)
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
    // 0x00173790 — 5-state machine, called by Update wrapper
    void _Update(float dt);
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(Coin) == 0x94, "Coin size mismatch"); // v1.6.1 Coin @0x1d90b8
#endif

#endif  // FN_COIN_H
