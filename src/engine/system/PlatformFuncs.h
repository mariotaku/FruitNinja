#ifndef FN_ENGINE_SYSTEM_PLATFORM_FUNCS_H
#define FN_ENGINE_SYSTEM_PLATFORM_FUNCS_H

// PlatformFuncs -- Mortar-namespace Bada-platform utility stubs.
// Binary: v1.6.1 includes no-op stubs for these Bada OS service calls.
// Port: matches binary no-op behavior.

namespace Mortar {

// PlatformSpecificRemoveFolder -- v1.6.1 Mortar::PlatformSpecificRemoveFolder @0x00251598
// Bada platform: remove a folder tree. Binary is already a no-op stub (returns 0).
// Returns 0 (failure / not implemented).
int PlatformSpecificRemoveFolder(const char* path);

} // namespace Mortar

#endif // FN_ENGINE_SYSTEM_PLATFORM_FUNCS_H
