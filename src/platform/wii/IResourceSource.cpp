#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/IResourceSource.h"
#include "asset/FileManager.h"

namespace fn {
namespace wii {

// Thin adapter over Mortar::FileManager::GetFileData -- same call
// File::Load() makes today (src/engine/asset/File.cpp:125). idFilter=0 means
// "any registered IFileSystem", matching File::Load's default systemID.
// outOwned is always true for the FileSystem_Direct backend (see
// FileSystemPosix.cpp::GetFileData, which libfat's POSIX-compatible fopen/
// fread surface serves on Wii) -- ReleaseWhole() below assumes new[]
// ownership on that basis.
bool ResourceSourceLooseSD::ReadWhole(const char* logicalPath, void** outBuf, unsigned long* outSize) {
    bool owned = false;
    bool ok = FileManager::GetInstance().GetFileData(
        logicalPath, outBuf, outSize, /*idFilter=*/0, owned);
    return ok && owned;
}

void ResourceSourceLooseSD::ReleaseWhole(void* buf) {
    // Matches File::Unload()'s ownership contract (File.cpp:134-136):
    // FileSystem_Direct::GetFileData allocates with new[] unsigned char.
    delete[] static_cast<unsigned char*>(buf);
}

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII
