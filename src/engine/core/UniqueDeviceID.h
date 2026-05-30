#ifndef FN_ENGINE_CORE_UNIQUE_DEVICE_ID_H
#define FN_ENGINE_CORE_UNIQUE_DEVICE_ID_H

#include <cstdint>
#include <cstring>

// UniqueDeviceID -- Bada platform type, 128 bytes.
// Member of SystemManager at +0x54 (ctor'd via blx 0x000f5094 thunk).
// Defunct: Bada device identification -- no-op stub; binary member at
// SystemManager+0x54. Size 128 bytes confirmed from ctor offset arithmetic:
// SystemManager ctor does (this+0x54) post-incremented by 0x54 for the sub-
// object ctor; sub-object size 128 -> total 0x54+128=212 == binary size.
class UniqueDeviceID {
public:
    UniqueDeviceID() { memset(m_data, 0, sizeof(m_data)); }
private:
    uint8_t m_data[128];
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
#include <cstddef>
static_assert(sizeof(UniqueDeviceID) == 128, "UniqueDeviceID size mismatch");
#endif

#endif // FN_ENGINE_CORE_UNIQUE_DEVICE_ID_H
