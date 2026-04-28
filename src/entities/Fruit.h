#ifndef FN_FRUIT_H
#define FN_FRUIT_H

#include "Entity.h"
#include "math/Quaternion.h"
#include "util/SmartPtr.h"
#include "asset/Mesh.h"

namespace Mortar { struct PSPParticleEmitter; }

// Per-fruit mesh slot layout. Matches the binary's 0x24-byte
// FruitModelInfo struct allocated by LoadFruitModels (0x1794e0).
//
// Binary layout:
//   +0x00: EffectProperty* prop[2]  (per half piece)
//   +0x08: EffectProperty* prop[2..3]  (optional outline variants)
//   +0x10: SmartPtr<Model> m_HalfA   (<name>_<c>_piece_1.mmd)
//   +0x14: SmartPtr<Model> m_HalfB   (<name>_<c>_piece_2.mmd)
//   +0x18: SmartPtr<Model> m_OptA    (<name>_<c>_outline.mmd etc)
//   +0x1c: SmartPtr<Model> m_OptB    (other optional variant)
//   +0x20: ???
//
// Port simplified to just the two pieces actually rendered by the
// sliced-fruit draw path. Outline/extras deferred.
struct FruitModelInfo {
    SmartPtr<Mortar::Model> m_HalfA;   // piece 1
    SmartPtr<Mortar::Model> m_HalfB;   // piece 2
};

// Matches original Fruit : Mortar::Entity
// Physics: ballistic arc with quaternion rotation, 2-body split on slice
class Fruit : public Entity {
public:
    // +0x3c: fruit type index into FRUIT_INFO array
    int m_FruitType;

    // +0x6c: slice countdown — init -1.0f, set positive by OnSliced,
    // counts down in Update until 0 → calls Slice() to split the fruit.
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

    // +0x114: m_bDrawWhole — when set, Fruit::Draw renders the whole
    // fruit mesh even if m_bSliced == 1. Set by ClearMenuItems
    // @ 0x0016ac7c when releasing menu fruits during the dojo
    // transition: the fruit is marked sliced (so MenuButton::Update
    // stops pinning it) AND m_bDrawWhole is set so it visually flies
    // off as a single object instead of splitting in two.
    bool m_bDrawWhole;

    // +0x80: detach flag set by SetVisible_FruitFact (0x0013785c).
    // When set, MenuButton::Update stops pinning this fruit to the
    // button center — the piece drifts freely with its current vel.
    bool m_bDetached;

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
    void PostUpdate(float dt) override;   // 0x0017501c — screen-edge bounce / push
    void Draw(Renderer& r) override;
    void Deactivate() override;

    // Matches Fruit::CollisionResponse (0x1780b0). Blade has hit the
    // fruit's collision sphere: record slice angle/impulse/pos, spawn
    // juice particle emitters, set m_SliceTimer to countdown until the
    // fruit splits.
    void OnSliced(const Vec3& bladeVel) override;

    // Matches Fruit::Slice (0x176d58, simplified). Flips m_bSliced,
    // computes halfVel/halfVelB from m_SliceAngle, blends with old vel,
    // marks the fruit as two-body. Called from Update when m_SliceTimer
    // hits zero.
    void Slice();

    // Matches Fruit::Sliced @ 0x001401c8. Pure predicate: returns true
    // if the fruit is already sliced (m_bSliced) OR if a slice countdown
    // is active (m_SliceTimer > -1.0f). Used by ShopScreen::ShrinkBuyButton
    // to skip the shrink trigger when the equip-button fruit is already
    // retracting.
    bool Sliced() const {
        return m_bSliced || (m_SliceTimer > -1.0f);
    }

    // Launch fruit with velocity (matches Fruit::Chuck)
    void Chuck(const Vec3& velocity, float delay = 0.0f);

    // Matches Fruit::CheckHasGoneOffscreen (0x00175218). Returns true
    // only when BOTH halves are past the offscreen boundary with outward
    // velocity. Also bounces sliced halves off the near edge.
    bool CheckHasGoneOffscreen();

    // Matches Fruit::KillFruit (0x00176abc). Clears emitters, applies
    // miss penalty (TODO), and marks the entity killed (flags |= 0x10).
    void KillFruit(bool doMissPenalty);

    // Matches Fruit::RotateFacingUp (0x001757f4). Sets m_Rot1/m_Rot2 to a
    // random starting orientation and m_RotVel1/m_RotVel2 to axisScale * scalar.
    // When flag=true, additional q_axis * q_up composition is applied to each
    // rotation slot. See Fruit.cpp for full algorithm.
    void RotateFacingUp(bool flag, const Vec3& axisScale);

    // Matches Fruit::FruitType (0x00175b10). Resolves a fruit name
    // string to the index in the FRUIT_INFO array by hashing and
    // comparing against m_NameHash / m_NameHashUpper. If not found:
    //   fallbackRandom=true → returns Random::Rand32(count-1)
    //   fallbackRandom=false → returns -1 (0xFFFFFFFF)
    static int FruitType(const char* name, bool fallbackRandom);

    // Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
    // Parses Data/xml/fruitlist.xml into FRUIT_INFO array
    static void LoadInfo();

    // Matches Fruit::LoadFruitModels (0x1794e0). Allocates the
    // per-fruit FruitModelInfo array and loads `<name>_<c>_piece_1.mmd`
    // and `<name>_<c>_piece_2.mmd` for each fruit entry via
    // MeshManager. Called once from GameInitialise.
    static void LoadFruitModels();

    // Accessor for a per-fruit pair of half meshes. Returns nullptr if
    // index out of range or LoadFruitModels hasn't run.
    static const FruitModelInfo* GetFruitModelInfo(int fruitType);
};

#endif
