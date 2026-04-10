#ifndef FN_FRUIT_H
#define FN_FRUIT_H

#include "Entity.h"
#include "math/Quaternion.h"
#include "util/SmartPtr.h"
#include "asset/Mesh.h"

// Matches original Fruit : Mortar::Entity
// Physics: ballistic arc with quaternion rotation, 2-body split on slice
class Fruit : public Entity {
public:
    // +0x3c: fruit type index into FRUIT_INFO array
    int m_FruitType;

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

    // Launch fruit with velocity (matches Fruit::Chuck)
    void Chuck(const Vec3& velocity, float delay = 0.0f);

    // Check if fruit has gone off-screen
    bool CheckOffscreen() const;

    // Matches Fruit::LoadInfo (0x17987c, 519 lines) — called once from GameInitialise step 24
    // Parses Data/xml/fruitlist.xml into FRUIT_INFO array
    static void LoadInfo();
};

#endif
