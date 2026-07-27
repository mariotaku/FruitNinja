#ifndef FN_SUPER_FRUIT_CONTROL_H
#define FN_SUPER_FRUIT_CONTROL_H

// SuperFruitControl — one instance per active super fruit (pomegranate/starfruit
// frenzy). HUDControl3d-derived controller attached to a host Fruit. Drives the
// multi-hit combo state machine: freeze/slow global time, accept repeated slashes,
// award bonus score, then explode with a finale VFX burst.
//
// ASM-spec v1.6.1 SuperFruitControl @0x001be1c8: class SuperFruitControl : HUDControl3d : HUDControl
//   (NOT Entity). vptr @0x002ce470, sizeof 0x108. Overrides Release+0x0c, DrawOrder+0x24 @0x001bd7c8,
//   Update+0x28 @0x001bca10. HUD::Update/HUD::Draw drive it; ctor sets m_LayerFlags=0x80.
//
// Registered via HUD::AddControl (born on first slice, in SuperFruitSliced); the
// SuperFruitControls map is a SEPARATE lookup index, NOT the HUD tick list.
//
// Binary sizes: ctor @ 0x001be1c8, restore-from-save ctor @ 0x001bea90.
// sizeof(SuperFruitControl) = 0x108 (confirmed via operator new @ 0x001be630).
//
// Static map: std::map<Fruit*, SuperFruitControl*> SuperFruitControl::SuperFruitControls
// (24-byte std::map per CLAUDE.md).
//
// Key field offsets (binary-confirmed from RE spec):
//   +0x7c: Fruit*        m_pHostFruit      (host fruit entity)
//   +0x80: Fruit*        m_pHostFruit2     (second host-fruit back-ref, set by ctor)
//   +0x84: float         m_HitCount        (combo count; += 1.0 per slice, decays -17.5/s)
//   +0x88: float         m_Timer           (life clock; += dt each frame)
//   +0x8c: float         m_PrevTimer       (previous-frame timer; edge tests)
//   +0x90: int           m_SliceCount      (total slices; incremented by Sliced())
//   +0x94: SlashEntity*  m_pLinkedSlasher  (linked slash entity; nullable)
//   +0xa0: float         m_Lifetime        (explode threshold; phase comparisons)
//   +0xa4: float         m_FadeIn          (0->1 fade-in progress)
//   +0xa8: Vec3          m_SpinAxis        (per-hit slice-dir spin axis; Sliced writes GetSliceDir*3)
//   +0xb4: Vec3          m_TintCurrent     (currently-displayed fruit tint)
//   +0xc0: Vec3          m_TintA           (tint lerp endpoint A)
//   +0xcc: Vec3          m_TintB           (tint lerp endpoint B; per-hit reroll target)
//   +0xd8: float         m_Scale           (0->1 scale-in progress; Sliced resets to 0 for scale-pop)
//   +0xdc: int           m_GlowCounter     (per-hit glow reroll counter)
//   +0xe8: float         m_InnerRadius     (explosion inner shockwave radius)
//   +0xec: float         m_OuterRadius     (explosion outer shockwave radius)
//   +0xf0: Vec3          m_ExplodeOrigin   (explosion epicenter; snapshot of host pos)
//   +0xfc: Vec3          m_ZoomTarget      (camera zoom target; clamped host pos)

#include "hud/HUDControl3d.h"
#include "render/FancyBakedString.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include <map>

class Fruit;
class SlashEntity;
class SuperFruitGlow;
struct SuperFruitState;
#include "engine/xml/TiXmlElement.h"

namespace Mortar { class Entity; }

class SuperFruitControl : public HUDControl3d {
public:
    // HUDControl3d base is 0x7c bytes; own fields follow at +0x7c.

    // +0x7c: host fruit pointer
    Fruit* m_pHostFruit;          // +0x7c

    // +0x80: second host-fruit back-ref, set by the fresh ctor (binary writes
    // fruit into both +0x7c and +0x80). Not cleared by Release.
    Fruit* m_pHostFruit2;         // +0x80

