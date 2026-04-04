#include "particle/PSPParticleManager.h"

namespace Mortar {

PSPParticleManager::PSPParticleManager() {
}

PSPParticleManager::~PSPParticleManager() {
    Clear();
}

PSPParticleEmitter* PSPParticleManager::AddEmitter(uint32_t hash, const Vec3& pos) {
    (void)hash;
    PSPParticleEmitter emitter;
    emitter.m_Pos = pos;
    emitter.m_bActive = true;
    m_Emitters.push_back(emitter);
    return &m_Emitters.back();
}

void PSPParticleManager::Update(float dt) {
    for (size_t i = 0; i < m_Emitters.size(); ) {
        PSPParticleEmitter& e = m_Emitters[i];
        if (!e.m_bActive) {
            m_Emitters.erase(m_Emitters.begin() + i);
            continue;
        }
        e.m_Timer += dt * e.m_TimeScale;
        e.m_Pos.x += e.m_Vel.x * dt;
        e.m_Pos.y += e.m_Vel.y * dt;
        e.m_Pos.z += e.m_Vel.z * dt;
        i++;
    }
}

void PSPParticleManager::Draw() {
    // Stub — particle rendering not yet implemented
    // Will render textured quads from particle templates
}

void PSPParticleManager::LoadFile(const char* path) {
    (void)path;
    // Stub — template loading not yet implemented
}

void PSPParticleManager::Clear() {
    m_Emitters.clear();
}

} // namespace Mortar
