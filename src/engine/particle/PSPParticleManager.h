#ifndef MORTAR_PSP_PARTICLE_MANAGER_H
#define MORTAR_PSP_PARTICLE_MANAGER_H

#include "math/Vec3.h"
#include "core/Singleton.h"
#include <cstdint>
#include <vector>

namespace Mortar {

// Matches original PSPParticleEmitter (~0x4C bytes)
struct PSPParticleEmitter {
    float m_Timer;           // +0x00
    Vec3 m_Pos;              // +0x08
    Vec3 m_Vel;              // +0x14
    float m_TimeScale;       // +0x20: speed multiplier (default 1.0)
    float m_ScaleX, m_ScaleY; // +0x28, +0x2C
    bool m_bActive;
    bool m_bUpdateWhenPaused; // +0x48

    PSPParticleEmitter()
        : m_Timer(0), m_Pos(0,0,0), m_Vel(0,0,0)
        , m_TimeScale(1.0f), m_ScaleX(1.0f), m_ScaleY(1.0f)
        , m_bActive(false), m_bUpdateWhenPaused(false)
    {}
};

// Matches original PSPParticleManager (singleton, 48 bytes)
// Template-based particle emitter system
class PSPParticleManager : public Singleton<PSPParticleManager> {
    friend class Singleton<PSPParticleManager>;

public:
    // Add emitter by template hash
    // Matches AddEmitter (0x1149e0)
    PSPParticleEmitter* AddEmitter(uint32_t hash, const Vec3& pos);

    // Update all active emitters and particles
    void Update(float dt);

    // Draw all particles
    void Draw();

    // Load particle templates from file
    void LoadFile(const char* path);

    // Remove all emitters
    void Clear();

private:
    PSPParticleManager();
    ~PSPParticleManager();

    std::vector<PSPParticleEmitter> m_Emitters;
};

} // namespace Mortar

#endif
