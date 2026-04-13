#ifndef FN_FRUIT_H
#define FN_FRUIT_H

#include "Entity.h"
#include "math/Quaternion.h"
#include "util/SmartPtr.h"
#include "asset/Mesh.h"

namespace Mortar { struct PSPParticleEmitter; }

// Matches original Fruit : Mortar::Entity
// Physics: ballistic arc with quaternion rotation, 2-body split on slice
class Fruit : public Entity {
public:
    // +0x3c: fruit type index into FRUIT_INFO array
    int m_FruitType;

    // +0x6c: slice countdown — init -1.0f, set positive by OnSliced,
    // counts down in Update until 0 → calls Slice() to split the fruit.
    // Ref: docs/engine/fruit-slice-notes.md.
    float m_SliceTimer;

    // +0x70: 16-bit angle index (Atan2Idx of bladeVel). Stored on hit,
    // used by Slice() to pick the half velocities.
    uint16_t m_SliceAngle;

    // +0x74: clamped blade speed (magnitude × 0.1, clamp [4..8] or [6..8]).
    float m_SliceImpulse;

    // +0x78..+0x84: snapshot of pos at slice time.
    Vec3 m_SlicePos;

    // +0x7c / +0x80: two juice-particle emitters spawned on hit, one
    // per eventual half. Point at pos and m_SecondPos respectively.
    Mortar::PSPParticleEmitter* m_pEmitter1;
    Mortar::PSPParticleEmitter* m_pEmitter2;

    // Port-only: menu buttons (MenuButton) create a fruit to decorate
    // the play button. Menu fruits normally stay pinned to the button
    // pos — but once sliced they should animate away like gameplay
    // fruits. This flag lets Fruit::Update skip unsliced physics and
    // MenuButton skip the pos override when the fruit has been cut.
    bool m_bPinnedByMenu;

    // +0xb4: sliced state
    bool m_bSliced;

    // Quaternion rotation (both halves)
    Quaternion m_Rot1, m_Rot2;           // +0xd0, +0xe0
    Vec3 m_RotVel1, m_RotVel2;          // +0xf0, +0xfc

    // Second half position/velocity (after slice)
    Vec3 m_SecondPos, m_SecondVel;       // +0xb8, m_HalfB

    // Gravity (grows over time for sliced halves)
    Vec3 m_Gravity;

    // Scale animation (0→1 on spawn)
    float m_ScaleAnim;                   // +0x110

    // Launch delay (fruit invisible during countdown)
    float m_ChuckDelay;

    // Rotation axis from config
    Vec3 m_RotAxis;                      // +0x84

    // Z depth for draw sorting
    float m_ZPosition;                   // +0x98

    // Model loaded via MeshManager (shared/cached)
    SmartPtr<Mortar::Model> m_Model;

    Fruit();
    ~Fruit();

    void Init(int param1, int fruitType, int param3) override;
    void Update(float dt) override;
    void Draw(Renderer& r) override;
    void Deactivate() override;

    // Matches Fruit::CollisionResponse (0x1780b0). Blade has hit the
    // fruit's collision sphere: record slice angle/impulse/pos, spawn
    // juice particle emitters, set m_SliceTimer to countdown until the
    // fruit splits. See docs/engine/fruit-slice-notes.md.
    void OnSliced(const Vec3& bladeVel) override;

    // Matches Fruit::Slice (0x176d58, simplified). Flips m_bSliced,
    // computes halfVel/halfVelB from m_SliceAngle, blends with old vel,
    // marks the fruit as two-body. Called from Update when m_SliceTimer
    // hits zero.
    void Slice();

    // Launch fruit with velocity (matches Fruit::Chuck)
    void Chuck(const Vec3& velocity, float delay = 0.0f);

    // Check if fruit has gone off-screen
    bool CheckOffscreen() const;

    // Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
    // Parses Data/xml/fruitlist.xml into FRUIT_INFO array
    static void LoadInfo();
};

#endif