    // +0x84: combo hit count (float; incremented per slice, decays over time)
    float m_HitCount;             // +0x84

    // +0x88: life-clock accumulator (ctor inits -2.0; first slice sets 0, then += dt per frame)
    float m_Timer;                // +0x88

    // +0x8c: previous-frame timer (for phase edge detection)
    float m_PrevTimer;            // +0x8c

    // +0x90: total slices received
    int m_SliceCount;             // +0x90

    // +0x94: linked SlashEntity (nullable; nulled when slice is consumed)
    SlashEntity* m_pLinkedSlasher;// +0x94

    // +0x98: combo popup text (Mortar::FancyBakedString*)
    Mortar::FancyBakedString* m_pComboText;  // +0x98
    // +0x9c: score popup text (Mortar::FancyBakedString*)
    Mortar::FancyBakedString* m_pScoreText;  // +0x9c

    // +0xa0: explosion-phase time baseline (set at construction)
    float m_Lifetime;             // +0xa0

    // +0xa4: fade-in progress [0..1]; += dt*3 per frame
    float m_FadeIn;               // +0xa4

    _Vector3<float> m_SpinAxis;              // +0xa8  per-hit slice-dir spin axis (Sliced: GetSliceDir*3)
    _Vector3<float> m_TintCurrent;           // +0xb4  currently-displayed fruit tint
    _Vector3<float> m_TintA;                 // +0xc0  tint lerp endpoint A
    _Vector3<float> m_TintB;                 // +0xcc  tint lerp endpoint B (per-hit reroll target)

    // +0xd8: scale-in progress [0..1]
    float m_Scale;                // +0xd8

    // +0xdc: per-hit glow reroll counter (decremented in Sliced; drives tint/scale pop)
    int m_GlowCounter;            // +0xdc

    uint8_t _pad_e0[8];           // +0xe0..+0xe7  (unresolved pad)
    float m_InnerRadius;          // +0xe8  explosion inner shockwave radius
    float m_OuterRadius;          // +0xec  explosion outer shockwave radius
    _Vector3<float> m_ExplodeOrigin;         // +0xf0  explosion epicenter (snapshot of host pos)
    _Vector3<float> m_ZoomTarget;            // +0xfc  camera zoom target (clamped host pos; spans to 0x107)

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
    // HUDControl vtable overrides
    // -----------------------------------------------------------------------
    void Update(float dt) override;                          // slot +0x28 @0x001bca10 -- state machine
    void DrawOrder(float* hudScaleRaw, int layerMask) override; // slot +0x24 @0x001bd7c8 -- finale VFX
    void Release() override;                                  // slot +0x0c @0x001bb664

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

    // Binary @ 0x001bda74. Called once from GameInitialise @ 0x0011daa8 (after
    // PowerUpShop::LoadContent). Subscribes SuperFruitSliced to the global
    // Fruit::FruitWasSlicedEvent() so a slice on a super fruit dispatches here
    // without Fruit::CollisionResponse calling it directly.
    static void LoadContent();

    // Frees the super-fruit finale visuals loaded by LoadContent (ShockWaveTexture,
    // JibletModel). Mirrors LoadContent; nulls the file-static SmartPtr globals.
    static void UnLoadContent();

    // Port specific: diagnostic accessor for tests/tooling. Not a binary symbol --
    // exposes whether LoadContent's file-static JibletModel SmartPtr loaded
    // successfully, without giving external code a handle to the SmartPtr itself.
    static bool HasJibletModel();

    // Binary @ 0x001b9828. Returns true while a super fruit is active.
    // Implementation: SuperFruitControls._M_node_count (+0x14) != 0, i.e. !empty().
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
    // Binary sig: (Fruit*, TiXmlElement*) — fruit param is unused in port (map lookup).
    static void SaveSuperFruitState(Fruit* fruit, TiXmlElement* parent);

