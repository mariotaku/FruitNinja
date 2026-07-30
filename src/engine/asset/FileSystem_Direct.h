#ifndef FN_ENGINE_ASSET_FILESYSTEM_DIRECT_H
#define FN_ENGINE_ASSET_FILESYSTEM_DIRECT_H

// Binary @ 0x001eb328 (vtable), sizeof(FileSystem_Direct) == 0x14 on 32-bit ARM

#include "asset/IFileSystem.h"

namespace Mortar {

// Binary @ 0x001eb328 — concrete IFileSystem backed by the OS filesystem.
// Extends IFileSystem vtable with slots 6 (TranslateFileName) and 7 (Initialise).
class FileSystem_Direct : public IFileSystem {
public:
    FileSystem_Direct();
    virtual ~FileSystem_Direct();

    // IFileSystem overrides (slots 2..5) — Binary @ 0x001eb328 vtable
    virtual bool         FileExists(const char* name) override;
    virtual unsigned int FileSize(const char* name) override;
    virtual bool         GetFileData(const char* name, void** outBuf,
                                     unsigned long* outSize, bool& outOwned) override;
    virtual IFile*       OpenFile(const char* name, unsigned long flags) override;

    // Slot 6 — Binary @ FileSystem_Direct vtable+0x18
    // Resolves a logical path to an absolute on-disk path in m_rootPath.
    // out must be large enough for the result (caller owns).
    virtual void TranslateFileName(const char* in, char* out);

    // Slot 7 — Binary @ FileSystem_Direct vtable+0x1c
    // Sets the root path for this filesystem and whether writes are allowed.
    virtual void Initialise(const char* root, bool writable);

private:
    // +0x0c: root path string (heap-allocated copy)
    char* m_rootPath;    // Binary @ FileSystem_Direct +0x0c
    // +0x10: writable flag
    bool  m_isWritable;  // Binary @ FileSystem_Direct +0x10
};

} // namespace Mortar

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(Mortar::FileSystem_Direct) == 0x14, "Mortar::FileSystem_Direct size mismatch"); // v1.6.1 GameInitialise @0x0011d25c -- operator new(0x14) sizes FileSystem_Direct
#endif

#endif // FN_ENGINE_ASSET_FILESYSTEM_DIRECT_H
