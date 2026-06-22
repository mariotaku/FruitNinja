#ifndef FN_ENGINE_ASSET_ANIMATIONMANAGER_H
#define FN_ENGINE_ASSET_ANIMATIONMANAGER_H

#include "asset/AnimationList.h"
#include "util/List.h"

namespace Mortar {

// AnimationManager -- global animation registry singleton.
// v1.6.1: Initialise @0x00236314, ReleaseAll @0x00236318, Destroy @0x0023631c.
// Sole member: List<Animation*> m_Anims (+0x00, 20 bytes).
// All method bodies are no-ops in the binary (Initialise = return this;
// ReleaseAll = return this; Destroy calls ReleaseAll).
// Load/LoadAnimInternal/Release have no callers in v1.6.1.
//
// DIFFERS: binary uses a file-scope static m_instance built during static-init;
// port uses a function-local static in GetInstance() (lazy-vs-eager).
// Observable behaviour is identical since all bodies are no-ops.
class AnimationManager {
public:
    static AnimationManager& GetInstance();

    // Defunct: animation registry -- no-op stub; v1.6.1 AnimationManager::Initialise @0x00236314
    void Initialise(int reserveBytes) { (void)reserveBytes; }

    // Defunct: animation registry -- no-op stub; v1.6.1 AnimationManager::ReleaseAll @0x00236318
    void ReleaseAll() {}

    // Defunct: animation registry -- no-op stub; v1.6.1 AnimationManager::Destroy @0x0023631c
    void Destroy() { ReleaseAll(); }

    // Defunct: animation registry -- omitted, no live callers; v1.6.1 AnimationManager::Load @0x00236348
    // (SmartPtr<AnimationState> Load(const AsciiString&) -- would require AnimationState.h include)

    List<Animation*> m_Anims;  // +0x00, 20 bytes

private:
    AnimationManager() {}
    ~AnimationManager() { m_Anims.Destroy(); }
};

#ifdef __bada__
static_assert(sizeof(Mortar::AnimationManager) == 20,
              "AnimationManager sizeof mismatch (expected 20 bytes: List<Animation*>)");
#endif

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_ANIMATIONMANAGER_H
