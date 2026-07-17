// Port specific: Wii libfat filesystem backend -- SCAFFOLDING ONLY.
// See FileSystemWii.h for why this subclasses Mortar::FileSystem_Direct
// instead of reimplementing IFileSystem from scratch.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/FileSystemWii.h"
#include "debug/Logger.h"

// TODO(wii): #include <fat.h> once libogc2/libfat headers are available.

namespace Mortar {

FileSystemWii::FileSystemWii()
    : FileSystem_Direct()
{
}

FileSystemWii::~FileSystemWii() {
}

void FileSystemWii::InitialiseWii(const char* subDir, bool writable) {
    // TODO(wii): if (!fatInitDefault()) { LOG_ERROR(...); return; } --
    // idempotent, safe to call once at process start (mainWii.cpp) rather
    // than per-FileSystemWii-instance; called here too defensively in case
    // this is ever the first FS touch.

    // TODO(wii): build root = std::string("sd:/") + subDir, verify it exists
    // (stat), fall back to "usb:/" + subDir if not (see header comment).
    // For now just forward to the base FileSystem_Direct::Initialise with a
    // placeholder root so the call shape is correct.
    Initialise(subDir, writable);

    LOG_INFO("FileSystemWii", "Wii libfat backend: InitialiseWii() scaffolding, root='%s'", subDir);
}

} // namespace Mortar

#endif // FRUIT_PLATFORM_WII
