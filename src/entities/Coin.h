#ifndef FN_COIN_H
#define FN_COIN_H

// Coin : Mortar::Entity — entity type 2.
// Binary struct: 0x94 / 148 bytes. Pool-based via Mortar::ActorManager(2).
//
// Binary references:
//   Coin::Coin   (C1)  v1.6.1 @0x001d7b94
//   Coin::Coin   (C2)  v1.6.1 @0x001d7c8c
//   ~Coin        (D1)  v1.6.1 @0x001d7a90
//   ~Coin        (D2)  v1.6.1 @0x001d7ae4  (base object dtor, identical body)
//   ~Coin        (D0)  v1.6.1 @0x001d7b38
//   Coin::Init        v1.6.1 Mortar::Entity::Init @0x0025623c  (Coin has no own
//                     override; vtable slot 2 points straight at the base no-op)
//   Coin::Release     v1.6.1 @0x001d7a5c  (vtable-dispatch only; NOT called from dtor)
//   Coin::Update      v1.6.1 @0x001d7940  (fixed-timestep wrapper)
//   Coin::_Update     v1.6.1 @0x001d81bc  (5-state machine)
//   Coin::Draw        v1.6.1 @0x001d8810
//   Coin::DrawUpdate  v1.6.1 @0x001d79b8  (body is `return this;` only)
//   Coin::InitCoin    v1.6.1 @0x001d7d84
//   Coin::Arrived     v1.6.1 @0x001d79bc
//   Coin::MakeCoins   v1.6.1 @0x001d7ec8
//   Coin::ClearCoins  v1.6.1 @0x001d7a00  (thunk 0x00106eb8)
//   Coin::LoadContent   v1.6.1 @0x001d7920
//   Coin::UnLoadContent v1.6.1 @0x001d87f0
//   CoinArrived       v1.6.1 @0x001d7a88  (_Z11CoinArrivedP4Coin) — 2 instructions:
//                     ldr r0,[r0,#0x3c] (m_CoinValue) then b -> PLT thunk 0x0010c734
//                     -> GOT 0x002d45b0 -> AddCoins @0x00119f78
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

    // v1.6.1 Mortar::Entity::Init @0x0025623c — Coin uses the base no-op Init
    // (vtable slot 2 points at the base body).
    // Coin is fully initialised by its ctor + MakeCoins/InitCoin; this override
    // is kept so the compiler knows the virtual is satisfied (same function ptr).
    void Init(void* p1, long p2, _Vector3<float>* p3) override;

    // v1.6.1 @0x001d7a5c — clear fly emitter; vtable-dispatch only, NOT called
    // from the destructor (D1 body has no bl to Release; confirmed via xref check).
    void Release() override;

    // v1.6.1 Coin::Update @0x001d7940 — fixed-timestep wrapper; subdivides dt by 1/60
    void Update(float dt) override;

    // v1.6.1 Coin::DrawUpdate @0x001d79b8 — body is `return this;` only
    void PostUpdate(float dt) override;

    // v1.6.1 Coin::Draw @0x001d8810 — scale × RotY(spin) × RotZ(heading) × Translate
    void Draw(Renderer& r) override;

    // Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
    void Deactivate();

    // --- Public API ------------------------------------------------------

    // v1.6.1 Coin::InitCoin @0x001d7d84 — set up all coin fields for a launch.
    //
    // Binary signature (HFA, stale/unverified against the current 9-param port
    // signature below -- the "gravity" name here is the same historical misnomer
    // fixed on the `target` param; needs a follow-up disasm pass to reconcile):
    //   InitCoin(_Vector3<float> gravity /*s0-s2*/,
    //   _Vector3<float>* pos /*r1*/, _Vector3<float>* target /*r2*/,
    //   ushort baseAngle /*r3*/, ushort launchAngle /*sp+0x.. -> +0x36 angle*/,
    //   int coinValue /*sp -> +0x3c*/, ulong flyFXHash /*sp -> +0x54*/,
    //   ulong collectFXHash /*sp -> +0x58*/, Delegate1<void,Coin*> onArrived
    //   /*sp -> +0x70*/, bool silent /*sp -> +0x48*/).
    //
    // Field writes proven from disassembly:
    //   +0x44 m_Timer   = -delay                (vneg; vstr [r4,#0x44])
    //   +0x36 angle     = (ushort)launchAngle  (strh r10,[r4,#0x36])
    //   +0x40 m_State   = 0                     (str r6,[r4,#0x40])
    //   +0x3c m_CoinValue                       (str [r4,#0x3c])
    //   +0x4c m_Speed   = (500 + rand/524287*550) * 0.66
    //   +0x10 pos       = *r1 ; +0x5c/+0x60/+0x64 m_TargetX/Y/Z = *r2 ; +0x1c vel = DAT vec
    //   +0x54 m_FlyFXHash     = flyFXHash raw (uint32, stored as-is -- NOT hashed here)
    //   +0x58 m_CollectFXHash = collectFXHash raw (uint32, stored as-is -- NOT hashed here)
    //   +0x68/+0x6c emitters = 0 ; +0x70 m_OnArrived = onArrived ; +0x50 = 0
    //
    // The `target` param (r2, was misnamed "gravity" -- it is NOT a gravity accel,
    // it is the homing destination stored into m_TargetX/Y/Z, consumed by states 3/4)
    // is passed by value here; MakeCoins resolves a NULL Vec3* target to (220,-140,0)
    // (COIN_DEFAULT_TARGET) before calling InitCoin.
    //
    // The null-name -> "coin_fly"/"coin_collect" default substitution and the
    // StringHash() call both happen in the CALLER (MakeCoins @0x001d7ec8), not here.
    // InitCoin takes pre-hashed StringHashes and stores them raw.
    void InitCoin(_Vector3<float> pos, _Vector3<float> target, uint16_t angle, int coinValue,
                  unsigned long flyFXHash, unsigned long collectFXHash,
                  Mortar::Delegate1<void, Coin*> onArrived, float delay, bool silent);

    // v1.6.1 Coin::Arrived @0x001d79bc — invoke m_OnArrived, cleanup emitters, mark dead
    void Arrived();

    // ASM-spec v1.6.1 Coin::MakeCoins @0x001d7ec8 — spawn N coins via
    // Mortar::ActorManager::Add(2). Binary demangled sig (this excluded):
    //   (int totalCoins, int coinsPerCoin, Vec3 spawnPos (BY VALUE), ushort baseAngle,
    //    ushort angleSpread, Vec3* target, float delayStep, float delayCap,
    //    char* flyFXName, char* collectFXName, Delegate1<void,Coin*> onArrived, bool silent)
    //   target: homing destination passed through to InitCoin; NULL -> resolved here to
    //   (220,-140,0) (COIN_DEFAULT_TARGET) before the InitCoin call.
    //   delayStep/delayCap exact semantics unconfirmed (labeled s0/s1 in Ghidra decompile);
    //   port maps them to per-coin delay step and max delay cap respectively.
    // TODO: v1.6.1 Coin::MakeCoins @0x001d7ec8 -- confirm delayStep/delayCap semantic
    //   against full binary decompile (currently inferred from per-coin stagger logic).
    //
    // Behaviour: default FX names ("coin_fly"/"coin_collect") filled from DAT_00173784/88
    //   when null; both StringHash'd here and the hashes passed to InitCoin.
    //   Per-coin loop: ActorManager::Add(2,true), random angle in baseAngle+/-spread,
    //   up to 10 retries against screen bounds, then InitCoin.
    static void MakeCoins(int totalCoins, int coinsPerCoin, _Vector3<float> spawnPos,
                          uint16_t baseAngle, uint16_t angleSpread,
                          _Vector3<float>* target, float delayStep, float delayCap,
                          const char* flyFXName, const char* collectFXName,
                          Mortar::Delegate1<void, Coin*> onArrived, bool silent);

    // v1.6.1 @0x001d7a00 (thunk 0x00106eb8) — unconditional sweep of all type-2
    // entities (no IsActive() gate); arrive=true credits via Arrived(), else
    // ORs ENT_INACTIVE|ENT_KILLED (0x11) directly.
    static void ClearCoins(bool arrive);

    // Returns a Delegate1<void,Coin*> bound to the file-static CoinArrived helper.
    // Callers outside Coin.cpp use this to obtain the standard arrived callback
    // without needing to know CoinArrived's linkage.
    // (v1.6.1 CoinArrived @0x001d7a88 — see the file header.)
    static Mortar::Delegate1<void, Coin*> DefaultArrivedDelegate();

    // v1.6.1 Coin::LoadContent @0x001d7920 — the binary body is ONLY
    // `s_isContentLoaded = 1; return;` (six instructions): no MeshManager load
    // and no guard. The `if (s_isContentLoaded == 0)` test lives in the caller
    // (Coin::Coin @0x001d7b94). Coins therefore have NO 3D model at all: the
    // s_coinModel SmartPtr stays NULL for the process lifetime and Coin::Draw
    // @0x001d8810 short-circuits on its null gate, so a coin is only its
    // fly/collect particle FX. Do not re-add a coin.mmd load -- the asset ships
    // but the binary never references it, and coins ARE spawned on-screen every
    // game-over (BonusScreen::AwardScores @0x0016393c), so loading it puts a
    // mesh in front of the player that the original never drew.
    static void LoadContent();

    // v1.6.1 Coin::UnLoadContent @0x001d87f0 — the binary body is ONLY
    // SmartPtr<Model>::SetPtr(&s_coinModel, NULL). It does NOT reset the
    // loaded flag, so LoadContent is effectively once-per-process.
    static void UnLoadContent();

