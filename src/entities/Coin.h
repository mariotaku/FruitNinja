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
#include <cstdint>
#include <functional>

// Forward declarations
struct Renderer;
namespace Mortar { struct PSPParticleEmitter; }

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
    Mortar::PSPParticleEmitter* m_pFlyEmitter;      // +0x68
    Mortar::PSPParticleEmitter* m_pCollectEmitter;  // +0x6C
    std::function<void(Coin*)>  m_OnArrived;        // +0x70  24 bytes (Delegate1<void,Coin*>)

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

    // 0x00173454 — set up all coin fields for a launch
    void InitCoin(const Vec3& pos, const Vec3& gravity, uint16_t baseAngle,
                  int playerIdx, uint16_t launchAngle, int coinValue,
                  const char* flyFXName, const char* collectFXName,
                  std::function<void(Coin*)> onArrived, float delay, bool silent);

    // 0x00173190 — invoke m_OnArrived, cleanup emitters, mark dead
    void Arrived();

    // 0x00173568 — spawn N coins via Mortar::ActorManager::Add(2)
    static void MakeCoins(int totalCoins, int coinsPerCoin, float delayRange,
                          uint16_t baseAngle, uint16_t angleSpread,
                          const Vec3& spawnPos,
                          const char* flyFXName, const char* collectFXName,
                          std::function<void(Coin*)> onArrived, bool silent);

    // 0x001731B8 — mark all active coins dead (arrive=true: credit them first)
    static void ClearCoins(bool arrive);

    // 0x00173114 — set loaded flag; model loaded elsewhere
    static void LoadContent();

    // 0x00173CA8 — null out model SmartPtr
    static void UnLoadContent();

private:
    // 0x00173790 — 5-state machine, called by Update wrapper
    void _Update(float dt);
};

#endif  // FN_COIN_H
