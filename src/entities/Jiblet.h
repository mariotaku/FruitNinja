#ifndef FN_ENTITIES_JIBLET_H
#define FN_ENTITIES_JIBLET_H

//
// Jiblet : Mortar::Entity (size = 0xB0 / 176 bytes)
// Mortar::Entity type 5. Diced fruit-piece (mesh chunk) flung when a fruit is sliced.
// Binary: ctor 0x1d9580 (CreateEntity memsets 0xB0 first) · ~Jiblet 0x1e5d34
//         Init 0x1e50c0 · Update 0x1e5330 · PostUpdate(empty) 0x1e5c04
//

#include "Entity.h"
#include "math/_Vector3.h"
#include "math/Matrix44.h"
#include "util/SmartPtr.h"
#include "asset/Model.h"
#include <cstdint>

struct Renderer;
struct PSPParticleEmitter;

class Jiblet : public Mortar::Entity {
public:
    // +0x3C: drip interval timer. Init: =DAT (param2<=0) or T_796(DAT, 1/param2).
    float m_SplatTimer;                         // +0x3C

    // +0x40: model smart pointer. ctor SmartPtr::SmartPtr; Init operator=.
    Mortar::SmartPtr<Mortar::Model> m_pModel;   // +0x40

    // +0x44: emitter template hash. Init = param_8.
    uint32_t m_EmitterHash;                     // +0x44

    // +0x48: lazy particle emitter. Init = 0; Update creates when m_Age>DAT.
    PSPParticleEmitter* m_pEmitter;             // +0x48

    // +0x4C: full rotation matrix (64 bytes). Init: unit Mat44 then RotX/Y/Z44
    // with random angles. Update: accumulated via quaternion spin each frame.
    Matrix44 m_Rotation;                        // +0x4C

    // +0x8C: drip rate, drips/sec (controls SplatEntity spawn interval; interval = 1/rate).
    // Init = dripRate param. >0 gates splat spawning in Update drip loop.
    float m_DripRate;                           // +0x8C

    // +0x90: fruit type index. Init = param_4; passed to MakeSplat.
    int m_FruitType;                            // +0x90

    // +0x94: gravity reference direction. Init = *param_9.
    _Vector3<float> m_GravBase;                            // +0x94

    // +0xA0: spin rate X axis. Init = random T_796.
    float m_SpinRateX;                          // +0xA0

    // +0xA4: spin rate Y axis.
    float m_SpinRateY;                          // +0xA4

    // +0xA8: spin rate Z axis.
    float m_SpinRateZ;                          // +0xA8

    // +0xAC: age accumulator. Update: += dt; gates emitter creation.
    float m_Age;                                // +0xAC

    Jiblet();
    virtual ~Jiblet();

    // Binary @ 0x1e50c0 — bespoke 8-arg spawn entry. Callers: SuperFruitControl::
    // ExplodeSuperFruit @0x1baa20, SuperFruitControl::SpawnJibs @0x1bc748.
    // NOT an override of Entity::Init vtable slot; distinct function.
    //
    // Binary-faithful signature (source-level; by-value Vec3/SmartPtr required
    // so the mangled name matches the binary for asm-verify pairing):
    //   _ZN6Jiblet4InitEiR8_Vector3IfEfS1_N6Mortar8SmartPtrINS3_5ModelEEEmfS1_
    void Init(int fruitType, _Vector3<float>& pos, float scale, _Vector3<float> vel,
              Mortar::SmartPtr<Mortar::Model> mdl,
              unsigned long emitterHash, float dripRate, _Vector3<float> grav);

    // Vtable slot 4: Binary @ 0x1e5330.
    // Quaternion-spin integrator, drip loop (SplatEntity::GetFree), emitter sync, bounds kill.
    void Update(float dt) override;

    // Vtable slot 5: Draw. Binary @ 0x1e5750. Renders m_pModel with m_Rotation at pos.
    void Draw(Renderer& r) override;

    // Vtable slot 6: PostUpdate (binary @ 0x1e5c04). Empty.
    void PostUpdate(float dt) override;

    // Binary @ 0x1e52ec. NOT a vtable slot -- plain member (Mortar::Entity has
    // no Kill()). Sole real call site: the bounds-kill branch at the tail of
    // Jiblet::Update (0x1e5714). Retires the trail emitter and drops the model
    // ref before marking the entity dead.
    void Kill();
};

#ifdef __bada__
static_assert(__builtin_offsetof(Jiblet, m_SplatTimer)   == 0x3C, "m_SplatTimer binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_pModel)       == 0x40, "m_pModel binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_EmitterHash)  == 0x44, "m_EmitterHash binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_pEmitter)     == 0x48, "m_pEmitter binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_Rotation)     == 0x4C, "m_Rotation binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_DripRate)     == 0x8C, "m_DripRate binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_FruitType)    == 0x90, "m_FruitType binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_GravBase)     == 0x94, "m_GravBase binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_SpinRateX)    == 0xA0, "m_SpinRateX binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_SpinRateY)    == 0xA4, "m_SpinRateY binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_SpinRateZ)    == 0xA8, "m_SpinRateZ binary offset wrong");
static_assert(__builtin_offsetof(Jiblet, m_Age)          == 0xAC, "m_Age binary offset wrong");
static_assert(sizeof(Jiblet)                             == 0xB0, "sizeof(Jiblet) wrong (binary 0xB0)");
#endif

#endif // FN_ENTITIES_JIBLET_H