private:
    // v1.6.1 Coin::_Update @0x001d81bc — 5-state machine, called by the Update
    // wrapper once per 1/60 s substep.
    //
    // Contract (see the control-flow map in Coin.cpp above the definition):
    //  - The collect/sparkle emitter (+0x6C) is torn down at the TOP of EVERY
    //    call, so it only exists across the gap between two coin updates. Any
    //    harness that ticks coins must run PSPParticleManager::Update inside
    //    that gap (i.e. ActorManager::Update then PSPParticleManager::Update,
    //    the GameUpdate order) or no coin particle is ever spawned.
    //  - The sparkle is a ONE-SHOT at two sites: the DECEL timer crossing 0.01s,
    //    and the HOMING arrival branch (dist < 30). It is NOT re-emitted per
    //    frame. Both sites share the throttle
    //    `GetNumEntities(2) < 20 || Rand32(3) == 0`; that Rand32 draw is on the
    //    shared LCG stream, so the short-circuit order is load-bearing.
    //  - The trail emitter (+0x68) is spawned at ONE shared transition label,
    //    reachable at most once per coin: on the launch tick for non-silent
    //    coins (state 0 -> 4) and on the FLYING -> DECEL tick for silent ones.
    //    It is never cleared inside _Update -- only Arrived / Release /
    //    Deactivate clear it -- so it persists through HOMING.
    //  - Neither emitter registers a back-ref (AddEmitter(hash, NULL, false)),
    //    matching the binary: a naturally-reaped emitter therefore leaves a
    //    stale pointer, which is safe because emitters are pool slots.
    //  - `pos += vel * dt` happens at exactly one shared tail site covering
    //    states 0/2/3/4-homing/default. The ARRIVED state and the HOMING
    //    arrival branch skip it.
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

// ASM-spec v1.6.1 BonusScreen::AwardScores @0x0016393c: writes g_oneInThree = 3
// (@0x00163c08) right before its own CreateCameraShake, priming the SAME
// file-static counter AddToScoreOnArrival (@0x00162ab8, above) reads/increments,
// so the next coin landing deterministically hits the ==3 firework branch.
// Exposes Coin.cpp's file-static g_oneInThree to other translation units.
void Coin_PrimeOneInThree(int value);

#endif  // FN_COIN_H