    // Binary @ 0x001bb52c. Resets global time scale to 1.0, re-enables input,
    // flags all type-6 entities with kill-flag, clears SuperFruitControls map.
    // Named ResetAll (not Reset) because HUDControl3d now brings in a virtual
    // void Reset() -- GCC 4.4.1 rejects a static member hiding an inherited
    // virtual of the same name; this is a class-level (not per-instance) helper.
    static void ResetAll();

    // Binary @ 0x001ba460. Stops (freezes/kills) all fruit entities during super state.
    // Instance method: reads explosion centre from this->m_ExplodeOrigin (+0xf0).
    void StopAllFruit();

    // -----------------------------------------------------------------------
    // Instance methods
    // -----------------------------------------------------------------------

    // Binary @ 0x001bb994. Per-hit combo response: bumps m_SliceCount / m_HitCount,
    // rerolls the glow counter, spawns slash particles, plays combo-pitch SFX,
    // fires combo-number popup (FancyBakedString::ChangeText). Accrues score.
    // slashEntity = aggressor (nullable).
    void Sliced(Mortar::Entity* slashEntity);  // vtable slot override @ 0x001bb994

    // Binary @ 0x001baa20. Finale VFX: spawns 10 or 25 radial jibs, 8 lettered
    // fragments, white flash, resets fruit colour.
    // TODO: v1.6.1 SuperFruitControl::ExplodeSuperFruit @0x001baa20 -- full ExplodeSuperFruit VFX not yet ported
    void ExplodeSuperFruit();

    // Binary @ 0x001b9d60. One-shot (static-flag) TTF glyph-cache warm: bakes
    // "0123456789HITSLICE" at size 50 via a throwaway FancyBakedString so the
    // first mid-combo popup pays no bake hitch. Invoked at the top of ChangeText.
    void PregenerateText();

    // Binary @ 0x001b9ee4. Create-or-replace a combo/score popup label.
    //   text     - the string to bake (e.g. "SLICE!", "5 HITS", "+12")
    //   resetFade - if true, restarts the fade-in (m_FadeIn = 0)
    //   target   - which slot to (re)build; NULL => &m_pComboText.
    // Colour-morphs the fill/stroke by m_SliceCount/35; stroke only on fast HW.
    // Calls PregenerateText() first (binary thunk @0x00112e18).
    void ChangeText(const char* text, bool resetFade, Mortar::FancyBakedString** target);

    // Binary @ 0x001bd4d8 (DrawExplosion): draws the two shockwave rings.
    void DrawExplosion();
    // Port-side extraction of the ring body that v1.6.1
    // SuperFruitControl::DrawExplosion @0x001bd4d8 has inlined twice -- there is
    // no separate DrawRing symbol. One ShockWaveTexture ring of radius r, fading
    // over the 0.25s window ending at `base`.
    void DrawRing(float r, float base);

    // Binary @ 0x1baeb8. Per-frame shockwave: writes PSPParticleManager globals
    // +0x00/+0x04/+0x08, then radially pushes Actor types 0/1/5.
    void UpdateExplosion(float dt);

    // Binary @ 0x1b9b4c. Iterates ActorManager type-6 entities, sets entity+0xe0=1
    // (stop flag). "Rays" are type-6 Entity actors, NOT PSPParticles.
    void StopRays();

    // ASM-spec v1.6.1 SuperFruitControl::SpawnRay @0x001ba810. Spawns one
    // type-6 FruitRay entity via ActorManager::Add(6,true), oriented by a
    // pseudo-random quaternion (rayNum-cycled elevation band + quadrant-swept
    // heading), and Init()s it against this control's host fruit.
    void SpawnRay();

    // Binary @ 0x1bc748. PSPParticleManager emitter hookup for jib particle trails.
    // PSPParticleManager hookup implemented; Jiblet mesh spawn pending Jiblet/MeshManager port.
    void SpawnJibs();

