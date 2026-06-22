#include "asset/AnimationManager.h"

namespace Mortar {

AnimationManager& AnimationManager::GetInstance() {
    static AnimationManager s_instance;
    return s_instance;
}

}  // namespace Mortar
