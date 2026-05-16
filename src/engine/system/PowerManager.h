#ifndef FN_ENGINE_SYSTEM_POWER_MANAGER_H
#define FN_ENGINE_SYSTEM_POWER_MANAGER_H
#include <cstdint>
namespace Mortar {
// Defunct: Bada power/focus state -- no-op stub; binary ctor @ 0x0018aca0.
// GameTaskUpdate calls Update() each frame and GetState() returns 0
// (foreground-active) on desktop -- port has no backgrounding.
class PowerManager {
public:
    static PowerManager* GetInstance();
    void Update();
    uint32_t GetState();
};
}  // namespace Mortar
#endif
