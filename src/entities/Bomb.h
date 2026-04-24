#ifndef FN_BOMB_H
#define FN_BOMB_H

//
// Bomb : Entity (size = 0xB0 / 176 bytes)
// Entity type 1. Docs: docs/entities/bomb.md
// Binary: ctor 0x171678, Init 0x172504, Update 0x1729fc, Draw 0x171be8
//
// Analysed: 2026-04-10T10:00

#include "Entity.h"
#include "math/Vec3.h"
#include "render/gl_funcs.h"
#include <functional>

namespace Mortar { struct PSPParticleEmitter; }

class Bomb : public Entity {
public:
    // +0x38: collision sphere (port: not yet implemented, stubbed)
    // ColSphere* m_Col;

    // +0x3c: BombBlast spawn timer; Init = 0.6 (DAT_001726ac)
    float m_SpawnTimer;

    // +0x40..+0x63: hit callback delegate. Binary stores a 0x24-byte
    // BaseDelegate; port uses std::function for the callable. Fired
    // when CollisionResponse hits the menu-rehit branch (player slices
    // a menu-decoration bomb that's already been registered as hit).
    // Used by Quit button to chain into the QuitGame flow.
    std::function<void()> m_HitCallback;

    // +0x64: bomb variant — 0=normal, 2=multiplayer
    int m_BombVariant;

    // +0x68: 0=in-flight, 1=hit/slashed
    uint8_t m_bHit;

    // +0x6c: Z layer from GetBombZPosition()
    float m_ZPosition;

    // +0x70..+0x76: rotation animation (16-bit short accumulators)
    int16_t m_RotVelX;     // +0x70: random 1..8
    int16_t m_RotVelY;     // +0x72: random 1..8
    int16_t m_RotX;        // +0x74: accumulated rotation X
    int16_t m_RotY;        // +0x76: accumulated rotation Y

    // Port specific: fractional-frame accumulators so the F7 debug
    // timescale slows bomb rotation along with physics. At 1.0× timescale
    // these hold 0 and rotation advances exactly one (m_RotVelX/Y) step
    // per frame, matching the binary. At 0.1× timescale the integer
    // part advances ~every 10 frames. No binary offset — off-struct.
    float m_RotAccumX;
    float m_RotAccumY;

    // +0x78: collision guard (prevents double-hit)
    uint8_t m_bCollisionGuard;

    // +0x7c: fuse particle emitter — lazy-created on first Update tick
    Mortar::PSPParticleEmitter* m_pEmitter;

    // +0x80: 1 = physics enabled
    uint8_t m_bMovement;

    // +0x84: backref to game state (port: not used)
    // int m_GameStateRef;

    // +0x88: 0=normal hit, 1=menu/zen hit
    uint8_t m_bMenuBombHit;

    // +0x8c..+0x94: acceleration/gravity
    Vec3 m_AccelForce;

    // +0x98..+0xa0: backup of scale from Init
    Vec3 m_OrigScale;

    // +0xa4: chuck delay timer; 0 = ready
    float m_Countdown;

    // +0xa8: speed multiplier; 1.0 normal, 0.666 for fat bombs
    float m_SpeedMult;

    Bomb();
    ~Bomb();

    void Init(int param1, int fruitType, int param3) override;
    void Update(float dt) override;
    void PostUpdate(float dt) override;   // 0x001714e4 — sync fuse emitter
    void Draw(Renderer& r) override;
    void Deactivate() override;

    // Matches Bomb::CollisionResponse (0x17280c). Blade has sliced the bomb:
    // set m_bHit, trigger HitBomb (camera shake + bombHitTimer), start the
    // BombBlast shockwave spawn loop.
    void OnSliced(const Vec3& bladeVel) override;

    // Matches Bomb::Chuck (0x170f68)
    void Chuck(float delay);

    // Matches Bomb::KillBomb (0x1716e8)
    void KillBomb();

    // Matches Bomb::Enabled (0x001507e0) — returns !m_bCollisionGuard.
    // "Enabled" == "still accepting slice collisions". Called by
    // ClearMenuItems to decide whether to fling the bomb.
    bool Enabled() const { return m_bCollisionGuard == 0; }

    // Matches Bomb::Disable (0x0012637c) — sets m_bCollisionGuard = 1.
    // Collision ignored afterwards, but the entity keeps updating in
    // Bomb::Update's alive branch so gravity integrates and the bomb
    // falls off screen. NOT the same as Deactivate/KillBomb.
    void Disable() { m_bCollisionGuard = 1; }

    // Matches Bomb::LoadContent (0x1726c8) — called once from GameInitialise
    // Loads bomb models, textures, particle hashes into g_bombData
    static void LoadContent();
};

#endif
