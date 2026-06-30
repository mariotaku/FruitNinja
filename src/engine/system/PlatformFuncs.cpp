// PlatformFuncs.cpp -- Mortar-namespace Bada-platform utility stubs.
// Binary: v1.6.1 Mortar::PlatformSpecificRemoveFolder @0x00251598 is already a no-op
// (returns 0). Port matches binary behavior.

#include "system/PlatformFuncs.h"

namespace Mortar {

// PlatformSpecificRemoveFolder -- v1.6.1 Mortar::PlatformSpecificRemoveFolder @0x00251598
// Defunct: Bada OS folder removal -- no-op stub; v1.6.1 Mortar::PlatformSpecificRemoveFolder @0x00251598
// Binary returns 0 unconditionally; port matches.
int PlatformSpecificRemoveFolder(const char* /*path*/)
{
    return 0;
}

} // namespace Mortar