    // Binary @ 0x001b9c6c. Per-frame: radially shove every nearby bomb (type-list 1)
    // away from the host fruit. Called while Timer < Lifetime (throw/anticipation phase).
    void PushBombsAway(float dt);

    // Binary @ 0x001b9850. Combo cancel: forces host fruit's m_SliceTimer to -1
    // when the combo is cancelled while the super fruit is still in anticipation phase
    // (Timer < Lifetime). Subscribed to SlashEntity::OnComboCancelEvent().
    void ComboCancel(SlashEntity* se);

    // Binary @ 0x001b9878. Zoom-done Delegate0<void> callback passed to FruitCamera::
    // StartZoomIn. Re-arms the host fruit's slice timer negative iff the finished
    // transition was a zoom-IN (mirrors ComboCancel's -1.0f re-arm).
    void TransitionFin();
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(SuperFruitControl, m_pHostFruit)      == 0x7c, "SuperFruitControl::m_pHostFruit offset");
static_assert(offsetof(SuperFruitControl, m_pHostFruit2)     == 0x80, "SuperFruitControl::m_pHostFruit2 offset");
static_assert(offsetof(SuperFruitControl, m_HitCount)        == 0x84, "SuperFruitControl::m_HitCount offset");
static_assert(offsetof(SuperFruitControl, m_Timer)           == 0x88, "SuperFruitControl::m_Timer offset");
static_assert(offsetof(SuperFruitControl, m_PrevTimer)       == 0x8c, "SuperFruitControl::m_PrevTimer offset");
static_assert(offsetof(SuperFruitControl, m_SliceCount)      == 0x90, "SuperFruitControl::m_SliceCount offset");
static_assert(offsetof(SuperFruitControl, m_pLinkedSlasher)  == 0x94, "SuperFruitControl::m_pLinkedSlasher offset");
static_assert(offsetof(SuperFruitControl, m_pComboText)      == 0x98, "SuperFruitControl::m_pComboText offset");
static_assert(offsetof(SuperFruitControl, m_pScoreText)      == 0x9c, "SuperFruitControl::m_pScoreText offset");
static_assert(offsetof(SuperFruitControl, m_Lifetime)        == 0xa0, "SuperFruitControl::m_Lifetime offset");
static_assert(offsetof(SuperFruitControl, m_FadeIn)          == 0xa4, "SuperFruitControl::m_FadeIn offset");
static_assert(offsetof(SuperFruitControl, m_SpinAxis)        == 0xa8, "SuperFruitControl::m_SpinAxis offset");
static_assert(offsetof(SuperFruitControl, m_TintCurrent)     == 0xb4, "SuperFruitControl::m_TintCurrent offset");
static_assert(offsetof(SuperFruitControl, m_TintA)           == 0xc0, "SuperFruitControl::m_TintA offset");
static_assert(offsetof(SuperFruitControl, m_TintB)           == 0xcc, "SuperFruitControl::m_TintB offset");
static_assert(offsetof(SuperFruitControl, m_Scale)           == 0xd8, "SuperFruitControl::m_Scale offset");
static_assert(offsetof(SuperFruitControl, m_GlowCounter)     == 0xdc, "SuperFruitControl::m_GlowCounter offset");
static_assert(offsetof(SuperFruitControl, m_InnerRadius)     == 0xe8, "SuperFruitControl::m_InnerRadius offset");
static_assert(offsetof(SuperFruitControl, m_OuterRadius)     == 0xec, "SuperFruitControl::m_OuterRadius offset");
static_assert(offsetof(SuperFruitControl, m_ExplodeOrigin)   == 0xf0, "SuperFruitControl::m_ExplodeOrigin offset");
static_assert(offsetof(SuperFruitControl, m_ZoomTarget)      == 0xfc, "SuperFruitControl::m_ZoomTarget offset");
static_assert(sizeof(SuperFruitControl)                      == 0x108, "SuperFruitControl sizeof wrong (binary 0x108, v1.6.1 @0x12c168)");
#endif

#endif // FN_SUPER_FRUIT_CONTROL_H
