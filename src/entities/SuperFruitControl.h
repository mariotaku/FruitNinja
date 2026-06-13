#ifndef FN_SUPER_FRUIT_CONTROL_H
#define FN_SUPER_FRUIT_CONTROL_H

// SuperFruitControl — one instance per active super fruit (pomegranate/starfruit
// frenzy). Entity-derived controller attached to a host Fruit. Drives the multi-
// hit combo state machine: freeze/slow global time, accept repeated slashes, award
// bonus score, then explode with a finale VFX burst.
//
// Binary sizes: ctor @ 0x001be1c8, restore-from-save ctor @ 0x001bea90.
// sizeof(SuperFruitControl) = 0x108 (confirmed via operator new @ 0x001be630).
//
// Static map: std::map<Fruit*, SuperFruitControl*> SuperFruitControl::SuperFruitControls
// (24-byte std::map per CLAUDE.md).
//
// Key field offsets (binary-confirmed from RE spec):
//   +0x7c: Fruit*        m_pHostFruit      (host fruit entity)
//   +0x84: float         m_HitCount        (combo count; += 1.0 per slice, decays -17.5/s)
//   +0x88: float         m_Timer           (life clock; += dt each frame)
//   +0x8c: float         m_PrevTimer       (previous-frame timer; edge tests)
//   +0x90: int           m_SliceCount      (total slices; incremented by Sliced())
//   +0x94: SlashEntity*  m_pLinkedSlasher  (linked slash entity; nullable)
//   +0xa0: float         m_Lifetime        (explode threshold; phase comparisons)
//   +0xa4: float         m_FadeIn          (0->1 fade-in progress)
//   +0xa8: Vec3          m_WorkVec1        (spin/vel/lerp work vector -- purpose unresolved)
//   +0xb4: Vec3          m_WorkVec2        (spin/vel/lerp work vector -- purpose unresolved)
//   +0xc0: Vec3          m_WorkVec3        (spin/vel/lerp work vector -- purpose unresolved)
//   +0xcc: Vec3          m_WorkVec4        (spin/vel/lerp work vector -- purpose unresolved)
//   +0xd8: float         m_Scale           (0->1 scale-in progress)
//   +0xdc: int           m_SliceCooldown   (decremented; gates re-slice)
//   +0xf0: Vec3          m_WorkVec5        (explosion-origin / work vector -- purpose unresolved)
//   +0xfc: Vec3          m_WorkVec6        (work vector -- purpose unresolved)

#include "Entity.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <map>

class Fruit;
class SlashEntity;
class SuperFruitGlow;
struct SuperFruitState;
struct Renderer;
namespace tinyxml2 { class XMLElement; }

class SuperFruitControl : public Mortar::Entity {
public:
    // +0x3c..+0x7b: Entity base + gap (binary fields unresolved past Entity's 0x3c)
    uint8_t _pad_own[64];         // +0x3c..+0x7b

    // +0x7c: host fruit pointer
    Fruit* m_pHostFruit;          // +0x7c

    uint8_t _pad_80[4];           // +0x80..+0x83

    // +0x84: combo hit count (float; incremented per slice, decays over time)
    float m_HitCount;             // +0x84

    // +0x88: life-clock accumulator (starts at 0, += dt per frame)
    float m_Timer;                // +0x88

    // +0x8c: previous-frame timer (for phase edge detection)
    float m_PrevTimer;            // +0x8c

    // +0x90: total slices received
    int m_SliceCount;             // +0x90

    // +0x94: linked SlashEntity (nullable; nulled when slice is consumed)
    SlashEntity* m_pLinkedSlasher;// +0x94

    uint8_t _pad_98[8];           // +0x98..+0x9f

    // +0xa0: explosion-phase time baseline (set at construction)
    float m_Lifetime;             // +0xa0

    // +0xa4: fade-in progress [0..1]; += dt*3 per frame
    float m_FadeIn;               // +0xa4

    Vec3 m_WorkVec1;              // +0xa8  (spin/vel/lerp work vector -- purpose unresolved)
    Vec3 m_WorkVec2;              // +0xb4  (spin/vel/lerp work vector -- purpose unresolved)
    Vec3 m_WorkVec3;              // +0xc0  (spin/vel/lerp work vector -- purpose unresolved)
    Vec3 m_WorkVec4;              // +0xcc  (spin/vel/lerp work vector -- purpose unresolved)

