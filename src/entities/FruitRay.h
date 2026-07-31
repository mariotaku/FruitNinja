#ifndef FN_ENTITIES_FRUITRAY_H
#define FN_ENTITIES_FRUITRAY_H

//
// FruitRay : Mortar::Entity (size = 0xE4 / 228 bytes). Entity type 6.
// Super-fruit "rays" VFX -- a burst of oriented ray quads spun off the host
// fruit while a super-fruit combo is active (see SuperFruitControl::SpawnRay,
// SuperFruitControl::StopRays). Binary: ctor @0x001d954c, Init @0x001e4740,
// Update @0x001e45e0.
//
// Rendering is a STANDALONE static batch (FruitRay::DrawRays @0x001e4ac4),
// called directly from GameDraw -- NOT ActorManager's per-entity Draw
// dispatch. ActorManager::Draw DOES call Entity::Draw(r) on every type-6
// entity too (it has no type-based skip), so the Draw/PostUpdate overrides
// below stay empty per the binary (FruitRay has no vtable Draw override
// distinct from the base no-op-shaped behaviour); the actual per-ray render
// work lives in DrawRay(), invoked only via the DrawRays() static batch.
//

#include "Entity.h"
#include "math/_Vector3.h"
#include "math/Matrix44.h"
#include "math/Quaternion.h"
#include "util/SmartPtr.h"
#include <cstdint>

namespace Mortar { class Texture; }
class Fruit;
struct Renderer;

class FruitRay : public Mortar::Entity {
public:
    // Entity base is 0x3C bytes (see Entity.h); FruitRay's own fields follow.
    // NOTE: the spec's "+0x28 m_ColourCurrent" is the Entity base's own
    // `scale` slot (Entity+0x28, a Vec3) reused by FruitRay for the live
    // lerped ray colour -- there is no separate FruitRay field at +0x28.
    // `scale` (inherited) IS m_ColourCurrent for this class.

    Matrix44 m_WorldMatrix;   // +0x3C  rebuilt each Update from m_pSourceFruit->m_Rot1
    Matrix44 m_StartMatrix;   // +0x7C  set once in Init, identity

    Fruit*   m_pSourceFruit;  // +0xBC
    float    m_Phase;         // +0xC0  0->1 colour-lerp phase (fixed dt accum)
    float    m_Life;          // +0xC4  fade-out countdown once expiring
    _Vector3<float> m_ColourEnd;    // +0xC8  lerp target colour
    _Vector3<float> m_ColourStart;  // +0xD4  lerp source colour (randomised per-spawn brightness)
    uint8_t  m_Expiring;      // +0xE0  0 = tracking host fruit; nonzero = fading out
    uint8_t  _pad_e1[3];      // +0xE1..+0xE3

    // Binary @ 0x001d954c -- base Entity ctor only, no field priming (Init does it).
    FruitRay();
    ~FruitRay() override;

    // ASM-spec v1.6.1 FruitRay::Init @0x001e4740
    // rot param is NOT used to build m_WorldMatrix/m_StartMatrix (both start
    // identity); orientation instead comes from m_pSourceFruit->m_Rot1 each Update.
    void Init(Fruit* src, Quaternion rot);

    // ASM-spec v1.6.1 FruitRay::Update @0x001e45e0
    void Update(float dt) override;

    // Rendering happens via the DrawRays() static batch below, not per-entity
    // vtable dispatch -- these overrides stay empty per the binary.
    void Draw(Renderer& r) override {}
    void PostUpdate(float dt) override {}

    // ASM-spec v1.6.1 FruitRay::RayTexture -- loaded by SuperFruitControl::LoadContent
    // via TextureManager::LoadLocalisedTexture("pomegranate_rays.tex").
    static Mortar::SmartPtr<Mortar::Texture> RayTexture;

    // ASM-spec v1.6.1 FruitRay::DrawRay @0x001e48b8. Per-instance render: builds
    // a 3-vertex QUADCUSTOMVERTEX tri-strip wedge -- apex at the local origin
    // (u=0.5, v=1.0), base edge from (-0.25,0,1) to (0.25,0,1) (u=0/1, v=0.05).
    // Binds RayTexture (both Set and UnSet are individually gated on
    // RayTexture.IsValid(); the draw itself is NOT gated -- no early return),
    // uploads a world matrix built from this ray's scale/orientation, and issues
    // one Mesh::DrawTriStrip. Full stack/offset map in FruitRay.cpp.
    // Called only from DrawRays() below.
    void DrawRay();

    // ASM-spec v1.6.1 FruitRay::DrawRays @0x001e4ac4. Static batch: walks every
    // ActorManager type-6 (FruitRay) entity and calls DrawRay() on each. Called
    // directly from GameDraw (v1.6.1 @0x001cd9d4), which sets the platform draw
    // colour to white before the batch and restores black after.
    static void DrawRays();
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(FruitRay, m_WorldMatrix)  == 0x3C, "FruitRay::m_WorldMatrix offset wrong");
static_assert(offsetof(FruitRay, m_StartMatrix)  == 0x7C, "FruitRay::m_StartMatrix offset wrong");
static_assert(offsetof(FruitRay, m_pSourceFruit) == 0xBC, "FruitRay::m_pSourceFruit offset wrong");
static_assert(offsetof(FruitRay, m_Phase)        == 0xC0, "FruitRay::m_Phase offset wrong");
static_assert(offsetof(FruitRay, m_Life)         == 0xC4, "FruitRay::m_Life offset wrong");
static_assert(offsetof(FruitRay, m_ColourEnd)    == 0xC8, "FruitRay::m_ColourEnd offset wrong");
static_assert(offsetof(FruitRay, m_ColourStart)  == 0xD4, "FruitRay::m_ColourStart offset wrong");
static_assert(offsetof(FruitRay, m_Expiring)     == 0xE0, "FruitRay::m_Expiring offset wrong");
static_assert(sizeof(FruitRay)                   == 0xE4, "sizeof(FruitRay) wrong (binary 0xE4)");
#endif

#endif // FN_ENTITIES_FRUITRAY_H
