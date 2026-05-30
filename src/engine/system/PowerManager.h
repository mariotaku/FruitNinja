#ifndef FN_ENGINE_SYSTEM_POWER_MANAGER_H
#define FN_ENGINE_SYSTEM_POWER_MANAGER_H

// Mortar::PowerManager -- Bada platform power/focus state manager.
// Polymorphic singleton: vptr @ +0x00; binary size = 8 bytes (vptr + 4B pad).
// Binary ctor @ 0x0018aca0 (base-object) / 0x0018acbc (complete-object).
// GetInstance @ 0x0018ad64 -- Meyers singleton in BSS.
// GameTaskUpdate calls Update() each frame; GetState() returns power/focus state.
// Port: Update() is a no-op; GetState() always returns 0 (foreground-active).

#include <cstdint>

namespace Mortar {

class PowerManager {
public:
    static PowerManager* GetInstance() {
        static PowerManager s_instance;
        return &s_instance;
    }

    // Polymorphic root: vptr @ +0x00; 2 virtual slots in binary vtable.
    virtual ~PowerManager() {}

    // Defunct: Bada power/focus state -- no-op stub; binary vtable slot.
    virtual void Update() {}

    // Defunct: Bada power/focus state -- no-op stub; binary vtable slot.
    // Returns 0 = foreground-active (desktop has no backgrounding).
    virtual uint32_t GetState() { return 0; }

private:
    PowerManager() {}

    // +0x04: 4-byte pad to reach binary sizeof = 8 on ARM32/Bada.
    // ctor/dtor touch only offset 0 (vtable); no data member is written.
    uint32_t m_pad;
};

} // namespace Mortar

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(Mortar::PowerManager) == 8,
    "Mortar::PowerManager must be 8 bytes on ARM32/Bada");
#endif

#endif // FN_ENGINE_SYSTEM_POWER_MANAGER_H