    // +0xd8: scale-in progress [0..1]
    float m_Scale;                // +0xd8

    // +0xdc: re-slice cooldown counter (decremented per frame; Sliced() gated on == 0)
    int m_SliceCooldown;          // +0xdc

    uint8_t _pad_e0[16];          // +0xe0..+0xef  (explosion-origin / colour -- unresolved)
    Vec3 m_WorkVec5;              // +0xf0  (work vector -- purpose unresolved)
    Vec3 m_WorkVec6;              // +0xfc  (work vector -- purpose unresolved; spans to 0x107)

    // Associated glow entity (not in binary struct; lifetime tied to this controller)
    SuperFruitGlow* m_pGlow;

    // -----------------------------------------------------------------------
    // Static map: indexes all active super-fruit controllers by host fruit.
    // Binary: static std::map<Fruit*, SuperFruitControl*> SuperFruitControls.
    // 24-byte std::map per CLAUDE.md (cached _M_node_count).
    // -----------------------------------------------------------------------
    static std::map<Fruit*, SuperFruitControl*> SuperFruitControls;

    // -----------------------------------------------------------------------
    // Ctors / dtor
    // -----------------------------------------------------------------------

    // Binary @ 0x001be1c8: construct fresh controller for fruit.
    explicit SuperFruitControl(Fruit* fruit);

    // Binary @ 0x001bea90: restore-from-save ctor (used by SaveSuperFruitState).
    SuperFruitControl(Fruit* fruit, SuperFruitState& state);

    ~SuperFruitControl();

    // -----------------------------------------------------------------------
    // Entity vtable overrides
    // -----------------------------------------------------------------------
    void Update(float dt) override;     // 0x001bca10 -- state machine
    void Draw(Renderer& r) override;    // TODO: 0x001bd7c8
    void PostUpdate(float dt) override; // TODO
    void Release() override;            // 0x001bb664

    // -----------------------------------------------------------------------
    // Static spawn/query interface
    // -----------------------------------------------------------------------

    // Binary @ 0x001bbf48. Called when ANY fruit is thrown. Gate: FruitInfo[+0x330] != 0
    // AND !(fruit->flags & 0x10). Sets up the throw trajectory AND spawns a SuperFruitGlow
    // (`new 0x8c`) at throw time; does NOT create the SuperFruitControl.
    static void SuperFruitThrown(Fruit* fruit);

    // Binary @ 0x001be630. Slice dispatch: lookup map by fruit; if found, forward to
    // instance Sliced(). If not found (first hit), creates/dispatches the SuperFruitControl
    // (`new 0x108`) via the SuperFruitControls map.
    // idx = slash index; slashEntity = the SlashEntity that triggered the collision.
    static void SuperFruitSliced(Fruit* fruit, int idx, Mortar::Entity* slashEntity);

    // Binary @ 0x001b9828. Returns true while a super fruit is active.
    // Implementation: game singleton +0x14 field != 0.
    static bool IsInSuperFruitState();

    // Binary @ 0x001b98c0. Returns how many super (pomegranate) fruits have been
    // spawned during this game session.
    static int NumPomegranatesSpawnedThisGame();

    // Binary @ 0x001b99d4. Returns true if it is permissible to spawn the final
    // pomegranate (game-mode gating; details unresolved).
    static bool CanSpawnFinalPomegranate();

    // Binary @ 0x001b98f4. Spawns the terminal pomegranate fruit into the wave.
    // Returns true (binary returns CONCAT44(undef,1)).
    static bool SpawnFinalPomegranate();

    // Binary @ 0x001ba73c. Serializes the active super-fruit state to XML for save.
    static void SaveSuperFruitState(tinyxml2::XMLElement* parent);

    // Binary @ 0x001bb52c. Resets global time scale to 1.0, re-enables input,
    // flags all type-6 entities with kill-flag, clears SuperFruitControls map.
    static void Reset();

    // Binary @ 0x001ba460. Stops (freezes/kills) all fruit entities during super state.
    // Instance method: reads explosion centre from this->m_WorkVec5 (+0xf0).
    void StopAllFruit();

    // -----------------------------------------------------------------------
    // Instance methods
    // -----------------------------------------------------------------------

    // Binary @ 0x001bb994. Per-hit combo response: bumps m_SliceCount / m_HitCount,
    // applies slice cooldown, spawns slash particles, plays combo-pitch SFX,
    // fires combo-number popup (FancyBakedString::ChangeText). Accrues score.
    // slashEntity = aggressor (nullable).
    void Sliced(Mortar::Entity* slashEntity);  // vtable slot override @ 0x001bb994

    // Binary @ 0x001baa20. Finale VFX: spawns 10 or 25 radial jibs, 8 lettered
    // fragments, white flash, resets fruit colour.
    // TODO: 0x001baa20 -- full ExplodeSuperFruit VFX not yet ported
    void ExplodeSuperFruit();

    // Binary @ 0x1baeb8. Per-frame shockwave: writes PSPParticleManager globals
    // +0x00/+0x04/+0x08, then radially pushes Actor types 0/1/5.
    void UpdateExplosion(float dt);

    // Binary @ 0x1b9b4c. Iterates ActorManager type-6 entities, sets entity+0xe0=1
    // (stop flag). "Rays" are type-6 Entity actors, NOT PSPParticles.
    void StopRays();

    // Binary @ 0x1bc748. PSPParticleManager emitter hookup for jib particle trails.
    // TODO: 0x1bc748 — SpawnJibs PSPParticleManager hookup pending asm-inspector
    //   resolution of DirCos/DirSin offset (+0x2C vs +0x30 ambiguity in v1.6.1).
    void SpawnJibs(int count);

    // Binary @ 0x001b9850. Combo cancel: clears linked slash entity when that
    // entity's combo is cancelled (e.g. swipe released mid-combo).
    void ComboCancel(SlashEntity* se);

private:
    // Spawn the glow entity and attach it to m_pHostFruit.
    // Called from both ctors.
    void AttachGlow();
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(SuperFruitControl, m_pHostFruit)      == 0x7c, "SuperFruitControl::m_pHostFruit offset");
static_assert(offsetof(SuperFruitControl, m_HitCount)        == 0x84, "SuperFruitControl::m_HitCount offset");
static_assert(offsetof(SuperFruitControl, m_Timer)           == 0x88, "SuperFruitControl::m_Timer offset");
static_assert(offsetof(SuperFruitControl, m_PrevTimer)       == 0x8c, "SuperFruitControl::m_PrevTimer offset");
static_assert(offsetof(SuperFruitControl, m_SliceCount)      == 0x90, "SuperFruitControl::m_SliceCount offset");
static_assert(offsetof(SuperFruitControl, m_pLinkedSlasher)  == 0x94, "SuperFruitControl::m_pLinkedSlasher offset");
static_assert(offsetof(SuperFruitControl, m_Lifetime)        == 0xa0, "SuperFruitControl::m_Lifetime offset");
static_assert(offsetof(SuperFruitControl, m_FadeIn)          == 0xa4, "SuperFruitControl::m_FadeIn offset");
static_assert(offsetof(SuperFruitControl, m_WorkVec1)        == 0xa8, "SuperFruitControl::m_WorkVec1 offset");
static_assert(offsetof(SuperFruitControl, m_WorkVec2)        == 0xb4, "SuperFruitControl::m_WorkVec2 offset");
static_assert(offsetof(SuperFruitControl, m_WorkVec3)        == 0xc0, "SuperFruitControl::m_WorkVec3 offset");
static_assert(offsetof(SuperFruitControl, m_WorkVec4)        == 0xcc, "SuperFruitControl::m_WorkVec4 offset");
static_assert(offsetof(SuperFruitControl, m_Scale)           == 0xd8, "SuperFruitControl::m_Scale offset");
static_assert(offsetof(SuperFruitControl, m_SliceCooldown)   == 0xdc, "SuperFruitControl::m_SliceCooldown offset");
static_assert(offsetof(SuperFruitControl, m_WorkVec5)        == 0xf0, "SuperFruitControl::m_WorkVec5 offset");
static_assert(offsetof(SuperFruitControl, m_WorkVec6)        == 0xfc, "SuperFruitControl::m_WorkVec6 offset");
#endif

#endif // FN_SUPER_FRUIT_CONTROL_H
